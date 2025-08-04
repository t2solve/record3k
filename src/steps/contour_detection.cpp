#include "contour_detection.h"

FrameMemoryObject ContourDetectionCPUStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty()) {
        return FrameMemoryObject();
    }
    
    cv::Mat inputMat = input.getCpuMat();
    
    // Parameters
    int retrievalMode = static_cast<int>(config.getParameter("retrieval_mode", cv::RETR_EXTERNAL));
    int approximationMethod = static_cast<int>(config.getParameter("approximation_method", cv::CHAIN_APPROX_SIMPLE));
    bool drawContours = config.getParameter("draw_contours", 0.0) > 0.5;
    double minArea = config.getParameter("min_area", 10.0);
    double maxArea = config.getParameter("max_area", 100000.0);
    
    // Drawing parameters
    cv::Scalar contourColor = cv::Scalar(
        config.getParameter("contour_color_b", 0.0),
        config.getParameter("contour_color_g", 255.0),
        config.getParameter("contour_color_r", 0.0)
    );
    int thickness = static_cast<int>(config.getParameter("thickness", 2));
    
    // Ensure we have a binary image
    cv::Mat binaryImage;
    if (inputMat.channels() > 1) {
        cv::cvtColor(inputMat, binaryImage, cv::COLOR_BGR2GRAY);
    } else {
        binaryImage = inputMat;
    }
    
    // Find contours
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(binaryImage, contours, hierarchy, retrievalMode, approximationMethod);
    
    // Filter contours by area
    std::vector<std::vector<cv::Point>> filteredContours;
    std::vector<cv::Vec4i> filteredHierarchy;
    std::vector<double> areas;
    std::vector<double> perimeters;
    
    for (size_t i = 0; i < contours.size(); i++) {
        double area = cv::contourArea(contours[i]);
        if (area >= minArea && area <= maxArea) {
            filteredContours.push_back(contours[i]);
            if (i < hierarchy.size()) {
                filteredHierarchy.push_back(hierarchy[i]);
            }
            areas.push_back(area);
            perimeters.push_back(cv::arcLength(contours[i], true));
        }
    }
    
    // Create result image
    FrameMemoryObject result;
    if (drawContours && !filteredContours.empty()) {
        cv::Mat contourImage = cv::Mat::zeros(inputMat.size(), CV_8UC3);
        for (size_t i = 0; i < filteredContours.size(); i++) {
            cv::drawContours(contourImage, filteredContours, static_cast<int>(i), contourColor, thickness);
        }
        result = FrameMemoryObject(contourImage);
    } else {
        result = FrameMemoryObject(binaryImage);
    }
    
    // Store contour output metadata
    result.setMetadata<std::vector<std::vector<cv::Point>>>("contours", filteredContours);
    result.setMetadata<std::vector<cv::Vec4i>>("hierarchy", filteredHierarchy);
    result.setMetadata<int>("contour_count", static_cast<int>(filteredContours.size()));
    result.setMetadata<std::vector<double>>("contour_areas", areas);
    result.setMetadata<std::vector<double>>("contour_perimeters", perimeters);
    
    // Calculate statistics
    if (!areas.empty()) {
        double totalArea = std::accumulate(areas.begin(), areas.end(), 0.0);
        double meanArea = totalArea / areas.size();
        double maxAreaFound = *std::max_element(areas.begin(), areas.end());
        double minAreaFound = *std::min_element(areas.begin(), areas.end());
        
        result.setMetadata<double>("total_contour_area", totalArea);
        result.setMetadata<double>("mean_contour_area", meanArea);
        result.setMetadata<double>("max_contour_area", maxAreaFound);
        result.setMetadata<double>("min_contour_area", minAreaFound);
    }
    
    return result;
}

#ifdef CUDA_ENABLED
bool ContourDetectionCUDAStep::isAvailable() const {
    return cv::cuda::getCudaEnabledDeviceCount() > 0;
}

