#pragma once

#include "../process_step.h"
#include "../frame_memory_object.h"
#include <opencv2/opencv.hpp>
#ifdef CUDA_ENABLED
#include <opencv2/cudaarithm.hpp>
#include <opencv2/cudafilters.hpp>
#include <opencv2/cudawarping.hpp>
#endif

class MorphologyCloseCPUStep : public ProcessStep {
public:
    FrameMemoryObject process(const FrameMemoryObject& input, const FilterConfig& config) override;
    std::string getName() const override { return "MorphologyClose_CPU"; }
    bool isAvailable() const override { return true; }
    ProcessingMode getMode() const override { return ProcessingMode::CPU_ONLY; }
    MemoryLocation getPreferredMemoryLocation() const override { return MemoryLocation::CPU; }
};

#ifdef CUDA_ENABLED
class MorphologyCloseCUDAStep : public ProcessStep {
public:
    FrameMemoryObject process(const FrameMemoryObject& input, const FilterConfig& config) override;
    std::string getName() const override { return "MorphologyClose_CUDA"; }
    bool isAvailable() const override { return cv::cuda::getCudaEnabledDeviceCount() > 0; }
    ProcessingMode getMode() const override { return ProcessingMode::CUDA_ONLY; }
    MemoryLocation getPreferredMemoryLocation() const override { return MemoryLocation::GPU; }
};
#endif