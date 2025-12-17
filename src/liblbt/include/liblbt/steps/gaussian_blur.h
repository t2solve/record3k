#pragma once

#include "../process_step.h"
#include <opencv2/opencv.hpp>
#ifdef CUDA_ENABLED
#include <opencv2/cudafilters.hpp> 
#include <opencv2/cudaimgproc.hpp>
#include "../cuda_utils.h"
#endif

class GaussianBlurCPUStep : public ProcessStep {
public:
    FrameMemoryObject process(const FrameMemoryObject& input, const FilterConfig& config) override;
    std::string getName() const override { return "GaussianBlur_CPU"; }
    bool isAvailable() const override { return true; }
    ProcessingMode getMode() const override { return ProcessingMode::CPU_ONLY; }
    MemoryLocation getPreferredMemoryLocation() const override { return MemoryLocation::CPU; }
};

#ifdef CUDA_ENABLED
class GaussianBlurCUDAStep : public ProcessStep {
public:
    FrameMemoryObject process(const FrameMemoryObject& input, const FilterConfig& config) override;
    std::string getName() const override { return "GaussianBlur_CUDA"; }
    bool isAvailable() const override;
    ProcessingMode getMode() const override { return ProcessingMode::CUDA_ONLY; }
    MemoryLocation getPreferredMemoryLocation() const override { return MemoryLocation::GPU; }
};
#endif