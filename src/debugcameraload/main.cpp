
#ifdef VIMBAX_ENABLED
#include <VmbCPP/VmbCPP.h>
#include <VmbCPP/Frame.h>
#include <VmbCPP/Camera.h>
#include <VmbCPP/Feature.h>
#include <VmbCPP/Interface.h>
#include <VmbCPP/IFrameObserver.h>
#endif

#include <QCoreApplication>
#include <QTimer>
#include <QDebug>
#include <QObject>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/opencv.hpp>

#include <thread>
#include <optional>
#include <cstdlib>

#include <liblbt/process_step.h>
#include <liblbt/process_step_factory.h>
#include <liblbt/processing_pipeline.h>
#include <liblbt/frame_memory_object.h>
#include <liblbt/pipeline_profiler.h>
#include <liblbt/pipeline_config_loader.h>
#include <liblbt/disk_image_frame_source.h>
#include <liblbt/vimbax_frame_source.h>
#include <liblbt/camera_manager.h>
#include <filesystem>
#include <iostream>
#include <cctype>


#ifdef VIMBAX_ENABLED
std::vector<std::tuple<std::string, std::string>> ListCameraSerialAndInterface()
{
    std::vector<std::tuple<std::string, std::string>> result;

    VmbCPP::VmbSystem& sys = VmbCPP::VmbSystem::GetInstance();
    VmbErrorType err = sys.Startup();
    if (err != VmbErrorSuccess) {
        std::cerr << "[vimba] Startup failed: " << err << "\n";
        return result;
    }

    VmbCPP::CameraPtrVector cameras;
    err = sys.GetCameras(cameras);
    if (err == VmbErrorSuccess)
    {
        std::cout << "[vimba] cameras found: " << cameras.size() << "\n";
        for (const auto& camera : cameras)
        {
            std::string serial, interfaceID;
            if (camera->GetSerialNumber(serial) != VmbErrorSuccess)
                serial = "";
            if (camera->GetInterfaceID(interfaceID) != VmbErrorSuccess)
                interfaceID = "";
            result.emplace_back(serial, interfaceID);
        }
    } else {
        std::cerr << "[vimba] GetCameras failed: " << err << "\n";
    }

    sys.Shutdown();
    return result;
}

