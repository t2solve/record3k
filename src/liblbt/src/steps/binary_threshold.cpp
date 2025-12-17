#include "binary_threshold.h"

FrameMemoryObject BinaryThresholdCPUStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty()) {
        return FrameMemoryObject();
    }
    
    cv::Mat inputMat = input.getCpuMat();
    
    // Get threshold value from config (default: 80)
    int threshold = static_cast<int>(config.getParameter("threshold", 80.0));
    int maxValue = static_cast<int>(config.getParameter("max_value", 255.0));
    
    // Apply binary threshold
    cv::Mat result;
    cv::threshold(inputMat, result, threshold, maxValue, cv::THRESH_BINARY);
    
    FrameMemoryObject frameResult(result);
    frameResult.setMetadata<int>("threshold_value", threshold);
    frameResult.setMetadata<int>("max_value", maxValue);
    
    return frameResult;
}

#ifdef CUDA_ENABLED
bool BinaryThresholdCUDAStep::isAvailable() const {
    return cv::cuda::getCudaEnabledDeviceCount() > 0;
}

FrameMemoryObject BinaryThresholdCUDAStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty() || !isAvailable()) {
        return FrameMemoryObject();
    }
    
    cv::cuda::GpuMat inputGpu = input.getGpuMat();
    
    // Get threshold value from config (default: 80)
    double threshold = config.getParameter("threshold", 80.0);
    double maxValue = config.getParameter("max_value", 255.0);
    
    // Apply binary threshold on GPU
    cv::cuda::GpuMat resultGpu;
    cv::cuda::threshold(inputGpu, resultGpu, threshold, maxValue, cv::THRESH_BINARY);
    
    FrameMemoryObject frameResult(resultGpu);
    frameResult.setMetadata<int>("threshold_value", static_cast<int>(threshold));
    frameResult.setMetadata<int>("max_value", static_cast<int>(maxValue));
    
    return frameResult;
}
#endif