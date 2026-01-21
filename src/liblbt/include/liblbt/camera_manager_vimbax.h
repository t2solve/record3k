#pragma once

#include <string>
#include <filesystem>
#include <vector>
#include <memory>

namespace lbt {

// Struct to hold VimbaX system status information
struct VimbaXStatus {
    bool isBuilt = false;              // Is VimbaX enabled in build?
    bool startupSuccess = false;       // Did VmbSystem::Startup() succeed?
    int versionMajor = 0;
    int versionMinor = 0;
    int versionPatch = 0;
    size_t transportLayerCount = 0;    // Number of transport layers found
    size_t interfaceCount = 0;         // Number of interfaces found
    size_t cameraCount = 0;            // Number of cameras found
    std::string errorMessage;          // Error description if any
    std::string transportLayerPath;    // Path to transport layer files used
};

// Information about a detected camera
struct VimbaXCameraInfo {
    std::string id;           // Camera ID
    std::string name;         // Camera name
    std::string serial;       // Serial number
    std::string model;        // Model name
    std::string macAddress;   // MAC address (for GigE cameras)
    std::string interfaceId;  // Interface ID
};

// Singleton VimbaX system manager
// Keeps the VimbaX system initialized throughout the application lifetime
class CameraManagerVimbax {
public:
    // Get singleton instance
    static CameraManagerVimbax& getInstance();
    
    // Initialize the VimbaX system (call once at application startup)
    bool startup(const std::filesystem::path& ctiFilePath = "");
    
    // Shutdown the VimbaX system (call once at application shutdown)
    void shutdown();
    
    // Check if the system is initialized and ready
    bool isReady() const;
    
    // Get system status information
    VimbaXStatus getStatus() const;
    
    // Get list of currently detected cameras (may change over time with network discovery)
    std::vector<VimbaXCameraInfo> getCameras();
    
    // Update camera list (triggers a new query)
    void updateCameraList();
    
    // Check if initial discovery is complete
    bool isDiscoveryComplete() const;
    
    // Prevent copying
    CameraManagerVimbax(const CameraManagerVimbax&) = delete;
    CameraManagerVimbax& operator=(const CameraManagerVimbax&) = delete;

private:
    CameraManagerVimbax() = default;
    ~CameraManagerVimbax();
    
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace lbt
