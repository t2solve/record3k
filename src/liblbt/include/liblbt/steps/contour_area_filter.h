#pragma once

#include "../process_step.h"
#include "../frame_memory_object.h"
#include <opencv2/opencv.hpp>
#ifdef CUDA_ENABLED
#include <opencv2/cudaarithm.hpp>
#include <opencv2/cudaimgproc.hpp>
#include "../cuda_utils.h"
#endif

class ContourAreaFilterCPUStep : public ProcessStep {
public:
    FrameMemoryObject process(const FrameMemoryObject& input, const FilterConfig& config) override;
    std::string getName() const override { return "ContourAreaFilter_CPU"; }
    bool isAvailable() const override { return true; }
    ProcessingMode getMode() const override { return ProcessingMode::CPU_ONLY; }
    MemoryLocation getPreferredMemoryLocation() const override { return MemoryLocation::CPU; }
};

#ifdef CUDA_ENABLED
class ContourAreaFilterCUDAStep : public ProcessStep {
public:
    FrameMemoryObject process(const FrameMemoryObject& input, const FilterConfig& config) override;
    std::string getName() const override { return "ContourAreaFilter_CUDA"; }
    bool isAvailable() const override;
    ProcessingMode getMode() const override { return ProcessingMode::CUDA_ONLY; }
    MemoryLocation getPreferredMemoryLocation() const override { return MemoryLocation::GPU; }
};
#endif