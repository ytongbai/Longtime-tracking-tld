#include "include\tracker.h"

Tracker::Tracker()
{
    criteria = cv::TermCriteria(cv::TermCriteria::COUNT+cv::TermCriteria::EPS,
                                20, 0.03);
    window_size = cv::Size(4,4);
    level = 5;//maxLevel
    lambda = 0.5;
}

void Tracker::normCrossCorrelation(const cv::Mat& img1,const cv::Mat& img2,
                                   cv::vector<cv::Point2f>& points1,
                                   cv::vector<cv::Point2f>& points2)
{
    cv::Mat rec0(10,10,CV_8U);
    cv::Mat rec1(10,10,CV_8U);
    cv::Mat res(1,1,CV_32F);
    for (unsigned int i = 0; i < points1.size(); i++)
    {
        if (status_forward[i] == 1) //跟踪成功
        {
            //以当前点为中心，提取10*10的小块
            cv::getRectSubPix(img1, cv::Size(10,10), points1[i],rec0);
            cv::getRectSubPix(img2, cv::Size(10,10), points2[i],rec1);
            //Cross Correlation
            cv::matchTemplate(rec0, rec1, res, CV_TM_CCOEFF_NORMED);
            similarity[i] = ((float *)(res.data))[0];
        }
        else
        {
            similarity[i] = 0.0;
        }
    }
}

//Filter out points where FB_error[i]  > median(FB_error)
//Filter out points where sim_error[i] > median(sim_error)
bool Tracker::filterPts(cv::vector<cv::Point2f>& points1,
                        cv::vector<cv::Point2f>& points2)
{
  int i, k;

  ///一轮筛选：Cross Correlation
  sim_media = median(similarity);
  for( i= k = 0; i<points2.size(); i++ )
  {
    //滤除前向跟踪失败的点
    if( !status_forward[i])
      continue;
    //滤除sim_error[i] > median(sim_error)的点
    if(similarity[i]> sim_media)
    {
        points1[k] = points1[i];
        points2[k] = points2[i];
        FBerror[k] = FBerror[i];
		status_backward[k] = status_backward[i];
        k++;
    }
  }
  //此帧跟踪失败
  if (k==0)
    return false;

  points1.resize(k);
  points2.resize(k);
  FBerror.resize(k);
  status_backward.resize(k);

  ///二轮筛选：Cross Correlation
  fb_media = median(FBerror);
  for( i=k = 0; i<points2.size(); i++ )
  {
    //滤除后向跟踪失败的点
    if( !status_backward[i])
        continue;
    //滤除FB_error[i]  > median(FB_error)的点
    if(FBerror[i] <= fb_media)
    {
        points1[k] = points1[i];
        points2[k] = points2[i];
        k++;
    }
  }
  points1.resize(k);
  points2.resize(k);

  if (k>0)
    return true;
  else
    return false;
}

//跟踪一帧：points1->points2
//注意points2中剩余的特征点已经过筛选
bool Tracker::trackOneFrame(const cv::Mat& img1, const cv::Mat& img2,
                            cv::vector<cv::Point2f> &points1,
                            cv::vector<cv::Point2f> &points2)
{
  //1. Track points;Forward-Backward tracking
  // 注意这里 similarity 和 FBerror 都是形式上的调用，它们的实际值在后面计算
  cv::calcOpticalFlowPyrLK(img1, img2, points1, points2, status_forward,
                           similarity, window_size, level, criteria, 0, 0);
  cv::calcOpticalFlowPyrLK(img2, img1, points2, pointsFB, status_backward,
                           FBerror, window_size, level, criteria, 0, 0);
  //2. Compute similarity
  normCrossCorrelation(img1, img2, points1, points2);
  
  //3. Compute FBerror
  for( unsigned int i= 0; i<points1.size(); i++ )
  {
      FBerror[i] = (float)cv::norm(pointsFB[i]-points1[i]);//欧氏距离
  }

  //4. Filter Points
  return filterPts(points1,points2);
}
