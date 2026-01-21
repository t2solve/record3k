#include <liblbt/factory/frame_source_factory.h>
#include <liblbt/disk_image_frame_source.h>
#include <liblbt/opencv_frame_source.h>

#ifdef VIMBAX_ENABLED
#include <liblbt/vimbax_frame_source.h>
#endif

#include <iostream>

namespace lbt {

std::unique_ptr<IFrameSource> FrameSourceFactory::createFromCameraInfo(
    const api::CameraInfo& cameraInfo,
    bool debugLog)
{
    std::cout << "[FrameSourceFactory] Creating frame source for camera: " << cameraInfo.camUID << std::endl;
    std::cout << "[FrameSourceFactory]   Type: " << cameraInfo.cameraType << std::endl;
    
    if (cameraInfo.cameraType == "TYPE_CAM_VIMBA") {
#ifdef VIMBAX_ENABLED
        std::cout << "[FrameSourceFactory]   Using VimbaXFrameSource" << std::endl;
        std::cout << "[FrameSourceFactory]   MAC Address: " << cameraInfo.macAddress << std::endl;
        
        if (cameraInfo.macAddress.empty()) {
            std::cerr << "[FrameSourceFactory] ERROR: TYPE_CAM_VIMBA requires macAddress" << std::endl;
            return nullptr;
        }
        
        // VimbaXFrameSource constructor: (serial, interfaceIdOrMac, debugLog)
        // We use empty serial and MAC address as interface identifier
        return std::make_unique<VimbaXFrameSource>("", cameraInfo.macAddress, debugLog);
#else
        std::cerr << "[FrameSourceFactory] ERROR: VimbaX not enabled in build" << std::endl;
        return nullptr;
#endif
    }
    else if (cameraInfo.cameraType == "TYPE_CAM_USB") {
        std::cout << "[FrameSourceFactory]   Using OpenCVFrameSource" << std::endl;
        
        if (!cameraInfo.devicePath || cameraInfo.devicePath->empty()) {
            std::cerr << "[FrameSourceFactory] ERROR: TYPE_CAM_USB requires devicePath" << std::endl;
            return nullptr;
        }
        
        std::cout << "[FrameSourceFactory]   Device Path: " << *cameraInfo.devicePath << std::endl;
        
        // OpenCVFrameSource with device path (e.g., /dev/video4)
        return std::make_unique<OpenCVFrameSource>(*cameraInfo.devicePath);
    }
    else if (cameraInfo.cameraType == "TYPE_FILE") {
        std::cout << "[FrameSourceFactory]   TYPE_FILE should use createFileSource() or createVideoFileSource()" << std::endl;
        return nullptr;
    }
    else {
        std::cerr << "[FrameSourceFactory] ERROR: Unknown camera type: " << cameraInfo.cameraType << std::endl;
        return nullptr;
    }
}

std::unique_ptr<IFrameSource> FrameSourceFactory::createFileSource(
    const std::string& directory,
    const std::string& pattern)
{
    std::cout << "[FrameSourceFactory] Creating DiskImageFrameSource" << std::endl;
    std::cout << "[FrameSourceFactory]   Directory: " << directory << std::endl;
    std::cout << "[FrameSourceFactory]   Pattern: " << pattern << std::endl;
    
    return std::make_unique<DiskImageFrameSource>(directory, pattern);
}

std::unique_ptr<IFrameSource> FrameSourceFactory::createVideoFileSource(
    const std::string& videoFilePath)
{
    std::cout << "[FrameSourceFactory] Creating OpenCVFrameSource for video file" << std::endl;
    std::cout << "[FrameSourceFactory]   Video File: " << videoFilePath << std::endl;
    
    return std::make_unique<OpenCVFrameSource>(videoFilePath);
}

bool FrameSourceFactory::isSupportedCameraType(const std::string& cameraType)
{
    if (cameraType == "TYPE_CAM_VIMBA") {
#ifdef VIMBAX_ENABLED
        return true;
#else
        return false;
#endif
    }
    else if (cameraType == "TYPE_CAM_USB") {
        return true;  // OpenCV always available
    }
    else if (cameraType == "TYPE_FILE") {
        return true;  // File sources always available
    }
    
    return false;
}

} // namespace lbt
