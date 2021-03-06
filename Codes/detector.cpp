#include "include\detector.h"
#include<strstream>
#include<Windows.h>
using namespace cv;
Detector::Detector()
{
	ncc_thesame = 0.95f;
	treeNum = 10;
	featureNum = 13;
	thr_fern = 0.6f;
	thr_nn = 0.6f;
	thr_nn_valid = thr_nn;
	std::strstream ss;
	std::string name;
}

//Generate 2-bit BP Features(for the first frame in the video)
void Detector::prepare(const cv::vector<cv::Size>& scales)
{
	/// 1. Initialize test locations for features
	/// 随机产生需要坐标对；注意范围[0,1)；实际对应的坐标要乘以实际的width和height
	/// 特征点一旦生成就保持不变
	int totalFeatures = treeNum*featureNum;
	features = cv::vector< cv::vector<Feature> >
		(scales.size(), cv::vector<Feature>(totalFeatures));
	cv::RNG& rng = cv::theRNG(); //random number generator
	float x1f, x2f, y1f, y2f;
	int x1, x2, y1, y2;
	for (int i = 0; i < totalFeatures; i++)
	{
		//随机产生[0,1)之间的浮点数
		x1f = (float)rng;
		y1f = (float)rng;
		x2f = (float)rng;
		y2f = (float)rng;
		for (int s = 0; s < (int)scales.size(); s++)
		{
			x1 = cvRound(x1f * scales[s].width);
			y1 = cvRound(y1f * scales[s].height);
			x2 = cvRound(x2f * scales[s].width);
			y2 = cvRound(y2f * scales[s].height);
			features[s][i] = Feature(x1, y1, x2, y2);
		}
	}

	/// 2. Initialize Posteriors
	for (int i = 0; i < treeNum; i++)
	{
		posteriors.push_back(cv::vector<float>((int)pow(2.0, featureNum), 0.0));
		pCounter.push_back(cv::vector<int>((int)pow(2.0, featureNum), 0));
		nCounter.push_back(cv::vector<int>((int)pow(2.0, featureNum), 0));
	}
}

//输入某一个随机蕨，更新样本集的统计直方图
void Detector::update(const cv::vector<int>& fern, int C, int N)
{
	int idx;
	for (int i = 0; i < treeNum; i++)
	{
		idx = fern[i]; //低13位是有意义的特征
		//C=1，正样本，C=0，负样本
		if (C == 1)
			pCounter[i][idx] += N;
		else
			nCounter[i][idx] += N;
		//更新统计直方图
		if (pCounter[i][idx] == 0)
			posteriors[i][idx] = 0.0f;
		else
			posteriors[i][idx] = ((float)(pCounter[i][idx])) /
			((float)(pCounter[i][idx] + nCounter[i][idx]));
	}
}

//提取某个指定样本的随机蕨（存储到 fern 中）
void Detector::getFeatures(const cv::Mat& image,
	const int& scale_idx,
	cv::vector<int>& fern)
{
	int leaf;  //int至少16位，够用了（>13）
	for (int t = 0; t < treeNum; t++)
	{
		leaf = 0;
		for (int f = 0; f < featureNum; f++)
		{
			//依次得到每一位
			Feature feature = features[scale_idx][t*treeNum + f];
			leaf = (leaf << 1) + feature(image);
		}
		fern[t] = leaf;
	}
}

//计算给定随机蕨的得票数
float Detector::measure_forest(cv::vector<int> fern)
{
	float votes = 0.0f;
	for (int i = 0; i < treeNum; i++)
	{
		votes += posteriors[i][fern[i]];
	}
	return votes;
}

// 训练：随机蕨分类器
// 注意分类器是迭代更新的（每测试一个样本，就更新以下；下一个样本送入的已经是上次循环更新过的样本）
// 因此输入的样本集必须是乱序的（P和N样本是混杂的）
void Detector::trainF(const cv::vector<std::pair<cv::vector<int>, int> >& ferns)
{
	thrP = thr_fern*treeNum; //0.6*10
	thrN = 0.5f*treeNum;
	for (int i = 0; i < (int)ferns.size(); i++)
	{
		if (ferns[i].second == 1) //正样本
		{
			//容易分错的正样本：是正样本但是支持率小于0.6
			if (measure_forest(ferns[i].first) <= thrP)
				update(ferns[i].first, 1, 1);
		}
		else  //负样本
		{
			//容易分错的负样本：是负样本但是支持率大于0.5
			if (measure_forest(ferns[i].first) >= thrN)
				update(ferns[i].first, 0, 1);
		}
	}
}

