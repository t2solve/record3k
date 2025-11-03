#pragma once

#ifdef VIMBAX_ENABLED
#include <VmbCPP/VmbCPP.h>
#include <opencv2/opencv.hpp>
#include <QDebug>
#include <string>
#include "frame_memory_object.h"

using namespace VmbCPP;

class FrameConverter {
public:
    // Main conversion method
    static FrameMemoryObject convertVmbFrameToMemoryObject(const FramePtr& frame, bool debugMode = false);
    
    // Convert to OpenCV Mat (for backward compatibility)
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
};
#endif // VIMBAX_ENABLED