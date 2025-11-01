#include "contour_area_filter.h"

FrameMemoryObject ContourAreaFilterCPUStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty()) {
        return FrameMemoryObject();
    }
    
    cv::Mat inputMat = input.getCpuMat();
    
    // Get config parameters
    double minArea = config.getParameter("min_area", 150.0);
    double maxArea = config.getParameter("max_area", 300000.0);
    // bool drawContours = config.getParameter("draw_contours", 0.0) > 0.5;
    // bool drawCenters = config.getParameter("draw_centers", 0.0) > 0.5;
    // bool drawRectangles = config.getParameter("draw_rectangles", 0.0) > 0.5;
    
    // Find all contours
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(inputMat.clone(), contours, hierarchy, 
                     cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    
    int originalCount = contours.size();
    
    // Filter contours by area
    std::vector<std::vector<cv::Point>> filteredContours;
    std::vector<cv::RotatedRect> minRects;
    std::vector<cv::Point2f> centers;
    
    for (const auto& contour : contours) {
        double area = cv::contourArea(contour);
        
        if (area >= minArea && area <= maxArea) {
            filteredContours.push_back(contour);
            cv::RotatedRect rect = cv::minAreaRect(contour);
            minRects.push_back(rect);
            centers.push_back(rect.center);
        }
    }
    
    // // Create output image
    // cv::Mat result;
    // if (drawContours || drawCenters || drawRectangles) {
    //     // Convert to color for visualization
    //     if (inputMat.channels() == 1) {
    //         cv::cvtColor(inputMat, result, cv::COLOR_GRAY2BGR);
    //     } else {
    //         result = inputMat.clone();
    //     }
        
    //     // Draw filtered contours
    //     if (drawContours) {
    //         cv::Scalar color(0, 255, 0); // Green
    //         cv::drawContours(result, filteredContours, -1, color, 2);
    //     }
        
    //     // Draw min area rectangles
    //     if (drawRectangles) {
    //         for (const auto& rect : minRects) {
    //             cv::Point2f vertices[4];
    //             rect.points(vertices);
    //             for (int i = 0; i < 4; i++) {
    //                 cv::line(result, vertices[i], vertices[(i+1)%4], cv::Scalar(255, 0, 0), 2);
    //             }
    //         }
    //     }
        
    //     // Draw centers
    //     if (drawCenters) {
    //         for (const auto& center : centers) {
    //             cv::circle(result, center, 5, cv::Scalar(0, 0, 255), -1);
    //         }
    //     }
    // } else {
    //     result = inputMat.clone();
    // }
    cv::Mat result = inputMat.clone();
    
    FrameMemoryObject frameResult(result);
    
    // Add metadata
    frameResult.setMetadata<int>("original_contour_count", originalCount);
    frameResult.setMetadata<int>("filtered_contour_count", static_cast<int>(filteredContours.size()));
    frameResult.setMetadata<double>("min_area_threshold", minArea);
    frameResult.setMetadata<double>("max_area_threshold", maxArea);
    frameResult.setMetadata<std::vector<std::vector<cv::Point>>>("filtered_contours", filteredContours);
    frameResult.setMetadata<std::vector<cv::RotatedRect>>("filtered_contour_rects", minRects);
    frameResult.setMetadata<std::vector<cv::Point2f>>("filtered_contour_centers", centers);
    
    return frameResult;
}

#ifdef CUDA_ENABLED
bool ContourAreaFilterCUDAStep::isAvailable() const {
    return cv::cuda::getCudaEnabledDeviceCount() > 0;
}

FrameMemoryObject ContourAreaFilterCUDAStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty() || !isAvailable()) {
        return FrameMemoryObject();
    }
    
    // Download from GPU to CPU for contour detection
    // Note: OpenCV CUDA doesn't have findContours, so we do it on CPU
    cv::Mat cpuMat = input.getCpuMat();
    
    // Get config parameters
    double minArea = config.getParameter("min_area", 150.0);
    double maxArea = config.getParameter("max_area", 300000.0);
    // bool drawContours = config.getParameter("draw_contours", 0.0) > 0.5;
    // bool drawCenters = config.getParameter("draw_centers", 0.0) > 0.5;
    // bool drawRectangles = config.getParameter("draw_rectangles", 0.0) > 0.5;
    
    // Find all contours (on CPU)
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(cpuMat.clone(), contours, hierarchy, 
                     cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    
    int originalCount = contours.size();
    
    // Filter contours by area
    std::vector<std::vector<cv::Point>> filteredContours;
    std::vector<cv::RotatedRect> minRects;
    std::vector<cv::Point2f> centers;
    
    for (const auto& contour : contours) {
        double area = cv::contourArea(contour);
        
        if (area >= minArea && area <= maxArea) {
            filteredContours.push_back(contour);
            cv::RotatedRect rect = cv::minAreaRect(contour);
            minRects.push_back(rect);
            centers.push_back(rect.center);
        }
    }
    
    // // Create output image on GPU
    // cv::cuda::GpuMat resultGpu;
    // if (drawContours || drawCenters || drawRectangles) {
    //     // Convert to color for visualization (on CPU, then upload)
    //     cv::Mat result;
    //     if (cpuMat.channels() == 1) {
    //         cv::cvtColor(cpuMat, result, cv::COLOR_GRAY2BGR);
    //     } else {
    //         result = cpuMat.clone();
    //     }
        
    //     // Draw filtered contours
    //     if (drawContours) {
    //         cv::Scalar color(0, 255, 0);
    //         cv::drawContours(result, filteredContours, -1, color, 2);
    //     }
        
    //     // Draw min area rectangles
    //     if (drawRectangles) {
    //         for (const auto& rect : minRects) {
    //             cv::Point2f vertices[4];
    //             rect.points(vertices);
    //             for (int i = 0; i < 4; i++) {
    //                 cv::line(result, vertices[i], vertices[(i+1)%4], cv::Scalar(255, 0, 0), 2);
    //             }
    //         }
    //     }
        
    //     // Draw centers
    //     if (drawCenters) {
    //         for (const auto& center : centers) {
    //             cv::circle(result, center, 5, cv::Scalar(0, 0, 255), -1);
    //         }
    //     }
        
    //     resultGpu.upload(result);
    // } else {
    //     resultGpu = input.getGpuMat();
    // }
    cv::cuda::GpuMat resultGpu = input.getGpuMat();
    resultGpu.upload(result);

    FrameMemoryObject frameResult(resultGpu);
    
    // Add metadata
    frameResult.setMetadata<int>("original_contour_count", originalCount);
    frameResult.setMetadata<int>("filtered_contour_count", static_cast<int>(filteredContours.size()));
    frameResult.setMetadata<double>("min_area_threshold", minArea);
    frameResult.setMetadata<double>("max_area_threshold", maxArea);
    frameResult.setMetadata<std::vector<std::vector<cv::Point>>>("filtered_contours", filteredContours);
    frameResult.setMetadata<std::vector<cv::RotatedRect>>("filtered_contour_rects", minRects);
    frameResult.setMetadata<std::vector<cv::Point2f>>("filtered_contour_centers", centers);
    

    return frameResult;
}
#endif