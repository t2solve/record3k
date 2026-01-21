#include "vimbax_frame_source.h"
#ifdef VIMBAX_ENABLED

#include "frame_converter.h"
#include <QDebug>
#include <opencv2/opencv.hpp>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <thread>

using namespace VmbCPP;

// Pull-based single-shot acquisition; no observer used

VimbaXFrameSource::VimbaXFrameSource(const std::string& cameraSerial,
                                     const std::string& interfaceIdOrMac,
                                     bool debugLog)
    : system_(VmbSystem::GetInstance()), debug_(debugLog) {
    std::cout << "[VimbaXFrameSource] Constructor called (serial='" << cameraSerial 
              << "', interface='" << interfaceIdOrMac << "')" << std::endl;
    ready_ = initialize(cameraSerial, interfaceIdOrMac);
    std::cout << "[VimbaXFrameSource] Constructor complete, ready=" << ready_ << std::endl;
}

VimbaXFrameSource::~VimbaXFrameSource() {
    shutdown();
}

bool VimbaXFrameSource::initialize(const std::string& cameraSerial, const std::string& interfaceIdOrMac) {
    VmbErrorType err = VmbErrorSuccess;
    
    std::cout << "[VimbaXFrameSource] initialize() started" << std::endl;
    std::cout << "[VimbaXFrameSource] Requested: serial='" << cameraSerial << "', interface/MAC='" << interfaceIdOrMac << "'" << std::endl;
    
    // Note: We don't call Startup() here anymore since CameraManagerVimbax singleton manages system lifecycle
    // The system should already be initialized by CameraManagerVimbax::startup()
    if (debug_) qDebug() << "[VimbaXFrameSource] Using already initialized VmbSystem";

    // Wait a moment for camera discovery to complete (GigE cameras may take time)
    std::cout << "[VimbaXFrameSource] Waiting 200ms for camera discovery..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Pick camera by serial and/or interface id (MAC)
    std::cout << "[VimbaXFrameSource] Calling GetCameras()..." << std::endl;
    CameraPtrVector cameras;
    err = system_.GetCameras(cameras);
    std::cout << "[VimbaXFrameSource] GetCameras() returned, err=" << err << ", count=" << cameras.size() << std::endl;
    if (err != VmbErrorSuccess) {
        std::cerr << "[VimbaXFrameSource] GetCameras failed with error: " << err << std::endl;
        if (debug_) qWarning() << "[VimbaXFrameSource] GetCameras failed:" << err;
        return false;
    }
    
    if (cameras.empty()) {
        std::cerr << "[VimbaXFrameSource] No cameras found by GetCameras()" << std::endl;
        if (debug_) qWarning() << "[VimbaXFrameSource] No cameras found";
        return false;
    }

    std::cout << "[VimbaXFrameSource] Searching for matching camera..." << std::endl;
    std::cout << "[VimbaXFrameSource] Search criteria: serial='" << cameraSerial << "', interface/MAC='" << interfaceIdOrMac << "'" << std::endl;
    
    // First, list all detected cameras for debugging
    std::cout << "[VimbaXFrameSource] Available cameras:" << std::endl;
    for (size_t i = 0; i < cameras.size(); i++) {
        std::string camId, camSerial, camModel, camInterface, camName;
        cameras[i]->GetID(camId);
        cameras[i]->GetSerialNumber(camSerial);
        cameras[i]->GetModel(camModel);
        cameras[i]->GetInterfaceID(camInterface);
        cameras[i]->GetName(camName);
        
        // Try to get MAC address
        std::string macAddr = "N/A";
        FeaturePtr macFeature;
        if (cameras[i]->GetFeatureByName("GevDeviceMACAddress", macFeature) == VmbErrorSuccess) {
            VmbInt64_t macValue;
            if (macFeature->GetValue(macValue) == VmbErrorSuccess) {
                std::ostringstream oss;
                oss << std::hex << std::setfill('0') << std::uppercase;
                oss << std::setw(2) << ((macValue >> 40) & 0xFF) << ":"
                    << std::setw(2) << ((macValue >> 32) & 0xFF) << ":"
                    << std::setw(2) << ((macValue >> 24) & 0xFF) << ":"
                    << std::setw(2) << ((macValue >> 16) & 0xFF) << ":"
                    << std::setw(2) << ((macValue >> 8) & 0xFF) << ":"
                    << std::setw(2) << (macValue & 0xFF);
                macAddr = oss.str();
            }
        }
        
        std::cout << "[VimbaXFrameSource]   Camera[" << i << "]:" << std::endl;
        std::cout << "[VimbaXFrameSource]     Name: " << camName << std::endl;
        std::cout << "[VimbaXFrameSource]     ID: " << camId << std::endl;
        std::cout << "[VimbaXFrameSource]     Serial: " << camSerial << std::endl;
        std::cout << "[VimbaXFrameSource]     Model: " << camModel << std::endl;
        std::cout << "[VimbaXFrameSource]     Interface: " << camInterface << std::endl;
        std::cout << "[VimbaXFrameSource]     MAC: " << macAddr << std::endl;
    }
    
    // Strategy 1: Try exact match on both serial and interface
    std::cout << "[VimbaXFrameSource] Strategy 1: Trying exact match on serial AND interface..." << std::endl;
    if (!cameraSerial.empty() && !interfaceIdOrMac.empty()) {
        for (auto& cam : cameras) {
            std::string ser, iid;
            cam->GetSerialNumber(ser);
            cam->GetInterfaceID(iid);
            if (ser == cameraSerial && iid.find(interfaceIdOrMac) != std::string::npos) {
                camera_ = cam;
                std::cout << "[VimbaXFrameSource] ✓ Found exact match: " << ser << " on " << iid << std::endl;
                break;
            }
        }
    }
    
    // Strategy 2: Try match by serial only
    if (!camera_ && !cameraSerial.empty()) {
        std::cout << "[VimbaXFrameSource] Strategy 2: Trying match by serial only..." << std::endl;
        for (auto& cam : cameras) {
            std::string ser;
            cam->GetSerialNumber(ser);
            if (ser == cameraSerial) {
                camera_ = cam;
                std::cout << "[VimbaXFrameSource] ✓ Found match by serial: " << ser << std::endl;
                break;
            }
        }
    }
    
    // Strategy 3: Try match by interface/MAC only
    if (!camera_ && !interfaceIdOrMac.empty()) {
        std::cout << "[VimbaXFrameSource] Strategy 3: Trying match by interface/MAC only..." << std::endl;
        for (auto& cam : cameras) {
            std::string iid;
            cam->GetInterfaceID(iid);
            
            // Also check MAC address
            FeaturePtr macFeature;
            std::string macAddr;
            if (cam->GetFeatureByName("GevDeviceMACAddress", macFeature) == VmbErrorSuccess) {
                VmbInt64_t macValue;
                if (macFeature->GetValue(macValue) == VmbErrorSuccess) {
                    std::ostringstream oss;
                    oss << std::hex << std::setfill('0') << std::uppercase;
                    oss << std::setw(2) << ((macValue >> 40) & 0xFF) << ":"
                        << std::setw(2) << ((macValue >> 32) & 0xFF) << ":"
                        << std::setw(2) << ((macValue >> 24) & 0xFF) << ":"
                        << std::setw(2) << ((macValue >> 16) & 0xFF) << ":"
                        << std::setw(2) << ((macValue >> 8) & 0xFF) << ":"
                        << std::setw(2) << (macValue & 0xFF);
                    macAddr = oss.str();
                }
            }
            
            bool matchInterface = iid.find(interfaceIdOrMac) != std::string::npos;
            bool matchMAC = !macAddr.empty() && macAddr == interfaceIdOrMac;
            
            if (matchInterface || matchMAC) {
                camera_ = cam;
                if (matchInterface) {
                    std::cout << "[VimbaXFrameSource] ✓ Found match by interface: " << iid << std::endl;
                } else {
                    std::cout << "[VimbaXFrameSource] ✓ Found match by MAC: " << macAddr << std::endl;
                }
                break;
            }
        }
    }
    
    // Strategy 4: Use first available camera
    if (!camera_) {
        std::cout << "[VimbaXFrameSource] Strategy 4: Using first available camera..." << std::endl;
        camera_ = cameras.front();
        std::string camId, camSerial;
        camera_->GetID(camId);
        camera_->GetSerialNumber(camSerial);
        std::cout << "[VimbaXFrameSource] ✓ Using first camera: " << camId << " (S/N: " << camSerial << ")" << std::endl;
        if (debug_) qWarning() << "[VimbaXFrameSource] No match found, using first available camera";
    }

    // Open
    std::cout << "[VimbaXFrameSource] Opening camera..." << std::endl;
    err = camera_->Open(VmbAccessModeFull);
    std::cout << "[VimbaXFrameSource] Camera open returned, err=" << err << std::endl;
    if (err != VmbErrorSuccess) {
        if (debug_) qWarning() << "[VimbaXFrameSource] Camera open failed:" << err;
        return false;
    }

    std::cout << "[VimbaXFrameSource] Ensuring preallocated frame..." << std::endl;
    // Pre-allocate a single frame buffer for AcquireSingleImage
    bool result = ensurePreallocFrame();
    std::cout << "[VimbaXFrameSource] initialize() complete, result=" << result << std::endl;
    return result;
}

