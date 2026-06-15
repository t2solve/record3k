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
    
    // /**
    //  * @brief Execute with a single config applied to all steps
    //  *
    //  * Creates a vector of configs filled with the same config for each step
    //  * and forwards to the main processLine overload.
    //  */
    // static FrameMemoryObject processLine(
    //     const std::vector<std::shared_ptr<ProcessStep>>& steps,
    //     const FrameMemoryObject& input,
    //     const FilterConfig& config);
    
    /**
     * @brief Analyze the pipeline to determine optimal memory strategy
     */
    static MemoryLocation determineOptimalMemoryLocation(
        const std::vector<std::shared_ptr<ProcessStep>>& steps);
    
    /**
     * @brief Check if pipeline contains a specific filter type
     * @param steps Vector of processing steps
     * @param filterType Type of filter to check for
     * @return true if the filter type exists in the pipeline
     */
    static bool hasProcessingStep(
        const std::vector<std::shared_ptr<ProcessStep>>& steps,
        FilterType filterType);
    
    /**
     * @brief Get processing step by filter type
     * @param steps Vector of processing steps
     * @param filterType Type of filter to retrieve
     * @return Shared pointer to the step, or nullptr if not found
     */
    static std::shared_ptr<ProcessStep> getProcessingStep(
        const std::vector<std::shared_ptr<ProcessStep>>& steps,
        FilterType filterType);
    
    /**
     * @brief Add FrameWriter step if not already present
     * @param steps Vector of processing steps (modified in place)
     * @param configs Vector of configurations (modified in place)
     * @param mode Processing mode for the FrameWriter
     * @param outputDir Output directory for frames
     * @param format Image format ("jpg", "png", etc.)
     * @return true if FrameWriter was added, false if already exists
     */
    static bool addFrameWriterIfNotExists(
        std::vector<std::shared_ptr<ProcessStep>>& steps,
        std::vector<FilterConfig>& configs,
        ProcessingMode mode,
        const std::string& outputDir,
        const std::string& format = "jpg");
    
    /**
     * @brief Enable or disable FrameWriter step
     * @param configs Vector of configurations (modified in place)
     * @param steps Vector of processing steps (for finding FrameWriter)
     * @param enable true to enable, false to disable
     * @return true if FrameWriter was found and modified
     */
    static bool setFrameWriterEnabled(
        std::vector<FilterConfig>& configs,
        const std::vector<std::shared_ptr<ProcessStep>>& steps,
        bool enable);

    /**
     * @brief Add DebugLogger step at the end of the pipeline if not already present.
     *        Should be called only when debug mode is active.
     */
    static bool addDebugLoggerIfNotExists(
        std::vector<std::shared_ptr<ProcessStep>>& steps,
        std::vector<FilterConfig>& configs,
        ProcessingMode mode = ProcessingMode::CPU_ONLY);

    /**
     * @brief Enable or disable the DebugLogger step via its config.
     */
    static bool setDebugLoggerEnabled(
        std::vector<FilterConfig>& configs,
        const std::vector<std::shared_ptr<ProcessStep>>& steps,
        bool enable);

    /**
     * @brief Flush accumulated debug data to a SQLite file.
     *        Call this at the end of a recording session.
     * @param steps  Pipeline step vector (used to locate the DebugLoggerStep)
     * @param dbPath Full path to the output .sqlite file
     * @return true on success
     */
    static bool flushDebugLogger(
        const std::vector<std::shared_ptr<ProcessStep>>& steps,
        const std::string& dbPath);
    
private:
    /**
     * @brief Count consecutive steps of the same memory type
     */
    static size_t countConsecutiveSteps(
        const std::vector<std::shared_ptr<ProcessStep>>& steps,
        size_t startIndex,
        MemoryLocation memoryLocation);
};