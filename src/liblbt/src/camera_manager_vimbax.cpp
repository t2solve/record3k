#include <liblbt/camera_manager_vimbax.h>
#include <iostream>

#ifdef VIMBAX_ENABLED
#include <VmbCPP/VmbCPP.h>
#endif

namespace lbt {

VimbaXStatus CameraManagerVimbax::testVimbaxReadyToUse(const std::filesystem::path& ctiFilePath) {
    VimbaXStatus status;
    
#ifdef VIMBAX_ENABLED
    status.isBuilt = true;
    std::cout << "[CameraManagerVimbax] VimbaX is built into this binary" << std::endl;
    
    try {
        // Prepare transport layer file path
        std::string pathConfig;
        if (!ctiFilePath.empty()) {
            std::cout << "[CameraManagerVimbax] Received .cti file path: " << ctiFilePath << std::endl;
            std::filesystem::path absPath = std::filesystem::absolute(ctiFilePath);
            std::cout << "[CameraManagerVimbax] Absolute path: " << absPath << std::endl;
            std::cout << "[CameraManagerVimbax] Checking if file exists..." << std::endl;
            
            if (std::filesystem::exists(absPath)) {
                pathConfig = absPath.string();
                status.transportLayerPath = pathConfig;
                std::cout << "[CameraManagerVimbax] File exists! Using .cti file: " << pathConfig << std::endl;
            } else {
                std::cerr << "[CameraManagerVimbax] ERROR: Specified .cti file does not exist: " << absPath << std::endl;
                std::cerr << "[CameraManagerVimbax] Current working directory: " << std::filesystem::current_path() << std::endl;
                std::cerr << "[CameraManagerVimbax] Falling back to GENICAM_GENTL64_PATH" << std::endl;
            }
        } else {
            std::cout << "[CameraManagerVimbax] No .cti file specified, using GENICAM_GENTL64_PATH" << std::endl;
        }
        
        // Get VmbSystem singleton
        VmbCPP::VmbSystem& sys = VmbCPP::VmbSystem::GetInstance();
        std::cout << "[CameraManagerVimbax] VmbSystem singleton obtained" << std::endl;
        
        // Query version before startup
        VmbVersionInfo_t versionInfo;
        VmbErrorType err = sys.QueryVersion(versionInfo);
        if (err == VmbErrorSuccess) {
            status.versionMajor = versionInfo.major;
            status.versionMinor = versionInfo.minor;
            status.versionPatch = versionInfo.patch;
            std::cout << "[CameraManagerVimbax] VimbaX Version: " << versionInfo.major 
                      << "." << versionInfo.minor << "." << versionInfo.patch << std::endl;
        } else {
            std::cerr << "[CameraManagerVimbax] WARNING: Could not query VimbaX version, error: " << err << std::endl;
        }
        
        // Initialize VmbSystem with explicit transport layer file or use environment variable
        std::cout << "[CameraManagerVimbax] Calling VmbSystem::Startup()..." << std::endl;
        if (pathConfig.empty()) {
            err = sys.Startup();
        } else {
            err = sys.Startup(pathConfig.c_str());
        }
        
        if (err != VmbErrorSuccess) {
            status.errorMessage = "VmbSystem::Startup() failed with error code: " + std::to_string(err);
            std::cerr << "[CameraManagerVimbax] ERROR: " << status.errorMessage << std::endl;
            return status;
        }
        
        status.startupSuccess = true;
        std::cout << "[CameraManagerVimbax] VmbSystem::Startup() succeeded" << std::endl;
        
        // Get transport layers
        VmbCPP::TransportLayerPtrVector transportLayers;
        err = sys.GetTransportLayers(transportLayers);
        if (err == VmbErrorSuccess) {
            status.transportLayerCount = transportLayers.size();
            std::cout << "[CameraManagerVimbax] Found " << status.transportLayerCount << " transport layer(s)" << std::endl;
            
            // Print transport layer details
            for (const auto& tl : transportLayers) {
                std::string tlID, tlPath, tlType;
                if (tl->GetID(tlID) == VmbErrorSuccess &&
                    tl->GetPath(tlPath) == VmbErrorSuccess) {
                    // Try to get transport layer type
                    VmbCPP::FeaturePtr typeFeature;
                    if (tl->GetFeatureByName("TLType", typeFeature) == VmbErrorSuccess) {
                        typeFeature->GetValue(tlType);
                    }
                    std::cout << "[CameraManagerVimbax]   - Transport Layer: " << tlID 
                              << " (Type: " << tlType << ", Path: " << tlPath << ")" << std::endl;
                }
            }
        } else {
            std::cerr << "[CameraManagerVimbax] WARNING: Could not get transport layers, error: " << err << std::endl;
        }
        
        // Get interfaces
        VmbCPP::InterfacePtrVector interfaces;
        err = sys.GetInterfaces(interfaces);
        if (err == VmbErrorSuccess) {
            status.interfaceCount = interfaces.size();
            std::cout << "[CameraManagerVimbax] Found " << status.interfaceCount << " interface(s)" << std::endl;
            
            // Print interface details
            for (const auto& iface : interfaces) {
                std::string ifaceID, ifaceType;
                if (iface->GetID(ifaceID) == VmbErrorSuccess) {
                    // Try to get interface type
                    VmbCPP::FeaturePtr typeFeature;
                    if (iface->GetFeatureByName("InterfaceType", typeFeature) == VmbErrorSuccess) {
                        typeFeature->GetValue(ifaceType);
                    }
                    std::cout << "[CameraManagerVimbax]   - Interface: " << ifaceID 
                              << " (Type: " << ifaceType << ")" << std::endl;
                }
            }
        } else {
            std::cerr << "[CameraManagerVimbax] WARNING: Could not get interfaces, error: " << err << std::endl;
        }
        
        // Get cameras
        VmbCPP::CameraPtrVector cameras;
        err = sys.GetCameras(cameras);
        if (err == VmbErrorSuccess) {
            status.cameraCount = cameras.size();
            std::cout << "[CameraManagerVimbax] Found " << status.cameraCount << " camera(s)" << std::endl;
            
            // Print camera details
            for (const auto& cam : cameras) {
                std::string camID, camName, camSerial, camModel;
                cam->GetID(camID);
                cam->GetName(camName);
                cam->GetSerialNumber(camSerial);
                cam->GetModel(camModel);
                std::cout << "[CameraManagerVimbax]   - Camera: " << camName 
                          << " (Model: " << camModel << ", ID: " << camID << ", S/N: " << camSerial << ")" << std::endl;
            }
        } else {
            std::cerr << "[CameraManagerVimbax] WARNING: Could not get cameras, error: " << err << std::endl;
        }
        
        // Shutdown VmbSystem
        std::cout << "[CameraManagerVimbax] Calling VmbSystem::Shutdown()..." << std::endl;
        sys.Shutdown();
        std::cout << "[CameraManagerVimbax] VimbaX test completed successfully" << std::endl;
        
    } catch (const std::exception& e) {
        status.errorMessage = std::string("Exception during VimbaX test: ") + e.what();
        std::cerr << "[CameraManagerVimbax] ERROR: " << status.errorMessage << std::endl;
        status.startupSuccess = false;
    } catch (...) {
        status.errorMessage = "Unknown exception during VimbaX test";
        std::cerr << "[CameraManagerVimbax] ERROR: " << status.errorMessage << std::endl;
        status.startupSuccess = false;
    }
    
#else
    status.isBuilt = false;
    status.errorMessage = "VimbaX not enabled in build (VIMBAX_ENABLED not defined)";
    std::cerr << "[CameraManagerVimbax] " << status.errorMessage << std::endl;
#endif
    
    return status;
}

} // namespace lbt
