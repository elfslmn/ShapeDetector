#include <royale/CameraManager.hpp>
#include <royale/ICameraDevice.hpp>
#include <iostream>
#include <jni.h>
#include <android/log.h>
#include <thread>
#include <chrono>
#include <mutex>
#include "opencv2/opencv.hpp"
#include <Shape.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "Native", __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "Native", __VA_ARGS__))

using namespace royale;
using namespace std;
using namespace cv;

JavaVM *m_vm;
jmethodID m_amplitudeCallbackID, m_shapeDetectedCallbackID;
jobject m_obj;

uint16_t width, height;
int mode = 1; // 1 camera, 2 test

// this represents the main camera device object
static std::unique_ptr<ICameraDevice> cameraDevice;

class MyListener : public IDepthDataListener
{
    Mat cameraMatrix, distortionCoefficients;
    Mat zImage, zImage8;

    mutex flagMutex;
    bool detecting = false;
    bool detected = false;
    int count = 0;
    Mat backgrMat;
    Mat diff, diffBin;
    Mat drawing;

    void onNewData (const DepthData *data)
    {
        if(detecting){
            zImage = Scalar::all (0);
        }
        else{
            zImage = backgrMat.clone();
        }
        int k = zImage.rows * zImage.cols -1 ; // to reverse scrren
        for (int y = 0; y < zImage.rows; y++)
        {
            float *zRowPtr = zImage.ptr<float> (y);
            for (int x = 0; x < zImage.cols; x++, k--)
            {
                auto curPoint = data->points.at (k);
                if (curPoint.depthConfidence > 0)
                {
                    zRowPtr[x] = curPoint.z;
                }
            }
        }

        //Background profile
        if(detecting){
            backgrMat += zImage;
            count++;
            if(count == 20){
                backgrMat /= 20;
                detecting = false;
                detected = true;
                LOGI("Background detecting has ended.");
            }
        }

        else if (detected){
            diff = backgrMat - zImage;
            Mat temp = diff.clone();
            undistort (temp, diff, cameraMatrix, distortionCoefficients);

            boxFilter(diff, diff, -1, Size(5,5));
            threshold(diff, diffBin, 0.005, 255, CV_THRESH_BINARY);
            // Find contours
            diffBin.convertTo(diffBin, CV_8UC1);
            vector<vector<Point> > contours;
            findContours(diffBin, contours, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE, Point(0, 0));

            if(mode == 1) drawing = Scalar::all (0);
            for( unsigned int i = 0; i< contours.size(); i++ )
            {
                Shape s = Shape(contours[i]);
                auto center = s.getCenter();
                if(center.x < width*0.1 || center.x > width*0.9 ||
                   center.y < height*0.1 || center.y > height*0.9){
                    s.isValidShape = false;
                }
                if(mode == 1) s.draw(drawing);
            }
        }

        if(mode == 1) {
            // fill a temp structure to use to populate the java int array
            //  int color = (A & 0xff) << 24 | (R & 0xff) << 16 | (G & 0xff) << 8 | (B & 0xff);
            jint fill[width * height];
            k = 0;
            for (int i = 0; i < drawing.rows; i++) {
                Vec3b *ptr = drawing.ptr<Vec3b>(i);
                for (int j = 0; j < drawing.cols; j++) {
                    Vec3b p = ptr[j];
                    int color = (255 & 0xff) << 24 | (p[2] & 0xff) << 16 | (p[1] & 0xff) << 8 |
                                (p[0] & 0xff);
                    fill[k] = color;
                    k++;
                }
            }
            // attach to the JavaVM thread and get a JNI interface pointer
            JNIEnv *env;
            m_vm->AttachCurrentThread((JNIEnv **) &env, NULL);
            jintArray intArray = env->NewIntArray(width * height);
            env->SetIntArrayRegion(intArray, 0, width * height, fill);
            env->CallVoidMethod(m_obj, m_amplitudeCallbackID, intArray);
            m_vm->DetachCurrentThread();
        }
        else if(mode == 2){

        }

    }

public :
    void setLensParameters (LensParameters lensParameters)
    {
        // Construct the camera matrix
        // (fx   0    cx)
        // (0    fy   cy)
        // (0    0    1 )
        cameraMatrix = (Mat1d (3, 3) << lensParameters.focalLength.first, 0, lensParameters.principalPoint.first,
                0, lensParameters.focalLength.second, lensParameters.principalPoint.second,
                0, 0, 1);
        LOGI("Camera params fx fy cx cy: %f,%f,%f,%f", lensParameters.focalLength.first, lensParameters.focalLength.second,
             lensParameters.principalPoint.first, lensParameters.principalPoint.second);

        // Construct the distortion coefficients
        // k1 k2 p1 p2 k3
        distortionCoefficients = (Mat1d (1, 5) << lensParameters.distortionRadial[0],
                lensParameters.distortionRadial[1],
                lensParameters.distortionTangential.first,
                lensParameters.distortionTangential.second,
                lensParameters.distortionRadial[2]);
        LOGI("Dist coeffs k1 k2 p1 p2 k3 : %f,%f,%f,%f,%f", lensParameters.distortionRadial[0],
             lensParameters.distortionRadial[1],
             lensParameters.distortionTangential.first,
             lensParameters.distortionTangential.second,
             lensParameters.distortionRadial[2]);
    }

