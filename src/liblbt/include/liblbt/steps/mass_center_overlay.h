#pragma once

#include "../process_step.h"
#include "../frame_memory_object.h"
#include <opencv2/opencv.hpp>

class MassCenterOverlayCPUStep : public ProcessStep {
public:
    FrameMemoryObject process(const FrameMemoryObject& input, const FilterConfig& config) override;
    std::string getName() const override { return "MassCenterOverlay_CPU"; }
    bool isAvailable() const override { return true; }
    ProcessingMode getMode() const override { return ProcessingMode::CPU_ONLY; }
    MemoryLocation getPreferredMemoryLocation() const override { return MemoryLocation::CPU; }

private:
    static cv::Point2f calculateConvexHullMassCenter(const std::vector<std::vector<cv::Point>>& contourSelection);
};
