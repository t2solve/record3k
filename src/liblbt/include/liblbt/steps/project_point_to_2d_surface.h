#pragma once

#include "../process_step.h"
#include "../frame_memory_object.h"
#include <opencv2/core.hpp>

class ProjectPointTo2DSurfaceCPUStep : public ProcessStep {
public:
    FrameMemoryObject process(const FrameMemoryObject& input, const FilterConfig& config) override;
    std::string getName() const override { return "ProjectPointTo2DSurface_CPU"; }
    bool isAvailable() const override { return true; }
    ProcessingMode getMode() const override { return ProcessingMode::CPU_ONLY; }
    MemoryLocation getPreferredMemoryLocation() const override { return MemoryLocation::CPU; }

private:
    bool initDuringFirstStart(cv::Mat cameraMatrix, cv::Mat distCoeffs, cv::Mat extrinsic_parameters);
    bool buildInverseHomography(const cv::Mat& cameraMatrix, const cv::Mat& rvec, const cv::Mat& tvec);
    static cv::Point2d projectPixelToPlane(const cv::Point2d& p, const cv::Mat& invH);

    bool m_initialized{false};
    cv::Mat m_inverseH; // CV_64F 3x3
};