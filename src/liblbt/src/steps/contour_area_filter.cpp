#include "contour_area_filter.h"

FrameMemoryObject ContourAreaFilterCPUStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty()) {
        return FrameMemoryObject();
    }
    
    cv::Mat inputMat = input.getCpuMat();
    
    // Get config parameters
    double minArea = config.getParameter("min_area", 150.0);
    double maxArea = config.getParameter("max_area", 300000.0);
    bool debug = config.getParameter("debug", 0.0) > 0.5; // when true, draw overlays
    
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
    //we do need a test here if no courts are left after filtering?
    if (filteredContours.empty() ) {
        std::cerr << "[ContourAreaFilterCPUStep] Warning: No contours left after filtering with min_area=" 
                  << minArea << " and max_area=" << maxArea << ".\n";
        if (originalCount > 0) {
            std::cerr << "  Original contour count was " << originalCount << ".\n";
            //we iterate the original contours and print their areas
            for (size_t i = 0; i < contours.size(); ++i) {
                double area = cv::contourArea(contours[i]);
                std::cerr << "    Contour " << i << " area: " << area << "\n";
            }   
        }
    }
    
    
    // Create output image with optional debug visualization
    cv::Mat result;
    if (debug) {
        // Convert to color for visualization
        if (inputMat.channels() == 1) {
            cv::cvtColor(inputMat, result, cv::COLOR_GRAY2BGR);
        } else {
            result = inputMat.clone();
        }
        // Draw filtered contours (green)
        cv::drawContours(result, filteredContours, -1, cv::Scalar(0, 255, 0), 2);
        // Draw min area rectangles (blue)
        for (const auto& rect : minRects) {
            cv::Point2f vertices[4];
            rect.points(vertices);
            for (int i = 0; i < 4; i++) {
                cv::line(result, vertices[i], vertices[(i+1)%4], cv::Scalar(255, 0, 0), 2);
            }
        }
        // Draw centers (blue)
        for (const auto& c : centers) {
            cv::circle(result, c, 5, cv::Scalar(255, 0, 0), -1);
        }
    } else {
        result = inputMat.clone();
    }
    
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
    double minArea = config.getParameter("min_area", 80.0);
    double maxArea = config.getParameter("max_area", 300000.0);
    bool debug = config.getParameter("debug", 0.0) > 0.5; // when true, draw overlays
    
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
    //we do need a test here if no courts are left after filtering?
    if (filteredContours.empty() ) {
        std::cerr << "[ContourAreaFilterCPUStep] Warning: No contours left after filtering with min_area=" 
                  << minArea << " and max_area=" << maxArea << ".\n";
        if (originalCount > 0) {
            std::cerr << "  Original contour count was " << originalCount << ".\n";    
            //we iterte the original contours and print their areas
            for (size_t i = 0; i < contours.size(); ++i) {
                double area = cv::contourArea(contours[i]);
                std::cerr << "    Contour " << i << " area: " << area << "\n";
            }   
        }
    }

    
    // Create output image on GPU; debug drawing done on CPU then uploaded
    cv::cuda::GpuMat resultGpu;
    if (debug) {
        cv::Mat result;
        if (cpuMat.channels() == 1) {
            cv::cvtColor(cpuMat, result, cv::COLOR_GRAY2BGR);
        } else {
            result = cpuMat.clone();
        }
        // Draw filtered contours (green)
        cv::drawContours(result, filteredContours, -1, cv::Scalar(0, 255, 0), 2);
        // Draw min area rectangles (blue)
        for (const auto& rect : minRects) {
            cv::Point2f vertices[4];
            rect.points(vertices);
            for (int i = 0; i < 4; i++) {
                cv::line(result, vertices[i], vertices[(i+1)%4], cv::Scalar(255, 0, 0), 2);
            }
        }
        // Draw centers (blue)
        for (const auto& c : centers) {
            cv::circle(result, c, 5, cv::Scalar(255, 0, 0), -1);
        }
        resultGpu.upload(result);
    } else {
        resultGpu = input.getGpuMat();
    }

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