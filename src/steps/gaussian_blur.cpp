#include "gaussian_blur.h"

FrameMemoryObject GaussianBlurCPUStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty()) {
        return FrameMemoryObject();
    }
    
    cv::Mat inputMat = input.getCpuMat();
    cv::Mat result;
    
    int kernelSize = static_cast<int>(config.getParameter("kernel_size", 5));
    double sigmaX = config.getParameter("sigma_x", 1.0);
    double sigmaY = config.getParameter("sigma_y", 1.0);
    
    // Ensure kernel size is odd
    if (kernelSize % 2 == 0) kernelSize++;
    
    cv::GaussianBlur(inputMat, result, cv::Size(kernelSize, kernelSize), sigmaX, sigmaY);
    
    return FrameMemoryObject(result);
}

#ifdef CUDA_ENABLED
bool GaussianBlurCUDAStep::isAvailable() const {
    return cv::cuda::getCudaEnabledDeviceCount() > 0;
}
FrameMemoryObject GaussianBlurCUDAStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty()) {
        return FrameMemoryObject();
    }

    cv::cuda::GpuMat inputGpu = input.getGpuMat();
    
    cv::cuda::GpuMat processedGpu;

    // Convert to grayscale if not 1 or 4 channels
    int scn = inputGpu.channels();
    if (scn != 1 && scn != 4) {
        cv::cuda::cvtColor(inputGpu, processedGpu, cv::COLOR_BGR2GRAY);
    } else {
        processedGpu = inputGpu;
    }

    cv::cuda::GpuMat resultGpu;
    int kernelSize = static_cast<int>(config.getParameter("kernel_size", 5));
    if (kernelSize % 2 == 0) kernelSize++;
    double sigmaX = config.getParameter("sigma_x", 1.0);
    double sigmaY = config.getParameter("sigma_y", 0.0);

    cv::Ptr<cv::cuda::Filter> filter = cv::cuda::createGaussianFilter(
        processedGpu.type(), processedGpu.type(), cv::Size(kernelSize, kernelSize), sigmaX, sigmaY);
    filter->apply(processedGpu, resultGpu);

    return FrameMemoryObject(resultGpu);
}

#endif