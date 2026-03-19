#include <liblbt/camera_manager_vimbax.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <memory>
#include <thread>
#include <QDebug>

// Logging helpers — use Qt's log infrastructure so verbosity can be controlled
// at runtime via QT_LOGGING_RULES="*.debug=false" without a rebuild.
// CMV_DEBUG : low-level operational detail (per-field, per-loop, mutex state)
// CMV_INFO  : key milestones visible at INFO level (startup, camera count)
// CMV_WARN  : recoverable problems
// CMV_ERROR : hard failures
#define CMV_DEBUG(msg) do { std::ostringstream _oss; _oss << msg; qDebug()    << _oss.str().c_str(); } while(0)
#define CMV_INFO(msg)  do { std::ostringstream _oss; _oss << msg; qInfo()     << _oss.str().c_str(); } while(0)
#define CMV_WARN(msg)  do { std::ostringstream _oss; _oss << msg; qWarning()  << _oss.str().c_str(); } while(0)
#define CMV_ERROR(msg) do { std::ostringstream _oss; _oss << msg; qCritical() << _oss.str().c_str(); } while(0)

#ifdef VIMBAX_ENABLED
#include <VmbCPP/VmbCPP.h>
#include <VmbCPP/IFeatureObserver.h>
#endif

namespace lbt {

#ifdef VIMBAX_ENABLED

// Event observer for VimbaX system and camera events
class VimbaxEventObserver : public VmbCPP::IFeatureObserver {
public:
    void FeatureChanged(const VmbCPP::FeaturePtr& feature) override {
        if (feature != nullptr) {
            std::string featureName;
            VmbInt64_t featureValue = 0;
            if (feature->GetName(featureName) == VmbErrorSuccess) {
                feature->GetValue(featureValue);
                CMV_DEBUG("[VimbaxEventObserver] Event: " << featureName << " = " << featureValue);
            }
        }
    }
};

// Implementation details hidden with pImpl pattern
class CameraManagerVimbax::Impl {
public:
    Impl() : system_(VmbCPP::VmbSystem::GetInstance()) {}
    
    bool startup(const std::filesystem::path& ctiFilePath) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            if (isStarted_) {
                CMV_DEBUG("[CameraManagerVimbax] System already started");
                return true;
            }
            
            status_.isBuilt = true;
            CMV_INFO("[CameraManagerVimbax] VimbaX is built into this binary");
        } // Release mutex before VmbSystem::Startup() which may take time
        
