#pragma once

#include <liblbt/api/api_models.h>
#include <liblbt/iframe_source.h>
#include <memory>
#include <string>

namespace lbt {

/**
 * @brief Factory for creating IFrameSource implementations based on camera configuration
 */
class FrameSourceFactory {
public:
    /**
     * @brief Create a frame source from CameraInfo
     * @param cameraInfo Camera configuration from JSON
     * @param debugLog Enable debug logging
     * @return Unique pointer to IFrameSource, or nullptr if camera type is unsupported
     * 
     * Creates appropriate frame source based on cameraType:
     * - TYPE_CAM_VIMBA: VimbaXFrameSource (uses macAddress)
     * - TYPE_CAM_USB: OpenCVFrameSource (uses devicePath)
     * - TYPE_FILE: Returns nullptr (use createFileSource instead)
     */
    static std::unique_ptr<IFrameSource> createFromCameraInfo(
        const api::CameraInfo& cameraInfo,
        bool debugLog = false,
        double targetFps = 5.0);
    
    /**
     * @brief Create a file-based frame source
     * @param directory Directory containing image files
     * @param pattern File pattern (e.g., "*.jpg")
     * @return Unique pointer to IFrameSource
     */
    static std::unique_ptr<IFrameSource> createFileSource(
        const std::string& directory,
        const std::string& pattern = "*.jpg");
    
    /**
     * @brief Create a video file frame source
     * @param videoFilePath Path to video file
     * @return Unique pointer to IFrameSource
     */
    static std::unique_ptr<IFrameSource> createVideoFileSource(
        const std::string& videoFilePath);
    
    /**
     * @brief Check if camera type is supported
     * @param cameraType Camera type string from CameraInfo
     * @return true if supported camera type
     */
    static bool isSupportedCameraType(const std::string& cameraType);
};

} // namespace lbt
