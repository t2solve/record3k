#pragma once

#include <opencv2/core.hpp>
#include "frame_memory_object.h"
#include <string>
#include <map>

// Processing modes
enum class ProcessingMode {
    CPU_ONLY,
    CUDA_PREFERRED,
    CUDA_ONLY
};

// Configuration structure for parameterized filters
struct FilterConfig {
    std::map<std::string, double> parameters;
    
    void setParameter(const std::string& key, double value) {
        parameters[key] = value;
    }
    
    double getParameter(const std::string& key, double defaultValue = 0.0) const {
        auto it = parameters.find(key);
        return (it != parameters.end()) ? it->second : defaultValue;
    }
};

// Base interface for processing steps
class ProcessStep {
public:
    virtual ~ProcessStep() = default;
    
    // Process a frame with given configuration using FrameMemoryObject
    virtual FrameMemoryObject process(const FrameMemoryObject& input, const FilterConfig& config) = 0;
    
    // Legacy process method for backward compatibility
    virtual cv::Mat process(const cv::Mat& input, const FilterConfig& config) {
        FrameMemoryObject frameObj(input);
        FrameMemoryObject result = process(frameObj, config);
        return result.getCpuMat();
    }
    
    // Get the name/type of this processing step
    virtual std::string getName() const = 0;
    
    // Check if this step is available (e.g., CUDA might not be available)
    virtual bool isAvailable() const = 0;
    
    // Get processing mode (CPU or CUDA)
    virtual ProcessingMode getMode() const = 0;
    
    // Get preferred memory type for this step
    virtual MemoryType getPreferredMemoryType() const = 0;
};
