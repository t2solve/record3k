#pragma once

#include <liblbt/factory/model_factory.h>
#include <liblbt/json_store.h>
#include <liblbt/vimbax_frame_source.h>
#include <liblbt/disk_image_frame_source.h>
#include <liblbt/iframe_source.h>
#include <liblbt/camera_manager_vimbax.h>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <memory>

namespace lbt {

// Enum representing detailed camera test results
enum class CameraTestResult {
    SUCCESS,                // Camera is online and working
    ERROR_NOT_FOUND,       // Camera not found/not connected
    ERROR_NOT_READY,       // Camera found but not ready
    ERROR_NO_FRAME,        // Camera ready but cannot get frame
    ERROR_PATH_NOT_EXIST,  // For TYPE_FILE: path doesn't exist
    ERROR_EXCEPTION,       // Exception during test
    ERROR_NOT_BUILT,       // Feature not built (e.g., VimbaX disabled)
    ERROR_UNKNOWN_TYPE     // Unknown camera type
};

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

    // Attempt to open and read one frame. Returns detailed test result
    CameraTestResult testCamera(const api::CameraInfo& cam, const std::string& vimbaxCtiPath = "");

private:
    struct CamEntry {
        api::CameraInfo info;
    };
    std::filesystem::path dir_;
    JsonStore store_; // wraps directory
    std::unordered_map<std::string, CamEntry> cams_;

    // Persist a camera's status back to JsonStore
    void persist(const api::CameraInfo& cam);
};

} // namespace lbt
