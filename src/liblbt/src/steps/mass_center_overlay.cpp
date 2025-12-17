#include "../../include/liblbt/steps/mass_center_overlay.h"
#include <iostream>

// Calculate the mass center of the convex hull from a contour selection
static cv::Point2f calcCenterFromContours(const std::vector<std::vector<cv::Point>>& contourSelection)
{
    if (contourSelection.empty()) {
        return cv::Point2f(-1, -1);
    }
    std::vector<cv::Point> allContourPoints;
    for (const auto& contour : contourSelection) {
        allContourPoints.insert(allContourPoints.end(), contour.begin(), contour.end());
    }
    if (allContourPoints.empty()) {
        return cv::Point2f(-1, -1);
    }
    std::vector<cv::Point> convexHull;
    cv::convexHull(allContourPoints, convexHull, false);
    if (convexHull.empty()) {
        return cv::Point2f(-1, -1);
    }
    cv::Moments muConvexHull = cv::moments(convexHull, true);
    if (muConvexHull.m00 == 0) {
        return cv::Point2f(-1, -1);
    }
    return cv::Point2f(
        static_cast<float>(muConvexHull.m10 / muConvexHull.m00),
        static_cast<float>(muConvexHull.m01 / muConvexHull.m00)
    );
}

FrameMemoryObject MassCenterOverlayCPUStep::process(const FrameMemoryObject& input, const FilterConfig& /*config*/)
{
    try {
        // Ensure we have an image on CPU
        cv::Mat img = input.getCpuMat().clone();
        if (img.empty()) {
            return FrameMemoryObject();
        }
        // Retrieve contours from metadata (produced by ContourAreaFilter)
        if (!input.hasMetadata("filtered_contours")) {
            std::cerr << "[MassCenterOverlay] Missing metadata 'filtered_contours'; ensure ContourAreaFilter runs before this step.\n";
            return FrameMemoryObject(img);
        }
        std::vector<std::vector<cv::Point>> contours;
        try {
            contours = input.getMetadata<std::vector<std::vector<cv::Point>>>("filtered_contours");
        } catch (const std::exception& e) {
            std::cerr << "[MassCenterOverlay] Metadata type mismatch for 'filtered_contours': " << e.what() << "\n";
            return FrameMemoryObject(img);
        }

        cv::Point2f center = calcCenterFromContours(contours);
        if (center.x >= 0 && center.y >= 0) {
            // Draw a red dot; ensure 3-channel for colored dot
            if (img.channels() == 1) {
                cv::cvtColor(img, img, cv::COLOR_GRAY2BGR);
            }
            cv::circle(img, center, 10, cv::Scalar(0, 0, 255), -1);
        } else {
            std::cerr << "[MassCenterOverlay] Center could not be calculated (no or invalid contours).\n";
        }
        //construct a new FrameMemoryObject with metadata
        FrameMemoryObject frameResult(img);
        frameResult.setMetadata<cv::Point2f>("mass_center_point", center);
        return frameResult;
    } catch (const std::exception& e) {
        std::cerr << "[MassCenterOverlay] Exception: " << e.what() << "\n";
        return FrameMemoryObject();
    }
}
