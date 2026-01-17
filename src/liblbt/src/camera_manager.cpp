#include <liblbt/camera_manager.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>

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
    
    if (cam.cameraType == "TYPE_CAM_VIMBA") {
        std::cout << "[CameraManager] TYPE_CAM_VIMBA camera test" << std::endl;
#ifdef VIMBAX_ENABLED
        // First check if VimbaX system is ready
        std::cout << "[CameraManager] Using VimbaX .cti path: " << (vimbaxCtiPath.empty() ? "(default)" : vimbaxCtiPath) << std::endl;
        auto vimbaxStatus = CameraManagerVimbax::testVimbaxReadyToUse(vimbaxCtiPath);
        if (!vimbaxStatus.isBuilt) {
            std::cerr << "[CameraManager] ERROR: VimbaX not built - " << vimbaxStatus.errorMessage << std::endl;
            return CameraTestResult::ERROR_NOT_BUILT;
        }
        if (!vimbaxStatus.startupSuccess) {
            std::cerr << "[CameraManager] ERROR: VimbaX startup failed - " << vimbaxStatus.errorMessage << std::endl;
            return CameraTestResult::ERROR_NOT_READY;
        }
        
        std::cout << "[CameraManager] VimbaX system ready (v" << vimbaxStatus.versionMajor 
                  << "." << vimbaxStatus.versionMinor << "." << vimbaxStatus.versionPatch 
                  << ", " << vimbaxStatus.cameraCount << " camera(s) detected)" << std::endl;
        
        try {
            // Try by serial (camUID) first
            std::cout << "[CameraManager] Trying to open by serial: " << cam.camUID << std::endl;
            {
                VimbaXFrameSource src_by_serial(cam.camUID, "", false);
                if (src_by_serial.isReady()) {
                    std::cout << "[CameraManager] Camera ready via serial" << std::endl;
                    auto frame = src_by_serial.nextFrame();
                    if (frame != nullptr) {
                        std::cout << "[CameraManager] SUCCESS: Got frame via serial" << std::endl;
                        return CameraTestResult::SUCCESS;
                    } else {
                        std::cerr << "[CameraManager] ERROR: Camera ready but no frame via serial" << std::endl;
                    }
                } else {
                    std::cout << "[CameraManager] Camera not ready via serial, trying MAC address" << std::endl;
                }
            }
            
            // Fallback: try by interface/MAC
            std::cout << "[CameraManager] Trying to open by MAC: " << cam.macAddress << std::endl;
            VimbaXFrameSource src_by_iface("", cam.macAddress, false);
            if (!src_by_iface.isReady()) {
                std::cerr << "[CameraManager] ERROR: Camera not ready via MAC address" << std::endl;
                return CameraTestResult::ERROR_NOT_READY;
            }
            
            std::cout << "[CameraManager] Camera ready via MAC" << std::endl;
            auto frame = src_by_iface.nextFrame();
            if (frame == nullptr) {
                std::cerr << "[CameraManager] ERROR: Camera ready but no frame via MAC" << std::endl;
                return CameraTestResult::ERROR_NO_FRAME;
            }
            
            std::cout << "[CameraManager] SUCCESS: Got frame via MAC address" << std::endl;
            return CameraTestResult::SUCCESS;
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
