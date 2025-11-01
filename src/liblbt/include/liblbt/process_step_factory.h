#pragma once

#include "process_step.h"
#include <memory>

class ProcessStepFactory {
public:
    // Basic filters
    static std::shared_ptr<ProcessStep> createGaussianBlur(ProcessingMode mode = ProcessingMode::CUDA_PREFERRED);
    static std::shared_ptr<ProcessStep> createBilateralFilter(ProcessingMode mode = ProcessingMode::CUDA_PREFERRED);
    static std::shared_ptr<ProcessStep> createMedianFilter(ProcessingMode mode = ProcessingMode::CUDA_PREFERRED);
    static std::shared_ptr<ProcessStep> createEdgeDetection(ProcessingMode mode = ProcessingMode::CUDA_PREFERRED);
    static std::shared_ptr<ProcessStep> createSharpen(ProcessingMode mode = ProcessingMode::CUDA_PREFERRED);
    static std::shared_ptr<ProcessStep> createDenoise(ProcessingMode mode = ProcessingMode::CPU_ONLY);
    static std::shared_ptr<ProcessStep> createContourDetection(ProcessingMode mode);
    static std::shared_ptr<ProcessStep> createLensCorrection(ProcessingMode mode = ProcessingMode::CUDA_PREFERRED);
    static std::shared_ptr<ProcessStep> createContourAreaFilter(ProcessingMode mode = ProcessingMode::CPU_ONLY);
    static std::shared_ptr<ProcessStep> createROICircleCrop(ProcessingMode mode = ProcessingMode::CUDA_PREFERRED);
    static std::shared_ptr<ProcessStep> createBinaryThreshold(ProcessingMode mode = ProcessingMode::CUDA_PREFERRED);
    // Background subtraction
    static std::shared_ptr<ProcessStep> createBackgroundSubtractionMOG2(ProcessingMode mode = ProcessingMode::CUDA_PREFERRED);
    static std::shared_ptr<ProcessStep> createBackgroundSubtractionGMG(ProcessingMode mode = ProcessingMode::CPU_ONLY);
    static std::shared_ptr<ProcessStep> createBackgroundSubtractionCNT(ProcessingMode mode = ProcessingMode::CPU_ONLY);
    

    // Factory method by filter type
    static std::shared_ptr<ProcessStep> createStep(FilterType filterType, ProcessingMode mode = ProcessingMode::CUDA_PREFERRED);
};