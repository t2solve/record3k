#include "frame_memory_object.h"
#include <stdexcept>
#include <chrono>


// Timestamp accessors
void FrameMemoryObject::setTimestampNs(uint64_t tsNs) noexcept { m_timestampNs = tsNs; }
uint64_t FrameMemoryObject::timestampNs() const noexcept { return m_timestampNs; }
bool FrameMemoryObject::hasTimestamp() const noexcept { return m_timestampNs != 0ULL; }
#ifdef VIMBAX_ENABLED
void FrameMemoryObject::setTimestampFromVimba(uint64_t tsNs) noexcept { m_timestampNs = tsNs; }
#endif

void FrameMemoryObject::touchTimestampNow() noexcept {
    using namespace std::chrono;
    const auto now = steady_clock::now().time_since_epoch();
    m_timestampNs = static_cast<uint64_t>(duration_cast<nanoseconds>(now).count());
}

FrameMemoryObject::FrameMemoryObject() 
    : m_memoryLocation(MemoryLocation::CPU), m_hasCpuData(false), m_hasGpuData(false), m_timestampNs(0) {
}

FrameMemoryObject::FrameMemoryObject(const cv::Mat& mat) 
    : m_cpuMat(mat), m_memoryLocation(MemoryLocation::CPU), m_hasCpuData(true), m_hasGpuData(false), m_timestampNs(0) {
}

#ifdef CUDA_ENABLED
FrameMemoryObject::FrameMemoryObject(const cv::cuda::GpuMat& gpuMat) 
    : m_gpuMat(gpuMat), m_memoryLocation(MemoryLocation::GPU), m_hasCpuData(false), m_hasGpuData(true), m_timestampNs(0) {
}
#endif

FrameMemoryObject::~FrameMemoryObject() {
    release();
}

bool FrameMemoryObject::isEmpty() const {
    if (m_memoryLocation == MemoryLocation::CPU) {
        return m_cpuMat.empty();
    }
#ifdef CUDA_ENABLED
    else {
        return m_gpuMat.empty();
    }
#endif
    return true;
}

cv::Mat FrameMemoryObject::getCpuMat() const {  // Fix: make const, remove parameter
    if (!m_hasCpuData && m_hasGpuData) {
#ifdef CUDA_ENABLED
        m_gpuMat.download(const_cast<cv::Mat&>(m_cpuMat));
        const_cast<FrameMemoryObject*>(this)->m_hasCpuData = true;
#endif
    }
    return m_cpuMat;
}

#ifdef CUDA_ENABLED
cv::cuda::GpuMat FrameMemoryObject::getGpuMat() const {
    if (!m_hasGpuData && m_hasCpuData) {
        const_cast<cv::cuda::GpuMat&>(m_gpuMat).upload(m_cpuMat);
        const_cast<FrameMemoryObject*>(this)->m_hasGpuData = true;
    }
    // Return compatible version without modifying original
    return ensureCudaFilterCompatible(m_gpuMat);
}
#endif

FrameMemoryObject FrameMemoryObject::clone() const {
    FrameMemoryObject result;
    
    // Clone the image data
    if (m_hasCpuData) {
        result.m_cpuMat = m_cpuMat.clone();
        result.m_hasCpuData = true;
    }
    
    #ifdef CUDA_ENABLED
    if (m_hasGpuData) {
        m_gpuMat.copyTo(result.m_gpuMat);
        result.m_hasGpuData = true;
    }
    #endif
    
    result.m_memoryLocation = m_memoryLocation;
    
    // Clone metadata
    result.m_metadata = m_metadata;
    
    // Clone timestamp
    result.m_timestampNs = m_timestampNs;
    
    return result;
}

MemoryLocation FrameMemoryObject::getMemoryLocation() const {
    return m_memoryLocation;
}

void FrameMemoryObject::moveToMemoryLocation(MemoryLocation targetLocation) {
    if (m_memoryLocation == targetLocation) {
        return;
    }
    
    if (targetLocation == MemoryLocation::CPU) {
        ensureCpuMemory();
    } else {
        ensureGpuMemory();
    }
    
    m_memoryLocation = targetLocation;
}

std::string FrameMemoryObject::getInfo() const {
    std::string info = "FrameMemoryObject: ";
    info += (m_memoryLocation == MemoryLocation::CPU ? "CPU" : "GPU");
    if (!isEmpty()) {
        cv::Size size = m_cpuMat.size();
        info += " " + std::to_string(size.width) + "x" + std::to_string(size.height);
    }
    if (m_timestampNs != 0ULL) {
        info += " ts=" + std::to_string(m_timestampNs) + "ns";
    }
    return info;
}

int FrameMemoryObject::channels() const {
    if (m_memoryLocation == MemoryLocation::CPU && !m_cpuMat.empty()) {
        return m_cpuMat.channels();
    }
#ifdef CUDA_ENABLED
    else if (m_memoryLocation == MemoryLocation::GPU && !m_gpuMat.empty()) {
        return m_gpuMat.channels();
    }
#endif
    return 0;
}

bool FrameMemoryObject::isCudaAvailable() {
#ifdef CUDA_ENABLED
    return cv::cuda::getCudaEnabledDeviceCount() > 0;
#else
    return false;
#endif
}

void FrameMemoryObject::ensureCpuMemory() {
    if (!m_hasCpuData && m_hasGpuData) {
#ifdef CUDA_ENABLED
        m_gpuMat.download(m_cpuMat);
        m_hasCpuData = true;
#endif
    }
}

void FrameMemoryObject::ensureGpuMemory() {
#ifdef CUDA_ENABLED
    if (!m_hasGpuData && m_hasCpuData) {
        m_gpuMat.upload(m_cpuMat);
        m_hasGpuData = true;
    }
#endif
}

void FrameMemoryObject::release() {
    m_cpuMat.release();
#ifdef CUDA_ENABLED
    m_gpuMat.release();
#endif
    m_hasCpuData = false;
    m_hasGpuData = false;
}

bool FrameMemoryObject::hasMetadata(const std::string& key) const {
    return m_metadata.find(key) != m_metadata.end();
}
