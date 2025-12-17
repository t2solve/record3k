#pragma once

#include <liblbt/factory/model_factory.h>
#include <liblbt/json_store.h>
#include <liblbt/vimbax_frame_source.h>
#include <liblbt/disk_image_frame_source.h>
#include <liblbt/iframe_source.h>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <memory>

namespace lbt {

// CameraManager loads camera JSON descriptors, probes reachability & frame capture, and updates status.
// It abstracts over different frame sources (Vimba cameras, disk images, etc.).
class CameraManager {
public:
    explicit CameraManager(std::filesystem::path camerasDir);
    // Load initial camera descriptors from directory using JsonStore; safe to call multiple times
    void load();
    // Probe all cameras: try connecting and grabbing one frame. Updates in-memory and persists status changes.
    void probeAll();
    // Return current camera info map keyed by camUID
    std::vector<api::CameraInfo> cameras() const;

    // Get a single camera info (optional)
    std::optional<api::CameraInfo> get(const std::string& camUID) const;

private:
    struct CamEntry {
        api::CameraInfo info;
    };
    std::filesystem::path dir_;
    JsonStore store_; // wraps directory
    std::unordered_map<std::string, CamEntry> cams_;

    // Attempt to open and read one frame. Returns true if reachable & frame obtained (or at least camera opened)
    bool testCamera(const api::CameraInfo& cam);
    // Persist a camera's status back to JsonStore
    void persist(const api::CameraInfo& cam);
};

} // namespace lbt
