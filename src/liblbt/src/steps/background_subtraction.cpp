#include "background_subtraction.h"


BackgroundSubtractionCPUStep::BackgroundSubtractionCPUStep() : m_initialized(false) {
}

BackgroundSubtractionCPUStep::~BackgroundSubtractionCPUStep() {
}

FrameMemoryObject BackgroundSubtractionCPUStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty()) {
        return FrameMemoryObject();
    }
    
    cv::Mat inputMat = input.getCpuMat();
    cv::Mat foregroundMask;
    
    // Initialize background subtractor if not done yet
    if (!m_initialized) {
        initializeSubtractor(config);
        m_initialized = true;
    }
    
    // Apply background subtraction
    m_backgroundSubtractor->apply(inputMat, foregroundMask);
    
    // Apply morphological operations if requested
    bool applyMorphology = config.getParameter("apply_morphology", 0.0) > 0.5;
    if (applyMorphology) {
        int kernelSize = static_cast<int>(config.getParameter("morph_kernel_size", 3));
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(kernelSize, kernelSize));
        cv::morphologyEx(foregroundMask, foregroundMask, cv::MORPH_CLOSE, kernel);
        cv::morphologyEx(foregroundMask, foregroundMask, cv::MORPH_OPEN, kernel);
    }
    
    return FrameMemoryObject(foregroundMask);
}

void BackgroundSubtractionCPUStep::initializeSubtractor(const FilterConfig& config) {
    // Use getParameter instead of getStringParameter
    // Since FilterConfig doesn't have getStringParameter, we'll default to MOG2
    std::string method = "MOG2";  // Default method
    
    if (method == "MOG2") {
        int history = static_cast<int>(config.getParameter("history", 500));
        double varThreshold = config.getParameter("var_threshold", 16.0);
        bool detectShadows = config.getParameter("detect_shadows", 1.0) > 0.5;
        
        m_backgroundSubtractor = cv::createBackgroundSubtractorMOG2(history, varThreshold, detectShadows);
    } else if (method == "KNN") {
        int history = static_cast<int>(config.getParameter("history", 500));
        double dist2Threshold = config.getParameter("dist2_threshold", 400.0);
        bool detectShadows = config.getParameter("detect_shadows", 1.0) > 0.5;
        
        m_backgroundSubtractor = cv::createBackgroundSubtractorKNN(history, dist2Threshold, detectShadows);
    } else {
        // Default to MOG2
        m_backgroundSubtractor = cv::createBackgroundSubtractorMOG2();
    }
}

#ifdef CUDA_ENABLED
BackgroundSubtractionCUDAStep::BackgroundSubtractionCUDAStep() 
    : m_initialized(false), m_cudaAvailable(false) {
    m_cudaAvailable = cv::cuda::getCudaEnabledDeviceCount() > 0;
}

BackgroundSubtractionCUDAStep::~BackgroundSubtractionCUDAStep() {
}

bool BackgroundSubtractionCUDAStep::isAvailable() const {
    return m_cudaAvailable;
}

FrameMemoryObject BackgroundSubtractionCUDAStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty() || !isAvailable()) {
        return FrameMemoryObject();
    }
    
    cv::cuda::GpuMat inputGpu = input.getGpuMat();
    
    // Initialize background subtractor if not done yet
    if (!m_initialized) {
        initializeSubtractor(config);
        m_initialized = true;
    }
    
    // Apply background subtraction
    m_backgroundSubtractor->apply(inputGpu, m_gpuForegroundMask);
    
    return FrameMemoryObject(m_gpuForegroundMask);
}

void BackgroundSubtractionCUDAStep::initializeSubtractor(const FilterConfig& config) {
    int history = static_cast<int>(config.getParameter("history", 500));
    double varThreshold = config.getParameter("var_threshold", 16.0);
    bool detectShadows = config.getParameter("detect_shadows", 1.0) > 0.5;
    
    m_backgroundSubtractor = cv::cuda::createBackgroundSubtractorMOG2(history, varThreshold, detectShadows);
}
#endif