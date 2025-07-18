#include "median_filter.h"

FrameMemoryObject MedianFilterCPUStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty()) {
        return FrameMemoryObject();
    }
    
    cv::Mat inputMat = input.getCpuMat();
    cv::Mat result;
    
    int kernelSize = static_cast<int>(config.getParameter("kernel_size", 5));
    
    // Ensure kernel size is odd
    if (kernelSize % 2 == 0) kernelSize++;
    
    cv::medianBlur(inputMat, result, kernelSize);
    
    return FrameMemoryObject(result);
}