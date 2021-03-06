#include "include\tld.h"
#include<iostream>
TLD::TLD()
{
    //固定参数
    min_win = 15;
	bad_overlap = 0.2f;

    //generatePositiveData相关参数
    patch_size   = 15;
    num_closest  = 10;
    num_warps    = 20;
    noise        = 5;
    angle        = 20;
    scale        = 0.02f;

	//generateNegativeData相关参数
    bad_patches = 200;
	good_patches = 5;
}

//生成正样本
/*
* For each of the bounding box(注：good boxes),
* we generate 20 warped versions by geometric transformations
* (shift 1%, scale change 1%, in-plane rotation 10)
* and add them with Gaussian noise (sigma = 5) on pixels
*/
void TLD::generatePositiveData(const cv::Mat& frame, int num_warps)
{
	cv::Scalar mean;
	cv::Scalar stdev;

	/// 仿射变换以后添加Fern和NN的正样本集
	cv::Mat img, img1, warped, patch, patt;
	cv::GaussianBlur(frame, img, cv::Size(9, 9), 1.5);
	cv::RNG& rng = cv::theRNG();
	cv::Point2f pt(bbhull.x + (bbhull.width - 1)*0.5f,
		bbhull.y + (bbhull.height - 1)*0.5f);//水平，垂直中心，即旋转的中心
	cv::vector<int> fern(detector.getTreeNum());
	//函数 vector::capacity() 返回容器的容积（最大能容纳的对象数量）
	//区分：vector::size() 返回容器中当前容纳的元素数量
	pX.clear();
	if (pX.capacity()< num_warps*good_boxes.size())
		pX.reserve(num_warps*good_boxes.size());
	pEx.clear();
	pEx = cv::vector<cv::Mat>(good_patches);
	getPattern(img(best_box), patt, mean, stdev);
	
	//cv::imshow("patt", patt);
	///*cv::imwrite("F:/sss.jpg", patt);*/
	//cv::waitKey(10);
	//std::cout << " " << patt.type()<<std::endl;

	pEx[0] = patt;  //best box
	/*for (int i = 0; i < patt.rows; i++)
	{
		for (int j = 0; j < patt.cols; j++)
			std::cout << patt.at<float>(i, j) << " ";
	}
	std::cout<<std::endl << patt.type()<<" "<<CV_32FC1;*/
	int idx, cnt = 1;
	for (int i = 0;i<num_warps;i++)
	{
		img1 = img.clone();
		//每一个good_boxes都生num_warps个pX
		if (i > 0)
		{
			// 在bbhull区域进行仿射变换，结果存在warped中，原图不变
			generator(img, pt, warped, bbhull.size(), rng);
			// 将bbhull区域替换成变换后的结果img1(bbhull)权重为0即可！）
			cv::addWeighted(img1(bbhull), 0, warped, 1, 0, img1(bbhull));
		}
			
		for (int b = 0;(int)b<good_boxes.size();b++)
		{
			idx = good_boxes[b];
			patch = img1(grid[idx]);
			//Fern
			detector.getFeatures(patch, grid[idx].sidx, fern);
			pX.push_back(std::make_pair(fern, 1));
			//NN
			if (cnt < good_patches)
			{
				getPattern(patch, patt, mean, stdev);
				pEx[cnt] = patt;
				cnt++;
			}
		}
	}
	/// 关于仿射变换部分的效果，建议自己写几行简单的代码测试以下效果
	/// 这样有利于对这个函数的处理效果有一个直观的认识
}