static void PrintVimbaSetupDiagnostics()
{
    // 1) Environment: GENICAM_GENTL64_PATH
    const char* gentl = std::getenv("GENICAM_GENTL64_PATH");
    std::cout << "[vimba] GENICAM_GENTL64_PATH=" << (gentl ? gentl : "<unset>") << "\n";
    // Try to list .cti files from the path segments and from local repo folders
    auto listCti = [](const std::filesystem::path& dir){
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec)) return;
        size_t count=0;
        for (auto& e : std::filesystem::directory_iterator(dir, ec)) {
            if (e.is_regular_file() && e.path().extension()==".cti") {
                std::cout << "  - CTI: " << e.path() << "\n";
                ++count;
            }
        }
        if (count==0) std::cout << "  (no .cti files in " << dir << ")\n";
    };
    if (gentl) {
        std::stringstream ss(gentl);
        std::string seg;
        while (std::getline(ss, seg, ':')) {
            if (!seg.empty()) listCti(seg);
        }
    }
    // Also check project-local paths
    for (auto p : { std::filesystem::path("vimbax/cti"), std::filesystem::path("../vimbax/cti"), std::filesystem::path("../../vimbax/cti") }) {
        if (std::filesystem::exists(p)) {
            std::cout << "[vimba] Checking local CTI dir: " << p << "\n";
            listCti(p);
        }
    }

    // 2) Vimba system details
    VmbCPP::VmbSystem& sys = VmbCPP::VmbSystem::GetInstance();
    VmbVersionInfo_t versionInfo{};
    sys.QueryVersion(versionInfo);
    std::cout << "[vimba] Version: " << versionInfo.major << '.' << versionInfo.minor << '.' << versionInfo.patch << "\n";

    VmbErrorType err = sys.Startup();
    if (err != VmbErrorSuccess) {
        std::cerr << "[vimba] Startup failed: " << err << "\n";
        return;
    }

    VmbCPP::TransportLayerPtrVector tls;
    err = sys.GetTransportLayers(tls);
    if (err == VmbErrorSuccess) {
        std::cout << "[vimba] TransportLayers: " << tls.size() << "\n";
        for (auto& tl : tls) {
            std::string id, path;
            if (tl->GetID(id) != VmbErrorSuccess) id = "<id-error>";
            if (tl->GetPath(path) != VmbErrorSuccess) path = "<path-error>";
            std::cout << "  - TL id=" << id << ", path=" << path << "\n";
        }
    } else {
        std::cerr << "[vimba] GetTransportLayers failed: " << err << "\n";
    }

    VmbCPP::InterfacePtrVector ifaces;
    err = sys.GetInterfaces(ifaces);
    if (err == VmbErrorSuccess) {
        std::cout << "[vimba] Interfaces: " << ifaces.size() << "\n";
        for (auto& iface : ifaces) {
            std::string iid;
            if (iface->GetID(iid) != VmbErrorSuccess) iid = "<iface-id-error>";
            std::cout << "  - IFACE id=" << iid << "\n";
        }
    } else {
        std::cerr << "[vimba] GetInterfaces failed: " << err << "\n";
    }

    VmbCPP::CameraPtrVector cameras;
    err = sys.GetCameras(cameras);
    if (err == VmbErrorSuccess) {
        std::cout << "[vimba] Cameras: " << cameras.size() << "\n";
        for (const auto& cam : cameras) {
            std::string id, name, model, serial, ifaceId;
            if (cam->GetID(id) != VmbErrorSuccess) id = "<id-error>";
            if (cam->GetName(name) != VmbErrorSuccess) name = "<name-error>";
            if (cam->GetModel(model) != VmbErrorSuccess) model = "<model-error>";
            if (cam->GetSerialNumber(serial) != VmbErrorSuccess) serial = "<serial-error>";
            if (cam->GetInterfaceID(ifaceId) != VmbErrorSuccess) ifaceId = "<iface-id-error>";
            std::cout << "  - ID=" << id << ", Name=" << name << ", Model=" << model
                      << ", Serial=" << serial << ", IFACE=" << ifaceId << "\n";
        }
    } else {
        std::cerr << "[vimba] GetCameras failed: " << err << "\n";
    }

    sys.Shutdown();
}
#else
std::vector<std::tuple<std::string, std::string>> ListCameraSerialAndInterface()
{
    // VimbaX not enabled; return empty list
    return {};
}
#endif

