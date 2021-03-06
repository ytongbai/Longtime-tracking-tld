#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <math.h>
#include <algorithm>

#ifndef UTILITIES_H
#define UTILITIES_H

///一些辅助函数

void drawBox(cv::Mat& image,
             cv::Rect box,
             cv::Scalar color = cv::Scalar::all(255),
             int thick=1);

void drawPoints(cv::Mat& image,
                cv::vector<cv::Point2f> points,
                cv::Scalar color=cv::Scalar::all(255));

cv::Mat createMask(const cv::Mat& image, cv::Rect box);

float median(cv::vector<float> v);

cv::vector<int> index_shuffle(int begin,int end);

#endif