//生成负样本
void TLD::generateNegativeData(const cv::Mat& frame)
{
	int idx;
	cv::Mat patch, patt;
	cv::Scalar dum1, dum2;
	cv::Scalar stdev, mean;
	cv::vector<int> fern(detector.getTreeNum());
	nX.clear();
	nX.reserve(bad_boxes.size());
	std::random_shuffle(bad_boxes.begin(), bad_boxes.end()); //打乱顺序（体现"随机选取"）

	cv::Mat img;
	cv::GaussianBlur(frame, img, cv::Size(9, 9), 1.5);
	/// 注意Fern的负样本（nX）和NN的负样本（nEX）添加规则不同
	/// 前者首先要经过Patch variance的var阈值过滤

	//将bad_boxes中variance > 0,25*var阈值的加入nX
	for (int i = 0; i<bad_boxes.size(); i++)
	{
		idx = bad_boxes[i];
		patch = img(grid[idx]);
		cv::meanStdDev(patch, mean, stdev);
		if (pow(stdev.val[0], 2)<var*0.25f)
			continue;
		detector.getFeatures(patch, grid[idx].sidx, fern);
		nX.push_back(std::make_pair(fern, 0));
	}

	//随机选择(bad_patches)个负样本加入负样本集nEX
	nEx.clear();
	nEx = cv::vector<cv::Mat>(bad_patches);
	for (int i = 0; i<bad_patches; i++)
	{
		idx = bad_boxes[i];
		patch = img(grid[idx]);
		getPattern(patch, patt, dum1, dum2);//尺寸归一化，零均值化
		nEx[i] = patt;
	}
}

void TLD::init(const cv::Mat &firstFrame, const cv::Rect &box)
{
    /// 1.preparation & allocation
    //Get sliding windows
    buildGrid(firstFrame,box);
    //预分配
    //注：函数vector::reserve() 预留容器空间，但是不分配对象，提高程序效率，免每次重新分配对象
    dconf.reserve(100);
    dbb.reserve(100);
    tmp.conf = cv::vector<float>(grid.size());
    tmp.patt = cv::vector<cv::vector<int> >
            (grid.size(),cv::vector<int>(10,0));
    dt.bb.reserve(grid.size());
    good_boxes.reserve(grid.size());
    bad_boxes.reserve(grid.size());
    generator = cv::PatchGenerator(0,0,noise,true,
                                   1-scale,1+scale,
                                   -angle*CV_PI/180,angle*CV_PI/180,
                                   -angle*CV_PI/180,angle*CV_PI/180);

    /// 2. 选出(num_closest_init)个good box，1个best_box,其余的都作为bad box
    /// 作者原文：First we select 10 bounding boxes on the scanning grid
    getOverlappingBoxes(num_closest);
    //Correct Bounding Box
    lastbox=best_box; //注意是 best_box
    lastvalid=true;

    ///3. Prepare for Fern-Classifier
    /// 主要工作是计算得到全部sliding window patches的 2-bit BP 特征
    detector.prepare(scales);

    ///4. Prepare initial P-samples for Fern & NN -Classifier
    /// 作者原文：
    /// For each of the bounding box(注：good boxes),
    /// we generate 20 warped versions by geometric transformations
    /// and add them with Gaussian noise on pixels
    generatePositiveData(firstFrame,num_warps);

    /// 5. Set variance threshold
    /// This stage rejects all patches,
    /// for which gray-value variance is smaller than
    /// 50% of variance of the patch that was selected for tracking
    cv::Scalar stdev, mean;
    cv::meanStdDev(firstFrame(best_box),mean,stdev); //注意是best_box，而不是我们自己框定的box
    var = (float)pow(stdev.val[0],2);


    /// 6. Prepare initial N-samples for Fern & NN -Classifier
    generateNegativeData(firstFrame);

    /// 7. 构造训练集和测试集
    ///		     |	           训练集			|	  测试集（只有负样本）
    /// NN分类器 |  [pEx nEx(N/2)]--> nn_data    |      nExT （N/2）
    /// 随机森林 |  [pX  nX(N/2) ]--> ferns_data |      nXT(N/2)
    //Split Negative Ferns into Training and Testing sets
    //Note that they are already shuffled in generateNegativeData()
    int half = cvRound(nX.size()*0.5f);    //负样本五五分
    nXT.assign(nX.begin()+half,nX.end()); //nX的后一半赋值给nXT（加入测试集）
    nX.resize(half);                      //前一半保留作为样本集
    //Split Negative NN Examples into Training and Testing sets
    half = cvRound(nEx.size()*0.5);
    nExT.assign(nEx.begin()+half,nEx.end());
    nEx.resize(half);
    // Merge Negative Data with Positive Data and shuffle it
    // [nX + pX]-->ferns_data
    // 注意一定要打乱！P和N不能分开，详见Detector类中两个trainF函数的注释
    cv::vector<std::pair<cv::vector<int>,int> > ferns_data(nX.size()+pX.size());
    cv::vector<int> idx = index_shuffle(0,(int)ferns_data.size());//生成乱序的标签索引值
    int a=0;
    for (int i=0;i<pX.size();i++)
    {
        //pX 是在 generatePositiveData()中产生的
        ferns_data[idx[a]] = pX[i];
        a++;
    }
    for (int i=0;i<nX.size();i++)
    {
        //nX 是在 generateNegativeData()中产生的
        ferns_data[idx[a]] = nX[i];
        a++;
    }
    //Data already have been shuffled, just putting it in the same vector
    //[pEx + nEx]->nn_data  注意保持best box在第一个
	
	cv::vector<cv::Mat> nn_data(pEx.size()+nEx.size());
	for (int i = 0; i < pEx.size(); i++)
	{
		nn_data[i] = pEx[i];
	}
    for (int i= (int)pEx.size(); i<(int)(pEx.size()+nEx.size()); i++)
    {
        nn_data[i]= nEx[i- pEx.size()];
    }

    /// 8.Train NN & Fern Classifier
	//detector.nEx = nEx;
	//detector.pEx = pEx;
    detector.trainF(ferns_data);
    detector.trainNN(nn_data,(int)pEx.size());
	detector.show();

    /// 9.Threshold Evaluation on testing sets
    /// 阈值修正
    /// 仅此一次调用，后面都不会提高了
    detector.evaluateTh(nXT,nExT);
}

