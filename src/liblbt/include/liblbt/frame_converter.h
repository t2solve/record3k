#pragma once

#include <opencv2/opencv.hpp>
#include "frame_memory_object.h"

#ifdef VIMBAX_ENABLED
#include <VmbCPP/VmbCPP.h>
#include <QDebug>
#include <string>

using namespace VmbCPP;
#endif // VIMBAX_ENABLED

class FrameConverter {
public:
    // Convert from OpenCV Mat to FrameMemoryObject (always available)
    static FrameMemoryObject convertMatToMemoryObject(const cv::Mat& mat);

#ifdef VIMBAX_ENABLED
    // VimbaX-specific conversions
    static FrameMemoryObject convertVmbFrameToMemoryObject(const FramePtr& frame, bool debugMode = false);
    static cv::Mat convertVmbFrameToMat(const FramePtr& frame, bool debugMode = false);
    
    // Utility methods
    static std::string getPixelFormatString(VmbPixelFormatType pixelFormat);
    static std::string getFrameStatusString(VmbFrameStatusType status);
    static bool isFrameValid(const FramePtr& frame);
    
private:
    // Internal conversion methods
    static cv::Mat convertPixelFormat(VmbUchar_t* pBuffer, VmbUint32_t width, VmbUint32_t height, 
                                     VmbPixelFormatType pixelFormat, bool debugMode = false);
    static int getBayerConversionCode(VmbPixelFormatType pixelFormat);
#endif // VIMBAX_ENABLED
};