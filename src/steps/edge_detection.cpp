#include "edge_detection.h"

FrameMemoryObject EdgeDetectionCPUStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty()) {
        return FrameMemoryObject();
    }
    
    cv::Mat inputMat = input.getCpuMat();
    cv::Mat result;
    
    double threshold1 = config.getParameter("threshold1", 100.0);
    double threshold2 = config.getParameter("threshold2", 200.0);
    int apertureSize = static_cast<int>(config.getParameter("aperture_size", 3));
    bool L2gradient = config.getParameter("l2_gradient", 0.0) > 0.5;
    
    cv::Mat gray;
    if (inputMat.channels() > 1) {
        cv::cvtColor(inputMat, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = inputMat;
    }
    
    cv::Canny(gray, result, threshold1, threshold2, apertureSize, L2gradient);
    
    // Create result with output metadata only
    FrameMemoryObject frameResult(result);
    
    // Edge detection output statistics - useful for contour detection
    int edgePixelCount = cv::countNonZero(result);
    double edgeDensity = static_cast<double>(edgePixelCount) / (result.rows * result.cols);
    
    frameResult.setMetadata<int>("edge_pixel_count", edgePixelCount);
    frameResult.setMetadata<double>("edge_density", edgeDensity);
    
    return frameResult;
}

#ifdef CUDA_ENABLED
bool EdgeDetectionCUDAStep::isAvailable() const {
    return cv::cuda::getCudaEnabledDeviceCount() > 0;
}

FrameMemoryObject EdgeDetectionCUDAStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty() || !isAvailable()) {
        return FrameMemoryObject();
    }
    
    cv::cuda::GpuMat inputGpu = input.getGpuMat();    
    cv::cuda::GpuMat resultGpu;
    
    double threshold1 = config.getParameter("threshold1", 100.0);
    double threshold2 = config.getParameter("threshold2", 200.0);
    int apertureSize = static_cast<int>(config.getParameter("aperture_size", 3));
    bool L2gradient = config.getParameter("l2_gradient", 0.0) > 0.5;
    
    cv::cuda::GpuMat gray;
    if (inputGpu.channels() > 1) {
        cv::cuda::cvtColor(inputGpu, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = inputGpu;
    }
    
    cv::Ptr<cv::cuda::CannyEdgeDetector> detector = cv::cuda::createCannyEdgeDetector(
    threshold1, threshold2, apertureSize, L2gradient);
    detector->detect(gray, resultGpu);

    // Create result with output metadata only
    FrameMemoryObject frameResult(resultGpu);
    
    // Calculate edge statistics on GPU/CPU
    cv::cuda::GpuMat nonZeroCoords;
    cv::cuda::findNonZero(resultGpu, nonZeroCoords);
    
    int edgePixelCount = nonZeroCoords.rows;
    double edgeDensity = static_cast<double>(edgePixelCount) / (resultGpu.rows * resultGpu.cols);
    
    frameResult.setMetadata<int>("edge_pixel_count", edgePixelCount);
    frameResult.setMetadata<double>("edge_density", edgeDensity);
    
    return frameResult;
}

#endif