//Tracking
void TLD::track(const cv::Mat& img1, const cv::Mat& img2,
           cv::vector<cv::Point2f>& points1,
           cv::vector<cv::Point2f>& points2)
{
    //Generate points
    bbPoints(points1,lastbox);
    if (points1.size()<1)
    {
        tvalid=false;
        tracked=false;
        return;
    }
    cv::vector<cv::Point2f> points = points1;
    //Frame-to-frame tracking with forward-backward error cheking
    tracked = tracker.trackOneFrame(img1,img2,points,points2);
    if (tracked)
    {
        bbPredict(points,points2,lastbox,tbb);
        //Failure detection
        if (tracker.getFB()>10
                || tbb.x>img2.cols ||  tbb.y>img2.rows
                || tbb.br().x < 1 || tbb.br().y <1)
        {
            tvalid =false;
            tracked = false;
            return;
        }
        //Estimate Confidence and Validity
        cv::Mat pattern;
        cv::Scalar mean, stdev;
        BoundingBox bb;
        bb.x = cv::max(tbb.x,0);
        bb.y = cv::max(tbb.y,0);
        bb.width  = cv::min(cv::min(img2.cols-tbb.x,tbb.width),
                            cv::min(tbb.width,tbb.br().x));
        bb.height = cv::min(cv::min(img2.rows-tbb.y,tbb.height),
                            cv::min(tbb.height,tbb.br().y));
        getPattern(img2(bb),pattern,mean,stdev);
		//getPattern(img2(tbb), pattern, mean, stdev);

        cv::vector<int> isin;
        float dummy;
        detector.NNConf(pattern,isin,dummy,tconf); //用Conservative Similarity作为tconf
        tvalid = lastvalid;
        if (tconf>=detector.getThr_nn_valid())
        {
            //tvalid标志轨迹是否有效
            tvalid =true;
        }
    }
    else
      std::cout << "No points tracked" << std::endl;
}

