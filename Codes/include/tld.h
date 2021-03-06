#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/legacy/legacy.hpp>

#include "utilities.h"
#include "tracker.h"
#include "detector.h"

#include <cmath>
#include<cstring>
#include<strstream>

#ifndef TLD_H
#define TLD_H

///Sub Classes
//Bounding Boxes
class BoundingBox : public cv::Rect
{
public:
    BoundingBox(){}
    BoundingBox(cv::Rect r): cv::Rect(r){}
public:
    float overlap; // Overlap with current Bounding Box
    int sidx;      // 注意是实际搜索的层的索引
};

//Detection class
class DetStruct
{
public:
    DetStruct(){}
public:
    cv::vector<int> bb;
    cv::vector<cv::vector<int> > patt;
    cv::vector<float> rsconf;          //Relative Similarity
    cv::vector<float> csconf;          //Conservative Similarity
    cv::vector<cv::vector<int> > isin;
    cv::vector<cv::Mat> patch;         //Corresponding patches
};

//Temporal class
class TempStruct
{
public:
    TempStruct(){}
public:
    cv::vector<cv::vector<int> > patt;
    cv::vector<float> conf;
};

class OComparator
{
public:
  OComparator(const cv::vector<BoundingBox>& _grid):grid(_grid){}
  bool operator()(int idx1,int idx2)
  {
      return grid[idx1].overlap > grid[idx2].overlap;
  }
private:
  cv::vector<BoundingBox> grid;
};

class CComparator
{
public:
  CComparator(const cv::vector<float>& _conf):conf(_conf){}
  bool operator()(int idx1,int idx2)
  {
      return conf[idx1]> conf[idx2];
  }
private:
  cv::vector<float> conf;

};

class TLD
{
public:
    TLD();
    
private:
	// 1.类对象
    cv::PatchGenerator generator;
    Detector detector;
    Tracker tracker;

    /// 2.固定参数
	int min_win;        //第一帧手动初始化框的最小尺寸
	float bad_overlap;  //overlap阈值，小于该值则认为矩形框相聚远（bad_overlap=0.2）
	

    /// 3.generatePositiveData相关参数
	int   good_patches; //初始化正样本集时随机选取good_patches个正样本加入NN分类器正样本集
    int   num_closest;  //目标框周围随机选取num_closest个框（num_closest=10）
    int   num_warps;    //num_closest个框中每一个生成num_warps个仿射变换 （num_warps=20）
	int   patch_size;   //每个样本统一变换成 patch_size x patch_size 的尺寸（patch_size=15）
	// 三个仿射变换参数
    int   noise; 
    float angle;     
    float scale;

	/// 4.generateNegativeData相关参数
	int bad_patches;  //初始化负样本集时随机选取bad_patches个负样本加入NN分类器负样本集

	/// 5. Detecror第一关：Patch variance
	float var; //方差阈值

	/// 6. Detecror第二关：Fern分类器
	// Training data
    cv::vector<std::pair<cv::vector<int>, int> > pX;  // positive ferns <features,labels=1>
	cv::vector<std::pair<cv::vector<int>, int> > nX;  // negative ferns <features,labels=0>
	// Test data
	cv::vector<std::pair<cv::vector<int>, int> > nXT; //negative data to Test

	/// 7. Detecror第三关：NN分类器
	// Training data
	cv::vector<cv::Mat> pEx; //positive NN examples
	cv::vector<cv::Mat> nEx; //negative NN examples
	//Test data
	cv::vector<cv::Mat> nExT;//negative NN examples to Test
    
	/// 8.向前处理一帧过程中使用的
    //Last frame data
    BoundingBox lastbox; // 下一帧要跟踪的目标
    bool lastvalid;      // Tracker 的轨迹是否有效
    //Current frame;Tracker data
    bool tracked;        // 是否成功跟踪
    BoundingBox tbb;     // Tracker 锁定的目标位置
	float tconf;         // 目标在tbb的把握（相关系数度量）
    bool tvalid;         // tvalid=true:Tracker 认为此次跟踪有效（成功找到了目标）
    //Current frame;Detector data
	DetStruct dt;
    TempStruct tmp;
    cv::vector<BoundingBox> dbb;
    cv::vector<float> dconf;
    bool detected;       //是否检测到目标
    //Bounding Boxes
    cv::vector<BoundingBox> grid;// set of sliding windows
    cv::vector<cv::Size> scales; // sliding windows 尺寸
    cv::vector<int> good_boxes;  // indexes of bboxes with overlap > 0.6
    cv::vector<int> bad_boxes;   // indexes of bboxes with overlap < 0.2
    BoundingBox bbhull;          // hull of good_boxes；所有good_boxes的外包
    BoundingBox best_box;        // maximum overlapping bbox

private:
	// 核心处理步骤
	void generatePositiveData(const cv::Mat& frame, int num_warps);
	void generateNegativeData(const cv::Mat& frame);
	void track(const cv::Mat& img1, const cv::Mat& img2,
		cv::vector<cv::Point2f>& points1,
		cv::vector<cv::Point2f>& points2);
	void detect(const cv::Mat& frame);
	void learn(const cv::Mat& img);
	// 辅助计算函数
	void buildGrid(const cv::Mat& img, const cv::Rect& box);
	void getOverlappingBoxes(int num_closest);
	void getBBHull();
	void getPattern(const cv::Mat& img, cv::Mat& pattern,
		cv::Scalar& mean, cv::Scalar& stdev);
	void bbPoints(cv::vector<cv::Point2f>& points, const BoundingBox& bb);
	void bbPredict(const cv::vector<cv::Point2f>& points1,
		const cv::vector<cv::Point2f>& points2,
		const BoundingBox& bb1, BoundingBox& bb2);
	void clusterConf(const cv::vector<BoundingBox>& dbb,
		const cv::vector<float>& dconf,
		cv::vector<BoundingBox>& cbb,
		cv::vector<float>& cconf);

public:
	//核心处理函数
	void init(const cv::Mat& firstFrame, const cv::Rect &box);
	void processFrame(const cv::Mat& img1, const cv::Mat& img2,
		cv::vector<cv::Point2f>& points1,
		cv::vector<cv::Point2f>& points2,
		BoundingBox& bbnext,
		bool& lastboxfound);
	//辅助函数
	float bbOverlap(const BoundingBox& box1, const BoundingBox& box2);
	int getMin_Win() { return min_win; }
};

#endif // TLD_H
