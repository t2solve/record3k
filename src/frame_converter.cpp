#include "frame_converter.h"
#include <opencv2/imgproc.hpp>

FrameMemoryObject FrameConverter::convertVmbFrameToMemoryObject(const FramePtr& frame, bool debugMode) {
    if (!frame) {
        if (debugMode) qWarning() << "Cannot convert null frame";
        return FrameMemoryObject();
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
            if (debugMode) qWarning() << "Failed to get frame buffer, error:" << result;
            return FrameMemoryObject();
        }
        
        // Get buffer size
        result = frame->GetBufferSize(bufferSize);
        if (VmbErrorSuccess != result) {
            if (debugMode) qWarning() << "Failed to get buffer size, error:" << result;
            return FrameMemoryObject();
        }
        
        // Get frame dimensions
        result = frame->GetWidth(width);
        if (VmbErrorSuccess != result) {
            if (debugMode) qWarning() << "Failed to get frame width, error:" << result;
            return FrameMemoryObject();
        }
        
        result = frame->GetHeight(height);
        if (VmbErrorSuccess != result) {
            if (debugMode) qWarning() << "Failed to get frame height, error:" << result;
            return FrameMemoryObject();
        }
        
        // Get pixel format
        result = frame->GetPixelFormat(pixelFormat);
        if (VmbErrorSuccess != result) {
            if (debugMode) qWarning() << "Failed to get pixel format, error:" << result;
            return FrameMemoryObject();
        }
        
        if (debugMode) {
            qDebug() << "Frame conversion - Width:" << width << "Height:" << height
                     << "Buffer size:" << bufferSize
                     << "Pixel format:" << QString::fromStdString(getPixelFormatString(pixelFormat));
        }
        
        // Convert based on pixel format
        cv::Mat image = convertPixelFormat(pBuffer, width, height, pixelFormat, debugMode);
        
        if (image.empty()) {
            if (debugMode) qWarning() << "Failed to convert pixel format:" << QString::fromStdString(getPixelFormatString(pixelFormat));
            return FrameMemoryObject();
        }
        
        if (debugMode) {
            qDebug() << "Successfully converted frame to OpenCV Mat:"
                     << "Size:" << image.cols << "x" << image.rows
                     << "Channels:" << image.channels()
                     << "Type:" << image.type();
        }
        
        // Create FrameMemoryObject with the converted image
        return FrameMemoryObject(image);
        
    } catch (const std::exception& e) {
        if (debugMode) qWarning() << "Exception in convertVmbFrameToMemoryObject:" << e.what();
        return FrameMemoryObject();
    }
}

cv::Mat FrameConverter::convertVmbFrameToMat(const FramePtr& frame, bool debugMode) {
    FrameMemoryObject memoryObject = convertVmbFrameToMemoryObject(frame, debugMode);
    if (memoryObject.isEmpty()) {
        return cv::Mat();
    }
    return memoryObject.getCpuMat();
}

cv::Mat FrameConverter::convertPixelFormat(VmbUchar_t* pBuffer, VmbUint32_t width, VmbUint32_t height, 
                                          VmbPixelFormatType pixelFormat, bool debugMode) {
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
            int conversionCode = getBayerConversionCode(pixelFormat);
            if (conversionCode != -1) {
                cv::cvtColor(bayerImage, image, conversionCode);
            } else {
                if (debugMode) qWarning() << "Unknown Bayer format:" << QString::fromStdString(getPixelFormatString(pixelFormat));
                return cv::Mat();
            }
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
            if (debugMode) qWarning() << "Unsupported pixel format:" << QString::fromStdString(getPixelFormatString(pixelFormat));
            return cv::Mat();
    }
    
    return image;
}

int FrameConverter::getBayerConversionCode(VmbPixelFormatType pixelFormat) {
    switch (pixelFormat) {
        case VmbPixelFormatBayerRG8: return cv::COLOR_BayerRG2BGR;
        case VmbPixelFormatBayerGB8: return cv::COLOR_BayerGB2BGR;
        case VmbPixelFormatBayerGR8: return cv::COLOR_BayerGR2BGR;
        case VmbPixelFormatBayerBG8: return cv::COLOR_BayerBG2BGR;
        default: return -1;
    }
}

bool FrameConverter::isFrameValid(const FramePtr& frame) {
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

std::string FrameConverter::getPixelFormatString(VmbPixelFormatType pixelFormat) {
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

std::string FrameConverter::getFrameStatusString(VmbFrameStatusType status) {
    switch (status) {
        case VmbFrameStatusComplete: return "Complete";
        case VmbFrameStatusIncomplete: return "Incomplete";
        case VmbFrameStatusTooSmall: return "TooSmall";
        case VmbFrameStatusInvalid: return "Invalid";
        default: return "Unknown";
    }
}