#include "frame_observer.h"
#include <VmbCPP/VmbCPP.h>
#include <opencv2/opencv.hpp>
#include <opencv2/imgcodecs.hpp>
#include <QDebug>
#include <QDateTime>
#include <chrono>
#include <iostream>
#include <fstream>
#include <sstream>

using namespace VmbCPP;

FrameObserver::FrameObserver(CameraPtr camera, QObject* parent)
    : QObject(parent)
    , m_camera(camera)
    , m_frameProcessor(nullptr)
    , m_debugMode(false)
    , m_maxFrames(0)
{
    qDebug() << "FrameObserver created";
}

FrameObserver::~FrameObserver() {
    qDebug() << "FrameObserver destroyed";
}

void FrameObserver::setFrameProcessor(std::shared_ptr<FrameProcessor> processor) {
    m_frameProcessor = processor;
    qDebug() << "Frame processor set";
}

void FrameObserver::setDebugMode(bool enabled) {
    m_debugMode = enabled;
    qDebug() << "Debug mode:" << enabled;
}

void FrameObserver::setMaxFrames(int maxFrames) {
    m_maxFrames = maxFrames;
    qDebug() << "Max frames set to:" << maxFrames;
}

void FrameObserver::resetStatistics() {
    m_frameCount = 0;
    m_processedFrameCount = 0;
    m_errorFrameCount = 0;
    qDebug() << "Statistics reset";
}

void FrameObserver::FrameReceived(const FramePtr& frame) {
    if (!frame) {
        qWarning() << "Received null frame pointer";
        return;
    }
    
    VmbUint64_t frameId = 0;
    VmbFrameStatusType frameStatus = VmbFrameStatusIncomplete;
    
    try {
        // Get frame ID
        VmbError_t result = frame->GetFrameID(frameId);
        if (VmbErrorSuccess != result) {
            qWarning() << "Failed to get frame ID, error:" << result;
            frameId = m_frameCount; // Use frame count as fallback
        }
        
        // Get frame status
        result = frame->GetReceiveStatus(frameStatus);
        if (VmbErrorSuccess != result) {
            qWarning() << "Failed to get frame status for frame" << frameId << ", error:" << result;
            emit frameError(frameId, QString("Failed to get frame status: %1").arg(result));
            m_errorFrameCount++;
            return;
        }
        
        m_frameCount++;
        
        if (m_debugMode) {
            qDebug() << "Frame received - ID:" << frameId 
                     << "Status:" << QString::fromStdString(getFrameStatusString(frameStatus))
                     << "Total frames:" << m_frameCount.load();
        }
        
        // Emit signal for frame received
        emit frameReceived(frameId, frameStatus);
        
        // Check if we should stop after max frames
        if (m_maxFrames > 0 && m_frameCount >= m_maxFrames) {
            qDebug() << "Reached maximum frame count:" << m_maxFrames;
            return;
        }
        
        // Check frame validity
        if (!isFrameValid(frame)) {
            if (m_debugMode) {
                qDebug() << "Frame" << frameId << "is invalid, skipping processing";
            }
            emit frameError(frameId, QString("Invalid frame status: %1").arg(QString::fromStdString(getFrameStatusString(frameStatus))));
            m_errorFrameCount++;
            return;
        }
        
        // Convert frame to OpenCV Mat
        cv::Mat image = convertVmbFrameToMat(frame);
        if (image.empty()) {
            qWarning() << "Failed to convert frame" << frameId << "to OpenCV Mat";
            emit frameError(frameId, "Failed to convert frame to OpenCV Mat");
            m_errorFrameCount++;
            return;
        }
        
        if (m_debugMode) {
            qDebug() << "Converted frame" << frameId << "to OpenCV Mat:"
                     << "Size:" << image.cols << "x" << image.rows
                     << "Channels:" << image.channels()
                     << "Type:" << image.type();
        }
        
        // Process frame if processor is available
        if (m_frameProcessor) {
            double timestamp = getCurrentTimestamp();
            ProcessingFrame processingFrame(image, frameId, timestamp);
            
            if (m_frameProcessor->enqueueFrame(processingFrame)) {
                m_processedFrameCount++;
                if (m_debugMode) {
                    qDebug() << "Frame" << frameId << "enqueued for processing";
                }
                emit frameProcessed(frameId);
            } else {
                qWarning() << "Failed to enqueue frame" << frameId << "for processing";
                emit frameError(frameId, "Failed to enqueue frame for processing");
                m_errorFrameCount++;
            }
        } else {
            // No processor available, just count as processed
            m_processedFrameCount++;
            if (m_debugMode) {
                qDebug() << "Frame" << frameId << "processed (no processor set)";
            }
            emit frameProcessed(frameId);
        }
        
    } catch (const std::exception& e) {
        qWarning() << "Exception in FrameReceived for frame" << frameId << ":" << e.what();
        emit frameError(frameId, QString("Exception: %1").arg(e.what()));
        m_errorFrameCount++;
    }
    
    // Requeue frame for continuous acquisition
    if (m_camera) {
        VmbError_t result = m_camera->QueueFrame(frame);
        if (VmbErrorSuccess != result) {
            qWarning() << "Failed to requeue frame" << frameId << ", error:" << result;
        }
    }
}

