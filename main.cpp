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

#include <pthread.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <semaphore.h>
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

static uint64_t st, frames; // for FPS stat

#define DRAW_RESULT 1
#define PERF 0
#define ASYNC_RUN 0
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
	int label_list_size = sizeof class_names / sizeof class_names[0];

	if( (obj.label==-1) || (obj.label>label_list_size) || std::isnan(obj.prob) )
		continue;
#if !DRAW_RESULT
        fprintf(stderr, "%2d: %3.0f%%, [%4.0f, %4.0f, %4.0f, %4.0f], %s\n", obj.label, obj.prob * 100, obj.rect.x,
                obj.rect.y, obj.rect.x + obj.rect.width, obj.rect.y + obj.rect.height, class_names[obj.label]);
#endif
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
#if PERF
    printf("Verify...\n");
    tmsStart = get_perf_count();
#endif
    status = vsi_nn_VerifyGraph( graph );
    TEST_CHECK_STATUS(status, final);
#if PERF
    tmsEnd = get_perf_count();
    msVal = (tmsEnd - tmsStart)/1000000;
    usVal = (tmsEnd - tmsStart)/1000;
    printf("Verify Graph: %"VSI_UINT64_SPECIFIER"ms or %"VSI_UINT64_SPECIFIER"us\n", msVal, usVal);
#endif
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
#if PERF
    tmsStart = get_perf_count();
    printf("Start run graph [%d] times...\n", loop);
#endif
    for(i = 0; i < loop; i++)
    {
#if PERF
        sigStart = get_perf_count();
#endif
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
#if PERF
        sigEnd = get_perf_count();
        msVal = (sigEnd - sigStart)/(float)1000000;
        usVal = (sigEnd - sigStart)/(float)1000;
        printf("Run the %u time: %.2fms or %.2fus\n", (i + 1), msVal, usVal);
#endif
    }
#if PERF
    tmsEnd = get_perf_count();
    msVal = (tmsEnd - tmsStart)/(float)1000000;
    usVal = (tmsEnd - tmsStart)/(float)1000;
    printf("vxProcessGraph execution time:\n");
    printf("Total   %.2fms or %.2fus\n", msVal, usVal);
    printf("Average %.2fms or %.2fus\n", ((float)usVal)/1000/loop, ((float)usVal)/loop);
#endif
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
#if PERF
    tmsStart = get_perf_count();
#endif
    graph = vnn_CreateYolov5sUint8( data_file_name, NULL,
                      vnn_GetPreProcessMap(), vnn_GetPreProcessMapCount(),
                      vnn_GetPostProcessMap(), vnn_GetPostProcessMapCount() );
    TEST_CHECK_PTR(graph, final);
#if PERF
    tmsEnd = get_perf_count();
    msVal = (tmsEnd - tmsStart)/1000000;
    usVal = (tmsEnd - tmsStart)/1000;
    printf("Create Neural Network: %"VSI_UINT64_SPECIFIER"ms or %"VSI_UINT64_SPECIFIER"us\n", msVal, usVal);
#endif
final:
    return graph;
}

typedef struct {
    vsi_nn_graph_t graph;
    cv::Mat img;
} DataBatch;

#define QUEUE_SIZE 5

DataBatch queue1[QUEUE_SIZE], queue2[QUEUE_SIZE], queue3[QUEUE_SIZE];
int head1 = 0, tail1 = 0, head2 = 0, tail2 = 0, head3 = 0, tail3 = 0;
sem_t empty1, full1, mutex1;
sem_t empty2, full2, mutex2;
sem_t empty3, full3, mutex3;

vsi_nn_graph_t *graph;// TODO: checkit
char* video_name;
const char* t = "yolov5s";// title of drawing window

void* cam_video_in(void* arg) {
    cv::VideoCapture cap;
    cv::Mat img;

    if ( 0 == strcmp(video_name,"cam") ){
        //
        // get frame data from camera
        //
        cap.open(0);
        cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M','J','P','G'));
        cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
        cap.set(cv::CAP_PROP_FPS, 20);

        if (!cap.isOpened()){
	        std::cerr<< "ERROE!!Unable to open camera\n";
	        pthread_exit(NULL);
        }
    }else{
        cap.open(video_name);
        if (!cap.isOpened()){
	        std::cerr<< "ERROE!!Unable to open file\n";
	        pthread_exit(NULL);
        }
    }

    while(1){
        //read frame from cam
        bool ret = cap.read(img);
        if(!ret){
            printf("Can't receive frame.. Exiting..\n");
            break;
        }

        //
        // convert to fit model's input data format
        //
        cv::flip( img, img, 1);
        cv::cvtColor( img, img, cv::COLOR_BGR2RGB );
        cv::resize( img, img, cv::Size(640,640), 0, 0, 0 );

	DataBatch batch;

        // clone data
	memcpy(&(batch.graph), graph, sizeof(vsi_nn_graph_t));
	batch.img=img.clone();

        sem_wait(&empty1);
        sem_wait(&mutex1);

        queue1[head1] = batch;
        head1 = (head1 + 1) % QUEUE_SIZE;

        sem_post(&mutex1);
        sem_post(&full1);
    }
    cap.release();//release cam

    pthread_exit(NULL);
}