//Detection
/*
 * 1.Patch variance
 * 2.Ensemble classifier(Fern)
 * 3.Nearest neighbor classifier(NN)
*/
void TLD::detect(const cv::Mat& frame)
{
    dbb.clear();
    dconf.clear();
    dt.bb.clear();

    cv::Scalar stdev, mean;
    int numtrees = detector.getTreeNum();
    float fern_th = detector.getFernTh();
    cv::vector <int> ferns(10);
    float conf;
    cv::Mat patch;

    for (int i=0;i<grid.size();i++)
    {
        cv::meanStdDev(frame(grid[i]),mean,stdev);
        if (pow(stdev.val[0],2)>=var*0.5f) //Patch variance
        {
            patch = frame(grid[i]);
            detector.getFeatures(patch,grid[i].sidx,ferns);
            conf = detector.measure_forest(ferns); //Ensemble classifier
            tmp.conf[i]=conf;
            tmp.patt[i]=ferns;         //只要能通过Patch variance就会保存到tmp
            if (conf>numtrees*fern_th) //能通过Ensemble classifier才会保存至dt
            {
                dt.bb.push_back(i);
            }
        }
        else
          tmp.conf[i]=0.0;
    }
    int detections = (int)dt.bb.size();
    if (detections>100)
    {
        //在通过前两层筛选的搜索框中最多保留最佳的100个
        std::nth_element(dt.bb.begin(),dt.bb.begin()+100,
                         dt.bb.end(),CComparator(tmp.conf));
        dt.bb.resize(100);
        detections=100;
    }
    if (detections==0)
    {
        detected=false;
        return;
    }

	//调试用：显示通过前两层筛选的框
	/*
	cv::Mat img = frame.clone();
	for (int i = 0; i < detections; i++)
	{
		drawBox(img, grid[dt.bb[i]]);
	}
	cv::namedWindow("first 2 filters");
	cv::imshow("first 2 filters", img);
	*/
	std::cout << "detections:" << detections << std::endl;

    // Initialize detection structure
    dt.patt   = cv::vector<cv::vector<int> >
            (detections,cv::vector<int>(10,0));
    dt.rsconf = cv::vector<float>(detections);
    dt.csconf = cv::vector<float>(detections);
    dt.isin   = cv::vector<cv::vector<int> >
            (detections,cv::vector<int>(3,-1));
    dt.patch  = cv::vector<cv::Mat>
            (detections,cv::Mat(patch_size,patch_size,CV_32F));
    int idx;
    //Nearest neighbor classifier
    //用 Relative Similarity 分类
    //用 Conservative Similarity 评估可信度
    for (int i=0; i<detections; i++)
    {
        idx=dt.bb[i];
        patch = frame(grid[idx]);
        getPattern(patch,dt.patch[i],mean,stdev);
        detector.NNConf(dt.patch[i],dt.isin[i],
                        dt.rsconf[i],dt.csconf[i]);
        dt.patt[i]=tmp.patt[idx];
        if (dt.rsconf[i]>detector.getNNTh())
        {
            dbb.push_back(grid[idx]);
            dconf.push_back(dt.csconf[i]);
        }
    }
    if (dbb.size()>0)
        detected=true;
    else
    {
		std::cout << "No patches detected" << std::endl;
        detected=false;
    }
}

//Learning
void TLD::learn(const cv::Mat& img)
{
	//cv::GaussianBlur(img, temp, cv::Size(9, 9), 1.5);
	///Check consistency
    BoundingBox bb;
    bb.x = cv::max(lastbox.x,0);
    bb.y = cv::max(lastbox.y,0);
    bb.width = cv::min(cv::min(img.cols-lastbox.x,lastbox.width),
                       cv::min(lastbox.width,lastbox.br().x));
    bb.height = cv::min(cv::min(img.rows-lastbox.y,lastbox.height),
                        cv::min(lastbox.height,lastbox.br().y));
    cv::Scalar mean, stdev;
    cv::Mat pattern;
    getPattern(img(bb),pattern,mean,stdev);
    cv::vector<int> isin;
    float dummy, conf;

    //重新计算一下 Relative Similarity
    detector.NNConf(pattern,isin,conf,dummy);
    if ( conf<0.5 )  //注意Relative Similarity阈值降低了，因为要接纳更多的样本
    {
        lastvalid =false;
        return;
    }
    if (pow(stdev.val[0],2)<var*0.5f)
    {
        lastvalid=false;
        return;
    }
    if(isin[2]==1) // 负样本空间中存在某个样本与pattern相似
    {
        lastvalid=false;
        return;
    }
    // 根据lastbox更新grid中每一个对象的overlap值
	// 更新过的grid[i].overlap供getOverlappingBoxes()使用
    for (int i=0;i<grid.size();i++)
    {
        grid[i].overlap = bbOverlap(lastbox,grid[i]);
    }
    cv::vector<std::pair<cv::vector<int>,int> > fern_examples;

    ///P-Expert:
    //用lastbox（上一帧结果），重新计算good,bad,best,bbhull
	good_boxes.clear();
	bad_boxes.clear();
    getOverlappingBoxes(num_closest);

	//cv::meanStdDev(img(best_box), mean, stdev); //注意是best_box，而不是我们自己框定的box
	//var = (float)pow(stdev.val[0], 2);

    if (good_boxes.size()>0)
    {
		//更新 pX pEx
		//注意：generatePositiveData生成正样本使用的是best_box，而不是lastbox
        generatePositiveData(img,num_warps);
    }
    else
    {
        lastvalid = false;
        return;
    }
    //新增Fern-Classifier正样本
    fern_examples.reserve(pX.size()+bad_boxes.size());
    fern_examples.assign(pX.begin(),pX.end());

    ///N-Expert
    //从bad_boxes挑选hard negative作为新增的Fern-Classifier负样本集
	//hard negative判别标准：fern分类器评分高于1
    int idx;
    for (int i=0; i<bad_boxes.size(); i++)
    {
        idx=bad_boxes[i];
        if (tmp.conf[idx]>=1)
        {
            //tmp.conf在detect()函数中赋值
            //是Fern分类器对能通过Patch variance的样本的评价分数
            fern_examples.push_back(std::make_pair(tmp.patt[idx],0));
        }
    }
    std::random_shuffle(fern_examples.begin(),fern_examples.end());
    //从dt.bb中挑选hard negative作为新增的NN-Classifier负样本集
	//hard negative判别标准：重叠系数 < bad_overlap (距离best box远)
    cv::vector<cv::Mat> nn_examples;
    nn_examples.reserve(pEx.size() + dt.bb.size());
	nn_examples.assign(pEx.begin(), pEx.end());
    for (int i=0; i<dt.bb.size(); i++)
    {
        idx = dt.bb[i];
		if (bbOverlap(lastbox, grid[idx]) < bad_overlap)
		{
			nn_examples.push_back(dt.patch[i]);
		}		
    }

    /// 用新样本重新训练分类器
    detector.trainF(fern_examples);
    detector.trainNN(nn_examples,(int)pEx.size(),img,lastbox);
}