    void initialize(){
        zImage.create (Size (width,height), CV_32FC1);
        backgrMat.create (Size (width,height), CV_32FC1);
        backgrMat = Scalar::all (0);
        drawing = Mat::zeros(height, width, CV_8UC3);
        putText(drawing, "Click Backgr button",Point(30,30),FONT_HERSHEY_PLAIN ,1,Scalar(0,0,255),1);
    }

    void detectBackground(){
        LOGI("Background detecting has started.");
        detecting = true;
        detected = false;
        count = 0;
        backgrMat = Scalar::all (0);
        drawing = Scalar::all (0);
        putText(drawing, "Detecting background...",Point(30,30),FONT_HERSHEY_PLAIN ,1,Scalar(0,0,255),1);
    }
};

MyListener listener;

jintArray Java_com_esalman17_shapedetector_MainActivity_OpenCameraNative (JNIEnv *env, jobject thiz, jint fd, jint vid, jint pid)
{
    // the camera manager will query for a connected camera
    {
        CameraManager manager;

        auto camlist = manager.getConnectedCameraList (fd, vid, pid);
        LOGI ("Detected %zu camera(s).", camlist.size());

        if (!camlist.empty())
        {
            cameraDevice = manager.createCamera (camlist.at (0));
        }
    }
    // the camera device is now available and CameraManager can be deallocated here

    if (cameraDevice == nullptr)
    {
        LOGI ("Cannot create the camera device");
        jintArray intArray();
    }

    // IMPORTANT: call the initialize method before working with the camera device
    CameraStatus ret = cameraDevice->initialize();
    if (ret != CameraStatus::SUCCESS)
    {
        LOGI ("Cannot initialize the camera device, CODE %d", (int) ret);
    }

    royale::Vector<royale::String> opModes;
    royale::String cameraName;
    royale::String cameraId;

    ret = cameraDevice->getUseCases (opModes);
    if (ret != CameraStatus::SUCCESS)
    {
        LOGI ("Failed to get use cases, CODE %d", (int) ret);
    }

    ret = cameraDevice->getMaxSensorWidth (width);
    if (ret != CameraStatus::SUCCESS)
    {
        LOGI ("Failed to get max sensor width, CODE %d", (int) ret);
    }

    ret = cameraDevice->getMaxSensorHeight (height);
    if (ret != CameraStatus::SUCCESS)
    {
        LOGI ("Failed to get max sensor height, CODE %d", (int) ret);
    }

    ret = cameraDevice->getId (cameraId);
    if (ret != CameraStatus::SUCCESS)
    {
        LOGI ("Failed to get camera ID, CODE %d", (int) ret);
    }

    ret = cameraDevice->getCameraName (cameraName);
    if (ret != CameraStatus::SUCCESS)
    {
        LOGI ("Failed to get camera name, CODE %d", (int) ret);
    }

    // display some information about the connected camera
    LOGI ("====================================");
    LOGI ("        Camera information");
    LOGI ("====================================");
    LOGI ("Id:              %s", cameraId.c_str());
    LOGI ("Type:            %s", cameraName.c_str());
    LOGI ("Width:           %d", width);
    LOGI ("Height:          %d", height);
    LOGI ("Operation modes: %zu", opModes.size());

    for (int i = 0; i < opModes.size(); i++)
    {
        LOGI ("    %s", opModes.at (i).c_str());
    }

    LensParameters lensParams;
    ret = cameraDevice->getLensParameters (lensParams);
    if (ret != CameraStatus::SUCCESS)
    {
        LOGE ("Failed to get lens parameters, CODE %d", (int) ret);
    }else{
        listener.setLensParameters (lensParams);
    }
    listener.initialize();

    // register a data listener
    ret = cameraDevice->registerDataListener (&listener);
    if (ret != CameraStatus::SUCCESS)
    {
        LOGI ("Failed to register data listener, CODE %d", (int) ret);
    }

    // set an operation mode
    ret = cameraDevice->setUseCase (opModes[0]);
    if (ret != CameraStatus::SUCCESS)
    {
        LOGI ("Failed to set use case, CODE %d", (int) ret);
    }

    //set exposure mode to manual
    ret = cameraDevice->setExposureMode (ExposureMode::MANUAL);
    if (ret != CameraStatus::SUCCESS)
    {
        LOGE ("Failed to set exposure mode, CODE %d", (int) ret);
    }

    //set exposure time (not working above 300)
    ret = cameraDevice->setExposureTime(250);
    if (ret != CameraStatus::SUCCESS)
    {
        LOGE ("Failed to set exposure time, CODE %d", (int) ret);
    }

    ret = cameraDevice->startCapture();
    if (ret != CameraStatus::SUCCESS)
    {
        LOGI ("Failed to start capture, CODE %d", (int) ret);
    }

    jint fill[2];
    fill[0] = width;
    fill[1] = height;

    jintArray intArray = env->NewIntArray (2);

    env->SetIntArrayRegion (intArray, 0, 2, fill);

    return intArray;
}

