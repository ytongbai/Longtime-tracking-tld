#include "include\utilities.h"

void drawBox(cv::Mat& image, cv::Rect box,
             cv::Scalar color, int thick)
{
  cv::rectangle(image,box,color, thick);
} 

void drawPoints(cv::Mat& image,
                cv::vector<cv::Point2f> points,
                cv::Scalar color)
{
  cv::vector<cv::Point2f>::const_iterator it = points.begin();
  cv::vector<cv::Point2f>::const_iterator ite = points.end();
  for( ; it != ite; it++ )
  {
      cv::Point center( cvRound(it->x ), cvRound(it->y));
      cv::circle(image,center,2,color);
  }
  ///cvRound()是OpenCV提供的C接口函数，用于四舍五入
}

cv::Mat createMask(const cv::Mat& image, cv::Rect box)
{
    cv::Mat mask = cv::Mat::zeros(image.rows,image.cols,CV_8U);
    drawBox(mask,box,cv::Scalar::all(255),CV_FILLED);
    return mask;
}

float median(cv::vector<float> v)
{
    int n = (int)floor(v.size() / 2);
    std::nth_element(v.begin(), v.begin()+n, v.end());
    return v[n];

    ///nth_element(start,start+n,end)
    ///STL函数，包含在头文件<algorithmfwd.h>中
    ///功能如下：
    ///元素start+n位置不变，大于它的元素都排列到它之前，小于它的元素都排列到它之后
    ///注意：不保证是有序排列
}

//生成一组标签索引值并乱序输出
cv::vector<int> index_shuffle(int begin,int end)
{
    cv::vector<int> indexes(end-begin);
    for (int i=begin;i<end;i++)
    {
        indexes[i]=i;
    }
    std::random_shuffle(indexes.begin(),indexes.end());
//    cv::randShuffle(indexes);
    return indexes;

    ///random_shuffle(start,end)
    ///STL函数，包含在头文件<algorithmfwd.h>中
    ///功能为“洗牌”
    ///算法内部大致如下：
    ///用for循环遍历容器，每次for生成一个随机数，将当前元素与随机数对应的元素互换，达到乱序的目的
    ///OpenCV中提供了同功能的函数cv::randShuffle()
}