        try {
            // Prepare transport layer file path
            std::string pathConfig;
            if (!ctiFilePath.empty()) {
                CMV_DEBUG("[CameraManagerVimbax] Received .cti file path: " << ctiFilePath.string());
                std::filesystem::path absPath = std::filesystem::absolute(ctiFilePath);
                CMV_DEBUG("[CameraManagerVimbax] Absolute path: " << absPath.string());
                if (std::filesystem::exists(absPath)) {
                    pathConfig = absPath.string();
                    CMV_DEBUG("[CameraManagerVimbax] Using .cti file: " << pathConfig);
                } else {
                    CMV_ERROR("[CameraManagerVimbax] Specified .cti file does not exist: " << absPath.string());
                    CMV_ERROR("[CameraManagerVimbax] CWD: " << std::filesystem::current_path().string() << " — falling back to GENICAM_GENTL64_PATH");
                }
            } else {
                CMV_DEBUG("[CameraManagerVimbax] No .cti file specified, using GENICAM_GENTL64_PATH");
            }
            
            // Query version before startup
            VmbVersionInfo_t versionInfo;
            VmbErrorType err = system_.QueryVersion(versionInfo);
            if (err == VmbErrorSuccess) {
                CMV_INFO("[CameraManagerVimbax] VimbaX version " << versionInfo.major
                         << "." << versionInfo.minor << "." << versionInfo.patch);
            } else {
                CMV_WARN("[CameraManagerVimbax] Could not query VimbaX version, error: " << err);
            }
            
            // Initialize VmbSystem
            CMV_DEBUG("[CameraManagerVimbax] Calling VmbSystem::Startup()...");
            if (pathConfig.empty()) {
                err = system_.Startup();
            } else {
                err = system_.Startup(pathConfig.c_str());
            }
            
            if (err != VmbErrorSuccess) {
                std::lock_guard<std::mutex> lock(mutex_);
                status_.errorMessage = "VmbSystem::Startup() failed with error code: " + std::to_string(err);
                CMV_ERROR("[CameraManagerVimbax] VmbSystem::Startup() failed, err=" << err);
                return false;
            }
            
            CMV_INFO("[CameraManagerVimbax] VmbSystem::Startup() succeeded");
            
            // Update status with mutex
            {
                std::lock_guard<std::mutex> lock(mutex_);
                isStarted_ = true;
                status_.startupSuccess = true;
                status_.transportLayerPath = pathConfig;
                status_.versionMajor = versionInfo.major;
                status_.versionMinor = versionInfo.minor;
                status_.versionPatch = versionInfo.patch;
                
                // Create observer instance (keep it alive as a member)
                eventObserver_ = VmbCPP::IFeatureObserverPtr(new VimbaxEventObserver());
                CMV_DEBUG("[CameraManagerVimbax] Event observer created");
            }
            
            // Enumerate system components (without holding mutex)
            enumerateTransportLayers();
            enumerateInterfaces();
            
            // Update cameras (this will acquire/release mutex as needed)
            CMV_DEBUG("[CameraManagerVimbax] Calling updateCameras()...");
            updateCameras();
            
            CMV_INFO("[CameraManagerVimbax] Initialization complete. System ready.");
            return true;
            
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.errorMessage = std::string("Exception during VimbaX startup: ") + e.what();
            CMV_ERROR("[CameraManagerVimbax] Exception during startup: " << e.what());
            status_.startupSuccess = false;
            isStarted_ = false;
            return false;
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.errorMessage = "Unknown exception during VimbaX startup";
            CMV_ERROR("[CameraManagerVimbax] Unknown exception during startup");
            status_.startupSuccess = false;
            isStarted_ = false;
            return false;
        }
    }
    
    void shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!isStarted_) {
            CMV_DEBUG("[CameraManagerVimbax] shutdown() called but system not started");
            return;
        }
        
        CMV_DEBUG("[CameraManagerVimbax] Calling VmbSystem::Shutdown()...");
        try {
            system_.Shutdown();
            isStarted_ = false;
            status_.startupSuccess = false;
            CMV_INFO("[CameraManagerVimbax] VimbaX system shutdown complete");
        } catch (const std::exception& e) {
            CMV_WARN("[CameraManagerVimbax] Exception during shutdown: " << e.what());
        } catch (...) {
            CMV_WARN("[CameraManagerVimbax] Unknown exception during shutdown");
        }
    }
    
    bool isReady() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return isStarted_ && status_.startupSuccess;
    }
    
    VimbaXStatus getStatus() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return status_;
    }
    
    std::vector<VimbaXCameraInfo> getCameras() {
        std::lock_guard<std::mutex> lock(mutex_);
        return cameras_;
    }
    
    void updateCameras() {
        // Check if system is started (with mutex)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!isStarted_) {
                CMV_DEBUG("[CameraManagerVimbax::updateCameras] System not started, skipping");
                return;
            }
        }
        
        // Give GigE cameras extra time for broadcast/multicast network discovery
        CMV_INFO("[CameraManagerVimbax] Starting GigE camera discovery via broadcast/multicast...");
        CMV_DEBUG("[CameraManagerVimbax::updateCameras] Waiting 1000ms for network discovery");
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        // GetCameras() is the broadcast-based discovery call — may block several seconds
        CMV_INFO("[CameraManagerVimbax] Calling GetCameras() — broadcast discovery in progress (may take a few seconds)");
        auto startTime = std::chrono::steady_clock::now();
        
        VmbCPP::CameraPtrVector vmbCameras;
        VmbErrorType err = system_.GetCameras(vmbCameras);
        
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        CMV_DEBUG("[CameraManagerVimbax::updateCameras] GetCameras() returned after " << elapsed << "ms, err=" << err);
        
        std::lock_guard<std::mutex> lock(mutex_);
        cameras_.clear();
        discoveryComplete_ = true;
        
        if (err != VmbErrorSuccess) {
            CMV_WARN("[CameraManagerVimbax] GetCameras() failed, err=" << err);
            status_.cameraCount = 0;
            return;
        }
        
        status_.cameraCount = vmbCameras.size();
        CMV_INFO("[CameraManagerVimbax] Broadcast discovery complete — found " << status_.cameraCount << " camera(s)");
        
        if (vmbCameras.empty()) {
            CMV_WARN("[CameraManagerVimbax] No cameras found via broadcast. Possible reasons:"
                     " (1) no cameras connected, (2) different subnet, (3) wrong .cti file,"
                     " (4) broadcast blocked by firewall/switch — consider enabling DETECT_UNI_CAST_ONLY");
            return;
        }
        
        for (size_t i = 0; i < vmbCameras.size(); ++i) {
            const auto& cam = vmbCameras[i];
            VimbaXCameraInfo info;
            VmbErrorType err;
            
            if (cam->GetName(info.name)         != VmbErrorSuccess) info.name     = "[Unknown]";
            if (cam->GetModel(info.model)        != VmbErrorSuccess) info.model    = "[Unknown]";
            if (cam->GetID(info.id)              != VmbErrorSuccess) info.id       = "[Unknown]";
            if (cam->GetSerialNumber(info.serial)!= VmbErrorSuccess) info.serial   = "[Unknown]";
            if (cam->GetInterfaceID(info.interfaceId) != VmbErrorSuccess) info.interfaceId = "[Unknown]";
            
            // Log the key identity fields at INFO so they always appear in the log
            CMV_INFO("[CameraManagerVimbax] Camera[" << (i+1) << "] "
                     << info.name << "  id=" << info.id
                     << "  serial=" << info.serial
                     << "  iface=" << info.interfaceId);
            
            // Transport layer — debug only
            VmbCPP::TransportLayerPtr pTransportLayer;
            err = cam->GetTransportLayer(pTransportLayer);
            if (err == VmbErrorSuccess && pTransportLayer) {
                std::string tlID, tlPath;
                pTransportLayer->GetID(tlID);
                pTransportLayer->GetPath(tlPath);
                CMV_DEBUG("[CameraManagerVimbax]   TL id=" << tlID << "  path=" << tlPath);
            } else {
                CMV_DEBUG("[CameraManagerVimbax]   TL: unavailable (err=" << err << ")");
            }
            
            // MAC address — debug only
            info.macAddress = "N/A";
            VmbCPP::FeaturePtr macFeature;
            if (cam->GetFeatureByName("GevDeviceMACAddress", macFeature) == VmbErrorSuccess) {
                VmbInt64_t macValue;
                if (macFeature->GetValue(macValue) == VmbErrorSuccess) {
                    std::ostringstream oss;
                    oss << std::hex << std::setfill('0') << std::uppercase;
                    oss << std::setw(2) << ((macValue >> 40) & 0xFF) << ":"
                        << std::setw(2) << ((macValue >> 32) & 0xFF) << ":"
                        << std::setw(2) << ((macValue >> 24) & 0xFF) << ":"
                        << std::setw(2) << ((macValue >> 16) & 0xFF) << ":"
                        << std::setw(2) << ((macValue >> 8)  & 0xFF) << ":"
                        << std::setw(2) << (macValue & 0xFF);
                    info.macAddress = oss.str();
                    CMV_DEBUG("[CameraManagerVimbax]   MAC=" << info.macAddress);
                }
            }
            
            cameras_.push_back(info);
        }
        
        // Release mutex before registering event observers (they may take time)
        lock.~lock_guard();
        
        for (size_t i = 0; i < vmbCameras.size(); ++i) {
            registerCameraEventObserver(vmbCameras[i], cameras_[i].id);
        }
        CMV_DEBUG("[CameraManagerVimbax::updateCameras] Camera discovery and event registration complete");
    }
    
    bool isDiscoveryComplete() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return discoveryComplete_;
    }

