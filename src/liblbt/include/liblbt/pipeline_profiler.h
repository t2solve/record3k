#pragma once

#include "process_step.h"
#include "frame_memory_object.h"
#include <vector>
#include <memory>
#include <chrono>
#include <iostream>

/**
 * @brief Performance profiler for processing pipelines
 */
class PipelineProfiler {
public:
    struct StepProfile {
        std::string stepName;
        std::chrono::microseconds processingTime;
        std::chrono::microseconds conversionTime;
        MemoryLocation inputMemoryLocation;
        MemoryLocation outputMemoryLocation;
        bool memoryConversionOccurred;
    };
    
    struct PipelineProfile {
        std::vector<StepProfile> steps;
        std::chrono::microseconds totalProcessingTime;
        std::chrono::microseconds totalConversionTime;
        size_t totalMemoryConversions;
        
        void print() const;
    };
    
    /**
     * @brief Execute pipeline with detailed profiling
     */
    static PipelineProfile profileProcessLine(
        const std::vector<std::shared_ptr<ProcessStep>>& steps,
        const FrameMemoryObject& input,
        const std::vector<FilterConfig>& configs);
};