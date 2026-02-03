#include <liblbt/camera_manager.h>
#include <liblbt/opencv_frame_source.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <thread>
#include <chrono>
#include <algorithm>

namespace lbt {

CameraManager::CameraManager(std::filesystem::path camerasDir)
    : dir_(std::move(camerasDir)), store_(dir_) {
    store_.start();
}

void CameraManager::load() {
    cams_.clear();
    auto keys = store_.keys();
    std::cout << "[CameraManager] Loading cameras from store, found " << keys.size() << " files" << std::endl;
    for (auto& k : keys) {
        std::cout << "[CameraManager] Loading key: " << k << std::endl;
        auto valOpt = store_.get(k);
        if (!valOpt) {
            std::cerr << "[CameraManager] WARNING: Failed to get value for key: " << k << std::endl;
            continue;
        }
        api::CameraInfo ci = api::CameraInfo::fromJson(*valOpt);
        std::cout << "[CameraManager] Loaded camera - File key: " << k << ", camUID in JSON: " << ci.camUID << std::endl;
        if (ci.camUID.empty()) ci.camUID = k; // fallback
        cams_[ci.camUID] = CamEntry{ci};
    }
    std::cout << "[CameraManager] Total cameras loaded: " << cams_.size() << std::endl;
}

std::vector<api::CameraInfo> CameraManager::cameras() const {
    std::vector<api::CameraInfo> out;
    out.reserve(cams_.size());
    for (auto& kv : cams_) out.push_back(kv.second.info);
    return out;
}

std::optional<api::CameraInfo> CameraManager::get(const std::string& camUID) const {
    auto it = cams_.find(camUID);
    if (it == cams_.end()) return std::nullopt;
    return it->second.info;
}

CameraTestResult CameraManager::testCamera(const api::CameraInfo& cam, const std::string& vimbaxCtiPath) {
    std::cout << "[CameraManager] Testing camera: " << cam.camUID 
              << " (type: " << cam.cameraType << ")" << std::endl;
    
    if (cam.cameraType == "TYPE_FILE") {
        std::cout << "[CameraManager] TYPE_FILE camera test" << std::endl;
        // disk image source under data/files/bin/<camUID>
        std::filesystem::path imgDir = dir_.parent_path().parent_path() / "files" / "bin" / cam.camUID;
        std::cout << "[CameraManager] Looking for images in: " << imgDir << std::endl;
        
        if (!std::filesystem::exists(imgDir)) {
            std::cerr << "[CameraManager] ERROR: Path does not exist: " << imgDir << std::endl;
            return CameraTestResult::ERROR_PATH_NOT_EXIST;
        }
        
        try {
            DiskImageFrameSource disk(imgDir.string(), "*.jpg");
            if (!disk.isReady()) {
                std::cerr << "[CameraManager] ERROR: DiskImageFrameSource not ready" << std::endl;
                return CameraTestResult::ERROR_NOT_READY;
            }
            
            auto frame = disk.nextFrame();
            if (frame == nullptr) {
                std::cerr << "[CameraManager] ERROR: Could not get frame from disk source" << std::endl;
                return CameraTestResult::ERROR_NO_FRAME;
            }
            
            std::cout << "[CameraManager] SUCCESS: TYPE_FILE camera test passed" << std::endl;
            return CameraTestResult::SUCCESS;
        } catch (const std::exception& e) {
            std::cerr << "[CameraManager] EXCEPTION in TYPE_FILE test: " << e.what() << std::endl;
            return CameraTestResult::ERROR_EXCEPTION;
        } catch (...) {
            std::cerr << "[CameraManager] UNKNOWN EXCEPTION in TYPE_FILE test" << std::endl;
            return CameraTestResult::ERROR_EXCEPTION;
        }
    } 
    
    if (cam.cameraType == "TYPE_CAM_USB") {
        std::cout << "[CameraManager] TYPE_CAM_USB camera test" << std::endl;
        
        if (!cam.devicePath || cam.devicePath->empty()) {
            std::cerr << "[CameraManager] ERROR: No devicePath specified for USB camera" << std::endl;
            return CameraTestResult::ERROR_PATH_NOT_EXIST;
        }
        
        std::cout << "[CameraManager] Testing USB camera: " << *cam.devicePath << std::endl;
        
        try {
            OpenCVFrameSource src(*cam.devicePath);
            if (!src.isReady()) {
                std::cerr << "[CameraManager] ERROR: OpenCVFrameSource not ready for " << *cam.devicePath << std::endl;
                return CameraTestResult::ERROR_NOT_READY;
            }
            
            std::cout << "[CameraManager] Camera ready, acquiring frame..." << std::endl;
            auto frame = src.nextFrame();
            if (frame == nullptr) {
                std::cerr << "[CameraManager] ERROR: Could not get frame from USB camera" << std::endl;
                return CameraTestResult::ERROR_NO_FRAME;
            }
            
            std::cout << "[CameraManager] SUCCESS: TYPE_CAM_USB camera test passed" << std::endl;
            std::cout << "[CameraManager]   Frame size: " << src.getFrameWidth() << "x" << src.getFrameHeight() << std::endl;
            std::cout << "[CameraManager]   FPS: " << src.getFPS() << std::endl;
            return CameraTestResult::SUCCESS;
        } catch (const std::exception& e) {
            std::cerr << "[CameraManager] EXCEPTION in TYPE_CAM_USB test: " << e.what() << std::endl;
            return CameraTestResult::ERROR_EXCEPTION;
        } catch (...) {
            std::cerr << "[CameraManager] UNKNOWN EXCEPTION in TYPE_CAM_USB test" << std::endl;
            return CameraTestResult::ERROR_EXCEPTION;
        }
    }
    
    if (cam.cameraType == "TYPE_CAM_VIMBA") {
        std::cout << "[CameraManager] TYPE_CAM_VIMBA camera test" << std::endl;
#ifdef VIMBAX_ENABLED
        // Get VimbaX singleton (should already be initialized at app startup)
        auto& vimbaxMgr = CameraManagerVimbax::getInstance();
        
        std::cout << "[CameraManager] Checking if VimbaX system is ready..." << std::endl;
        // Check if system is ready
        if (!vimbaxMgr.isReady()) {
            std::cerr << "[CameraManager] ERROR: VimbaX system not ready" << std::endl;
            std::cerr << "[CameraManager] System may still be initializing in background" << std::endl;
            return CameraTestResult::ERROR_NOT_READY;
        }
        std::cout << "[CameraManager] VimbaX system is ready" << std::endl;
        
        // Check if initial discovery is complete to avoid blocking
        if (!vimbaxMgr.isDiscoveryComplete()) {
            std::cerr << "[CameraManager] ERROR: VimbaX camera discovery still in progress" << std::endl;
            std::cerr << "[CameraManager] Background thread is still discovering cameras" << std::endl;
            std::cerr << "[CameraManager] Please wait a moment and try again" << std::endl;
            return CameraTestResult::ERROR_NOT_READY;
        }
        std::cout << "[CameraManager] Camera discovery is complete" << std::endl;
        
        // Note: We skip updateCameraList() and getCameras() to avoid blocking on mutex
        // If background thread is still discovering cameras, we'll just try to open the camera directly
        std::cout << "[CameraManager] Skipping camera list query to avoid blocking" << std::endl;
        std::cout << "[CameraManager] ======================================" << std::endl;
        std::cout << "[CameraManager] Attempting to test camera:" << std::endl;
        std::cout << "[CameraManager]   - camUID: " << cam.camUID << std::endl;
        std::cout << "[CameraManager]   - Serial: " << (cam.serialNumber ? *cam.serialNumber : "N/A") << std::endl;
        std::cout << "[CameraManager]   - MAC address: " << cam.macAddress << std::endl;
        std::cout << "[CameraManager]   - Description: " << (cam.description ? *cam.description : "N/A") << std::endl;
        std::cout << "[CameraManager] ======================================" << std::endl;
        std::cout << "[CameraManager] Will try matching by serial number first, then MAC address" << std::endl;
        
        try {
            // Try by serial number first (most reliable)
            if (cam.serialNumber && !cam.serialNumber->empty()) {
                std::cout << "[CameraManager] Attempting to open camera by serial number: " << *cam.serialNumber << std::endl;
                VimbaXFrameSource src_by_serial(*cam.serialNumber, "", false);
                std::cout << "[CameraManager] VimbaXFrameSource created, checking if ready..." << std::endl;
                if (src_by_serial.isReady()) {
                    std::cout << "[CameraManager] Camera ready via serial, acquiring frame..." << std::endl;
                    auto frame = src_by_serial.nextFrame();
                    if (frame != nullptr) {
                        std::cout << "[CameraManager] SUCCESS: Got frame via serial number" << std::endl;
                        return CameraTestResult::SUCCESS;
                    } else {
                        std::cerr << "[CameraManager] ERROR: Camera ready but no frame via serial" << std::endl;
                        return CameraTestResult::ERROR_NO_FRAME;
                    }
                } else {
                    std::cerr << "[CameraManager] ERROR: Camera not ready via serial number" << std::endl;
                }
            }
            
            // Fallback: Try by MAC address (for GigE cameras)
            if (!cam.macAddress.empty() && cam.macAddress != "N/A") {
                std::cout << "[CameraManager] Attempting to open camera by MAC address: " << cam.macAddress << std::endl;
                VimbaXFrameSource src_by_mac("", cam.macAddress, false);
                std::cout << "[CameraManager] VimbaXFrameSource created, checking if ready..." << std::endl;
                if (src_by_mac.isReady()) {
                    std::cout << "[CameraManager] Camera ready via MAC, acquiring frame..." << std::endl;
                    auto frame = src_by_mac.nextFrame();
                    if (frame != nullptr) {
                        std::cout << "[CameraManager] SUCCESS: Got frame via MAC address" << std::endl;
                        return CameraTestResult::SUCCESS;
                    } else {
                        std::cerr << "[CameraManager] ERROR: Camera ready but no frame via MAC" << std::endl;
                        return CameraTestResult::ERROR_NO_FRAME;
                    }
                } else {
                    std::cerr << "[CameraManager] ERROR: Camera not ready via MAC address" << std::endl;
                }
            }
            
            std::cerr << "[CameraManager] ERROR: No valid serial or MAC address provided for VimbaX camera" << std::endl;
            std::cerr << "[CameraManager] Either serialNumber or macAddress is required to identify specific VimbaX cameras" << std::endl;
            return CameraTestResult::ERROR_NOT_FOUND;
        } catch (const std::exception& e) {
            std::cerr << "[CameraManager] EXCEPTION in VimbaX test: " << e.what() << std::endl;
            return CameraTestResult::ERROR_EXCEPTION;
        } catch (...) {
            std::cerr << "[CameraManager] UNKNOWN EXCEPTION in VimbaX test" << std::endl;
            return CameraTestResult::ERROR_EXCEPTION;
        }
#else
        std::cerr << "[CameraManager] ERROR: VimbaX not enabled during build (mac=" 
                  << cam.macAddress << ")" << std::endl;
        return CameraTestResult::ERROR_NOT_BUILT;
#endif
    }
    
    std::cerr << "[CameraManager] ERROR: Unknown camera type: " << cam.cameraType << std::endl;
    return CameraTestResult::ERROR_UNKNOWN_TYPE;
}

void CameraManager::persist(const api::CameraInfo& cam) {
    // Merge into existing JSON instead of overwriting entirely
    Json::Value j;
    if (auto existing = store_.get(cam.camUID)) {
        j = *existing;
    }
    j["camUID"] = cam.camUID;
    j["macAddress"] = cam.macAddress;
    j["status"] = cam.status;
    if (!cam.datetimeLastSeen.empty()) j["datetimeLastSeen"] = cam.datetimeLastSeen;
    if (!cam.cameraType.empty()) j["cameraType"] = cam.cameraType;
    if (cam.description) j["description"] = *cam.description; // preserve/refresh optional
    store_.put(cam.camUID, j);
}

void CameraManager::probeAll() {
    for (auto& kv : cams_) {
        auto& info = kv.second.info;
        CameraTestResult result = testCamera(info);
        bool ok = (result == CameraTestResult::SUCCESS);
        info.status = ok ? "online" : "offline";
        if (ok) {
            // Update last seen timestamp in ISO-8601 (UTC)
            auto now = std::chrono::system_clock::now();
            std::time_t tt = std::chrono::system_clock::to_time_t(now);
            std::tm gmt{};
#if defined(_WIN32)
            gmtime_s(&gmt, &tt);
#else
            gmt = *std::gmtime(&tt);
#endif
            std::ostringstream oss;
            oss << std::put_time(&gmt, "%Y-%m-%dT%H:%M:%SZ");
            info.datetimeLastSeen = oss.str();
        }
        persist(info);
    }
}

} // namespace lbt
