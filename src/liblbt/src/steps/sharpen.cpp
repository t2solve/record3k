#include "sharpen.h"

FrameMemoryObject SharpenCPUStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty()) {
        return FrameMemoryObject();
    }
    
    cv::Mat inputMat = input.getCpuMat();
    cv::Mat result;
    
    double strength = config.getParameter("strength", 1.0);
    
    // Sharpening kernel
    cv::Mat kernel = (cv::Mat_<float>(3, 3) << 
        0, -1, 0,
        -1, 5, -1,
        0, -1, 0);
    
    // Scale kernel based on strength
    kernel *= strength;
    kernel.at<float>(1, 1) = 1 + 4 * strength;
    
    cv::filter2D(inputMat, result, -1, kernel);
    
    return FrameMemoryObject(result);
}

#ifdef CUDA_ENABLED
bool SharpenCUDAStep::isAvailable() const {
    return cv::cuda::getCudaEnabledDeviceCount() > 0;
}

FrameMemoryObject SharpenCUDAStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty() || !isAvailable()) {
        return FrameMemoryObject();
    }
    
    cv::cuda::GpuMat inputGpu = input.getGpuMat();

    cv::cuda::GpuMat resultGpu;
    
    double strength = config.getParameter("strength", 1.0);
    
    // Sharpening kernel
    cv::Mat kernel = (cv::Mat_<float>(3, 3) << 
        0, -1, 0,
        -1, 5, -1,
        0, -1, 0);
    
    // Scale kernel based on strength
    kernel *= strength;
    kernel.at<float>(1, 1) = 1 + 4 * strength;
    
    cv::Ptr<cv::cuda::Filter> filter = cv::cuda::createLinearFilter(inputGpu.type(), -1, kernel);
    filter->apply(inputGpu, resultGpu);
    
    return FrameMemoryObject(resultGpu);
}
#endif