cv::Mat FrameObserver::convertVmbFrameToMat(const FramePtr& frame) {
    if (!frame) {
        qWarning() << "Cannot convert null frame";
        return cv::Mat();
    }
    
    try {
        VmbUchar_t* pBuffer = nullptr;
        VmbUint32_t bufferSize = 0;
        VmbUint32_t width = 0;
        VmbUint32_t height = 0;
        VmbPixelFormatType pixelFormat = VmbPixelFormatMono8;
        
        // Get frame buffer
        VmbError_t result = frame->GetBuffer(pBuffer);
        if (VmbErrorSuccess != result || !pBuffer) {
            qWarning() << "Failed to get frame buffer, error:" << result;
            return cv::Mat();
        }
        
        // Get buffer size
        result = frame->GetBufferSize(bufferSize);
        if (VmbErrorSuccess != result) {
            qWarning() << "Failed to get buffer size, error:" << result;
            return cv::Mat();
        }
        
        // Get frame dimensions
        result = frame->GetWidth(width);
        if (VmbErrorSuccess != result) {
            qWarning() << "Failed to get frame width, error:" << result;
            return cv::Mat();
        }
        
        result = frame->GetHeight(height);
        if (VmbErrorSuccess != result) {
            qWarning() << "Failed to get frame height, error:" << result;
            return cv::Mat();
        }
        
        // Get pixel format
        result = frame->GetPixelFormat(pixelFormat);
        if (VmbErrorSuccess != result) {
            qWarning() << "Failed to get pixel format, error:" << result;
            return cv::Mat();
        }
        
        if (m_debugMode) {
            qDebug() << "Frame conversion - Width:" << width << "Height:" << height
                     << "Buffer size:" << bufferSize
                     << "Pixel format:" << QString::fromStdString(getPixelFormatString(pixelFormat));
        }
        
        // Convert based on pixel format
        cv::Mat image;
        
        switch (pixelFormat) {
            case VmbPixelFormatMono8:
                image = cv::Mat(height, width, CV_8UC1, pBuffer).clone();
                break;
                
            case VmbPixelFormatBayerRG8:
            case VmbPixelFormatBayerGB8:
            case VmbPixelFormatBayerGR8:
            case VmbPixelFormatBayerBG8: {
                // Create Mat from raw Bayer data
                cv::Mat bayerImage(height, width, CV_8UC1, pBuffer);
                
                // Convert Bayer to BGR
                int conversionCode = cv::COLOR_BayerRG2BGR;
                switch (pixelFormat) {
                    case VmbPixelFormatBayerRG8: conversionCode = cv::COLOR_BayerRG2BGR; break;
                    case VmbPixelFormatBayerGB8: conversionCode = cv::COLOR_BayerGB2BGR; break;
                    case VmbPixelFormatBayerGR8: conversionCode = cv::COLOR_BayerGR2BGR; break;
                    case VmbPixelFormatBayerBG8: conversionCode = cv::COLOR_BayerBG2BGR; break;
                }
                
                cv::cvtColor(bayerImage, image, conversionCode);
                break;
            }
            
            case VmbPixelFormatRgb8:
                image = cv::Mat(height, width, CV_8UC3, pBuffer).clone();
                // Convert RGB to BGR for OpenCV
                cv::cvtColor(image, image, cv::COLOR_RGB2BGR);
                break;
                
            case VmbPixelFormatBgr8:
                image = cv::Mat(height, width, CV_8UC3, pBuffer).clone();
                break;
                
            case VmbPixelFormatRgba8:
                image = cv::Mat(height, width, CV_8UC4, pBuffer).clone();
                // Convert RGBA to BGR
                cv::cvtColor(image, image, cv::COLOR_RGBA2BGR);
                break;
                
            case VmbPixelFormatBgra8:
                image = cv::Mat(height, width, CV_8UC4, pBuffer).clone();
                // Convert BGRA to BGR
                cv::cvtColor(image, image, cv::COLOR_BGRA2BGR);
                break;
                
            default:
                qWarning() << "Unsupported pixel format:" << QString::fromStdString(getPixelFormatString(pixelFormat));
                return cv::Mat();
        }
        
        if (m_debugMode && !image.empty()) {
            qDebug() << "Successfully converted frame to OpenCV Mat:"
                     << "Size:" << image.cols << "x" << image.rows
                     << "Channels:" << image.channels()
                     << "Type:" << image.type();
        }
        
        return image;
        
    } catch (const std::exception& e) {
        qWarning() << "Exception in convertVmbFrameToMat:" << e.what();
        return cv::Mat();
    }
}

bool FrameObserver::isFrameValid(const FramePtr& frame) {
    if (!frame) {
        return false;
    }
    
    VmbFrameStatusType status;
    VmbError_t result = frame->GetReceiveStatus(status);
    if (VmbErrorSuccess != result) {
        return false;
    }
    
    return (status == VmbFrameStatusComplete);
}

std::string FrameObserver::getPixelFormatString(VmbPixelFormatType pixelFormat) {
    switch (pixelFormat) {
        case VmbPixelFormatMono8: return "Mono8";
        case VmbPixelFormatBayerRG8: return "BayerRG8";
        case VmbPixelFormatBayerGB8: return "BayerGB8";
        case VmbPixelFormatBayerGR8: return "BayerGR8";
        case VmbPixelFormatBayerBG8: return "BayerBG8";
        case VmbPixelFormatRgb8: return "RGB8";
        case VmbPixelFormatBgr8: return "BGR8";
        case VmbPixelFormatRgba8: return "RGBA8";
        case VmbPixelFormatBgra8: return "BGRA8";
        default: return "Unknown";
    }
}

std::string FrameObserver::getFrameStatusString(VmbFrameStatusType status) {
    switch (status) {
        case VmbFrameStatusComplete: return "Complete";
        case VmbFrameStatusIncomplete: return "Incomplete";
        case VmbFrameStatusTooSmall: return "TooSmall";
        case VmbFrameStatusInvalid: return "Invalid";
        default: return "Unknown";
    }
}

double FrameObserver::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    return static_cast<double>(millis) / 1000.0;
}

#include "frame_observer.moc"