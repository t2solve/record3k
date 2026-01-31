#include "processing_pipeline.h"
#include "process_step_factory.h"
#include <iostream>
#include <stdexcept>

FrameMemoryObject ProcessingPipeline::processLine(
    const std::vector<std::shared_ptr<ProcessStep>>& steps,
    const FrameMemoryObject& input,
    const std::vector<FilterConfig>& configs) {
    
    if (steps.empty()) {
        return input.clone();
    }
    
    if (steps.size() != configs.size()) {
        throw std::invalid_argument("Number of steps must match number of configs");
    }
    
    // Determine the optimal memory location for the pipeline
    MemoryLocation optimalMemoryLocation = determineOptimalMemoryLocation(steps);
    
    // Start with a copy of the input
    FrameMemoryObject current = input.clone();
    
    // Convert to optimal memory location if needed
    current.moveToMemoryLocation(optimalMemoryLocation);
    
    // Process each step
    for (size_t i = 0; i < steps.size(); ++i) {
        const auto& step = steps[i];
        const auto& config = configs[i];
        
        if (!step->isAvailable()) {
            std::cerr << "Warning: Step " << step->getName() << " is not available, skipping\n";
            continue;
        }
        
        // Get the step's preferred memory location
        MemoryLocation stepPreferredLocation = step->getPreferredMemoryLocation();
        
        // Convert to step's preferred location if it's beneficial
        // (avoid unnecessary conversions for single-step operations)
        if (i == 0 || stepPreferredLocation != current.getMemoryLocation()) {
            // Only convert if we have multiple consecutive steps of the same type
            size_t consecutiveSteps = countConsecutiveSteps(steps, i, stepPreferredLocation);
            if (consecutiveSteps > 1 || i == 0) {
                current.moveToMemoryLocation(stepPreferredLocation);
            }
        }
        
        // Execute the step
        current = step->process(current, config);
    }
    
    return current;
}

// Commented out: single-config overload not currently needed
// FrameMemoryObject ProcessingPipeline::processLine(
//     const std::vector<std::shared_ptr<ProcessStep>>& steps,
//     const FrameMemoryObject& input,
//     const FilterConfig& config) {
//     
//     // Create a vector of configs with the same config for all steps
//     std::vector<FilterConfig> configs(steps.size(), config);
//     return processLine(steps, input, configs);
// }

MemoryLocation ProcessingPipeline::determineOptimalMemoryLocation(
    const std::vector<std::shared_ptr<ProcessStep>>& steps) {
    
    if (steps.empty()) {
        return MemoryLocation::CPU;
    }
    
    // Count steps by memory location preference
    size_t cpuSteps = 0;
    size_t gpuSteps = 0;
    
    for (const auto& step : steps) {
        if (step->getPreferredMemoryLocation() == MemoryLocation::CPU) {
            cpuSteps++;
        } else {
            gpuSteps++;
        }
    }
    
    // If we have more GPU steps, and CUDA is available, prefer GPU
    if (gpuSteps > cpuSteps) {
#ifdef CUDA_ENABLED
        if (FrameMemoryObject::isCudaAvailable()) {
            return MemoryLocation::GPU;
        }
#endif
    }
    
    return MemoryLocation::CPU;
}

size_t ProcessingPipeline::countConsecutiveSteps(
    const std::vector<std::shared_ptr<ProcessStep>>& steps,
    size_t startIndex,
    MemoryLocation memoryLocation) {
    
    size_t count = 0;
    for (size_t i = startIndex; i < steps.size(); ++i) {
        if (steps[i]->getPreferredMemoryLocation() == memoryLocation) {
            count++;
        } else {
            break;
        }
    }
    return count;
}

bool ProcessingPipeline::hasProcessingStep(
    const std::vector<std::shared_ptr<ProcessStep>>& steps,
    FilterType filterType) {
    
    for (const auto& step : steps) {
        if (step && step->getFilterType() == filterType) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<ProcessStep> ProcessingPipeline::getProcessingStep(
    const std::vector<std::shared_ptr<ProcessStep>>& steps,
    FilterType filterType) {
    
    for (const auto& step : steps) {
        if (step && step->getFilterType() == filterType) {
            return step;
        }
    }
    return nullptr;
}

bool ProcessingPipeline::addFrameWriterIfNotExists(
    std::vector<std::shared_ptr<ProcessStep>>& steps,
    std::vector<FilterConfig>& configs,
    ProcessingMode mode,
    const std::string& outputDir,
    const std::string& format) {
    
    // Check if FrameWriter already exists
    if (hasProcessingStep(steps, FilterType::FRAME_WRITER)) {
        std::cout << "[ProcessingPipeline] FrameWriter already exists in pipeline" << std::endl;
        return false;
    }
    
    // Create FrameWriter step
    auto frameWriter = ProcessStepFactory::createStep(FilterType::FRAME_WRITER, mode);
    if (!frameWriter) {
        std::cerr << "[ProcessingPipeline] Failed to create FrameWriter step" << std::endl;
        return false;
    }
    
    // Create config for FrameWriter
    FilterConfig frameWriterConfig;
    frameWriterConfig.setParameter("output_dir", outputDir);
    frameWriterConfig.setParameter("format", format);
    frameWriterConfig.setParameter("enabled", true);
    
    // Add to beginning of pipeline (position 0) to capture raw camera frames
    steps.insert(steps.begin(), frameWriter);
    configs.insert(configs.begin(), frameWriterConfig);
    
    std::cout << "[ProcessingPipeline] Added FrameWriter at beginning of pipeline (output: " << outputDir << ")" << std::endl;
    return true;
}

bool ProcessingPipeline::setFrameWriterEnabled(
    std::vector<FilterConfig>& configs,
    const std::vector<std::shared_ptr<ProcessStep>>& steps,
    bool enable) {
    
    // Find the index of FrameWriter step
    for (size_t i = 0; i < steps.size(); ++i) {
        if (steps[i] && steps[i]->getFilterType() == FilterType::FRAME_WRITER) {
            if (i < configs.size()) {
                configs[i].setParameter("enabled", enable);
                std::cout << "[ProcessingPipeline] FrameWriter " 
                         << (enable ? "enabled" : "disabled") << std::endl;
                return true;
            }
        }
    }
    
    std::cerr << "[ProcessingPipeline] FrameWriter not found in pipeline" << std::endl;
    return false;
}
