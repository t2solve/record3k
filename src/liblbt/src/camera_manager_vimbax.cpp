#include <liblbt/camera_manager_vimbax.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <memory>
#include <thread>

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
                std::cout << "[VimbaxEventObserver] Event occurred: " << featureName 
                          << " = " << featureValue << std::endl;
                
                // Log specific event types
                if (featureName.find("EventAcquisitionStart") != std::string::npos) {
                    std::cout << "[VimbaxEventObserver] >>> Camera acquisition started" << std::endl;
                }
                else if (featureName.find("EventAcquisitionEnd") != std::string::npos) {
                    std::cout << "[VimbaxEventObserver] >>> Camera acquisition ended" << std::endl;
                }
                else if (featureName.find("EventFrameTrigger") != std::string::npos) {
                    std::cout << "[VimbaxEventObserver] >>> Frame trigger received" << std::endl;
                }
                else if (featureName.find("EventExposureEnd") != std::string::npos) {
                    std::cout << "[VimbaxEventObserver] >>> Exposure completed" << std::endl;
                }
                else {
                    std::cout << "[VimbaxEventObserver] >>> Generic event" << std::endl;
                }
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
                std::cout << "[CameraManagerVimbax] System already started" << std::endl;
                return true;
            }
            
            status_.isBuilt = true;
            std::cout << "[CameraManagerVimbax] VimbaX is built into this binary" << std::endl;
        } // Release mutex before VmbSystem::Startup() which may take time
        
        try {
            // Prepare transport layer file path
            std::string pathConfig;
            if (!ctiFilePath.empty()) {
                std::cout << "[CameraManagerVimbax] Received .cti file path: " << ctiFilePath << std::endl;
                std::filesystem::path absPath = std::filesystem::absolute(ctiFilePath);
                std::cout << "[CameraManagerVimbax] Absolute path: " << absPath << std::endl;
                
                if (std::filesystem::exists(absPath)) {
                    pathConfig = absPath.string();
                    std::cout << "[CameraManagerVimbax] File exists! Using .cti file: " << pathConfig << std::endl;
                } else {
                    std::cerr << "[CameraManagerVimbax] ERROR: Specified .cti file does not exist: " << absPath << std::endl;
                    std::cerr << "[CameraManagerVimbax] Current working directory: " << std::filesystem::current_path() << std::endl;
                    std::cerr << "[CameraManagerVimbax] Falling back to GENICAM_GENTL64_PATH" << std::endl;
                }
            } else {
                std::cout << "[CameraManagerVimbax] No .cti file specified, using GENICAM_GENTL64_PATH" << std::endl;
            }
            
            std::cout << "[CameraManagerVimbax] VmbSystem singleton obtained" << std::endl;
            
            // Query version before startup
            VmbVersionInfo_t versionInfo;
            VmbErrorType err = system_.QueryVersion(versionInfo);
            if (err == VmbErrorSuccess) {
                std::cout << "[CameraManagerVimbax] VimbaX Version: " << versionInfo.major 
                          << "." << versionInfo.minor << "." << versionInfo.patch << std::endl;
            } else {
                std::cerr << "[CameraManagerVimbax] WARNING: Could not query VimbaX version, error: " << err << std::endl;
            }
            
            // Initialize VmbSystem
            std::cout << "[CameraManagerVimbax] Calling VmbSystem::Startup()..." << std::endl;
            if (pathConfig.empty()) {
                err = system_.Startup();
            } else {
                std::cout << "[CameraManagerVimbax] Using CTI path: " << pathConfig << std::endl;
                err = system_.Startup(pathConfig.c_str());
            }
            
            if (err != VmbErrorSuccess) {
                std::lock_guard<std::mutex> lock(mutex_);
                status_.errorMessage = "VmbSystem::Startup() failed with error code: " + std::to_string(err);
                std::cerr << "[CameraManagerVimbax] ERROR: " << status_.errorMessage << std::endl;
                std::cerr << "[CameraManagerVimbax] ERROR: VmbErrorSuccess = 0, received error = " << err << std::endl;
                std::cerr << "[CameraManagerVimbax] ERROR: This means VimbaX failed to initialize!" << std::endl;
                return false;
            }
            
            std::cout << "[CameraManagerVimbax] *** VmbSystem::Startup() SUCCEEDED (err == VmbErrorSuccess) ***" << std::endl;
            
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
                std::cout << "[CameraManagerVimbax] Event observer created" << std::endl;
            }
            
            // Enumerate system components (without holding mutex)
            enumerateTransportLayers();
            enumerateInterfaces();
            
            // Update cameras (this will acquire/release mutex as needed)
            std::cout << "[CameraManagerVimbax] Calling updateCameras()..." << std::endl;
            updateCameras();
            
            std::cout << "[CameraManagerVimbax] Initialization complete. System ready." << std::endl;
            return true;
            
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.errorMessage = std::string("Exception during VimbaX startup: ") + e.what();
            std::cerr << "[CameraManagerVimbax] ERROR: " << status_.errorMessage << std::endl;
            status_.startupSuccess = false;
            isStarted_ = false;
            return false;
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.errorMessage = "Unknown exception during VimbaX startup";
            std::cerr << "[CameraManagerVimbax] ERROR: " << status_.errorMessage << std::endl;
            status_.startupSuccess = false;
            isStarted_ = false;
            return false;
        }
    }
    
    void shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!isStarted_) {
            std::cout << "[CameraManagerVimbax] System not started, nothing to shutdown" << std::endl;
            return;
        }
        
        std::cout << "[CameraManagerVimbax] Calling VmbSystem::Shutdown()..." << std::endl;
        try {
            system_.Shutdown();
            isStarted_ = false;
            status_.startupSuccess = false;
            std::cout << "[CameraManagerVimbax] VimbaX system shutdown complete" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[CameraManagerVimbax] Exception during shutdown: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[CameraManagerVimbax] Unknown exception during shutdown" << std::endl;
        }
    }
    
    bool isReady() const {
        std::cout << "[CameraManagerVimbax::isReady] Attempting to acquire mutex..." << std::endl;
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << "[CameraManagerVimbax::isReady] Mutex acquired! isStarted_=" << isStarted_ 
                  << ", startupSuccess=" << status_.startupSuccess << std::endl;
        bool result = isStarted_ && status_.startupSuccess;
        std::cout << "[CameraManagerVimbax::isReady] Returning: " << result << std::endl;
        return result;
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
        std::cout << "[CameraManagerVimbax::updateCameras] Starting camera update..." << std::endl;
        
        // Check if system is started (with mutex)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!isStarted_) {
                std::cout << "[CameraManagerVimbax::updateCameras] System not started, skipping" << std::endl;
                return;
            }
        }
        
        // Give GigE cameras extra time for network discovery
        std::cout << "[CameraManagerVimbax::updateCameras] Waiting 1000ms for GigE camera discovery on network interfaces..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        // Do the blocking GetCameras() call WITHOUT holding the mutex!
        std::cout << "[CameraManagerVimbax::updateCameras] Calling GetCameras() WITHOUT mutex - other threads can proceed..." << std::endl;
        std::cout << "[CameraManagerVimbax::updateCameras] NOTE: This may block for several seconds on network discovery" << std::endl;
        auto startTime = std::chrono::steady_clock::now();
        
        VmbCPP::CameraPtrVector vmbCameras;
        VmbErrorType err = system_.GetCameras(vmbCameras);
        
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        std::cout << "[CameraManagerVimbax::updateCameras] GetCameras() returned after " << elapsed << "ms with error code: " << err << std::endl;
        
        // Now acquire mutex to update the results
        std::cout << "[CameraManagerVimbax::updateCameras] Acquiring mutex to update results..." << std::endl;
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << "[CameraManagerVimbax::updateCameras] Mutex acquired, updating camera list..." << std::endl;
        
        cameras_.clear();
        discoveryComplete_ = true;
        std::cout << "[CameraManagerVimbax::updateCameras] Set discoveryComplete_ = true" << std::endl;
        
        if (err != VmbErrorSuccess) {
            std::cerr << "[CameraManagerVimbax] WARNING: Could not get cameras, error: " << err << std::endl;
            status_.cameraCount = 0;
            return;
        }
        
        status_.cameraCount = vmbCameras.size();
        std::cout << "[CameraManagerVimbax] === Camera Discovery Results ===" << std::endl;
        std::cout << "[CameraManagerVimbax] *** Found " << status_.cameraCount << " camera(s) ***" << std::endl;
        
        if (vmbCameras.empty()) {
            std::cerr << "[CameraManagerVimbax] WARNING: NO CAMERAS FOUND!" << std::endl;
            std::cerr << "[CameraManagerVimbax] Possible reasons:" << std::endl;
            std::cerr << "[CameraManagerVimbax]   1. No cameras connected" << std::endl;
            std::cerr << "[CameraManagerVimbax]   2. Camera not on same subnet as interface" << std::endl;
            std::cerr << "[CameraManagerVimbax]   3. Wrong transport layer (.cti file)" << std::endl;
            std::cerr << "[CameraManagerVimbax]   4. Insufficient discovery time (GigE needs time)" << std::endl;
            std::cerr << "[CameraManagerVimbax]   5. Firewall blocking GigE Vision discovery" << std::endl;
            return;
        }
        
        for (size_t i = 0; i < vmbCameras.size(); ++i) {
            const auto& cam = vmbCameras[i];
            std::cout << "[CameraManagerVimbax] ================================" << std::endl;
            std::cout << "[CameraManagerVimbax] === Camera " << (i+1) << " ===" << std::endl;
            
            VimbaXCameraInfo info;
            VmbErrorType err;
            
            err = cam->GetName(info.name);
            if (err == VmbErrorSuccess) {
                std::cout << "[CameraManagerVimbax] /// Camera Name           : " << info.name << std::endl;
            } else {
                std::cout << "[CameraManagerVimbax] /// Camera Name           : [Could not get name, error: " << err << "]" << std::endl;
                info.name = "[Unknown]";
            }
            
            err = cam->GetModel(info.model);
            if (err == VmbErrorSuccess) {
                std::cout << "[CameraManagerVimbax] /// Model Name            : " << info.model << std::endl;
            } else {
                std::cout << "[CameraManagerVimbax] /// Model Name            : [Could not get model, error: " << err << "]" << std::endl;
                info.model = "[Unknown]";
            }
            
            err = cam->GetID(info.id);
            if (err == VmbErrorSuccess) {
                std::cout << "[CameraManagerVimbax] /// Camera ID             : " << info.id << std::endl;
            } else {
                std::cout << "[CameraManagerVimbax] /// Camera ID             : [Could not get ID, error: " << err << "]" << std::endl;
                info.id = "[Unknown]";
            }
            
            err = cam->GetSerialNumber(info.serial);
            if (err == VmbErrorSuccess) {
                std::cout << "[CameraManagerVimbax] /// Serial Number         : " << info.serial << std::endl;
            } else {
                std::cout << "[CameraManagerVimbax] /// Serial Number         : [Could not get serial, error: " << err << "]" << std::endl;
                info.serial = "[Unknown]";
            }
            
            err = cam->GetInterfaceID(info.interfaceId);
            if (err == VmbErrorSuccess) {
                std::cout << "[CameraManagerVimbax] /// @ Interface ID        : " << info.interfaceId << std::endl;
            } else {
                std::cout << "[CameraManagerVimbax] /// @ Interface ID        : [Could not get interface ID, error: " << err << "]" << std::endl;
                info.interfaceId = "[Unknown]";
            }
            
            // Get Transport Layer info
            VmbCPP::TransportLayerPtr pTransportLayer;
            err = cam->GetTransportLayer(pTransportLayer);
            if (err == VmbErrorSuccess && pTransportLayer) {
                std::string tlID, tlPath;
                if (pTransportLayer->GetID(tlID) == VmbErrorSuccess) {
                    std::cout << "[CameraManagerVimbax] /// @ TransportLayer ID   : " << tlID << std::endl;
                }
                if (pTransportLayer->GetPath(tlPath) == VmbErrorSuccess) {
                    std::cout << "[CameraManagerVimbax] /// @ TransportLayer Path : " << tlPath << std::endl;
                }
            } else {
                std::cout << "[CameraManagerVimbax] /// @ TransportLayer      : [Could not get TL, error: " << err << "]" << std::endl;
            }
            
            // Try to get MAC address (GigE cameras)
            info.macAddress = "N/A";
            VmbCPP::FeaturePtr macFeature;
            if (cam->GetFeatureByName("GevDeviceMACAddress", macFeature) == VmbErrorSuccess) {
                VmbInt64_t macValue;
                if (macFeature->GetValue(macValue) == VmbErrorSuccess) {
                    // Convert MAC address from integer to string format
                    std::ostringstream oss;
                    oss << std::hex << std::setfill('0') << std::uppercase;
                    oss << std::setw(2) << ((macValue >> 40) & 0xFF) << ":"
                        << std::setw(2) << ((macValue >> 32) & 0xFF) << ":"
                        << std::setw(2) << ((macValue >> 24) & 0xFF) << ":"
                        << std::setw(2) << ((macValue >> 16) & 0xFF) << ":"
                        << std::setw(2) << ((macValue >> 8) & 0xFF) << ":"
                        << std::setw(2) << (macValue & 0xFF);
                    info.macAddress = oss.str();
                    std::cout << "[CameraManagerVimbax] /// MAC Address          : " << info.macAddress << std::endl;
                }
            }
            
            cameras_.push_back(info);
        }
        std::cout << "[CameraManagerVimbax] ================================" << std::endl;
        std::cout << "[CameraManagerVimbax] === End Camera List ===" << std::endl;
        
        std::cout << "[CameraManagerVimbax::updateCameras] Camera list updated, mutex will be released" << std::endl;
        
        // Release mutex before registering event observers (they may take time)
        lock.~lock_guard();
        
        // Register event observers for each camera (without holding mutex)
        std::cout << "[CameraManagerVimbax::updateCameras] Registering event observers for cameras..." << std::endl;
        for (size_t i = 0; i < vmbCameras.size(); ++i) {
            registerCameraEventObserver(vmbCameras[i], cameras_[i].id);
        }
        
        std::cout << "[CameraManagerVimbax::updateCameras] Camera discovery and event registration complete" << std::endl;
    }
    
    bool isDiscoveryComplete() const {
        std::cout << "[CameraManagerVimbax::isDiscoveryComplete] Attempting to acquire mutex..." << std::endl;
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << "[CameraManagerVimbax::isDiscoveryComplete] Mutex acquired! discoveryComplete_=" << discoveryComplete_ << std::endl;
        return discoveryComplete_;
    }