void Java_com_esalman17_shapedetector_MainActivity_RegisterCallback (JNIEnv *env, jobject thiz)
{
    // save JavaVM globally; needed later to call Java method in the listener
    env->GetJavaVM (&m_vm);

    m_obj = env->NewGlobalRef (thiz);

    // save refs for callback
    jclass g_class = env->GetObjectClass (m_obj);
    if (g_class == NULL)
    {
        std::cout << "Failed to find class" << std::endl;
    }

    // save method ID to call the method later in the listener
    m_amplitudeCallbackID = env->GetMethodID (g_class, "amplitudeCallback", "([I)V");
    m_shapeDetectedCallbackID = env->GetMethodID (g_class, "shapeDetectedCallback", "([I)V");
}

void Java_com_esalman17_shapedetector_MainActivity_DetectBackgroundNative (JNIEnv *env, jobject thiz)
{
    listener.detectBackground();
}

void Java_com_esalman17_shapedetector_MainActivity_CloseCameraNative (JNIEnv *env, jobject thiz)
{
    cameraDevice->stopCapture();
}

void Java_com_esalman17_shapedetector_MainActivity_ChangeModeNative (JNIEnv *env, jobject thiz, jint m)
{
    mode = m;
}

#ifdef __cplusplus
}
#endif
