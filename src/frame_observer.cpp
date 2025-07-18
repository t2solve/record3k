#include <opencv2/opencv.hpp>
#include <QDebug>
#include <sstream>
#include <iomanip>
#include "frame_observer.h"
#include "frame_converter.h"

// Constructor for the FrameObserver class
FrameObserver::FrameObserver(CameraPtr pCamera) : IFrameObserver(pCamera) {}

// Frame callback notifies about incoming frames
void FrameObserver::FrameReceived(const FramePtr pFrame)
{
    static int frameCount = 0;
    ++frameCount;
    if (frameCount % 10 == 0) {
        try {
            VmbFrameStatusType status;
            pFrame->GetReceiveStatus(status);
            if (status != VmbFrameStatusComplete) {
                qWarning() << "[FrameObserver] Frame status not complete:" << status;
                return;
            }
            qDebug() << "[FrameObserver] Received" << frameCount << "frames";

            // //load image data from frame to buffer
            // VmbUchar_t* buffer = nullptr;
            // VmbErrorType imgErr = pFrame->GetImage(buffer);
            // if (imgErr != VmbErrorSuccess || buffer == nullptr) {
            //     qWarning() << "[FrameObserver] GetImage failed or buffer is null!";
            //     return;
            // }
            VmbUint32_t width = 0, height = 0;
            pFrame->GetWidth(width);
            pFrame->GetHeight(height);
            VmbUint64_t timestamp;
            pFrame->GetTimestamp(timestamp);
            timestamp /= 1e6; //from ns to ms
            qDebug() << "[FrameObserver] Frame timestamp:" << timestamp << "ms";
            VmbPixelFormatType pixelFormat;
            pFrame->GetPixelFormat(pixelFormat);
            qDebug() << "[FrameObserver] Frame pixel format:" << pixelFormat;

            FrameMemoryObject memoryObject = FrameConverter::convertVmbFrameToMemoryObject(frame, m_debugMode);
            cv::Mat img = memoryObject.getCpuMat();
            // // Adjust type if your camera is not 8UC1
            // cv::Mat img(height, width, CV_8UC1, buffer);
            // if (img.empty() || img.rows != (int)height || img.cols != (int)width) {
            //     qWarning() << "[FrameObserver] Image construction failed or image is empty!";
            // } else {
            //     qDebug() << "[FrameObserver] Image constructed:" << img.rows << "x" << img.cols << "type" << img.type();
            // }
            std::ostringstream filename;
            filename << "/tmp/frame_" << std::setw(5) << std::setfill('0') << frameCount << ".jpg";
            bool success = cv::imwrite(filename.str(), img);
            if (success) {
                qDebug() << "[FrameObserver] Saved frame to" << QString::fromStdString(filename.str());
            } else {
                qWarning() << "[FrameObserver] Failed to save frame to" << QString::fromStdString(filename.str());
            }
            qDebug() << "[FrameObserver] done";
        } catch (const std::exception& e) {
            qCritical() << "[FrameObserver] Exception caught:" << e.what();
        } catch (...) {
            qCritical() << "[FrameObserver] Unknown exception caught!";
        }
    }
    m_pCamera->QueueFrame(pFrame);
}