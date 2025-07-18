#pragma once

#include "../process_step.h"
#include "../frame_memory_object.h"
#include <opencv2/opencv.hpp>
#include <opencv2/video/background_segm.hpp>
#include <opencv2/cudafilters.hpp>
#ifdef CUDA_ENABLED
#include <opencv2/cudabgsegm.hpp>
#include "../cuda_utils.h"
#endif

class BackgroundSubtractionCPUStep : public ProcessStep {
public:
    BackgroundSubtractionCPUStep();
    ~BackgroundSubtractionCPUStep();
    
    FrameMemoryObject process(const FrameMemoryObject& input, const FilterConfig& config) override;
    std::string getName() const override { return "BackgroundSubtraction_CPU"; }
    bool isAvailable() const override { return true; }
    ProcessingMode getMode() const override { return ProcessingMode::CPU_ONLY; }
    MemoryLocation getPreferredMemoryLocation() const override { return MemoryLocation::CPU; }
    
private:
    cv::Ptr<cv::BackgroundSubtractor> m_backgroundSubtractor;
    bool m_initialized;
    
    void initializeSubtractor(const FilterConfig& config);
};

#ifdef CUDA_ENABLED
class BackgroundSubtractionCUDAStep : public ProcessStep {
public:
    BackgroundSubtractionCUDAStep();
    ~BackgroundSubtractionCUDAStep();
    
    FrameMemoryObject process(const FrameMemoryObject& input, const FilterConfig& config) override;
    std::string getName() const override { return "BackgroundSubtraction_CUDA"; }
    bool isAvailable() const override;
    ProcessingMode getMode() const override { return ProcessingMode::CUDA_ONLY; }
    MemoryLocation getPreferredMemoryLocation() const override { return MemoryLocation::GPU; }
    
private:
    cv::Ptr<cv::cuda::BackgroundSubtractorMOG2> m_backgroundSubtractor;
    cv::cuda::GpuMat m_gpuForegroundMask;
    bool m_initialized;
    bool m_cudaAvailable;
    
    void initializeSubtractor(const FilterConfig& config);
};
#endif