//计算相似度度量参数（在训练NN分类器中使用）
//具体公式在论文中有
void Detector::NNConf(const cv::Mat& example,
	cv::vector<int>& isin,
	float& rsconf, float& csconf)
{
	isin = cv::vector<int>(3, -1);
	if (pEx.empty())
	{
		//IF positive examples in the model are not defined
		//THEN everything is negative
		rsconf = 0;
		csconf = 0;
		return;
	}
	if (nEx.empty())
	{
		//IF negative examples in the model are not defined
		//THEN everything is positive
		rsconf = 1;
		csconf = 1;
		return;
	}

	cv::Mat ncc(1, 1, CV_32F);
    float nccP, maxP = 0;
    float nccN, maxN = 0;
	int maxPidx; // maxP 对应的下标索引
	float csmaxP;
	bool anyP = false;
	bool anyN = false;
	//取出前50%的正样本；在计算Conservative similarit时用到
	int validatedPart = (int)ceil(pEx.size()*0.5);

	//遍历正样本集
	for (int i = 0; i < (int)pEx.size(); i++)
	{
		// measure NCC to positive examples
		/*cv::imshow("example", example);
		cv::imshow("233", pEx[i]);
		cv::waitKey(10);*/
		cv::matchTemplate(pEx[i], example, ncc, CV_TM_CCORR_NORMED);
		//相关系数的取值范围是[-1,1]，加上1变成[0,2]，再将范围缩小为[0,1]
		//nccP=(((float*)ncc.data)[0]+1)*0.5;
		/*if (ncc.empty())
			cout << "dddddddddddddddddddddddddddddddddd";*/
		nccP = ((float*)ncc.data)[0];
		nccP = nccP + 1;
		nccP = nccP*0.5f;
		if (nccP > ncc_thesame) //0.95
			anyP = true;
		if (nccP > maxP)
		{
			maxP = nccP;     //Relative similarity
			maxPidx = i;
			if (i < validatedPart)
				csmaxP = maxP; //Conservative similarity
		}
	}

	//遍历负样本集
	for (int i = 0; i < (int)nEx.size(); i++)
	{
		//measure NCC to negative examples
		cv::matchTemplate(nEx[i], example, ncc, CV_TM_CCORR_NORMED);
		nccN = (((float*)ncc.data)[0] + 1)*0.5f;
		if (nccN > ncc_thesame)  //0.95
			anyN = true;
		if (nccN > maxN)
			maxN = nccN;
	}

	//if he query patch is highly correlated with
	//any positive patch in the model
	//then it is considered to be one of them
	//the same for anyN
	if (anyP) isin[0] = 1;
	if (anyN) isin[2] = 1;
	//get the index of the maximall correlated positive patch
	isin[1] = maxPidx;

	//注意这里和原文公式有区别；然而原理上并没有区别
	//Relative Similarity
	float dN = 1 - maxN;
	float dP = 1 - maxP;
	rsconf = dN / (dN + dP);
	//Conservative Similarity
	dP = 1 - csmaxP;
	csconf = dN / (dN + dP);
}

