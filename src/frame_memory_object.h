#pragma once

#include <opencv2/opencv.hpp>
#ifdef CUDA_ENABLED
#include <opencv2/cudaimgproc.hpp>
#include <opencv2/cudawarping.hpp>
#include <opencv2/cudaarithm.hpp>
#include <opencv2/cudafilters.hpp>
#endif
#include <memory>
#include <stdexcept>

/**
 * @brief Memory type enumeration for frame data
 */
enum class MemoryType {
    CPU,    // Data stored in cv::Mat (CPU memory)
    CUDA    // Data stored in cv::cuda::GpuMat (GPU memory)
};

/**
 * @brief Abstraction for frame memory that can hold either CPU or GPU data
 * 
 * This class provides a unified interface for working with image data that can
 * be stored either in CPU memory (cv::Mat) or GPU memory (cv::cuda::GpuMat).
 * It handles automatic conversions between memory types when needed.
 */
class FrameMemoryObject {
public:
    /**
     * @brief Construct from CPU Mat
     */
    explicit FrameMemoryObject(const cv::Mat& cpuMat);
    
#ifdef CUDA_ENABLED
    /**
     * @brief Construct from GPU Mat
     */
    explicit FrameMemoryObject(const cv::cuda::GpuMat& gpuMat);
#endif
    
    /**
     * @brief Default constructor - creates empty object
     */
    FrameMemoryObject();
    
    /**
     * @brief Copy constructor
     */
    FrameMemoryObject(const FrameMemoryObject& other);
    
    /**
     * @brief Assignment operator
     */
    FrameMemoryObject& operator=(const FrameMemoryObject& other);
    
    /**
     * @brief Move constructor
     */
    FrameMemoryObject(FrameMemoryObject&& other) noexcept;
    
    /**
     * @brief Move assignment operator
     */
    FrameMemoryObject& operator=(FrameMemoryObject&& other) noexcept;
    
    /**
     * @brief Get the current memory type
     */
    MemoryType getMemoryType() const { return memoryType_; }
    
    /**
     * @brief Check if the object is empty
     */
    bool empty() const;
    
    /**
     * @brief Get image size
     */
    cv::Size size() const;
    
    /**
     * @brief Get image type (CV_8UC3, etc.)
     */
    int type() const;
    
    /**
     * @brief Get number of channels
     */
    int channels() const;
    
    /**
     * @brief Get CPU Mat (converts from GPU if needed)
     * @param forceConvert If true, always convert. If false, only convert if not already CPU
     */
    cv::Mat getCpuMat(bool forceConvert = false);
    
#ifdef CUDA_ENABLED
    /**
     * @brief Get GPU Mat (converts from CPU if needed)
     * @param forceConvert If true, always convert. If false, only convert if not already GPU
     */
    cv::cuda::GpuMat getGpuMat(bool forceConvert = false);
#endif
    
    /**
     * @brief Convert to CPU memory
     */
    void toCpu();
    
#ifdef CUDA_ENABLED
    /**
     * @brief Convert to GPU memory
     */
    void toGpu();
#endif
    
    /**
     * @brief Clone the object (deep copy)
     */
    FrameMemoryObject clone() const;
    
    /**
     * @brief Create a new object with the same properties but empty data
     */
    FrameMemoryObject createSimilar() const;
    
    /**
     * @brief Ensure the object has the specified memory type
     * @param targetType Target memory type
     * @return true if conversion was needed, false if already correct type
     */
    bool ensureMemoryType(MemoryType targetType);
    
    /**
     * @brief Get a reference to the underlying data without conversion
     * Use with caution - only when you're sure of the memory type
     */
    const cv::Mat& getCpuMatRef() const;
    
#ifdef CUDA_ENABLED
    /**
     * @brief Get a reference to the underlying GPU data without conversion
     * Use with caution - only when you're sure of the memory type
     */
    const cv::cuda::GpuMat& getGpuMatRef() const;
#endif

private:
    MemoryType memoryType_;
    cv::Mat cpuMat_;
#ifdef CUDA_ENABLED
    cv::cuda::GpuMat gpuMat_;
#endif
    
    // Helper methods
    void syncToTarget(MemoryType target);
    void copyFrom(const FrameMemoryObject& other);
    void moveFrom(FrameMemoryObject&& other) noexcept;
};

/**
 * @brief Helper function to create FrameMemoryObject from CPU Mat
 */
inline FrameMemoryObject createCpuFrame(const cv::Mat& mat) {
    return FrameMemoryObject(mat);
}

#ifdef CUDA_ENABLED
/**
 * @brief Helper function to create FrameMemoryObject from GPU Mat
 */
inline FrameMemoryObject createGpuFrame(const cv::cuda::GpuMat& mat) {
    return FrameMemoryObject(mat);
}
#endif
