#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include<cstring>
#include<string>
#include<iostream>
#include "utilities.h"
//#define LOADING
#define LEARNING

#ifndef DETECTOR_H
#define DETECTOR_H

class Feature//一对坐标
{
private:
    uchar x1, y1, x2, y2;

public:
    Feature() : x1(0), y1(0), x2(0), y2(0) {}
    Feature(int _x1, int _y1, int _x2, int _y2)
        : x1((uchar)_x1), y1((uchar)_y1), x2((uchar)_x2), y2((uchar)_y2){}
    bool operator ()(const cv::Mat& patch) const
    {
        return patch.at<uchar>(y1,x1) > patch.at<uchar>(y2, x2);
    }
};

class Detector
{
public:
    Detector();

private:
	/// Fern分类器相关
	cv::vector<cv::vector<Feature> > features;   //Ferns features
	cv::vector< cv::vector<int> > nCounter;      //update()函数中使用；负样本计数
	cv::vector< cv::vector<int> > pCounter;      //update()函数中使用；正样本计数
	int featureNum;    // 每棵树中的2-bit BP 特征数目（13）
	int treeNum;       // 树的数目（10）
	float thrN;        // trainF()函数中使用；Negative threshold
	float thrP;        // trainF()函数中使用；Positive thershold
	float thr_fern;    // Fern 分类器阈值
	cv::vector< cv::vector<float> > posteriors;  //Ferns posteriors(统计直方图)

	/// NN分类器相关
	float ncc_thesame;       // NNConf()函数中使用; 相关系数阈值
	float thr_nn;            // 作者给出的参考范围是0.5-0.7
	cv::vector<cv::Mat> pEx; //NN positive examples
	cv::vector<cv::Mat> nEx; //NN negative examples

	float thr_nn_valid; 

private:
	// Fern分类器相关
	void update(const cv::vector<int>& fern, int C, int N);
	
public:
	/// 检测器初始化工作
	void prepare(const cv::vector<cv::Size>& scales);

	/// Fern分类器相关
	void getFeatures(const cv::Mat& image,const int& scale_idx,
					cv::vector<int>& fern);
	
	float measure_forest(cv::vector<int> fern);
	void trainF(const cv::vector<std::pair<cv::vector<int>, int> >& ferns);

	/// NN分类器相关
	void NNConf(const cv::Mat& example,cv::vector<int>& isin,
		float& rsconf, float& csconf);
	void Detector::trainNN(const cv::vector<cv::Mat>& nn_examples, int numP);
	void Detector::trainNN(const cv::vector<cv::Mat>& nn_examples, int numP,cv::Mat frame,cv::Rect lastrect);

	/// 其他
	void evaluateTh(const cv::vector<std::pair<cv::vector<int>, int> >& nXT,
		const cv::vector<cv::Mat>& nExT);
	void show();

	/// Get Private Members
	int   getTreeNum()      { return treeNum;      }
	float getFernTh()       { return thr_fern;     }
	float getNNTh()         { return thr_nn;       }
	float getThr_nn_valid() { return thr_nn_valid; }
};

#endif // DETECTOR_H