//训练：NN分类器
//这个分类器的训练是时间消耗最多的
//输入的样本集必须是乱序的（P和N样本是混杂的），原因同trainF()
/*
* In:
*	nn_examples:[+][+]...[-][-]....
*   其中第一个正样本是best box
* Out:
*	nEx <-- nn_examples的负样本中满足 Relative similarity  < 0.5
*	pEx <-- nn_examples的正样本中满足 Relative similarity  > thr_nn
* 注意： 是不断地增加 nEx 和 pEx
*/
void Detector::trainNN(const cv::vector<cv::Mat>& nn_examples, int numP)
{
	// 第一种构建方法：P与N分开（前面n个是P，后面m个是N）
	float conf, dummy;
	cv::vector<int> isin;
	cv::vector<int> y(nn_examples.size(), 0);
	y[0] = 2;
	for (int i = 1; i < numP; i++)
		y[i] = 1;

	for (int i = 0; i < nn_examples.size(); i++)
	{
		NNConf(nn_examples[i], isin, conf, dummy);
		if (y[i] == 2 && conf <= thr_nn)
		{
			if (isin[1] < 0)
				// 已有正样本集中没有任何一个接近nn_examples中的best box
				// 那么就舍弃从前的所有正样本，重新建立正样本集
			{
				pEx.clear();
				pEx = cv::vector<cv::Mat>(1, nn_examples[i]);
				std::strstream ss;
				std::string name;
#ifdef LOADING

				if (true)
				{
					for (int i = 0; i < 32; i++)
					{

						ss.clear();
						name.clear();
						ss << i;
						ss >> name;

						switch (name.length())
						{
						case 1:
							name = "F:/TLD-robomaster/robomaster-psample/arrow000" + name + ".xml";
							break;
						case 2:
							name = "F:/TLD-robomaster/robomaster-psample/arrow00" + name + ".xml";
							break;
						case 3:
							name = "F:/TLD-robomaster/robomaster-psample/arrow0" + name + ".xml";
							break;
						case 4:
							name = "F:/TLD-robomaster/robomaster-psample/arrow" + name + ".xml";
							break;
						}
						cv::FileStorage fs(name, cv::FileStorage::READ);
						cv::Mat m;
						fs["Mat"] >> m;
					
						pEx.push_back(m);
						
					}
					for (int i = 0; i < 20; i++)
					{

						ss.clear();
						name.clear();
						ss << i;
						ss >> name;

						switch (name.length())
						{
						case 1:
							name = "F:/TLD-robomaster/robomaster-nsample/arrow000" + name + ".xml";
							break;
						case 2:
							name = "F:/TLD-robomaster/robomaster-nsample/arrow00" + name + ".xml";
							break;
						case 3:
							name = "F:/TLD-robomaster/robomaster-nsample/arrow0" + name + ".xml";
							break;
						case 4:
							name = "F:/TLD-robomaster/robomaster-nsample/arrow" + name + ".xml";
							break;
						}
						cv::FileStorage fs(name, cv::FileStorage::READ);
						cv::Mat m;
						fs["Mat"] >> m;
						nEx.push_back(m);
					}
				}

#endif //LOADING
				continue;
			}
			// 否则就在原有正样本集上追加
			pEx.push_back(nn_examples[i]);
			/*char s = pEx.size();
			string str;
			str = "F:/robomaster-psample/";
			str += s;
			str += ".jpg";
			cv::imwrite(str,nn_examples[i]);*/
		}
		if (y[i] == 1 && conf <= thr_nn)
		{
			pEx.push_back(nn_examples[i]);
			/*char s = pEx.size();
			string str;
			str = "F:/robomaster-psample/";
			str += s;
			str += ".jpg";
			cv::imwrite(str, nn_examples[i]);*/
		}
		if (y[i] == 0 && conf > 0.5)
		{
			nEx.push_back(nn_examples[i]);
			/*char s = nEx.size();
			string str;
			str = "F:/robomaster-nsample/";
			str += s;
			str += ".jpg";
			cv::imwrite(str, nn_examples[i]);*/
		}
	}

	// 第二种方法不对！因为必须首先添加正样本才能准确识别负样本！
	// （根据前面已经添加好的正样本选择conf > thr_nn的hard N）
	/*
	// 第二种构建方法：：P与N打乱顺序
	float conf, dummy;
	cv::vector<int> isin;
	cv::vector<int> idx = index_shuffle(0, (int)nn_examples.size());
	cv::vector<int> y(nn_examples.size(), 0);
	for (int i = 0; i < (int)nn_examples.size(); i++)
	{
		if (idx[i] < numP && idx[i]>0)
			y[i] = 1;
		if (idx[i] == 0)
			y[i] = 2;
	}

	for (int i = 0; i< (int)nn_examples.size(); i++)
	{
		NNConf(nn_examples[idx[i]], isin, conf, dummy);
		if (y[i] == 2 && conf < thr_nn)
		{
			if (isin[1]<0)
				// 已有正样本集中没有任何一个接近nn_examples中的best box
				// 那么就舍弃从前的所有正样本，重新建立正样本集
			{
				pEx = cv::vector<cv::Mat>(1, nn_examples[idx[i]]);
				continue;
			}
			// 否则就在原有正样本集上追加
			pEx.push_back(nn_examples[idx[i]]);
		}
		if (y[i] == 1 && conf < thr_nn)
			pEx.push_back(nn_examples[idx[i]]);
		if (y[i] == 0 && conf > thr_nn)
			nEx.push_back(nn_examples[idx[i]]);

	}
	*/
}
void Detector::trainNN(const cv::vector<cv::Mat>& nn_examples, int numP,cv::Mat frame, cv::Rect lastrect)
{
	// 第一种构建方法：P与N分开（前面n个是P，后面m个是N）
	float conf, dummy;
	cv::vector<int> isin;
	cv::vector<int> y(nn_examples.size(), 0);
	y[0] = 2;
	for (int i = 1; i < numP; i++)
		y[i] = 1;

	for (int i = 0; i < nn_examples.size(); i++)
	{
		NNConf(nn_examples[i], isin, conf, dummy);
		if (y[i] == 2 && conf <= thr_nn)
		{
			if (isin[1] < 0)
				// 已有正样本集中没有任何一个接近nn_examples中的best box
				// 那么就舍弃从前的所有正样本，重新建立正样本集
			{
				pEx.clear();
				pEx = cv::vector<cv::Mat>(1, nn_examples[i]);
				continue;
			}
			// 否则就在原有正样本集上追加
			pEx.push_back(nn_examples[i]);
#ifdef SAVE
			std::strstream ss;
			std::string str = "F:/TLD-robomaster/robomaster-psample/", s;
			SYSTEMTIME sys;
			GetLocalTime(&sys);
			ss << sys.wDay << sys.wHour << sys.wMinute << sys.wSecond << sys.wMilliseconds;
			ss >> s;
			/*if (lastrect.area()>0 && lastrect.x + lastrect.width<frame.cols&&lastrect.y + lastrect.height<frame.rows)
				cv::imwrite("F:/robomaster-psample/" + s + ".jpg", frame(lastrect));*/
			str = str + s + ".xml";
			cv::FileStorage fs(str, cv::FileStorage::WRITE);
			fs << "Mat" << nn_examples[i];
			fs.release();
#endif // SAVE
		}
		if (y[i] == 1 && conf <= thr_nn)
		{
			pEx.push_back(nn_examples[i]);

#ifdef SAVE
			std::strstream ss;
			std::string str = "F:/TLD-robomaster/robomaster-psample/", s;
			SYSTEMTIME sys;
			GetLocalTime(&sys);
			ss << sys.wDay << sys.wHour << sys.wMinute << sys.wSecond << sys.wMilliseconds;
			ss >> s; 
			/*if (lastrect.area()>0 && lastrect.x + lastrect.width<frame.cols&&lastrect.y + lastrect.height<frame.rows)
				cv::imwrite("F:/robomaster-psample/" + s + ".jpg", frame(lastrect));*/
			str = str + s + ".xml";
			cv::FileStorage fs(str, cv::FileStorage::WRITE);
			fs << "Mat" << nn_examples[i];
			fs.release();
#endif // SAVE
		}
		if (y[i] == 0 && conf > 0.5)
		{
			nEx.push_back(nn_examples[i]);
#ifdef SAVE
			std::strstream ss;
			std::string str = "F:/TLD-robomaster/robomaster-nsample/", s;
			SYSTEMTIME sys;
			GetLocalTime(&sys);
			ss << sys.wDay << sys.wHour << sys.wMinute << sys.wSecond << sys.wMilliseconds;
			ss >> s;
			str = str + s + ".xml";
			cv::FileStorage fs(str, cv::FileStorage::WRITE);
			fs << "Mat" << nn_examples[i];
			fs.release();
#endif // SAVE
		}
	}
}

