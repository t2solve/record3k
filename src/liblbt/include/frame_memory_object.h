#pragma once

#include <opencv2/opencv.hpp>
#ifdef CUDA_ENABLED
#include <opencv2/cudaimgproc.hpp>
#include "cuda_utils.h"
#endif
#include <string>
#include "filter_config.h"
#include <map>
#include <any>

enum class MemoryLocation {
    CPU,
    GPU
};

class FrameMemoryObject {
public:
    FrameMemoryObject();
    FrameMemoryObject(const cv::Mat& mat);
    #ifdef CUDA_ENABLED
    FrameMemoryObject(const cv::cuda::GpuMat& gpuMat);
    #endif
    ~FrameMemoryObject();
    
    // Core methods
    bool isEmpty() const;
    cv::Mat getCpuMat() const;
    FrameMemoryObject clone() const;
    MemoryLocation getMemoryLocation() const;
    void moveToMemoryLocation(MemoryLocation targetLocation);
    std::string getInfo() const;
    
    #ifdef CUDA_ENABLED
    cv::cuda::GpuMat getGpuMat() const;
    #endif

    // Utility methods
    int channels() const;
    static bool isCudaAvailable();

    // Metadata methods
    template<typename T>
    void setMetadata(const std::string& key, const T& value);
    
    template<typename T>
    T getMetadata(const std::string& key) const;
    
    bool hasMetadata(const std::string& key) const;

private:
    cv::Mat m_cpuMat;
    #ifdef CUDA_ENABLED
    cv::cuda::GpuMat m_gpuMat;
    #endif
    cv::Mat cpuMat;
    cv::cuda::GpuMat gpuMat;
    std::map<std::string, std::any> m_metadata;  // Store additional data
    
    MemoryLocation m_memoryLocation;
    bool m_hasCpuData;
    bool m_hasGpuData;
    
    void ensureCpuMemory();
    void ensureGpuMemory();
    void release();
    
};

// Template implementations (must be in header for templates)
template<typename T>
void FrameMemoryObject::setMetadata(const std::string& key, const T& value) {
    m_metadata[key] = value;
}

template<typename T>
T FrameMemoryObject::getMetadata(const std::string& key) const {
    auto it = m_metadata.find(key);
    if (it != m_metadata.end()) {
        return std::any_cast<T>(it->second);
    }
    throw std::runtime_error("Metadata key not found: " + key);
}