FrameMemoryObject ContourDetectionCUDAStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty() || !isAvailable()) {
        return FrameMemoryObject();
    }
    
    cv::cuda::GpuMat inputGpu = input.getGpuMat();
    
    // Parameters
    int retrievalMode = static_cast<int>(config.getParameter("retrieval_mode", cv::RETR_EXTERNAL));
    int approximationMethod = static_cast<int>(config.getParameter("approximation_method", cv::CHAIN_APPROX_SIMPLE));
    bool drawContours = config.getParameter("draw_contours", 0.0) > 0.5;
    double minArea = config.getParameter("min_area", 10.0);
    double maxArea = config.getParameter("max_area", 100000.0);
    
    // Drawing parameters
    cv::Scalar contourColor = cv::Scalar(
        config.getParameter("contour_color_b", 0.0),
        config.getParameter("contour_color_g", 255.0),
        config.getParameter("contour_color_r", 0.0)
    );
    int thickness = static_cast<int>(config.getParameter("thickness", 2));
    
    // Download to CPU for contour detection (OpenCV contour detection is CPU-only)
    cv::Mat binaryImageCpu;
    if (inputGpu.channels() > 1) {
        cv::cuda::GpuMat grayCpu;
        cv::cuda::cvtColor(inputGpu, grayCpu, cv::COLOR_BGR2GRAY);
        grayCpu.download(binaryImageCpu);
    } else {
        inputGpu.download(binaryImageCpu);
    }
    
    // Find contours on CPU
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(binaryImageCpu, contours, hierarchy, retrievalMode, approximationMethod);
    
    // Filter contours by area
    std::vector<std::vector<cv::Point>> filteredContours;
    std::vector<cv::Vec4i> filteredHierarchy;
    std::vector<double> areas;
    std::vector<double> perimeters;
    
    for (size_t i = 0; i < contours.size(); i++) {
        double area = cv::contourArea(contours[i]);
        if (area >= minArea && area <= maxArea) {
            filteredContours.push_back(contours[i]);
            if (i < hierarchy.size()) {
                filteredHierarchy.push_back(hierarchy[i]);
            }
            areas.push_back(area);
            perimeters.push_back(cv::arcLength(contours[i], true));
        }
    }
    
    // Create result
    FrameMemoryObject result;
    if (drawContours && !filteredContours.empty()) {
        // Draw contours on CPU then upload to GPU
        cv::Mat contourImageCpu = cv::Mat::zeros(binaryImageCpu.size(), CV_8UC3);
        for (size_t i = 0; i < filteredContours.size(); i++) {
            cv::drawContours(contourImageCpu, filteredContours, static_cast<int>(i), contourColor, thickness);
        }
        cv::cuda::GpuMat contourImageGpu;
        contourImageGpu.upload(contourImageCpu);
        result = FrameMemoryObject(contourImageGpu);
    } else {
        result = FrameMemoryObject(inputGpu);
    }
    
    // Store contour output metadata
    result.setMetadata<std::vector<std::vector<cv::Point>>>("contours", filteredContours);
    result.setMetadata<std::vector<cv::Vec4i>>("hierarchy", filteredHierarchy);
    result.setMetadata<int>("contour_count", static_cast<int>(filteredContours.size()));
    result.setMetadata<std::vector<double>>("contour_areas", areas);
    result.setMetadata<std::vector<double>>("contour_perimeters", perimeters);
    
    // Calculate statistics
    if (!areas.empty()) {
        double totalArea = std::accumulate(areas.begin(), areas.end(), 0.0);
        double meanArea = totalArea / areas.size();
        double maxAreaFound = *std::max_element(areas.begin(), areas.end());
        double minAreaFound = *std::min_element(areas.begin(), areas.end());
        
        result.setMetadata<double>("total_contour_area", totalArea);
        result.setMetadata<double>("mean_contour_area", meanArea);
        result.setMetadata<double>("max_contour_area", maxAreaFound);
        result.setMetadata<double>("min_contour_area", minAreaFound);
    }
    
    return result;
}
#endif