//向前处理一帧
/*
 * img1: last frame (gray scale)
 * img2: current frame
*/
void TLD::processFrame(const cv::Mat& img1,const cv::Mat& img2,
                       cv::vector<cv::Point2f>& points1,
                       cv::vector<cv::Point2f>& points2,
                       BoundingBox& bbnext,
                       bool& lastboxfound)
{
	int confident_detections = 0;
	int didx;
	cv::vector<BoundingBox> cbb; //聚类之后的 bounding box
	cv::vector<float> cconf;
	
	/// 1.Track
    //更新对象的值：tbb
    //更新相关参数t：racked，tvalid，tconf
    if(lastboxfound)
    {
        //前一帧目标出现过才能T，否则只能D
        track(img1,img2,points1,points2);
    }
    else
    {
        tracked = false;
    }

    /// 2.Detect
    //更新对象的值：dt，dbb
    //更新相关参数：dconf, detected
    detect(img2);

    /// 3.Integration
    /// Combine the bounding box of the tracker &
    /// the bounding boxes of the detector
    /// into a single bounding box output by TLD
    /// If neither the tracker nor the detector output a bounding box
    /// the object is declared as not visible
    /// Otherwise the integrator outputs the maximally confident bounding box
	if (tracked)
	{
		lastvalid = tvalid;
		lastboxfound = true;
		bbnext = tbb;
		if (detected)
		{
			clusterConf(dbb, dconf, cbb, cconf);
			//D v.s. T
			for (int i = 0; i<cbb.size(); i++)
			{
				if (bbOverlap(tbb, cbb[i])<0.5 && cconf[i]>tconf)
					//D 与 T 结果差距大，而且 D 的结果又相对准确
				{
					confident_detections++; //D 优于 T 的的数目
					didx = i;
				}
			}

			if (confident_detections == 1)
				// D 优于 T 的结果就一个，那就是这个了
			{
				bbnext = cbb[didx];
				lastvalid = false; //tracker无效
				std::cout << "D" << std::endl;
			}
			else
				// 如果 D 有多个结果优于 T，进行加权处理
			{
				int cx = 0, cy = 0, cw = 0, ch = 0;
				int close_detections = 0;
				for (int i = 0; i<dbb.size(); i++)
				{
					if (bbOverlap(tbb, dbb[i])>0.7)
					{
						//对接近T的D的位置求平均
						cx += dbb[i].x;
						cy += dbb[i].y;
						cw += dbb[i].width;
						ch += dbb[i].height;
						close_detections++;
					}
				}
				if (close_detections>0)
				{
					//由于tracker只能输出一个目标框，而detector可能输出多个，因此赋予tracker更高的权值
					int weight_t = close_detections * 10; 
					bbnext.x = cvRound((float)(weight_t * tbb.x + cx) /
						(float)(weight_t + close_detections));
					bbnext.y = cvRound((float)(weight_t * tbb.y + cy) /
						(float)(weight_t + close_detections));
					bbnext.width = cvRound((float)(weight_t * tbb.width + cw) /
						(float)(weight_t + close_detections));
					bbnext.height = cvRound((float)(weight_t * tbb.height + ch) /
						(float)(weight_t + close_detections));
				}
				std::cout << "T+D" << std::endl;
			}
		}
		else
		{
			std::cout << "T" << std::endl;
		}
	}
	else
	{
		lastvalid = false;
		lastboxfound = false;
		if (detected)
		{
			clusterConf(dbb, dconf, cbb, cconf);
			if (cconf.size() == 1)
				//聚类结果只有一个，那就是这个了
			{
				bbnext = cbb[0];
				lastboxfound = true;
				std::cout << "D" << std::endl;
			}
			else
				//聚类结果有多个，取得分最高的那个
			{
				//lastboxfound = false;
				//std::cout << "Neither T Nor D" << std::endl;
				float cMax = cconf[0];
				int k = 0;
				for (int i = 0; i < cconf.size(); i++)
				{
					if (cconf[i] > cMax)
					{
						cMax = cconf[i];
						k = i;
					}
				}
				bbnext = cbb[k];
				lastboxfound = true;
				std::cout << "D" << std::endl;
			}
		}
		else
		{
			std::cout << "Neither T Nor D" << std::endl;
		}
	}
    lastbox=bbnext;
#ifdef LEARNING

    /// 4.Learn
	if (lastvalid)
	{
		learn(img2);
		std::cout << "Learning..." << std::endl;
	}

	std::cout << "Var: " << var << std::endl;
#endif
	detector.show();
}

