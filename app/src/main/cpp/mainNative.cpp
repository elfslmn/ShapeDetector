#include <royale/CameraManager.hpp>
#include <royale/ICameraDevice.hpp>
#include <iostream>
#include <jni.h>
#include <android/log.h>
#include <thread>
#include <chrono>

#ifdef __cplusplus
extern "C"
{
#endif

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "Native", __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "Native", __VA_ARGS__))

using namespace royale;
using namespace std;

JavaVM *m_vm;
jmethodID m_amplitudeCallbackID;
jobject m_obj;

uint16_t width, height;

// this represents the main camera device object
static std::unique_ptr<ICameraDevice> cameraDevice;

class MyListener : public IDepthDataListener
{
    void onNewData (const DepthData *data)
    {
        int i;
        // Determine min and max value and calculate span
        int max = 0;
        int min = 65535;
        for (i = 0; i < width * height; i++)
        {
            if (data->points.at (i).grayValue < min)
            {
                min = data->points.at (i).grayValue;
            }
            if (data->points.at (i).grayValue > max)
            {
                max = data->points.at (i).grayValue;
            }
        }

        int span = max - min;

        // Prevent division by zero
        if (!span)
        {
            span = 1;
        }

        // fill a temp structure to use to populate the java int array
        jint fill[width * height];
        for (i = 0; i < width * height; i++)
        {
            // use min value and span to have values between 0 and 255 (for visualisation)
            fill[i] = (int) ( ( (data->points.at (i).grayValue - min) / (float) span) * 255.0f);
            // set same value for red, green and blue; alpha to 255; to create gray image
            fill[i] = fill[i] | fill[i] << 8 | fill[i] << 16 | 255 << 24;
        }

        // attach to the JavaVM thread and get a JNI interface pointer
        JNIEnv *env;
        m_vm->AttachCurrentThread ( (JNIEnv **) &env, NULL);

        // create java int array
        jintArray intArray = env->NewIntArray (width * height);

        // populate java int array with fill data
        env->SetIntArrayRegion (intArray, 0, width * height, fill);

        // call java method and pass amplitude array
        env->CallVoidMethod (m_obj, m_amplitudeCallbackID, intArray);

        // detach from the JavaVM thread
        m_vm->DetachCurrentThread();
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
}

void Java_com_esalman17_shapedetector_MainActivity_CloseCameraNative (JNIEnv *env, jobject thiz)
{
    cameraDevice->stopCapture();
}

#ifdef __cplusplus
}
#endif
