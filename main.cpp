/****************************************************************************
*   Generated by ACUITY 6.21.1
*   Match ovxlib 1.1.30
*
*   Neural Network application project entry file
****************************************************************************/
/*-------------------------------------------
                Includes
-------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <time.h>
#include <inttypes.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

#define _BASETSD_H

extern "C" {
#include "vsi_nn_pub.h"

#include "vnn_global.h"
#include "vnn_pre_process.h"
#include "vnn_yolov5suint8.h"
}

#include "vnn_post_process.hpp"
#include <opencv2/opencv.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

/*-------------------------------------------
        Macros and Variables
-------------------------------------------*/
#ifdef __linux__
#define VSI_UINT64_SPECIFIER PRIu64
#elif defined(_WIN32)
#define VSI_UINT64_SPECIFIER "I64u"
#endif

/*-------------------------------------------
                  Functions
-------------------------------------------*/

static void draw_objects(const cv::Mat& image, const std::vector<Object>& objects)
{
    static const char* class_names[] = {
        "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat", "traffic light",
        "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow",
        "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
        "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard",
        "tennis racket", "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple",
        "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
        "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse", "remote", "keyboard", "cell phone",
        "microwave", "oven", "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors", "teddy bear",
        "hair drier", "toothbrush"};

   for (size_t i = 0; i < objects.size(); i++)
    {
	//
	// random color mapping
	//
	std::vector<cv::Scalar> colors;
	cv::RNG rng;
	for (int kI = 0; kI < 100; ++kI) {
		int r = rng.uniform(0, 256);
		int g = rng.uniform(0, 256);
		int b = rng.uniform(0, 256);
		colors.emplace_back(b, g, r);
    }

	const Object& obj = objects[i];

	if( (obj.label==-1) || std::isnan(obj.prob) )
		continue;

        fprintf(stderr, "%2d: %3.0f%%, [%4.0f, %4.0f, %4.0f, %4.0f], %s\n", obj.label, obj.prob * 100, obj.rect.x,
                obj.rect.y, obj.rect.x + obj.rect.width, obj.rect.y + obj.rect.height, class_names[obj.label]);

        cv::rectangle(image, cv::Rect(obj.rect.x, obj.rect.y, obj.rect.width, obj.rect.height), colors[obj.label], 3);

        char text[256];
        sprintf(text, "%s %.1f%%", class_names[obj.label], obj.prob * 100);

        int baseLine = 0;
        cv::Size label_size = cv::getTextSize(text, cv::FONT_HERSHEY_COMPLEX, 0.5, 1, &baseLine);

        int x = obj.rect.x;
        int y = obj.rect.y - label_size.height - baseLine;
        if (y < 0)
            y = 0;
        if (x + label_size.width > image.cols)
            x = image.cols - label_size.width;


        cv::rectangle(image, cv::Rect(cv::Point(x, y), cv::Size(label_size.width, label_size.height + baseLine)),
                     colors[obj.label], -1);

        cv::putText(image, text, cv::Point(x, y + label_size.height), cv::FONT_HERSHEY_COMPLEX, 0.5,
                    cv::Scalar(0, 0, 0), 1);
    }
}

//
// 数据是直接从内存读的，只好把图像数据转成数组
//
int Mat_to_array(const cv::Mat input, uint8_t* pRgb)
{
    int height = input.rows;
    int width = input.cols;

    for (int i = 0;i < height;i++)
    {
	for (int j = 0;j < width;j++)
	{
            for (int k = 0;k < 3;k++)
            {
	        pRgb[i * width * 3 + j * 3 + k] = input.at<cv::Vec3b>(i, j)[k];
	    }
	}
    }
    return 0;
}

static void vnn_ReleaseNeuralNetwork
    (
    vsi_nn_graph_t *graph
    )
{
    vnn_ReleaseYolov5sUint8( graph, TRUE );
    if (vnn_UseImagePreprocessNode())
    {
        vnn_ReleaseBufferImage();
    }
}