void* preprocess(void* arg) {
	vsi_status status = VSI_FAILURE;

	while (1) {

        sem_wait(&full1);
        sem_wait(&mutex1);

        DataBatch batch = queue1[tail1];
        tail1 = (tail1 + 1) % QUEUE_SIZE;

        sem_post(&mutex1);
        sem_post(&empty1);

	//dummy input
        int argc = 3;
        char *argv[4]={"test","network_binary.bg","cv.jpg"};

	uint8_t* pRgb = new uint8_t[batch.img.rows * batch.img.cols * 3];
        Mat_to_array(batch.img, pRgb);

	/* Pre process the image data */
        status = vnn_PreProcessNeuralNetwork( &(batch.graph), argc, argv, pRgb);
        //TEST_CHECK_STATUS( status, final );

        sem_wait(&empty2);
        sem_wait(&mutex2);

        queue2[head2] = batch;
        head2 = (head2 + 1) % QUEUE_SIZE;

        sem_post(&mutex2);
        sem_post(&full2);
    }
    return NULL;
}

void* inference(void* arg) {
	vsi_status status = VSI_FAILURE;
    struct timeval now;
    struct timespec outtime;

	while (1) {
        sem_wait(&full2);
        sem_wait(&mutex2);

        DataBatch batch = queue2[tail2];
        tail2 = (tail2 + 1) % QUEUE_SIZE;

        sem_post(&mutex2);
        sem_post(&empty2);

#if ASYNC_RUN
        status = vsi_nn_AsyncRunGraph( &(batch.graph) );
        if(status != VSI_SUCCESS){
            printf("Async Run graph fail\n");
        }
        //TEST_CHECK_STATUS( status, final );

        pthread_yield();

        status = vsi_nn_AsyncRunWait( &(batch.graph) );
        if(status != VSI_SUCCESS){
            printf("Wait graph fail\n");
        }
#else
        status = vsi_nn_RunGraph( &(batch.graph) );
        if(status != VSI_SUCCESS){
            printf("Run graph fail\n");
        }
#endif
        //TEST_CHECK_STATUS( status, final );

        sem_wait(&empty3);
        sem_wait(&mutex3);

        queue3[head3] = batch;
        head3 = (head3 + 1) % QUEUE_SIZE;

        sem_post(&mutex3);
        sem_post(&full3);
    }
    return NULL;
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
    const char *data_name = NULL;

    if(argc < 3)
    {
        printf("Usage: %s nb_file [cam/video_file_name]\n", argv[0]);
        return -1;
    }

    // it is necessary, if multiple threads might use Xlib concurrently
    XInitThreads();

    data_name = (const char *)argv[1];

    video_name = argv[2];

    /* Create the neural network */
    graph = vnn_CreateNeuralNetwork( data_name );
    //TEST_CHECK_PTR( graph, final );
#if 0// disable verification
    /* Verify graph */
    status = vnn_VerifyGraph( graph );
    //TEST_CHECK_STATUS( status, final);
#endif

    pthread_t cam_thread, preprocessor_thread,
	      inference_thread, postprocessor_thread;

    sem_init(&empty1, 0, QUEUE_SIZE);
    sem_init(&full1, 0, 0);
    sem_init(&mutex1, 0, 1);
    sem_init(&empty2, 0, QUEUE_SIZE);
    sem_init(&full2, 0, 0);
    sem_init(&mutex2, 0, 1);
    sem_init(&empty3, 0, QUEUE_SIZE);
    sem_init(&full3, 0, 0);
    sem_init(&mutex3, 0, 1);

    pthread_create(&cam_thread, NULL, cam_video_in, NULL);
    pthread_create(&preprocessor_thread, NULL, preprocess, NULL);
    pthread_create(&inference_thread, NULL, inference, NULL);

    /* Inference Process Loop */
    while(1) {

        {// POST-start
        sem_wait(&full3);
        sem_wait(&mutex3);

        DataBatch batch = queue3[tail3];
        tail3 = (tail3 + 1) % QUEUE_SIZE;

        sem_post(&mutex3);
        sem_post(&empty3);

        /* Post process output data */
        std::vector<Object> objects;

        status = vnn_PostProcessNeuralNetwork( &(batch.graph), objects );
        //TEST_CHECK_STATUS( status, final );

        draw_objects(batch.img, objects);

        //
        // to make sure output image's colorspace is BGR (OpenCV default)
        //
        cv::cvtColor( batch.img, batch.img, cv::COLOR_RGB2BGR );

        uint64_t t0 = get_perf_count();
        if (frames) {
            char s[360];
            sprintf(s, "%s FPS:%.1f", t, (float)frames / (t0 - st));
#if DRAW_RESULT
            cv::setWindowTitle(t, s);
#else
            puts(s);
#endif
        } else {
            st = t0;
        }

        frames += 1000000000;

#if DRAW_RESULT
        cv::namedWindow(t, cv::WINDOW_AUTOSIZE);
        cv::imshow(t, batch.img);
#else
    #if 0 // if no display, write out result pic
		std::vector<int> compression_params;
		compression_params.push_back(0);
		cv::imwrite("output.png", img, compression_params);
    #endif
#endif
        }// POST-end

        // press 'q' key break main loop
        if (cv::waitKey(1) == 'q') {
            printf("Quiting inference loop..\n");
            break;
        }
    }
    pthread_cancel(cam_thread);
    pthread_join(cam_thread, NULL);
    usleep(1);
    pthread_cancel(preprocessor_thread);
    pthread_join(preprocessor_thread, NULL);
    usleep(1);
    pthread_cancel(inference_thread);
    pthread_join(inference_thread, NULL);

    sem_destroy(&empty1);
    sem_destroy(&full1);
    sem_destroy(&mutex1);
    sem_destroy(&empty2);
    sem_destroy(&full2);
    sem_destroy(&mutex2);
    sem_destroy(&empty3);
    sem_destroy(&full3);
    sem_destroy(&mutex3);

final:
    cv::destroyAllWindows();
    vnn_ReleaseNeuralNetwork( graph );
    fflush(stdout);
    fflush(stderr);
    return status;
}