//修正分类器阈值
void Detector::evaluateTh(const cv::vector<std::pair<cv::vector<int>, int> >& nXT,
	const cv::vector<cv::Mat>& nExT)
{
	//修正随机蕨分类器阈值
	float fconf;
	for (int i = 0; i < (int)nXT.size(); i++)
	{
		fconf = measure_forest(nXT[i].first) / treeNum;//平均
		if (fconf > thr_fern)
			thr_fern = fconf;  //修正初始值
	}

	//修正NN分类器阈值
	cv::vector<int> isin;
	float rsconf, csconf;
	for (int i = 0; i < (int)nExT.size(); i++)
	{
		NNConf(nExT[i], isin, rsconf, csconf);
		if (rsconf > thr_nn)
			thr_nn = rsconf;
	}

	if (thr_nn > thr_nn_valid)
		thr_nn_valid = thr_nn;
}

//显示当前样本集
void Detector::show()
{
	cv::Mat pExamples((int)pEx.size()*pEx[0].rows, pEx[0].cols, CV_8U);
	cv::Mat nExamples((int)nEx.size()*nEx[0].rows, nEx[0].cols, CV_8U);
	double minval;
	cv::Mat ex(pEx[0].rows, pEx[0].cols, pEx[0].type());

	for (int i = 0; i < (int)pEx.size(); i++)
	{
		cv::minMaxLoc(pEx[i], &minval, NULL, NULL, NULL);
		pEx[i].copyTo(ex);
		ex = ex - minval;
		cv::Mat tmp = pExamples.rowRange(cv::Range(i*pEx[i].rows, (i + 1)*pEx[i].rows));
		ex.convertTo(tmp, CV_8U);
	}

	for (int i = 0; i < (int)nEx.size(); i++)
	{
		cv::minMaxLoc(nEx[i], &minval, NULL, NULL, NULL);
		nEx[i].copyTo(ex);
		ex = ex - minval;
		cv::Mat tmp = nExamples.rowRange(cv::Range(i*nEx[i].rows, (i + 1)*nEx[i].rows));
		ex.convertTo(tmp, CV_8U);
	}

	imshow("Positive Examples", pExamples);
	imshow("Negative Examples", nExamples);
}