void VimbaXFrameSource::shutdown() {
    try {
        if (camera_) {
            (void)camera_->Close();
        }
    } catch (const std::exception& e) {
        qWarning() << "[VimbaXFrameSource] Exception while closing camera:" << e.what();
    } catch (...) {
        qWarning() << "[VimbaXFrameSource] Unknown exception while closing camera";
    }
    // Note: We don't call system_.Shutdown() here anymore since CameraManagerVimbax singleton manages system lifecycle
    preallocFrame_.reset();
    camera_.reset();
    ready_ = false;
}

std::unique_ptr<FrameMemoryObject> VimbaXFrameSource::nextFrame() {
    int frameTimeoutMs = 1000;
    if (!ready_) return nullptr;
    if (!ensurePreallocFrame()) return nullptr;

    // Blocking grab with timeout
    VmbErrorType err = camera_->AcquireSingleImage(preallocFrame_, frameTimeoutMs, FrameAllocationMode::FrameAllocation_AllocAndAnnounceFrame);
    if (err != VmbErrorSuccess) {
        if (err == VmbErrorTimeout) {
            if (debug_) qDebug() << "[VimbaXFrameSource] AcquireSingleImage timeout";
        } else {
            qWarning() << "[VimbaXFrameSource] AcquireSingleImage failed, err=" << err;
        }
        return nullptr;
    }

    // Convert to FrameMemoryObject and attach timestamp
    FrameMemoryObject out = FrameConverter::convertVmbFrameToMemoryObject(preallocFrame_, false);
    VmbUint64_t tsNs = 0;
    if (preallocFrame_->GetTimestamp(tsNs) == VmbErrorSuccess && tsNs != 0) {
        out.setTimestampNs(static_cast<uint64_t>(tsNs));
    } else {
        qWarning() << "[VimbaXFrameSource] Frame timestamp is invalid; using current time.";
        //out.touchTimestampNow();
    }

    return std::make_unique<FrameMemoryObject>(std::move(out));
}

bool VimbaXFrameSource::isReady() const { return ready_; }

bool VimbaXFrameSource::ensurePreallocFrame() {
    if (preallocFrame_) return true;
    if (!camera_) return false;
    VmbUint32_t payloadSize = 0;
    VmbErrorType err = camera_->GetPayloadSize(payloadSize);
    if (err != VmbErrorSuccess || payloadSize == 0) {
        if (debug_) qWarning() << "[VimbaXFrameSource] GetPayloadSize failed:" << err;
        return false;
    }
    preallocFrame_.reset(new Frame(payloadSize));
    return true;
}

#endif // VIMBAX_ENABLED
