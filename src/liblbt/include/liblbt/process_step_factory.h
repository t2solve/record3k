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
    static std::shared_ptr<ProcessStep> createContourDetection(ProcessingMode mode= ProcessingMode::CUDA_PREFERRED);
    static std::shared_ptr<ProcessStep> createLensCorrection(ProcessingMode mode = ProcessingMode::CUDA_PREFERRED);
    static std::shared_ptr<ProcessStep> createContourAreaFilter(ProcessingMode mode = ProcessingMode::CPU_ONLY);
    static std::shared_ptr<ProcessStep> createROICircleCrop(ProcessingMode mode = ProcessingMode::CUDA_PREFERRED);
    static std::shared_ptr<ProcessStep> createBinaryThreshold(ProcessingMode mode = ProcessingMode::CUDA_PREFERRED);
    static std::shared_ptr<ProcessStep> createMassCenterOverlay(ProcessingMode mode = ProcessingMode::CPU_ONLY);
    static std::shared_ptr<ProcessStep> createMorphologyClose(ProcessingMode mode = ProcessingMode::CUDA_PREFERRED);
    static std::shared_ptr<ProcessStep> createProjectPointTo2DSurface(ProcessingMode mode= ProcessingMode::CPU_ONLY);
    
    // Background subtraction
    static std::shared_ptr<ProcessStep> createBackgroundSubtractionMOG2(ProcessingMode mode = ProcessingMode::CUDA_PREFERRED);
    static std::shared_ptr<ProcessStep> createBackgroundSubtractionGMG(ProcessingMode mode = ProcessingMode::CPU_ONLY);
    static std::shared_ptr<ProcessStep> createBackgroundSubtractionCNT(ProcessingMode mode = ProcessingMode::CPU_ONLY);
    

    // Factory method by filter type
    static std::shared_ptr<ProcessStep> createStep(FilterType filterType, ProcessingMode mode = ProcessingMode::CUDA_PREFERRED);

    // Name/type helpers to avoid duplicating mappings across the codebase
    // Map a step name (e.g., "GaussianBlur_CPU" or "GaussianBlur_CUDA") to a FilterType
    static FilterType nameToFilterType(const std::string& stepName);
    // Get a canonical base name for a FilterType (e.g., "GaussianBlur")
    static std::string filterTypeBaseName(FilterType filterType);
};