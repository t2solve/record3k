#include "frame_memory_object.h"
#include <opencv2/opencv.hpp>
#ifdef CUDA_ENABLED
#include <opencv2/cudaimgproc.hpp>
#include <opencv2/cudaarithm.hpp>
#endif

FrameMemoryObject::FrameMemoryObject() : memoryLocation(MemoryLocation::CPU) {}

FrameMemoryObject::FrameMemoryObject(const cv::Mat& mat) 
    : cpuMat(mat), memoryLocation(MemoryLocation::CPU) {}

#ifdef CUDA_ENABLED
FrameMemoryObject::FrameMemoryObject(const cv::cuda::GpuMat& gpuMat) 
    : gpuMat(gpuMat), memoryLocation(MemoryLocation::GPU) {}
#endif

FrameMemoryObject::~FrameMemoryObject() {
    // OpenCV handles memory cleanup automatically
}

FrameMemoryObject::FrameMemoryObject(const FrameMemoryObject& other) {
    copyFrom(other);
}

FrameMemoryObject& FrameMemoryObject::operator=(const FrameMemoryObject& other) {
    if (this != &other) {
        copyFrom(other);
    }
    return *this;
}

void FrameMemoryObject::copyFrom(const FrameMemoryObject& other) {
    memoryLocation = other.memoryLocation;
    
    if (other.memoryLocation == MemoryLocation::CPU) {
        cpuMat = other.cpuMat.clone();
#ifdef CUDA_ENABLED
        gpuMat.release();
#endif
    }
#ifdef CUDA_ENABLED
    else if (other.memoryLocation == MemoryLocation::GPU) {
        other.gpuMat.copyTo(gpuMat);
        cpuMat.release();
    }
#endif
}

bool FrameMemoryObject::isEmpty() const {
    if (memoryLocation == MemoryLocation::CPU) {
        return cpuMat.empty();
    }
#ifdef CUDA_ENABLED
    else if (memoryLocation == MemoryLocation::GPU) {
        return gpuMat.empty();
    }
#endif
    return true;
}

cv::Size FrameMemoryObject::size() const {
    if (memoryLocation == MemoryLocation::CPU) {
        return cpuMat.size();
    }
#ifdef CUDA_ENABLED
    else if (memoryLocation == MemoryLocation::GPU) {
        return gpuMat.size();
    }
#endif
    return cv::Size(0, 0);
}

int FrameMemoryObject::type() const {
    if (memoryLocation == MemoryLocation::CPU) {
        return cpuMat.type();
    }
#ifdef CUDA_ENABLED
    else if (memoryLocation == MemoryLocation::GPU) {
        return gpuMat.type();
    }
#endif
    return -1;
}

int FrameMemoryObject::channels() const {
    if (memoryLocation == MemoryLocation::CPU) {
        return cpuMat.channels();
    }
#ifdef CUDA_ENABLED
    else if (memoryLocation == MemoryLocation::GPU) {
        return gpuMat.channels();
    }
#endif
    return 0;
}

MemoryLocation FrameMemoryObject::getMemoryLocation() const {
    return memoryLocation;
}

cv::Mat& FrameMemoryObject::getCpuMat() {
    ensureCpuMemory();
    return cpuMat;
}

const cv::Mat& FrameMemoryObject::getCpuMat() const {
    const_cast<FrameMemoryObject*>(this)->ensureCpuMemory();
    return cpuMat;
}

#ifdef CUDA_ENABLED
cv::cuda::GpuMat& FrameMemoryObject::getGpuMat() {
    ensureGpuMemory();
    return gpuMat;
}

const cv::cuda::GpuMat& FrameMemoryObject::getGpuMat() const {
    const_cast<FrameMemoryObject*>(this)->ensureGpuMemory();
    return gpuMat;
}
#endif

void FrameMemoryObject::ensureCpuMemory() {
    if (memoryLocation == MemoryLocation::CPU) {
        return; // Already on CPU
    }
    
#ifdef CUDA_ENABLED
    if (memoryLocation == MemoryLocation::GPU) {
        // Download from GPU to CPU
        gpuMat.download(cpuMat);
        memoryLocation = MemoryLocation::CPU;
    }
#endif
}

void FrameMemoryObject::ensureGpuMemory() {
#ifdef CUDA_ENABLED
    if (memoryLocation == MemoryLocation::GPU) {
        return; // Already on GPU
    }
    
    if (memoryLocation == MemoryLocation::CPU) {
        // Upload from CPU to GPU
        gpuMat.upload(cpuMat);
        memoryLocation = MemoryLocation::GPU;
    }
#endif
}

void FrameMemoryObject::moveToMemoryLocation(MemoryLocation targetLocation) {
    if (memoryLocation == targetLocation) {
        return; // Already at target location
    }
    
    if (targetLocation == MemoryLocation::CPU) {
        ensureCpuMemory();
    }
#ifdef CUDA_ENABLED
    else if (targetLocation == MemoryLocation::GPU) {
        ensureGpuMemory();
    }
#endif
}

void FrameMemoryObject::release() {
    cpuMat.release();
#ifdef CUDA_ENABLED
    gpuMat.release();
#endif
    memoryLocation = MemoryLocation::CPU;
}

FrameMemoryObject FrameMemoryObject::clone() const {
    FrameMemoryObject result;
    result.copyFrom(*this);
    return result;
}

void FrameMemoryObject::copyTo(FrameMemoryObject& dest) const {
    dest.copyFrom(*this);
}

bool FrameMemoryObject::isOnCpu() const {
    return memoryLocation == MemoryLocation::CPU;
}

bool FrameMemoryObject::isOnGpu() const {
    return memoryLocation == MemoryLocation::GPU;
}

size_t FrameMemoryObject::getMemoryUsage() const {
    size_t usage = 0;
    
    if (!cpuMat.empty()) {
        usage += cpuMat.total() * cpuMat.elemSize();
    }
    
#ifdef CUDA_ENABLED
    if (!gpuMat.empty()) {
        usage += gpuMat.total() * gpuMat.elemSize();
    }
#endif
    
    return usage;
}

std::string FrameMemoryObject::getInfo() const {
    std::ostringstream oss;
    oss << "FrameMemoryObject: ";
    
    if (isEmpty()) {
        oss << "empty";
    } else {
        cv::Size s = size();
        oss << s.width << "x" << s.height 
            << " type=" << type() 
            << " channels=" << channels()
            << " location=" << (memoryLocation == MemoryLocation::CPU ? "CPU" : "GPU")
            << " memory=" << getMemoryUsage() << " bytes";
    }
    
    return oss.str();
}

void FrameMemoryObject::optimizeMemoryUsage() {
    // Keep only the data in the current memory location
    if (memoryLocation == MemoryLocation::CPU) {
#ifdef CUDA_ENABLED
        gpuMat.release();
#endif
    }
#ifdef CUDA_ENABLED
    else if (memoryLocation == MemoryLocation::GPU) {
        cpuMat.release();
    }
#endif
}

bool FrameMemoryObject::isCudaAvailable() {
#ifdef CUDA_ENABLED
    return cv::cuda::getCudaEnabledDeviceCount() > 0;
#else
    return false;
#endif
}