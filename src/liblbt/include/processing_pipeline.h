#pragma once

#include "process_step.h"
#include "frame_memory_object.h"
#include <vector>
#include <memory>
#include <chrono>
#include <iostream>

/**
 * @brief Processing pipeline that efficiently chains multiple processing steps
 * 
 * This function optimizes memory transfers by keeping data in the most efficient
 * memory location (CPU or GPU) throughout the pipeline execution.
 */
class ProcessingPipeline {
public:
    struct StepProfile {
        std::string stepName;
        std::chrono::microseconds processingTime;
        std::chrono::microseconds conversionTime;
        MemoryLocation inputMemoryLocation;
        MemoryLocation outputMemoryLocation;
        bool memoryConversionOccurred;
    };

    /**
     * @brief Execute a series of processing steps on a frame
     * 
     * @param steps Vector of processing steps to execute in order
     * @param input Input frame object
     * @param configs Configuration for each step (must match steps size)
     * @return Processed frame object
     */
    static FrameMemoryObject processLine(
        const std::vector<std::shared_ptr<ProcessStep>>& steps,
        const FrameMemoryObject& input,
        const std::vector<FilterConfig>& configs);
    
    /**
     * @brief Execute with a single config applied to all steps
     */
    static FrameMemoryObject processLine(
        const std::vector<std::shared_ptr<ProcessStep>>& steps,
        const FrameMemoryObject& input,
        const FilterConfig& config);
    
    /**
     * @brief Analyze the pipeline to determine optimal memory strategy
     */
    static MemoryLocation determineOptimalMemoryLocation(
        const std::vector<std::shared_ptr<ProcessStep>>& steps);
    
private:
    /**
     * @brief Count consecutive steps of the same memory type
     */
    static size_t countConsecutiveSteps(
        const std::vector<std::shared_ptr<ProcessStep>>& steps,
        size_t startIndex,
        MemoryLocation memoryLocation);
};