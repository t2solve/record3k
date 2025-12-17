#include "bilateral_filter.h"

FrameMemoryObject BilateralFilterCPUStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty()) {
        return FrameMemoryObject();
    }
    
    cv::Mat inputMat = input.getCpuMat();
    cv::Mat result;
    
    int d = static_cast<int>(config.getParameter("d", 9));
    double sigmaColor = config.getParameter("sigma_color", 75.0);
    double sigmaSpace = config.getParameter("sigma_space", 75.0);
    
    cv::bilateralFilter(inputMat, result, d, sigmaColor, sigmaSpace);
    
    return FrameMemoryObject(result);
}

#ifdef CUDA_ENABLED
bool BilateralFilterCUDAStep::isAvailable() const {
    return cv::cuda::getCudaEnabledDeviceCount() > 0;
}

FrameMemoryObject BilateralFilterCUDAStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty() || !isAvailable()) {
        return FrameMemoryObject();
    }
    
    cv::cuda::GpuMat inputGpu = input.getGpuMat();

    cv::cuda::GpuMat resultGpu;
    
    int kernelSize = static_cast<int>(config.getParameter("kernel_size", 5));
    double sigmaColor = config.getParameter("sigma_color", 75.0);
    double sigmaSpace = config.getParameter("sigma_space", 75.0);
    
    cv::cuda::bilateralFilter(inputGpu, resultGpu, kernelSize, sigmaColor, sigmaSpace);
    
    return FrameMemoryObject(resultGpu);
}
#endif