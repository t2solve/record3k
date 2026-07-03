#include "contour_area_filter.h"
#include <algorithm>
#include <numeric>
#include <sstream>

// Keep only the N largest contours (by area). N=0 means keep all.
// Reorders all three parallel vectors together.
static void selectLargestContours(
    int maxContours,
    std::vector<std::vector<cv::Point>>& contours,
    std::vector<cv::RotatedRect>& rects,
    std::vector<cv::Point2f>& centers)
{
    if (maxContours <= 0 || static_cast<int>(contours.size()) <= maxContours)
        return;

    // Sort indices by contour area descending
    std::vector<int> idx(contours.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](int a, int b) {
        return cv::contourArea(contours[a]) > cv::contourArea(contours[b]);
    });
    idx.resize(static_cast<size_t>(maxContours));

    std::vector<std::vector<cv::Point>> selContours;
    std::vector<cv::RotatedRect>        selRects;
    std::vector<cv::Point2f>            selCenters;
    selContours.reserve(idx.size());
    selRects.reserve(idx.size());
    selCenters.reserve(idx.size());
    for (int i : idx) {
        selContours.push_back(std::move(contours[i]));
        selRects.push_back(rects[i]);
        selCenters.push_back(centers[i]);
    }
    contours = std::move(selContours);
    rects    = std::move(selRects);
    centers  = std::move(selCenters);
}

FrameMemoryObject ContourAreaFilterCPUStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty()) {
        return FrameMemoryObject();
    }
    
    cv::Mat inputMat = input.getCpuMat();
    
    // Get config parameters
    double minArea    = config.getParameter("min_area",     150.0);
    double maxArea    = config.getParameter("max_area",     300000.0);
    int    maxContours = static_cast<int>(config.getParameter("max_contours", 0.0)); // 0 = keep all
    bool   debug      = config.getParameter("debug", 0.0) > 0.5;

    // Find all contours
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(inputMat.clone(), contours, hierarchy,
                     cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    int originalCount = static_cast<int>(contours.size());

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

    // area_filter_count: how many passed the area gate (used for η / multi-contour metric)
    int areaFilterCount = static_cast<int>(filteredContours.size());

    // Select only the N largest if max_contours > 0 (prevents centroid smear)
    selectLargestContours(maxContours, filteredContours, minRects, centers);

    if (filteredContours.empty()) {
        std::stringstream ss;
        ss << "[ContourAreaFilterCPUStep] Warning: No contours left after filtering with min_area="
           << minArea << " and max_area=" << maxArea;
        logWarning(ss.str());
        if (originalCount > 0) {
            std::stringstream debugSs;
            debugSs << "  Original contour count was " << originalCount;
            logDebug(debugSs.str());
            for (size_t i = 0; i < contours.size(); ++i) {
                double area = cv::contourArea(contours[i]);
                std::stringstream areaSs;
                areaSs << "    Contour " << i << " area: " << area;
                logDebug(areaSs.str());
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
    frameResult.setMetadata<int>("original_contour_count",  originalCount);
    frameResult.setMetadata<int>("area_filter_count",        areaFilterCount);  // before max_contours; use for η
    frameResult.setMetadata<int>("filtered_contour_count",   static_cast<int>(filteredContours.size()));
    frameResult.setMetadata<double>("min_area_threshold",    minArea);
    frameResult.setMetadata<double>("max_area_threshold",    maxArea);
    frameResult.setMetadata<std::vector<std::vector<cv::Point>>>("filtered_contours",        filteredContours);
    frameResult.setMetadata<std::vector<cv::RotatedRect>>        ("filtered_contour_rects",  minRects);
    frameResult.setMetadata<std::vector<cv::Point2f>>            ("filtered_contour_centers",centers);

    return frameResult;
}

#ifdef CUDA_ENABLED
bool ContourAreaFilterCUDAStep::isAvailable() const {
    return cv::cuda::getCudaEnabledDeviceCount() > 0;
}

FrameMemoryObject ContourAreaFilterCUDAStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty()) {
        logWarning("[ContourAreaFilterCUDAStep] Input frame is empty, passing through");
        return input;
    }
    
    if (!isAvailable()) {
        logWarning("[ContourAreaFilterCUDAStep] CUDA not available, passing through input frame");
        return input;
    }
    
    // Download from GPU to CPU for contour detection
    // Note: OpenCV CUDA doesn't have findContours, so we do it on CPU
    cv::Mat cpuMat = input.getCpuMat();
    
    // Get config parameters
    double minArea     = config.getParameter("min_area",     150.0);
    double maxArea     = config.getParameter("max_area",     300000.0);
    int    maxContours = static_cast<int>(config.getParameter("max_contours", 0.0)); // 0 = keep all
    bool   debug       = config.getParameter("debug", 0.0) > 0.5;

    // Find all contours (on CPU — OpenCV CUDA has no findContours)
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(cpuMat.clone(), contours, hierarchy,
                     cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    int originalCount = static_cast<int>(contours.size());

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

    // area_filter_count: how many passed the area gate (used for η / multi-contour metric)
    int areaFilterCount = static_cast<int>(filteredContours.size());

    // Select only the N largest if max_contours > 0 (prevents centroid smear)
    selectLargestContours(maxContours, filteredContours, minRects, centers);

    if (filteredContours.empty()) {
        std::stringstream ss;
        ss << "[ContourAreaFilterCUDAStep] Warning: No contours left after filtering with min_area="
           << minArea << " and max_area=" << maxArea;
        logWarning(ss.str());
        if (originalCount > 0) {
            std::stringstream debugSs;
            debugSs << "  Original contour count was " << originalCount;
            logDebug(debugSs.str());
            for (size_t i = 0; i < contours.size(); ++i) {
                double area = cv::contourArea(contours[i]);
                std::stringstream areaSs;
                areaSs << "    Contour " << i << " area: " << area;
                logDebug(areaSs.str());
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
    frameResult.setMetadata<int>("original_contour_count",  originalCount);
    frameResult.setMetadata<int>("area_filter_count",        areaFilterCount);  // before max_contours; use for η
    frameResult.setMetadata<int>("filtered_contour_count",   static_cast<int>(filteredContours.size()));
    frameResult.setMetadata<double>("min_area_threshold",    minArea);
    frameResult.setMetadata<double>("max_area_threshold",    maxArea);
    frameResult.setMetadata<std::vector<std::vector<cv::Point>>>("filtered_contours",        filteredContours);
    frameResult.setMetadata<std::vector<cv::RotatedRect>>        ("filtered_contour_rects",  minRects);
    frameResult.setMetadata<std::vector<cv::Point2f>>            ("filtered_contour_centers",centers);

    return frameResult;
}
#endif