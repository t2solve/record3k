#include "pipeline_profiler.h"
#include <stdexcept>

PipelineProfiler::PipelineProfile PipelineProfiler::profileProcessLine(
    const std::vector<std::shared_ptr<ProcessStep>>& steps,
    const FrameMemoryObject& input,
    const std::vector<FilterConfig>& configs) {
    
    PipelineProfile profile;
    profile.totalProcessingTime = std::chrono::microseconds(0);
    profile.totalConversionTime = std::chrono::microseconds(0);
    profile.totalMemoryConversions = 0;
    
    if (steps.empty()) {
        return profile;
    }
    
    if (steps.size() != configs.size()) {
        throw std::invalid_argument("Number of steps must match number of configs");
    }
    
    FrameMemoryObject current = input.clone();
    
    for (size_t i = 0; i < steps.size(); ++i) {
        const auto& step = steps[i];
        const auto& config = configs[i];
        
        StepProfile stepProfile;
        stepProfile.stepName = step->getName();
        stepProfile.inputMemoryLocation = current.getMemoryLocation();
        
        if (!step->isAvailable()) {
            std::cerr << "Warning: Step " << step->getName() << " is not available, skipping\n";
            continue;
        }
        
        // Measure conversion time
        auto conversionStart = std::chrono::high_resolution_clock::now();
        MemoryLocation stepPreferredLocation = step->getPreferredMemoryLocation();
        MemoryLocation oldLocation = current.getMemoryLocation();
        current.moveToMemoryLocation(stepPreferredLocation);
        bool conversionOccurred = (oldLocation != stepPreferredLocation);
        auto conversionEnd = std::chrono::high_resolution_clock::now();
        
        stepProfile.conversionTime = std::chrono::duration_cast<std::chrono::microseconds>(
            conversionEnd - conversionStart);
        stepProfile.memoryConversionOccurred = conversionOccurred;
        
        if (conversionOccurred) {
            profile.totalMemoryConversions++;
        }
        
        // Measure processing time
        auto processingStart = std::chrono::high_resolution_clock::now();
        current = step->process(current, config);
        auto processingEnd = std::chrono::high_resolution_clock::now();
        
        stepProfile.processingTime = std::chrono::duration_cast<std::chrono::microseconds>(
            processingEnd - processingStart);
        stepProfile.outputMemoryLocation = current.getMemoryLocation();
        
        profile.steps.push_back(stepProfile);
        profile.totalProcessingTime += stepProfile.processingTime;
        profile.totalConversionTime += stepProfile.conversionTime;
    }
    
    return profile;
}

void PipelineProfiler::PipelineProfile::print() const {
    std::cout << "\n=== Pipeline Performance Profile ===\n";
    std::cout << "Total Processing Time: " << totalProcessingTime.count() << " μs\n";
    std::cout << "Total Conversion Time: " << totalConversionTime.count() << " μs\n";
    std::cout << "Total Memory Conversions: " << totalMemoryConversions << "\n";
    std::cout << "Conversion Overhead: " << 
        (totalConversionTime.count() * 100.0 / (totalProcessingTime.count() + totalConversionTime.count())) 
        << "%\n\n";
    
    for (const auto& step : steps) {
        std::cout << "Step: " << step.stepName << "\n";
        std::cout << "  Processing Time: " << step.processingTime.count() << " μs\n";
        std::cout << "  Conversion Time: " << step.conversionTime.count() << " μs\n";
        std::cout << "  Input Memory: " << 
            (step.inputMemoryLocation == MemoryLocation::CPU ? "CPU" : "CUDA") << "\n";
        std::cout << "  Output Memory: " << 
            (step.outputMemoryLocation == MemoryLocation::CPU ? "CPU" : "CUDA") << "\n";
        std::cout << "  Memory Conversion: " << 
            (step.memoryConversionOccurred ? "Yes" : "No") << "\n\n";
    }
}