int main(int argc, char* argv[])
{
      // Defaults: image directory and calibration file
    std::string image_dir = "build/bin/data-frames";
    std::string calib_xml = "build/bin/camera_calibration.xml";
    // Positional overrides if provided
    if (argc >= 2) image_dir = argv[1];
    if (argc >= 3) calib_xml = argv[2];
    if (argc == 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
        std::cerr << "Usage: " << argv[0] << " [image_directory] [calibration_xml]" << std::endl;
        std::cerr << "Defaults: image_directory='" << image_dir << "', calibration_xml='" << calib_xml << "'\n";
        return 0;
    }

    // Report VIMBAX availability
#ifdef VIMBAX_ENABLED
    std::cout << "[info] VIMBAX_ENABLED: yes\n";
#else
    std::cout << "[info] VIMBAX_ENABLED: no (build without vimbax; camera enumeration disabled)\n";
#endif

    //we list all connected cameras
    auto cams = ListCameraSerialAndInterface();
    std::cout << "Connected cameras:\n";
    for (const auto& [serial, interfaceID] : cams) {
        std::cout << " - Serial: " << serial << ", Interface: " << interfaceID << "\n";
    }

#ifdef VIMBAX_ENABLED
    if (cams.empty()) {
        std::cout << "[vimba] No cameras reported; running setup diagnostics...\n";
        PrintVimbaSetupDiagnostics();
    }
#endif

    // Use CameraManager to load and probe cameras from data/info/cameras
    std::filesystem::path camerasDir = "data/info/cameras";
    if (argc >= 4) {
        camerasDir = argv[3];
    } else {
        // Try a few sensible fallbacks relative to CWD and executable
        auto tryPaths = std::vector<std::filesystem::path>{
            std::filesystem::path("data/info/cameras"),
            std::filesystem::path("../data/info/cameras"),
            std::filesystem::path("../../data/info/cameras")
        };
        for (const auto& p : tryPaths) {
            if (std::filesystem::exists(p)) { camerasDir = p; break; }
        }
    }
    std::cout << "CameraManager: using cameras dir: " << camerasDir << "\n";
    try {
        lbt::CameraManager cm(camerasDir);
        cm.load();
        cm.probeAll();
        auto infos = cm.cameras();
        std::cout << "CameraManager cameras (" << infos.size() << "):\n";
        for (const auto& c : infos) {
            std::cout << " - camUID=" << c.camUID
                      << ", mac=" << c.macAddress
                      << ", type=" << c.cameraType
                      << ", status=" << c.status
                      << ", lastSeen=" << c.datetimeLastSeen
                      << "\n";
        }

#ifdef VIMBAX_ENABLED
        // Simple heuristic comparison by camUID vs Vimba serial and MAC-ish matching
        auto normalize = [](std::string s){
            std::string out; out.reserve(s.size());
            for (char c : s) if (std::isxdigit(static_cast<unsigned char>(c))) out.push_back(std::toupper(static_cast<unsigned char>(c)));
            return out;
        };
        for (const auto& c : infos) {
            bool matched = false;
            for (const auto& [serial, interfaceID] : cams) {
                if (!serial.empty() && (serial == c.camUID)) {
                    std::cout << "Match: camUID=" << c.camUID << " == serial=" << serial
                              << " (iface=" << interfaceID << ")\n";
                    matched = true;
                    break;
                }
                // Try to match by MAC-like pattern (strip separators)
                if (!c.macAddress.empty()) {
                    auto macN = normalize(c.macAddress);
                    if (!serial.empty() && normalize(serial).find(macN) != std::string::npos) { matched = true; }
                    else if (!interfaceID.empty() && normalize(interfaceID).find(macN) != std::string::npos) { matched = true; }
                    if (matched) {
                        std::cout << "Match by MAC: mac=" << c.macAddress
                                  << " ~ serial=" << serial << ", iface=" << interfaceID << "\n";
                        break;
                    }
                }
            }
            if (!matched) {
                std::cout << "No Vimba match for camUID=" << c.camUID << " (type=" << c.cameraType << ")\n";
            }
        }
#endif
    } catch (const std::exception& ex) {
        std::cerr << "CameraManager error: " << ex.what() << "\n";
    }
    //we select the first camera if any
    std::shared_ptr<IFrameSource> frameSource;
    if (!cams.empty()) {
        const auto& [serial, interfaceID] = cams.front();
        frameSource = std::make_shared<VimbaXFrameSource>(serial, interfaceID, true);
    }
    else {
        std::cout << "No cameras found; will try disk image frame source instead.\n";
        if (!std::filesystem::exists(image_dir)) {
            std::cerr << "[error] Disk image directory not found: " << image_dir << "\n"
                      << "        Pass an existing directory as first argument, e.g. ./debugcameraload ./data/files/bin/cam-001\n";
            return 2;
        }
        frameSource = std::make_shared<DiskImageFrameSource>(image_dir, "*.jpg");
    }


    return 0;
}