//生成sliding windows
/*作者给出的参数
 * scales step = 1.2
 * horizontal step = 10% of width
 * vertical step = 10% of height
 * minimal bounding box size = 20 pixels
*/
void TLD::buildGrid(const cv::Mat& img, const cv::Rect& box)
{
    const float SHIFT = 0.1f;
    const float SCALES[] =  //scales step 1.2，最多21层
    {0.16151f,0.19381f,0.23257f,0.27908f,0.33490f,0.40188f,0.48225f,
     0.57870f,0.69444f,0.83333f,1.0f    ,1.20000f,1.44000f,1.72800f,
     2.07360f,2.48832f,2.98598f,3.58318f,4.29982f,5.15978f,6.19174f};
    int width, height, min_bb_side;
    //Rect bbox;
    BoundingBox bbox;
    cv::Size scale;
    int sc=0;
    for (int s=0;s<21;s++)
    {
        width = cvRound(box.width*SCALES[s]);
        height = cvRound(box.height*SCALES[s]);
        min_bb_side = std::min(height,width);
        if (min_bb_side < min_win || width > img.cols || height > img.rows)
          continue;
        scale.width = width;
        scale.height = height;
		
        scales.push_back(scale);
        //步长分别是以检测窗口width,height的0.1
        for (int y=1;y<img.rows-height;y+=cvRound(SHIFT*min_bb_side))
        {
            for (int x=1;x<img.cols-width;x+=cvRound(SHIFT*min_bb_side))
            {
                bbox.x = x;
                bbox.y = y;
                bbox.width = width;
                bbox.height = height;
                bbox.overlap = bbOverlap(bbox,BoundingBox(box));//重合面积交/并
                bbox.sidx = sc;//尺度索引
                grid.push_back(bbox);
            }
        }
        sc++;
    }
}

