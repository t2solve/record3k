#include "lens_correction.h"

FrameMemoryObject LensCorrectionCPUStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty()) {
        return FrameMemoryObject();
    }
    
    cv::Mat inputMat = input.getCpuMat();
    cv::Mat result;
    
    // Get lens correction parameters
    double k1 = config.getParameter("k1", 0.0);  // Radial distortion coefficient 1
    double k2 = config.getParameter("k2", 0.0);  // Radial distortion coefficient 2
    double p1 = config.getParameter("p1", 0.0);  // Tangential distortion coefficient 1
    double p2 = config.getParameter("p2", 0.0);  // Tangential distortion coefficient 2
    double k3 = config.getParameter("k3", 0.0);  // Radial distortion coefficient 3
    
    // Camera matrix parameters (assuming image center if not provided)
    double fx = config.getParameter("fx", inputMat.cols * 0.8);  // Focal length x
    double fy = config.getParameter("fy", inputMat.rows * 0.8);  // Focal length y
    double cx = config.getParameter("cx", inputMat.cols * 0.5);  // Principal point x
    double cy = config.getParameter("cy", inputMat.rows * 0.5);  // Principal point y
    
    // Create camera matrix
    cv::Mat cameraMatrix = (cv::Mat_<double>(3, 3) << 
        fx, 0, cx,
        0, fy, cy,
        0, 0, 1);
    
    // Create distortion coefficients
    cv::Mat distCoeffs = (cv::Mat_<double>(1, 5) << k1, k2, p1, p2, k3);
    
    // Undistort the image
    cv::undistort(inputMat, result, cameraMatrix, distCoeffs);
    
    return FrameMemoryObject(result);
}

#ifdef CUDA_ENABLED
bool LensCorrectionCUDAStep::isAvailable() const {
    return cv::cuda::getCudaEnabledDeviceCount() > 0;
}

FrameMemoryObject LensCorrectionCUDAStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty() || !isAvailable()) {
        return FrameMemoryObject();
    }
    
    cv::cuda::GpuMat inputGpu = input.getGpuMat();
    cv::cuda::GpuMat resultGpu;
    
    // Get lens correction parameters
    double k1 = config.getParameter("k1", 0.0);
    double k2 = config.getParameter("k2", 0.0);
    double p1 = config.getParameter("p1", 0.0);
    double p2 = config.getParameter("p2", 0.0);
    double k3 = config.getParameter("k3", 0.0);
    
    // Camera matrix parameters
    double fx = config.getParameter("fx", inputGpu.cols * 0.8);
    double fy = config.getParameter("fy", inputGpu.rows * 0.8);
    double cx = config.getParameter("cx", inputGpu.cols * 0.5);
    double cy = config.getParameter("cy", inputGpu.rows * 0.5);
    
    // Create camera matrix
    cv::Mat cameraMatrix = (cv::Mat_<double>(3, 3) << 
        fx, 0, cx,
        0, fy, cy,
        0, 0, 1);
    
    // Create distortion coefficients
    cv::Mat distCoeffs = (cv::Mat_<double>(1, 5) << k1, k2, p1, p2, k3);
    
    // For CUDA, we need to compute the undistortion maps first
    cv::Mat map1, map2;
    cv::initUndistortRectifyMap(cameraMatrix, distCoeffs, cv::Mat(), cameraMatrix, 
                                cv::Size(inputGpu.cols, inputGpu.rows), CV_32FC1, map1, map2);
    
    // Upload maps to GPU
    cv::cuda::GpuMat map1Gpu, map2Gpu;
    map1Gpu.upload(map1);
    map2Gpu.upload(map2);
    
    // Apply undistortion using remap
    cv::cuda::remap(inputGpu, resultGpu, map1Gpu, map2Gpu, cv::INTER_LINEAR);
    
    return FrameMemoryObject(resultGpu);
}
#endif