static vsi_status vnn_PostProcessNeuralNetwork
    (
    vsi_nn_graph_t *graph,
    std::vector<Object>& objects
    )
{
    return vnn_PostProcessYolov5sUint8( graph, objects );
}

#define BILLION                                 1000000000
static uint64_t get_perf_count()
{
#if defined(__linux__) || defined(__ANDROID__) || defined(__QNX__) || defined(__CYGWIN__)
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (uint64_t)((uint64_t)ts.tv_nsec + (uint64_t)ts.tv_sec * BILLION);
#elif defined(_WIN32) || defined(UNDER_CE)
    LARGE_INTEGER freq;
    LARGE_INTEGER ln;

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&ln);

    return (uint64_t)(ln.QuadPart * BILLION / freq.QuadPart);
#endif
}

static vsi_status vnn_VerifyGraph
    (
    vsi_nn_graph_t *graph
    )
{
    vsi_status status = VSI_FAILURE;
    uint64_t tmsStart, tmsEnd, msVal, usVal;

    /* Verify graph */
    printf("Verify...\n");
    tmsStart = get_perf_count();
    status = vsi_nn_VerifyGraph( graph );
    TEST_CHECK_STATUS(status, final);
    tmsEnd = get_perf_count();
    msVal = (tmsEnd - tmsStart)/1000000;
    usVal = (tmsEnd - tmsStart)/1000;
    printf("Verify Graph: %"VSI_UINT64_SPECIFIER"ms or %"VSI_UINT64_SPECIFIER"us\n", msVal, usVal);

final:
    return status;
}

static vsi_status vnn_ProcessGraph
    (
    vsi_nn_graph_t *graph
    )
{
    vsi_status status = VSI_FAILURE;
    int32_t i,loop;
    char *loop_s;
    uint64_t tmsStart, tmsEnd, sigStart, sigEnd;
    float msVal, usVal;

    status = VSI_FAILURE;
    loop = 1; /* default loop time is 1 */
    loop_s = getenv("VNN_LOOP_TIME");
    if(loop_s)
    {
        loop = atoi(loop_s);
    }

    /* Run graph */
    tmsStart = get_perf_count();
    printf("Start run graph [%d] times...\n", loop);
    for(i = 0; i < loop; i++)
    {
        sigStart = get_perf_count();
#ifdef VNN_APP_ASYNC_RUN
        status = vsi_nn_AsyncRunGraph( graph );
        if(status != VSI_SUCCESS)
        {
            printf("Async Run graph the %d time fail\n", i);
        }
        TEST_CHECK_STATUS( status, final );

        //do something here...

        status = vsi_nn_AsyncRunWait( graph );
        if(status != VSI_SUCCESS)
        {
            printf("Wait graph the %d time fail\n", i);
        }
#else
        status = vsi_nn_RunGraph( graph );
        if(status != VSI_SUCCESS)
        {
            printf("Run graph the %d time fail\n", i);
        }
#endif
        TEST_CHECK_STATUS( status, final );

        sigEnd = get_perf_count();
        msVal = (sigEnd - sigStart)/(float)1000000;
        usVal = (sigEnd - sigStart)/(float)1000;
        printf("Run the %u time: %.2fms or %.2fus\n", (i + 1), msVal, usVal);
    }
    tmsEnd = get_perf_count();
    msVal = (tmsEnd - tmsStart)/(float)1000000;
    usVal = (tmsEnd - tmsStart)/(float)1000;
    printf("vxProcessGraph execution time:\n");
    printf("Total   %.2fms or %.2fus\n", msVal, usVal);
    printf("Average %.2fms or %.2fus\n", ((float)usVal)/1000/loop, ((float)usVal)/loop);

final:
    return status;
}

static vsi_status vnn_PreProcessNeuralNetwork
    (
    vsi_nn_graph_t *graph,
    int argc,
    char **argv,
    uint8_t* rgbData
    )
{
    /*
     * argv0:   execute file
     * argv1:   data file
     * argv2~n: inputs n file
     */
    const char **inputs = (const char **)argv + 2;
    uint32_t input_num = argc - 2;

    return vnn_PreProcessYolov5sUint8( graph, inputs, input_num, rgbData );
}

