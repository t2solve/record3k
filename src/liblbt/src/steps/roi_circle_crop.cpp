#include "roi_circle_crop.h"

cv::Mat cropImageCircle(const cv::Mat& input, int centerX, int centerY, int radius) {
    // Create a mask with the same size as input
    cv::Mat mask = cv::Mat::zeros(input.size(), CV_8UC1);
    
    // Draw filled circle on mask
    cv::circle(mask, cv::Point(centerX, centerY), radius, cv::Scalar(255), -1);
    
    // Calculate bounding rectangle around the circle
    cv::Rect boundingRect(
        std::max(0, centerX - radius),
        std::max(0, centerY - radius),
        std::min(2 * radius, input.cols - std::max(0, centerX - radius)),
        std::min(2 * radius, input.rows - std::max(0, centerY - radius))
    );
    
    // Create output image with black background
    cv::Mat result = cv::Mat::zeros(boundingRect.size(), input.type());
    
    // Copy the circular region
    cv::Mat croppedInput = input(boundingRect);
    cv::Mat croppedMask = mask(boundingRect);
    
    croppedInput.copyTo(result, croppedMask);
    
    return result;
}

FrameMemoryObject ROICircleCropCPUStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty()) {
        return FrameMemoryObject();
    }
    
    cv::Mat inputMat = input.getCpuMat();
    
    // Get circle parameters from config
    int centerX = static_cast<int>(config.getParameter("center_x", inputMat.cols / 2));
    int centerY = static_cast<int>(config.getParameter("center_y", inputMat.rows / 2));
    int radius = static_cast<int>(config.getParameter("radius", std::min(inputMat.cols, inputMat.rows) / 4));
    
    // Validate parameters
    if (centerX < 0 || centerX >= inputMat.cols || 
        centerY < 0 || centerY >= inputMat.rows || 
        radius <= 0) {
        return FrameMemoryObject(); // Invalid parameters
    }
    
    cv::Mat result = cropImageCircle(inputMat, centerX, centerY, radius);
    
    return FrameMemoryObject(result);
}

#ifdef CUDA_ENABLED
bool ROICircleCropCUDAStep::isAvailable() const {
    return cv::cuda::getCudaEnabledDeviceCount() > 0;
}

FrameMemoryObject ROICircleCropCUDAStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty() || !isAvailable()) {
        return FrameMemoryObject();
    }
    
    cv::cuda::GpuMat inputGpu = input.getGpuMat();
    
    // Get circle parameters from config
    int centerX = static_cast<int>(config.getParameter("center_x", inputGpu.cols / 2));
    int centerY = static_cast<int>(config.getParameter("center_y", inputGpu.rows / 2));
    int radius = static_cast<int>(config.getParameter("radius", std::min(inputGpu.cols, inputGpu.rows) / 4));
    
    // Validate parameters
    if (centerX < 0 || centerX >= inputGpu.cols || 
        centerY < 0 || centerY >= inputGpu.rows || 
        radius <= 0) {
        return FrameMemoryObject(); // Invalid parameters
    }
    
    // Create mask on GPU
    cv::cuda::GpuMat mask(inputGpu.size(), CV_8UC1);
    mask.setTo(cv::Scalar(0));
    
    // For CUDA, we need to create the circle mask on CPU first, then upload
    cv::Mat maskCpu = cv::Mat::zeros(inputGpu.size(), CV_8UC1);
    cv::circle(maskCpu, cv::Point(centerX, centerY), radius, cv::Scalar(255), -1);
    mask.upload(maskCpu);
    
    // Calculate bounding rectangle
    cv::Rect boundingRect(
        std::max(0, centerX - radius),
        std::max(0, centerY - radius),
        std::min(2 * radius, inputGpu.cols - std::max(0, centerX - radius)),
        std::min(2 * radius, inputGpu.rows - std::max(0, centerY - radius))
    );
    
    // Crop the regions on GPU
    cv::cuda::GpuMat croppedInput = inputGpu(boundingRect);
    cv::cuda::GpuMat croppedMask = mask(boundingRect);
    cv::cuda::GpuMat resultGpu(boundingRect.size(), inputGpu.type());
    resultGpu.setTo(cv::Scalar::all(0));
    
    // Copy with mask
    croppedInput.copyTo(resultGpu, croppedMask);
    
    return FrameMemoryObject(resultGpu);
}
#endif