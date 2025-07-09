

#include <VmbCPP/VmbCPP.h>
#include <VmbCPP/Frame.h>
#include <VmbCPP/Camera.h>
#include <VmbCPP/Feature.h>
#include <VmbCPP/Interface.h>
#include <VmbCPP/IFrameObserver.h>

#include <VmbCPP/SharedPointerDefines.h>


#include <QCoreApplication>
#include <QTimer>
#include <QDebug>
#include <QObject>
#include <QThread>
#include <QDebug>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/opencv.hpp>

#include "frame_observer.h"

using namespace VmbCPP;


int main()
{
    // #TODO move to new  build env
    VmbErrorType err;
    VmbSystem& system = VmbSystem::GetInstance();
    qDebug() << "[INFO] Starting Vimba system...";
    err = system.Startup();
    if (err != VmbErrorSuccess) {
        qCritical() << "[ERROR] Vimba system startup failed with code:" << err;
        return 1;
    }

    CameraPtrVector cameras;
    err = system.GetCameras(cameras);
    qDebug() << "[INFO] Cameras found:" << cameras.size();
    if (err != VmbErrorSuccess || cameras.empty()) {
        qCritical() << "[ERROR] No cameras found or error occurred. Code:" << err;
        return 1;
    }

    CameraPtr camera = cameras.at(0);
    qDebug() << "[INFO] Opening camera...";
    err = camera->Open(VmbAccessModeFull);
    if (err != VmbErrorSuccess) {
        qCritical() << "[ERROR] Failed to open camera. Code:" << err;
        return 1;
    }
   

    FramePtrVector frames(5);
    IFrameObserverPtr observer(new FrameObserver(camera));
    VmbUint32_t payloadSize;
    qDebug() << "[INFO] Registering frame observer...";

    err = camera->GetPayloadSize(payloadSize);
    qDebug() << "[INFO] Camera payload size:" << payloadSize;

    for (FramePtrVector::iterator iter = frames.begin(); frames.end() != iter; ++iter) {
        (*iter).reset(new Frame(payloadSize));
        err = (*iter)->RegisterObserver(observer);
        err = camera->AnnounceFrame(*iter);
    }

    qDebug() << "[INFO] Starting capture...";
    err = camera->StartCapture();
    if (err != VmbErrorSuccess) {
        qCritical() << "[ERROR] Failed to start capture. Code:" << err;
        return 1;
    }

    for (FramePtrVector::iterator iter = frames.begin(); frames.end() != iter; ++iter) {
        err = camera->QueueFrame(*iter);
        if (err != VmbErrorSuccess) {
        qCritical() << "[ERROR] Failed to queue frame. Code:" << err;
        // Optionally: break or return 1; if you want to abort on error
       }
    }
    FeaturePtr feature;
    err = camera->GetFeatureByName("AcquisitionMode", feature);
    if (err == VmbErrorSuccess) {
      err = feature->SetValue("Continuous");
      if (err != VmbErrorSuccess) {
         qWarning() << "[Camera] Failed to set AcquisitionMode to Continuous. Error:" << err;
      }
    } else {
      qWarning() << "[Camera] Failed to get AcquisitionMode feature. Error:" << err;
    }
    err = camera->GetFeatureByName("TriggerMode", feature);
    if (err == VmbErrorSuccess) {
         err = feature->SetValue("Off");
         if (err != VmbErrorSuccess) {
            qWarning() << "[Camera] Failed to set TriggerMode to Off. Error:" << err;
         }
    }else {
         qWarning() << "[Camera] Failed to get TriggerMode feature. Error:" << err;
      }
   err = camera->GetFeatureByName("PixelFormat", feature);
      if (err == VmbErrorSuccess) {
           err = feature->SetValue("BayerRG8");
           if (err != VmbErrorSuccess) {
              qWarning() << "[Camera] Failed to set PixelFormat to BayerRG8. Error:" << err;
           }
        } else {
           qWarning() << "[Camera] Failed to get PixelFormat feature. Error:" << err;
        }
    err = camera->GetFeatureByName("AcquisitionStart", feature);
    if (err != VmbErrorSuccess) {
        qCritical() << "[ERROR] Failed to get AcquisitionStart feature. Code:" << err;
        return 1;
    }
    qDebug() << "[INFO] Starting acquisition...";
    err = feature->RunCommand();
    if (err != VmbErrorSuccess) {
        qCritical() << "[ERROR] Failed to run AcquisitionStart command. Code:" << err;
        return 1;
    }

    // Program runtime ...
    qDebug() << "[INFO] Acquisition running. Press Ctrl+C to exit.";
    // For demo: sleep for a while (simulate acquisition)
    QThread::sleep(5);

    // When finished, tear down the acquisition chain, close camera and API
    qDebug() << "[INFO] Stopping acquisition...";
    err = camera->GetFeatureByName("AcquisitionStop", feature);
    err = feature->RunCommand();
    err = camera->EndCapture();
    err = camera->FlushQueue();
    err = camera->RevokeAllFrames();
    err = camera->Close();
    err = system.Shutdown();
    qDebug() << "[INFO] Shutdown complete.";
    return 0;
}