//计算两个矩形框的overlap
/*作者给出的定义
 * a ratio between intersection and union
 * 两个矩形框的∩ / 两个矩形框的∪
*/
float TLD::bbOverlap(const BoundingBox& box1,const BoundingBox& box2)
{
    if (box1.x > box2.x+box2.width) { return 0.0; }
    if (box1.y > box2.y+box2.height) { return 0.0; }
    if (box1.x+box1.width < box2.x) { return 0.0; }
    if (box1.y+box1.height < box2.y) { return 0.0; }

    int colInt =  cv::min(box1.x+box1.width,box2.x+box2.width) -
            cv::max(box1.x, box2.x);
    int rowInt =  cv::min(box1.y+box1.height,box2.y+box2.height) -
            cv::max(box1.y,box2.y);

    float intersection = (float)(colInt * rowInt);
    float area1 = (float)box1.width*(float)box1.height;
    float area2 = (float)box2.width*(float)box2.height;

    float overlap = intersection / (area1 + area2 - intersection);
    return overlap;
}

//从sliding windows中提取(num_closest)个good boxes 其余为bad boxes
void TLD::getOverlappingBoxes(int num_closest)
{
    float max_overlap = 0;
    for (int i=0;i<grid.size();i++)
    {
        if (grid[i].overlap > max_overlap)
        {
            max_overlap = grid[i].overlap;
            best_box = grid[i];
        }

        if (grid[i].overlap > 0.6)
        {
            good_boxes.push_back(i);
        }
        else if (grid[i].overlap < bad_overlap)
        {
            bad_boxes.push_back(i);
        }
    }

    // 只保留overlap最大的(num_closest)个good boxes
    if (good_boxes.size()>num_closest)
    {
        //OComparator(grid) 是自定义的比较函数
        //因为这里涉及自定义类，因此要用户指定比较规则
        std::nth_element(good_boxes.begin(),
                         good_boxes.begin()+num_closest,
                         good_boxes.end(),
                         OComparator(grid));
        good_boxes.resize(num_closest);
    }
    getBBHull();
}

//提取所有good boxes的一个外包
void TLD::getBBHull()
{
    int x1=INT_MAX, x2=0;
    int y1=INT_MAX, y2=0;
    int idx;
    for (int i=0;i<good_boxes.size();i++)
    {
        idx= good_boxes[i];
        x1=cv::min(grid[idx].x,x1);
        y1=cv::min(grid[idx].y,y1);
        x2=cv::max(grid[idx].x+grid[idx].width,x2);
        y2=cv::max(grid[idx].y+grid[idx].height,y2);
    }
    bbhull.x = x1;
    bbhull.y = y1;
    bbhull.width = x2-x1;
    bbhull.height = y2 -y1;
}

// 尺寸标准化(patch_size X patch_size) & 均值归零
void TLD::getPattern(const cv::Mat& img, cv::Mat& pattern,
                     cv::Scalar& mean,cv::Scalar& stdev)
{
    cv::resize(img,pattern,cv::Size(patch_size,patch_size));
    cv::meanStdDev(pattern,mean,stdev);
    pattern.convertTo(pattern,CV_32F);
    pattern = pattern-mean.val[0];

    /// 函数 cv::meanStdDev()
    /// 求均值和标准差
}

//将bb网格分割；points存储网格交点
void TLD::bbPoints(cv::vector<cv::Point2f>& points, const BoundingBox& bb)
{
    int max_pts=10; //10 X 10网格
    int margin_h=0; //不留白
    int margin_v=0;

    int stepx = (bb.width-2*margin_h)/max_pts;
    int stepy = (bb.height-2*margin_v)/max_pts;
    for (int y=bb.y+margin_v; y<bb.y+bb.height-margin_v; y+=stepy)
    {
        for (int x=bb.x+margin_h; x<bb.x+bb.width-margin_h; x+=stepx)
        {
            points.push_back(cv::Point2f((float)x,(float)y));
        }
    }
}