private:
    void enumerateTransportLayers() {
        std::cout << "[CameraManagerVimbax] === Enumerating Transport Layers ===" << std::endl;
        VmbCPP::TransportLayerPtrVector transportLayers;
        VmbErrorType err = system_.GetTransportLayers(transportLayers);
        
        if (err != VmbErrorSuccess) {
            std::cerr << "[CameraManagerVimbax] ERROR: GetTransportLayers() failed with error: " << err << std::endl;
            std::cerr << "[CameraManagerVimbax] ERROR: Cannot enumerate transport layers!" << std::endl;
            status_.transportLayerCount = 0;
            return;
        }
        
        status_.transportLayerCount = transportLayers.size();
        std::cout << "[CameraManagerVimbax] *** GetTransportLayers() succeeded ***" << std::endl;
        std::cout << "[CameraManagerVimbax] Found " << status_.transportLayerCount << " transport layer(s)" << std::endl;
        
        if (transportLayers.empty()) {
            std::cerr << "[CameraManagerVimbax] WARNING: No transport layers found! Check CTI files." << std::endl;
            return;
        }
        
        for (size_t i = 0; i < transportLayers.size(); ++i) {
            const auto& tl = transportLayers[i];
            std::cout << "[CameraManagerVimbax] --- Transport Layer " << (i+1) << " ---" << std::endl;
            
            std::string tlID, tlPath, tlType, tlModel, tlVendor, tlVersion;
            
            if (tl->GetID(tlID) == VmbErrorSuccess) {
                std::cout << "[CameraManagerVimbax]   ID: " << tlID << std::endl;
            } else {
                std::cout << "[CameraManagerVimbax]   ID: [Could not get ID]" << std::endl;
            }
            
            if (tl->GetPath(tlPath) == VmbErrorSuccess) {
                std::cout << "[CameraManagerVimbax]   Path: " << tlPath << std::endl;
            } else {
                std::cout << "[CameraManagerVimbax]   Path: [Could not get path]" << std::endl;
            }
            
            // if (tl->GetModel(tlModel) == VmbErrorSuccess) {
            //     std::cout << "[CameraManagerVimbax]   Model: " << tlModel << std::endl;
            // }
            
            // if (tl->GetVendor(tlVendor) == VmbErrorSuccess) {
            //     std::cout << "[CameraManagerVimbax]   Vendor: " << tlVendor << std::endl;
            // }
            
            VmbCPP::FeaturePtr feature;
            if (tl->GetFeatureByName("TLType", feature) == VmbErrorSuccess) {
                if (feature->GetValue(tlType) == VmbErrorSuccess) {
                    std::cout << "[CameraManagerVimbax]   Type: " << tlType << std::endl;
                }
            }
            
            if (tl->GetFeatureByName("TLVersion", feature) == VmbErrorSuccess) {
                if (feature->GetValue(tlVersion) == VmbErrorSuccess) {
                    std::cout << "[CameraManagerVimbax]   Version: " << tlVersion << std::endl;
                }
            }
        }
        std::cout << "[CameraManagerVimbax] === End Transport Layers ===" << std::endl;
    }
    
    void enumerateInterfaces() {
        std::cout << "[CameraManagerVimbax] === Enumerating Interfaces ===" << std::endl;
        VmbCPP::InterfacePtrVector interfaces;
        VmbErrorType err = system_.GetInterfaces(interfaces);
        
        if (err != VmbErrorSuccess) {
            std::cerr << "[CameraManagerVimbax] ERROR: GetInterfaces() failed with error: " << err << std::endl;
            std::cerr << "[CameraManagerVimbax] ERROR: Cannot enumerate interfaces!" << std::endl;
            status_.interfaceCount = 0;
            return;
        }
        
        status_.interfaceCount = interfaces.size();
        std::cout << "[CameraManagerVimbax] *** GetInterfaces() succeeded ***" << std::endl;
        std::cout << "[CameraManagerVimbax] Found " << status_.interfaceCount << " interface(s)" << std::endl;
        
        if (interfaces.empty()) {
            std::cerr << "[CameraManagerVimbax] WARNING: No interfaces found!" << std::endl;
            return;
        }
        
        for (size_t i = 0; i < interfaces.size(); ++i) {
            const auto& iface = interfaces[i];
            std::cout << "[CameraManagerVimbax] --- Interface " << (i+1) << " ---" << std::endl;
            
            std::string ifaceID, ifaceType, ifaceName;
            
            if (iface->GetID(ifaceID) == VmbErrorSuccess) {
                std::cout << "[CameraManagerVimbax]   ID: " << ifaceID << std::endl;
            } else {
                std::cout << "[CameraManagerVimbax]   ID: [Could not get ID]" << std::endl;
            }
            
            if (iface->GetName(ifaceName) == VmbErrorSuccess) {
                std::cout << "[CameraManagerVimbax]   Name: " << ifaceName << std::endl;
            }
            
            VmbCPP::FeaturePtr feature;
            if (iface->GetFeatureByName("InterfaceType", feature) == VmbErrorSuccess) {
                if (feature->GetValue(ifaceType) == VmbErrorSuccess) {
                    std::cout << "[CameraManagerVimbax]   Type: " << ifaceType << std::endl;
                }
            }
            
            // For GigE interfaces, try to get IP and MAC info
            if (ifaceType == "GEV" || ifaceID.find("GigE") != std::string::npos || ifaceID.find("eno") != std::string::npos) {
                std::cout << "[CameraManagerVimbax]   >>> This is a GigE interface <<<" << std::endl;
                
                VmbInt64_t ipAddr = 0, subnetMask = 0, macAddr = 0;
                if (iface->GetFeatureByName("GevInterfaceIPAddress", feature) == VmbErrorSuccess) {
                    if (feature->GetValue(ipAddr) == VmbErrorSuccess) {
                        std::cout << "[CameraManagerVimbax]   IP Address: "
                                  << ((ipAddr >> 24) & 0xFF) << "."
                                  << ((ipAddr >> 16) & 0xFF) << "."
                                  << ((ipAddr >> 8) & 0xFF) << "."
                                  << (ipAddr & 0xFF) << std::endl;
                    }
                }
                
                if (iface->GetFeatureByName("GevInterfaceSubnetMask", feature) == VmbErrorSuccess) {
                    if (feature->GetValue(subnetMask) == VmbErrorSuccess) {
                        std::cout << "[CameraManagerVimbax]   Subnet Mask: "
                                  << ((subnetMask >> 24) & 0xFF) << "."
                                  << ((subnetMask >> 16) & 0xFF) << "."
                                  << ((subnetMask >> 8) & 0xFF) << "."
                                  << (subnetMask & 0xFF) << std::endl;
                    }
                }
                
                if (iface->GetFeatureByName("GevInterfaceMACAddress", feature) == VmbErrorSuccess) {
                    if (feature->GetValue(macAddr) == VmbErrorSuccess) {
                        std::cout << "[CameraManagerVimbax]   MAC Address: "
                                  << std::hex << std::setfill('0') << std::uppercase
                                  << std::setw(2) << ((macAddr >> 40) & 0xFF) << ":"
                                  << std::setw(2) << ((macAddr >> 32) & 0xFF) << ":"
                                  << std::setw(2) << ((macAddr >> 24) & 0xFF) << ":"
                                  << std::setw(2) << ((macAddr >> 16) & 0xFF) << ":"
                                  << std::setw(2) << ((macAddr >> 8) & 0xFF) << ":"
                                  << std::setw(2) << (macAddr & 0xFF)
                                  << std::dec << std::endl;
                    }
                }
            }
        }
        std::cout << "[CameraManagerVimbax] === End Interfaces ===" << std::endl;
    }
    
    void registerCameraEventObserver(const VmbCPP::CameraPtr& camera, const std::string& camId) {
        if (!eventObserver_) {
            std::cout << "[CameraManagerVimbax] No event observer available, skipping event registration for: " << camId << std::endl;
            return;
        }
        
        std::cout << "[CameraManagerVimbax] Registering event observer for camera: " << camId << std::endl;
        
        // Open camera to access its features
        VmbErrorType err = camera->Open(VmbAccessModeRead);
        if (err == VmbErrorSuccess) {
            // List of common camera events to monitor
            const std::vector<std::string> cameraEvents = {
                "AcquisitionStart",
                "AcquisitionEnd",
                "FrameTrigger",
                "ExposureEnd",
                "Error"
            };
            
            VmbCPP::FeaturePtr selectorFeature, notificationFeature;
            
            // Check if EventSelector is available
            err = camera->GetFeatureByName("EventSelector", selectorFeature);
            if (err == VmbErrorSuccess) {
                std::cout << "[CameraManagerVimbax]   EventSelector available, registering events..." << std::endl;
                for (const auto& eventType : cameraEvents) {
                    // Set event selector
                    if (selectorFeature->SetValue(eventType.c_str()) == VmbErrorSuccess) {
                        // Enable notification for this event
                        if (camera->GetFeatureByName("EventNotification", notificationFeature) == VmbErrorSuccess) {
                            if (notificationFeature->SetValue("On") == VmbErrorSuccess) {
                                // Register observer for the event feature
                                std::string eventFeatureName = "Event" + eventType;
                                VmbCPP::FeaturePtr eventFeature;
                                if (camera->GetFeatureByName(eventFeatureName.c_str(), eventFeature) == VmbErrorSuccess) {
                                    if (eventFeature->RegisterObserver(eventObserver_) == VmbErrorSuccess) {
                                        std::cout << "[CameraManagerVimbax]     ✓ Registered observer for: " << eventType << std::endl;
                                    } else {
                                        std::cout << "[CameraManagerVimbax]     ✗ Failed to register observer for: " << eventType << std::endl;
                                    }
                                } else {
                                    std::cout << "[CameraManagerVimbax]     ✗ Event feature not found: " << eventFeatureName << std::endl;
                                }
                            }
                        }
                    }
                }
            } else {
                std::cout << "[CameraManagerVimbax]   EventSelector not available (error: " << err << ")" << std::endl;
            }
            
            // Close camera after registration
            camera->Close();
            std::cout << "[CameraManagerVimbax] Event registration complete for: " << camId << std::endl;
        } else {
            std::cout << "[CameraManagerVimbax] Could not open camera for event registration (error: " << err << ")" << std::endl;
        }
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