private:
    void enumerateTransportLayers() {
        VmbCPP::TransportLayerPtrVector transportLayers;
        VmbErrorType err = system_.GetTransportLayers(transportLayers);
        
        if (err != VmbErrorSuccess) {
            CMV_ERROR("[CameraManagerVimbax] GetTransportLayers() failed, err=" << err);
            status_.transportLayerCount = 0;
            return;
        }
        
        status_.transportLayerCount = transportLayers.size();
        CMV_INFO("[CameraManagerVimbax] Found " << status_.transportLayerCount << " transport layer(s)");
        
        if (transportLayers.empty()) {
            CMV_WARN("[CameraManagerVimbax] No transport layers found — check CTI files");
            return;
        }
        
        for (size_t i = 0; i < transportLayers.size(); ++i) {
            const auto& tl = transportLayers[i];
            std::string tlID, tlPath, tlType, tlModel, tlVendor, tlVersion;
            
            tl->GetID(tlID);
            tl->GetPath(tlPath);
            
            VmbCPP::FeaturePtr feature;
            if (tl->GetFeatureByName("TLType", feature) == VmbErrorSuccess) feature->GetValue(tlType);
            if (tl->GetFeatureByName("TLVersion", feature) == VmbErrorSuccess) feature->GetValue(tlVersion);
            CMV_DEBUG("[CameraManagerVimbax] TL[" << (i+1) << "] id=" << tlID
                      << "  type=" << tlType << "  version=" << tlVersion
                      << "  path=" << tlPath);
        }
    }
    
    void enumerateInterfaces() {
        VmbCPP::InterfacePtrVector interfaces;
        VmbErrorType err = system_.GetInterfaces(interfaces);
        
        if (err != VmbErrorSuccess) {
            CMV_ERROR("[CameraManagerVimbax] GetInterfaces() failed, err=" << err);
            status_.interfaceCount = 0;
            return;
        }
        
        status_.interfaceCount = interfaces.size();
        CMV_INFO("[CameraManagerVimbax] Found " << status_.interfaceCount << " network interface(s)");
        
        if (interfaces.empty()) {
            CMV_WARN("[CameraManagerVimbax] No interfaces found");
            return;
        }
        
        for (size_t i = 0; i < interfaces.size(); ++i) {
            const auto& iface = interfaces[i];
            std::string ifaceID, ifaceType, ifaceName;
            iface->GetID(ifaceID);
            iface->GetName(ifaceName);
            VmbCPP::FeaturePtr feature;
            if (iface->GetFeatureByName("InterfaceType", feature) == VmbErrorSuccess)
                feature->GetValue(ifaceType);
            
            // For GigE interfaces also log IP/subnet — relevant for broadcast discovery
            if (ifaceType == "GEV" || ifaceID.find("GigE") != std::string::npos || ifaceID.find("eno") != std::string::npos) {
                VmbInt64_t ipAddr = 0, subnetMask = 0, macAddr = 0;
                if (iface->GetFeatureByName("GevInterfaceIPAddress",  feature) == VmbErrorSuccess) feature->GetValue(ipAddr);
                if (iface->GetFeatureByName("GevInterfaceSubnetMask", feature) == VmbErrorSuccess) feature->GetValue(subnetMask);
                if (iface->GetFeatureByName("GevInterfaceMACAddress", feature) == VmbErrorSuccess) feature->GetValue(macAddr);
                std::ostringstream macOss;
                macOss << std::hex << std::setfill('0') << std::uppercase
                       << std::setw(2) << ((macAddr >> 40) & 0xFF) << ":"
                       << std::setw(2) << ((macAddr >> 32) & 0xFF) << ":"
                       << std::setw(2) << ((macAddr >> 24) & 0xFF) << ":"
                       << std::setw(2) << ((macAddr >> 16) & 0xFF) << ":"
                       << std::setw(2) << ((macAddr >> 8)  & 0xFF) << ":"
                       << std::setw(2) << (macAddr & 0xFF);
                CMV_DEBUG("[CameraManagerVimbax] GEV iface[" << (i+1) << "] id=" << ifaceID
                          << "  ip=" << ((ipAddr>>24)&0xFF) << "." << ((ipAddr>>16)&0xFF)
                          << "." << ((ipAddr>>8)&0xFF) << "." << (ipAddr&0xFF)
                          << "  mask=" << ((subnetMask>>24)&0xFF) << "." << ((subnetMask>>16)&0xFF)
                          << "." << ((subnetMask>>8)&0xFF) << "." << (subnetMask&0xFF)
                          << "  mac=" << macOss.str());
            } else {
                CMV_DEBUG("[CameraManagerVimbax] Iface[" << (i+1) << "] id=" << ifaceID
                          << "  type=" << ifaceType);
            }
        }
    }
    
    void registerCameraEventObserver(const VmbCPP::CameraPtr& camera, const std::string& camId) {
        if (!eventObserver_) {
            CMV_DEBUG("[CameraManagerVimbax] No event observer, skipping registration for: " << camId);
            return;
        }
        
        VmbErrorType err = camera->Open(VmbAccessModeRead);
        if (err != VmbErrorSuccess) {
            CMV_DEBUG("[CameraManagerVimbax] Cannot open camera for event registration (err=" << err << "): " << camId);
            return;
        }
        
        const std::vector<std::string> cameraEvents = {
            "AcquisitionStart", "AcquisitionEnd", "FrameTrigger", "ExposureEnd", "Error"
        };
        
        VmbCPP::FeaturePtr selectorFeature, notificationFeature;
        err = camera->GetFeatureByName("EventSelector", selectorFeature);
        if (err == VmbErrorSuccess) {
            for (const auto& eventType : cameraEvents) {
                if (selectorFeature->SetValue(eventType.c_str()) != VmbErrorSuccess) continue;
                if (camera->GetFeatureByName("EventNotification", notificationFeature) != VmbErrorSuccess) continue;
                if (notificationFeature->SetValue("On") != VmbErrorSuccess) continue;
                std::string evFeatName = "Event" + eventType;
                VmbCPP::FeaturePtr evFeat;
                if (camera->GetFeatureByName(evFeatName.c_str(), evFeat) == VmbErrorSuccess)
                    CMV_DEBUG("[CameraManagerVimbax] Event observer registered: " << eventType
                              << " for " << camId
                              << " (" << (evFeat->RegisterObserver(eventObserver_) == VmbErrorSuccess ? "ok" : "failed") << ")");
            }
        } else {
            CMV_DEBUG("[CameraManagerVimbax] EventSelector unavailable for " << camId << " (err=" << err << ")");
        }
        
        camera->Close();
    }
    
    VmbCPP::VmbSystem& system_;
    bool isStarted_ = false;
    bool discoveryComplete_ = false;
    VimbaXStatus status_;
    std::vector<VimbaXCameraInfo> cameras_;
    mutable std::mutex mutex_;
    VmbCPP::IFeatureObserverPtr eventObserver_;  // Keep observer alive
};

