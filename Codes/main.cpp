#include <opencv2\core\core.hpp>
#include <opencv2\highgui\highgui.hpp>
#include <opencv2\imgproc\imgproc.hpp>
#include<Windows.h>

#include "include\utilities.h"
#include "include\tld.h"

#include <string>
#include<cstring>
#include <strstream>
//Global variables
cv::Rect box;
bool drawing_box = false;
bool gotBB = false;
bool fromFile = false;

void mouseHandler(int event, int x, int y, int flags, void *param) ;

/// 说明：
/// 1、提供了从文件 或者 从摄像头两种方式，注释掉的部分是使用摄像头
/// 2、对于文件读入的模式，由于要不断从磁盘读取文件（耗时），而且笔记本性能有限，所以画面会显得很卡
/// 3、对于摄像头模式，类似的原因（主要还是机器性能的问题）会导致帧速率不尽如人意，
///    所以物体运动过快的时候跟踪失败的可能性会增大（两帧图像之间采集间隔过大了）
/// 4、代码中文件读入的路径请自行更改

int main()
{
	/*std::string strp = "F:\\robomaster-psample\\"+ (char)0;
	strp = strp + ".jpg";
	cv::Mat m=cv::imread(strp);
	cv::imshow("p", m);
	cv::waitKey(0);*/
	cv::VideoCapture capture;
	std::string name;
	std::strstream ss;

	cv::Mat current_gray;
	BoundingBox pbox;
	cv::vector<cv::Point2f> pts1;
	cv::vector<cv::Point2f> pts2;
	bool status = true;
	int frames = 1;
	int detections = 1;

	if (!fromFile)
	{
		capture.open(0);
		if (!capture.isOpened())
		{
			std::cout << "capture device failed to open!" << std::endl;
			return 1;
		}
		else
		{
			capture.set(CV_CAP_PROP_FRAME_WIDTH, 340);
			capture.set(CV_CAP_PROP_FRAME_HEIGHT, 240);
		}
	}

    cv::namedWindow("TLD");
    cv::setMouseCallback("TLD", mouseHandler, NULL);
    TLD tld;
    cv::Mat frame;
    cv::Mat last_gray;

GETBOUNDINGBOX:
    //等待鼠标圈出初始目标
    while (!gotBB)
    {
        if(!fromFile)
			capture.read(frame);
		else
			frame = cv::imread("Tests/test/00001.jpg");
        cv::cvtColor(frame, last_gray, CV_RGB2GRAY);
        drawBox(frame, box);
        cv::imshow("TLD", frame);
        cv::waitKey(20);
    }
    if ((cv::min)(box.width, box.height)<tld.getMin_Win())
    {
        std::cout << "Bounding box too small, try again." << std::endl;
        gotBB = false;
        goto GETBOUNDINGBOX;
    }
    //Remove callback
    cv::setMouseCallback("TLD", NULL, NULL);
    tld.init(last_gray, box);// 初始目标位置存储在box

    int i = 2;
	int max;
	if (!fromFile)
		max = 50000;
	else
		max = 50000;
    while (i<=max)
    {
        if (!fromFile)
			capture.read(frame);
		else
		{
			ss.clear();
			name.clear();
			ss << i;
			ss >> name;
			
			switch (name.length())
			{
			case 1:
				name = "Tests/test/0000" + name +".jpg";
				break;
			case 2:
				name = "Tests/test/000" + name + ".jpg";
				break;
			case 3:
				name = "Tests/test/00" + name + ".jpg";
				break;
			case 4:
				name = "Tests/test/0" + name + ".jpg";
				break;
			case 5:
				name = "Tests/test/" + name + ".jpg";
				break;
			}
			frame = cv::imread(name);
		}

        cv::cvtColor(frame, current_gray, CV_RGB2GRAY);
        tld.processFrame(last_gray, current_gray,
                         pts1, pts2, pbox, status);
        if (status)
        {
            //绘制特征点
            //drawPoints(frame, pts1);  //原始点
            //drawPoints(frame, pts2, cv::Scalar(0, 255, 0));  //选作用来跟踪的点
            //矩形框
            drawBox(frame, pbox, cv::Scalar(0,0,255),2);
			
			/*std::strstream ss;
			std::string str = "F:/robomaster-psample/",s;
			SYSTEMTIME sys;
			GetLocalTime(&sys);
			ss<<sys.wDay<<sys.wHour<<sys.wMinute<< sys.wSecond<< sys.wMilliseconds;
			ss >> s;
			str = str + s + ".jpg";
			cv::imwrite(str, frame(pbox));*/
            
			detections++;
        }
        cv::imshow("TLD", frame);

        swap(last_gray, current_gray);
        pts1.clear();
        pts2.clear();
        frames++;
        //有几帧跟踪上了？
        printf("Detection rate: %d/%d\n", detections, frames);
        cv::waitKey(5);
        i++;
    }

	if (!fromFile)
		capture.release();
	cv::waitKey(0);
}

//bounding box mouse callback
void mouseHandler(int event, int x, int y, int flags, void *param)
{
    switch (event)
    {
    case CV_EVENT_MOUSEMOVE:
        if (drawing_box)
        {
            box.width = x - box.x;
            box.height = y - box.y;
        }
        break;
    case CV_EVENT_LBUTTONDOWN:
        drawing_box = true;
        box = cv::Rect(x, y, 0, 0);
        break;
    case CV_EVENT_LBUTTONUP:
        drawing_box = false;
        if (box.width < 0)
        {
            box.x += box.width;
            box.width *= -1;
        }
        if (box.height < 0)
        {
            box.y += box.height;
            box.height *= -1;
        }
        gotBB = true;
        break;
    }
}
//#include <opencv2/opencv.hpp>
//#include <iostream>
//using namespace cv;
//int main(int argc, char** argv)
//{
//	namedWindow("img");
//	VideoCapture capture(0);
//	while (1)
//	{
//		Mat frame;
//		capture >> frame;
//		if (frame.empty())
//			return 0;
//		imshow("img", frame);
//		waitKey(30);
//	}
//	return 0;
//}
//