//根据Tracker结果计算后一帧中矩形框的位置和大小
void TLD::bbPredict(const cv::vector<cv::Point2f>& points1,
               const cv::vector<cv::Point2f>& points2,
               const BoundingBox& bb1,BoundingBox& bb2)
{
    int npoints = (int)points1.size();
    cv::vector<float> xshift(npoints);
    cv::vector<float> yshift(npoints);

    // 用位移的中值，作为目标位移的估计
    for (int i=0; i<npoints; i++)
    {
        xshift[i]=points2[i].x-points1[i].x;
        yshift[i]=points2[i].y-points1[i].y;
    }
    float dx = median(xshift);
    float dy = median(yshift);

    // 用点对之间的距离的伸缩比例的中值，作为目标尺度变化的估计
    float s;
    if (npoints>1)
    {
        cv::vector<float> d;
        d.reserve(npoints*(npoints-1)/2);
        int a =0;
        for (int i=0; i<npoints; i++)
        {
            for (int j=i+1; j<npoints; j++)
            {
                d.push_back((float)cv::norm(points2[i]-points2[j])
                            / (float)cv::norm(points1[i]-points1[j]));
                a++;
            }
        }
        s = median(d);
    }
    else
    {
        s = 1.0;
    }

    // top-left 坐标的偏移(s1,s2)
    float s1 = 0.5f*(s-1)*bb1.width;
    float s2 = 0.5f*(s-1)*bb1.height;
    bb2.x = cvRound( bb1.x + dx -s1);
    bb2.y = cvRound( bb1.y + dy -s2);
    bb2.width = cvRound(bb1.width*s);
    bb2.height = cvRound(bb1.height*s);
}

//注意不是TLD成员函数
bool bbcomp(const BoundingBox& b1,const BoundingBox& b2)
{
    TLD t;
    if (t.bbOverlap(b1,b2)<0.5)
        return false;
    else
        return true;
}

//将矩形框聚类；聚类依据: bb overlap
void TLD::clusterConf(const cv::vector<BoundingBox>& dbb,
                 const cv::vector<float>& dconf,
                 cv::vector<BoundingBox>& cbb,
                 cv::vector<float>& cconf)
{
    int numbb =(int)dbb.size();
    cv::vector<int> T; //聚类结果
    int c=1;           //类别数目
    float space_thr = 0.5;

    switch (numbb)
    {
    case 1:
        cbb=cv::vector<BoundingBox>(1,dbb[0]);
        cconf=cv::vector<float>(1,dconf[0]);
        return;
    case 2:
        T =cv::vector<int>(2,0);
        if (1-bbOverlap(dbb[0],dbb[1])>space_thr) //dbb 中两个框分歧很大
        {
            T[1]=1;
            c=2;
        }
        break;
    default:
        T = cv::vector<int>(numbb,0);
        //OpenCV聚类函数
        //由于使用了自定义类BoundigBox，需要自定义比较函数bbcomp()并传送其句柄
        c = cv::partition(dbb,T,(*bbcomp));
        break;
    }
    cconf=cv::vector<float>(c);
    cbb=cv::vector<BoundingBox>(c);
    BoundingBox bx;
    for (int i=0; i<c; i++)
    {
        float cnf=0;
        int N=0,mx=0,my=0,mw=0,mh=0;
        for (int j=0; j<T.size(); j++)
        {
            if (T[j]==i)  //属于标签是i的类
            {
                cnf=cnf+dconf[j];
                mx=mx+dbb[j].x;
                my=my+dbb[j].y;
                mw=mw+dbb[j].width;
                mh=mh+dbb[j].height;
                N++;
            }
        }
        if (N>0)
        {
            cconf[i]=cnf/N;
            //求标签是i的类中所有框的平均
            bx.x=cvRound(mx/N);
            bx.y=cvRound(my/N);
            bx.width=cvRound(mw/N);
            bx.height=cvRound(mh/N);
            cbb[i]=bx;
        }
    }
}

/*
//get var of a patch
//论文中给出公式：var = E(p^2) - E^2(p)
//注：积分图定义 sum(X,Y) = ∑ image(x,y) :s.t. x < X, y < Y
float getVar(const BoundingBox& box,
	const cv::Mat& sum, const cv::Mat& sqsum)
{
	//积分图中box四个角的坐标
	float brs = sum.at<int>(box.y + box.height, box.x + box.width);
	float bls = sum.at<int>(box.y + box.height, box.x);
	float trs = sum.at<int>(box.y, box.x + box.width);
	float tls = sum.at<int>(box.y, box.x);

	//平方积分图中box四个角的坐标
	float brsq = sqsum.at<float>(box.y + box.height, box.x + box.width);
	float blsq = sqsum.at<float>(box.y + box.height, box.x);
	float trsq = sqsum.at<float>(box.y, box.x + box.width);
	float tlsq = sqsum.at<float>(box.y, box.x);

	float mean = (brs + tls - trs - bls) / ((float)box.area());
	float sqmean = (brsq + tlsq - trsq - blsq) / ((float)box.area());
	return sqmean - mean*mean;
}
*/