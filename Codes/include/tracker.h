#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#include "utilities.h"

#ifndef TRACKER_H
#define TRACKER_H

///Media Flow Tracker
class Tracker
{
public:
    Tracker();

private:
	// calcOpticalFlowPyrLK()函数参数
    cv::vector<cv::Point2f> pointsFB;  //追踪的特征点
	cv::vector<uchar> status_forward;  //optical flow前向跟踪状态
	cv::vector<uchar> status_backward; //optical flow反响跟踪状态
    cv::Size window_size;              //窗口尺寸
    int level;
    cv::TermCriteria criteria;
    float lambda;
	//其他
	cv::vector<float> similarity;      //相似度度量：Cross Correlation
	cv::vector<float> FBerror;         //Forward-Backword Error
	float sim_media;                   //similarity 中值
	float fb_media;                    //FBerror中值

private:
    //Calculate Cross Correlation
    void normCrossCorrelation(const cv::Mat& img1,const cv::Mat& img2,
                              cv::vector<cv::Point2f>& points1,
                              cv::vector<cv::Point2f>& points2);
    //过滤特征点
    bool filterPts(cv::vector<cv::Point2f>& points1,
                   cv::vector<cv::Point2f>& points2);

public:
	float getFB() { return fb_media; }
    //Tracker调用接口
    bool trackOneFrame(const cv::Mat& img1, const cv::Mat& img2,
                       cv::vector<cv::Point2f> &points1,
                       cv::vector<cv::Point2f> &points2);
};

#endif // TRACKER_H
