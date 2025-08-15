#pragma once

#include "../process_step.h"
#include "../frame_memory_object.h"
#include <opencv2/opencv.hpp>
#ifdef CUDA_ENABLED
#include <opencv2/cudaimgproc.hpp>
#include "../cuda_utils.h"
#endif
#include <numeric>  //  for std::accumulate
#include <algorithm>  // for std::max_element, std::min_element



class ContourDetectionCPUStep : public ProcessStep {
public:
    FrameMemoryObject process(const FrameMemoryObject& input, const FilterConfig& config) override;
    std::string getName() const override { return "ContourDetection_CPU"; }
    bool isAvailable() const override { return true; }
    ProcessingMode getMode() const override { return ProcessingMode::CPU_ONLY; }
    MemoryLocation getPreferredMemoryLocation() const override { return MemoryLocation::CPU; }
};

#ifdef CUDA_ENABLED
class ContourDetectionCUDAStep : public ProcessStep {
public:
    FrameMemoryObject process(const FrameMemoryObject& input, const FilterConfig& config) override;
    std::string getName() const override { return "ContourDetection_CUDA"; }
    bool isAvailable() const override;
    ProcessingMode getMode() const override { return ProcessingMode::CUDA_ONLY; }
    MemoryLocation getPreferredMemoryLocation() const override { return MemoryLocation::GPU; }
};
#endif