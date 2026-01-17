#pragma once

#include <string>
#include <filesystem>

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

// VimbaX-specific camera manager functionality
class CameraManagerVimbax {
public:
    /**
     * \brief Check if VimbaX is ready to use (built and initialized)
     * 
     * This method will:
     * 1. Check if VimbaX is built into the binary
     * 2. Initialize VmbSystem with the specified transport layer file
     * 3. Query version information
     * 4. Enumerate transport layers, interfaces, and cameras
     * 5. Properly shutdown VmbSystem
     * 
     * \param[in] ctiFilePath Path to a specific .cti file.
     *                        If empty, uses GENICAM_GENTL64_PATH environment variable.
     * 
     * \returns VimbaXStatus containing detailed information about VimbaX system state
     */
    static VimbaXStatus testVimbaxReadyToUse(const std::filesystem::path& ctiFilePath = "");
};

} // namespace lbt