#else

// Stub implementation when VimbaX is not enabled
class CameraManagerVimbax::Impl {
public:
    bool startup(const std::filesystem::path&) {
        std::cerr << "[CameraManagerVimbax] VimbaX not enabled in build" << std::endl;
        return false;
    }
    void shutdown() {}
    bool isReady() const { return false; }
    VimbaXStatus getStatus() const {
        VimbaXStatus status;
        status.isBuilt = false;
        status.errorMessage = "VimbaX not enabled in build (VIMBAX_ENABLED not defined)";
        return status;
    }
    std::vector<VimbaXCameraInfo> getCameras() { return {}; }
    void updateCameras() {}
    bool isDiscoveryComplete() const { return false; }
};

#endif // VIMBAX_ENABLED

// Singleton implementation
CameraManagerVimbax& CameraManagerVimbax::getInstance() {
    static CameraManagerVimbax instance;
    return instance;
}

CameraManagerVimbax::~CameraManagerVimbax() {
    if (pImpl_) {
        pImpl_->shutdown();
    }
}

bool CameraManagerVimbax::startup(const std::filesystem::path& ctiFilePath) {
    if (!pImpl_) {
        pImpl_ = std::make_unique<Impl>();
    }
    return pImpl_->startup(ctiFilePath);
}

void CameraManagerVimbax::shutdown() {
    if (pImpl_) {
        pImpl_->shutdown();
    }
}

bool CameraManagerVimbax::isReady() const {
    return pImpl_ && pImpl_->isReady();
}

VimbaXStatus CameraManagerVimbax::getStatus() const {
    if (!pImpl_) {
        VimbaXStatus status;
        status.errorMessage = "System not initialized";
        return status;
    }
    return pImpl_->getStatus();
}

std::vector<VimbaXCameraInfo> CameraManagerVimbax::getCameras() {
    if (!pImpl_) return {};
    return pImpl_->getCameras();
}

void CameraManagerVimbax::updateCameraList() {
    if (pImpl_) {
        pImpl_->updateCameras();
    }
}

bool CameraManagerVimbax::isDiscoveryComplete() const {
    return pImpl_ && pImpl_->isDiscoveryComplete();
}

} // namespace lbt