static vsi_nn_graph_t *vnn_CreateNeuralNetwork
    (
    const char *data_file_name
    )
{
    vsi_nn_graph_t *graph = NULL;
    uint64_t tmsStart, tmsEnd, msVal, usVal;

    tmsStart = get_perf_count();
    graph = vnn_CreateYolov5sUint8( data_file_name, NULL,
                      vnn_GetPreProcessMap(), vnn_GetPreProcessMapCount(),
                      vnn_GetPostProcessMap(), vnn_GetPostProcessMapCount() );
    TEST_CHECK_PTR(graph, final);

    tmsEnd = get_perf_count();
    msVal = (tmsEnd - tmsStart)/1000000;
    usVal = (tmsEnd - tmsStart)/1000;
    printf("Create Neural Network: %"VSI_UINT64_SPECIFIER"ms or %"VSI_UINT64_SPECIFIER"us\n", msVal, usVal);

final:
    return graph;
}

/*-------------------------------------------
                  Main Functions
-------------------------------------------*/
int main
    (
    int argc,
    char **argv
    )
{
    vsi_status status = VSI_FAILURE;
    vsi_nn_graph_t *graph;
    const char *data_name = NULL;

    if(argc < 3)
    {
        printf("Usage: %s data_file inputs...\n", argv[0]);
        return -1;
    }

    data_name = (const char *)argv[1];

    cv::VideoCapture cap;
    cv::Mat img;

    /* Create the neural network */
    graph = vnn_CreateNeuralNetwork( data_name );
    //TEST_CHECK_PTR( graph, final );

    /* Verify graph */
    status = vnn_VerifyGraph( graph );
    //TEST_CHECK_STATUS( status, final);

#if 0
    //
    // get frame data from camera
    //

    cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M','J','P','G'));
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);

    cap.open(0);

    if (!cap.isOpened())
    {
	std::cerr<< "ERROE!!Unable to open camera\n";
	return -1;
    }
#else
    //video
    cap.open("./input.mp4");
#endif

    /* Inference Process Loop */
    while(1) {
        //read frame from cam
        bool ret = cap.read(img);
        if(!ret){
            printf("Can't receive frame.. Exiting..\n");
            break;
        }

        //
        // convert to fit model's input data format
        //
        cv::resize( img, img, cv::Size(640,640) );
        cv::cvtColor( img, img, cv::COLOR_BGR2RGB );

        uint8_t* pRgb = new uint8_t[img.rows * img.cols * 3];
        Mat_to_array(img, pRgb);

        //dummy input
        argc = 3;
        std::string cv_file="cv.jpg";
        argv[2]=cv_file.c_str();

        /* Pre process the image data */
        status = vnn_PreProcessNeuralNetwork( graph, argc, argv, pRgb);
        //TEST_CHECK_STATUS( status, final );

        /* Process graph */
        status = vnn_ProcessGraph( graph );
        //TEST_CHECK_STATUS( status, final );

        if(VNN_APP_DEBUG)
        {
            /* Dump all node outputs */
            vsi_nn_DumpGraphNodeOutputs(graph, "./network_dump", NULL, 0, TRUE,
			                (vsi_nn_dim_fmt_e)0);
        }

        /* Post process output data */
        std::vector<Object> objects;

        status = vnn_PostProcessNeuralNetwork( graph, objects);
        //TEST_CHECK_STATUS( status, final );

        draw_objects(img, objects);

        //
        // to make sure output image's colorspace is BGR (OpenCV default)
        //
        cv::cvtColor( img, img, cv::COLOR_RGB2BGR );

        cv::namedWindow("C3V-yolov5s", cv::WINDOW_AUTOSIZE);
        cv::imshow("C3V-yolov5s", img);

        // press 'q' key break main loop
        if (cv::waitKey(1) == 'q') {
            break;
        }
    }
final:
    //delete[] pRgb;//release data //TODO: check
    cap.release();//release cam
    cv::destroyAllWindows();

    vnn_ReleaseNeuralNetwork( graph );
    fflush(stdout);
    fflush(stderr);
    return status;
}

