#pragma once

#include <opencv2/core.hpp>
#include "frame_memory_object.h"
#include "step_logger.h"
#include <string>
#include <map>
#include <memory>

// Processing modes
enum class ProcessingMode {
    CPU_ONLY,
    CUDA_PREFERRED,
    CUDA_ONLY
};

enum class FilterType {
    NONE,
    GAUSSIAN_BLUR,
    BILATERAL_FILTER,
    MEDIAN_FILTER,
    EDGE_DETECTION,
    SHARPEN,
    DENOISE,
    BACKGROUND_SUBTRACTION_MOG2,
    BACKGROUND_SUBTRACTION_GMG,
    BACKGROUND_SUBTRACTION_CNT,
    CONTOUR_DETECTION,
    LENS_CORRECTION,
    ROI_CIRCLE_CROP,
    BINARY_THRESHOLD,
    CONTOUR_AREA_FILTER,
    MASS_CENTER_OVERLAY,
    MORPHOLOGY_CLOSE, 
    PROJECT_POINT_TO_2D_SURFACE,
    FRAME_WRITER,
    DEBUG_LOGGER,
    CUSTOM
};



// Base interface for processing steps
class ProcessStep {
public:
    virtual ~ProcessStep() = default;
    
    // Process a frame with given configuration using FrameMemoryObject
    virtual FrameMemoryObject process(const FrameMemoryObject& input, const FilterConfig& config) = 0;
    
    // Get the name/type of this processing step
    virtual std::string getName() const = 0;
    
    // Get the filter type of this processing step
    virtual FilterType getFilterType() const = 0;
    
    // Check if this step is available (e.g., CUDA might not be available)
    virtual bool isAvailable() const = 0;
    
    // Get processing mode (CPU or CUDA)
    virtual ProcessingMode getMode() const = 0;
    
    // Get preferred memory location for this step
    virtual MemoryLocation getPreferredMemoryLocation() const = 0;
    
    // Set logger for this step (optional, for centralized logging)
    virtual void setLogger(std::shared_ptr<lbt::StepLogger> logger) {
        m_logger = logger;
    }
    
protected:
    // Logger instance (nullptr if not set)
    std::shared_ptr<lbt::StepLogger> m_logger;
    
    // Helper methods for logging (safe to call even if logger is null)
    void logDebug(const std::string& message) const {
        if (m_logger) m_logger->debug(message);
    }
    
    void logInfo(const std::string& message) const {
        if (m_logger) m_logger->info(message);
    }
    
    void logWarning(const std::string& message) const {
        if (m_logger) m_logger->warning(message);
    }
    
    void logError(const std::string& message) const {
        if (m_logger) m_logger->error(message);
    }
};
