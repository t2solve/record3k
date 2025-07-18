#pragma once

#include <opencv2/opencv.hpp>
#ifdef CUDA_ENABLED
#include <opencv2/cudaimgproc.hpp>
#include "cuda_utils.h"
#endif
#include <string>

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

private:
    cv::Mat m_cpuMat;
    #ifdef CUDA_ENABLED
    cv::cuda::GpuMat m_gpuMat;
    #endif
    MemoryLocation m_memoryLocation;
    bool m_hasCpuData;
    bool m_hasGpuData;
    
    void ensureCpuMemory();
    void ensureGpuMemory();
    void release();
    
};