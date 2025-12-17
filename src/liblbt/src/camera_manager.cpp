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
    for (auto& k : keys) {
        auto valOpt = store_.get(k);
        if (!valOpt) continue;
        api::CameraInfo ci = api::CameraInfo::fromJson(*valOpt);
        if (ci.camUID.empty()) ci.camUID = k; // fallback
        cams_[ci.camUID] = CamEntry{ci};
    }
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

bool CameraManager::testCamera(const api::CameraInfo& cam) {
    // Choose strategy by cameraType
    bool success = false;
    if (cam.cameraType == "TYPE_FILE") {
        //TODO here
        // disk image source under data/files/bin/<camUID>
        std::filesystem::path imgDir = dir_.parent_path().parent_path() / "files" / "bin" / cam.camUID; // heuristic
        if (std::filesystem::exists(imgDir)) {
            try {
                DiskImageFrameSource disk(imgDir.string(), "*.jpg");
                if (disk.isReady()) {
                    auto frame = disk.nextFrame();
                    success = (frame != nullptr);
                }
            } catch (...) {
                success = false;
            }
        }
    } 
    if (cam.cameraType == "TYPE_CAM_VIMBA") {
        // Default to VimbaX when available
#ifdef VIMBAX_ENABLED
        try {
            // Try by serial (camUID) first
            {
                VimbaXFrameSource src_by_serial(cam.camUID, "", false);
                if (src_by_serial.isReady()) {
                    auto frame = src_by_serial.nextFrame();
                    success = (frame != nullptr);
                }
            }
            // Fallback: try by interface/MAC
            if (!success) {
                VimbaXFrameSource src_by_iface("", cam.macAddress, false);
                if (src_by_iface.isReady()) {
                    auto frame = src_by_iface.nextFrame();
                    success = (frame != nullptr);
                }
            }
        } catch (...) {
            success = false;
        }
#else
    // VimbaX not enabled; cannot test camera. Leave success=false and log.
    (void)cam; // input is const; do not modify fields here
    std::cerr << "[CameraManager] VimbaX not enabled during build; cannot test camera "
          << "(mac=" << cam.macAddress << ")" << std::endl;
#endif
        // If VIMBAX not enabled, success stays false
    }
    return success;
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
        bool ok = testCamera(info);
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
