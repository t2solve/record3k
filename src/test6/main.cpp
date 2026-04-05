
// test6 — Load camera configs from JSON, start VimbaX without broadcast autodiscovery,
// then connect directly to each TYPE_CAM_VIMBA camera.
//
// Discovery strategy (in order of preference):
//   1. Unicast GVCP injection via VmbC interface features (requires cam.ip in JSON):
//      Writes DeviceIPAddress on each GEV interface, runs DeviceUpdateList (unicast
//      DISCOVERY_CMD), reads back the camera's GenTL ID — no broadcast needed.
//      Falls back to bare-IP open if the TL doesn't expose DeviceIPAddress (older TL).
//   2. MAC-based direct open: DEV_<MAC_without_colons_uppercase> (fallback when no IP
//      is configured, or when unicast discovery finds nothing).
//
// Usage: test6con [--cameras-dir <path>] [--cti <path>] [--camera <camUID>]
//   --cameras-dir  Path to directory with camera JSON files (default: data/cameras)
//   --cti          Path to VimbaX transport layer .cti file (optional)
//   --camera       Connect only to this specific camUID (optional, default: all VIMBA cameras)

#include <QCoreApplication>
#include <QDebug>

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cctype>

#include <liblbt/camera_manager.h>
#include <liblbt/api/api_models.h>

#ifdef VIMBAX_ENABLED
#include <VmbCPP/VmbCPP.h>
#include <VmbCPP/Frame.h>
#include <VmbC/VmbC.h>   // C API — used for interface-level unicast injection
#include <arpa/inet.h>
#include <time.h>
#endif

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Build VimbaX camera ID from MAC address string.
// "00:0A:47:08:ED:EF"  ->  "DEV_000A4708EDEF"
static std::string macToCameraId(const std::string& mac) {
    std::string out = "DEV_";
    for (char c : mac) {
        if (c != ':' && c != '-')
            out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Argument parsing
// ---------------------------------------------------------------------------
struct Args {
    std::string camerasDir = "data/cameras";
    std::string ctiPath;
    std::string targetCamUID; // empty = process all
};

static Args parseArgs(int argc, char* argv[]) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--cameras-dir") && (i + 1 < argc)) {
            a.camerasDir = argv[++i];
        } else if ((arg == "--cti") && (i + 1 < argc)) {
            a.ctiPath = argv[++i];
        } else if ((arg == "--camera") && (i + 1 < argc)) {
            a.targetCamUID = argv[++i];
        } else {
            std::cerr << "[test6] Unknown argument: " << arg << std::endl;
        }
    }
    return a;
}

// ---------------------------------------------------------------------------
// Unicast GVCP discovery helpers (adapted from UnicastDiscover.c, VmbC API)
// Called after VmbSystem::Startup() — VmbCPP wraps VmbC so all VmbC calls
// work once the system is running.
// ---------------------------------------------------------------------------
#ifdef VIMBAX_ENABLED

static unsigned int ipStringToUint32(const std::string& ipStr) {
    struct in_addr addr;
    if (inet_pton(AF_INET, ipStr.c_str(), &addr) != 1) return INADDR_NONE;
    return ntohl(addr.s_addr);
}

static void sleep200ms() {
    struct timespec ts = { 0, 200000000L };
    nanosleep(&ts, nullptr);
}

// Write DeviceIPAddress on the GEV interface and run DeviceUpdateList so the
// transport layer sends a unicast GVCP DISCOVERY_CMD to the given IP.
// Returns VmbErrorNotFound if the TL doesn't expose the writable feature (older TL).
static VmbError_t injectUnicastTarget(VmbHandle_t ifaceHandle, unsigned int cameraIpHostOrder)
{
    VmbError_t err = VmbFeatureIntSet(ifaceHandle, "DeviceIPAddress",
                                      static_cast<VmbInt64_t>(cameraIpHostOrder));
    if (err != VmbErrorSuccess) {
        std::cout << "    [unicast] DeviceIPAddress not writable (err=" << err
                  << ") – older TL, will try bare-IP open\n";
        return VmbErrorNotFound;
    }
    std::printf("    [unicast] Wrote DeviceIPAddress = 0x%08X\n", cameraIpHostOrder);

    err = VmbFeatureCommandRun(ifaceHandle, "DeviceUpdateList");
    if (err != VmbErrorSuccess) {
        std::cout << "    [unicast] DeviceUpdateList failed (err=" << err << ")\n";
        return err;
    }

    // Poll until the command is done (usually nearly instant)
    VmbBool_t done = VmbBoolFalse;
    int retries = 20;
    do {
        VmbFeatureCommandIsDone(ifaceHandle, "DeviceUpdateList", &done);
        if (!done) sleep200ms();
    } while (!done && --retries > 0);

    std::cout << "    [unicast] DeviceUpdateList complete\n";
    return VmbErrorSuccess;
}

// After DeviceUpdateList, iterate the interface device list to find the camera
// whose IP matches. Returns the GenTL camera ID string, or empty on failure.
static std::string findCameraIdOnInterface(VmbHandle_t ifaceHandle, unsigned int cameraIpHostOrder)
{
    VmbInt64_t devMin = 0, devMax = -1;
    VmbError_t err = VmbFeatureIntRangeQuery(ifaceHandle, "DeviceSelector", &devMin, &devMax);
    if (err != VmbErrorSuccess || devMax < 0) return {};

    for (VmbInt64_t idx = devMin; idx <= devMax; ++idx) {
        if (VmbFeatureIntSet(ifaceHandle, "DeviceSelector", idx) != VmbErrorSuccess) continue;

        VmbInt64_t devIp = 0;
        if (VmbFeatureIntGet(ifaceHandle, "GevDeviceIPAddress", &devIp) != VmbErrorSuccess) continue;

        if (static_cast<unsigned int>(devIp) == cameraIpHostOrder) {
            VmbUint32_t idLen = 0;
            VmbFeatureStringGet(ifaceHandle, "DeviceID", nullptr, 0, &idLen);
            if (idLen == 0) return {};

            std::string id(idLen, '\0');
            err = VmbFeatureStringGet(ifaceHandle, "DeviceID", id.data(), idLen, &idLen);
            if (err == VmbErrorSuccess) {
                // Strip any embedded null terminator the TL may have included
                while (!id.empty() && id.back() == '\0') id.pop_back();
                std::cout << "    [unicast] Found camera on interface. CameraID=\"" << id << "\"\n";
                return id;
            }
        }
    }
    return {};
}

// Top-level unicast discovery: probe each GEV interface in turn.
// Returns the GenTL camera ID to pass to OpenCameraByID, or empty on failure.
static std::string unicastDiscoverCameraId(const std::string& cameraIpStr)
{
    const unsigned int cameraIp = ipStringToUint32(cameraIpStr);
    if (cameraIp == INADDR_NONE) {
        std::cerr << "    [unicast] Invalid IP string: " << cameraIpStr << "\n";
        return {};
    }

    VmbUint32_t ifaceCount = 0;
    VmbError_t err = VmbInterfacesList(nullptr, 0, &ifaceCount, sizeof(VmbInterfaceInfo_t));
    if ((err != VmbErrorSuccess && err != VmbErrorMoreData) || ifaceCount == 0) {
        std::cerr << "    [unicast] No interfaces found (err=" << err << ")\n";
        return {};
    }

    std::vector<VmbInterfaceInfo_t> ifaces(ifaceCount);
    err = VmbInterfacesList(ifaces.data(), ifaceCount, &ifaceCount, sizeof(VmbInterfaceInfo_t));
    if (err != VmbErrorSuccess) {
        std::cerr << "    [unicast] VmbInterfacesList failed (err=" << err << ")\n";
        return {};
    }

    for (const auto& iface : ifaces) {
        if (iface.interfaceType != VmbTransportLayerTypeGEV) continue;

        std::cout << "    [unicast] Trying GEV interface \"" << iface.interfaceName << "\"\n";

        VmbError_t injErr = injectUnicastTarget(iface.interfaceHandle, cameraIp);

        if (injErr == VmbErrorNotFound) {
            // Older TL without writable DeviceIPAddress — cannot do unicast injection.
            // Return empty; the caller will try bare-IP and MAC-based IDs separately.
            std::cout << "    [unicast] TL too old for unicast injection – skipping this interface\n";
            continue;
        }

        if (injErr != VmbErrorSuccess) continue;

        std::string id = findCameraIdOnInterface(iface.interfaceHandle, cameraIp);
        if (!id.empty()) return id;

        std::cout << "    [unicast] Camera not responding on this interface\n";
    }

    return {};
}

#endif // VIMBAX_ENABLED

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    Args args = parseArgs(argc, argv);

    // ------------------------------------------------------------------
    // 1. Load all camera infos from JSON directory
    // ------------------------------------------------------------------
    std::cout << "\n=== [test6] Loading camera configs from: " << args.camerasDir << " ===" << std::endl;

    if (!std::filesystem::exists(args.camerasDir)) {
        std::cerr << "[test6] ERROR: cameras directory does not exist: " << args.camerasDir << std::endl;
        return 1;
    }

    lbt::CameraManager camMgr(args.camerasDir);
    camMgr.load();
    auto cameras = camMgr.cameras();

    std::cout << "[test6] Loaded " << cameras.size() << " camera(s):" << std::endl;
    for (const auto& cam : cameras) {
        std::cout << "  [" << cam.cameraType << "] " << cam.camUID;
        if (cam.serialNumber && !cam.serialNumber->empty())
            std::cout << "  serial=" << *cam.serialNumber;
        if (!cam.macAddress.empty() && cam.macAddress != "N/A")
            std::cout << "  mac=" << cam.macAddress;
        if (cam.ip && !cam.ip->empty())
            std::cout << "  ip=" << *cam.ip;
        if (cam.description && !cam.description->empty())
            std::cout << "  desc=\"" << *cam.description << "\"";
        std::cout << std::endl;
    }

#ifdef VIMBAX_ENABLED
    // ------------------------------------------------------------------
    // 2. Start VimbaX system directly — skip broadcast autodiscovery
    //    (We do NOT go through CameraManagerVimbax::startup() to avoid
    //     the 1-second GigE broadcast wait and background discovery thread.)
    // ------------------------------------------------------------------
    auto& vmb = VmbCPP::VmbSystem::GetInstance();

    std::cout << "\n=== [test6] Starting VimbaX (no autodiscovery) ===" << std::endl;

    VmbVersionInfo_t ver{};
    if (vmb.QueryVersion(ver) == VmbErrorSuccess) {
        std::cout << "[test6] VimbaX version: " << ver.major << "." << ver.minor << "." << ver.patch << std::endl;
    }

    VmbErrorType err;
    if (args.ctiPath.empty()) {
        err = vmb.Startup();
    } else {
        std::cout << "[test6] Using CTI: " << args.ctiPath << std::endl;
        err = vmb.Startup(args.ctiPath.c_str());
    }

    if (err != VmbErrorSuccess) {
        std::cerr << "[test6] ERROR: VmbSystem::Startup() failed, err=" << err << std::endl;
        return 1;
    }
    std::cout << "[test6] VimbaX started successfully" << std::endl;

    // ------------------------------------------------------------------
    // 3. Connect to TYPE_CAM_VIMBA cameras
    //    Strategy:
    //      a) cam.ip present → unicast GVCP injection on each GEV interface
    //                          → retrieve GenTL camera ID from device list
    //         (if TL too old for writable DeviceIPAddress → bare-IP fallback)
    //      b) no IP or unicast failed → MAC-based ID  DEV_<MAC>
    // ------------------------------------------------------------------
    std::cout << "\n=== [test6] Connecting to camera(s) ===" << std::endl;

    int attempted = 0;
    int connected = 0;

    for (const auto& cam : cameras) {
        if (cam.cameraType != "TYPE_CAM_VIMBA") continue;
        if (!args.targetCamUID.empty() && cam.camUID != args.targetCamUID) continue;

        ++attempted;

        std::cout << "\n--- [test6] Camera: " << cam.camUID;
        if (!cam.macAddress.empty() && cam.macAddress != "N/A")
            std::cout << "  mac='" << cam.macAddress << "'";
        if (cam.ip && !cam.ip->empty())
            std::cout << "  ip='" << *cam.ip << "'";
        std::cout << std::endl;

        // Build ordered candidate list: unicast GenTL ID → bare-IP → MAC
        // Try each in turn and use the first one that opens successfully.
        using Candidate = std::pair<std::string, std::string>; // {id, label}
        std::vector<Candidate> candidates;

        if (cam.ip && !cam.ip->empty()) {
            std::cout << "  [test6] Unicast GVCP discovery for IP " << *cam.ip << std::endl;
            std::string genTlId = unicastDiscoverCameraId(*cam.ip);
            if (!genTlId.empty()) {
                std::cout << "  [test6] Unicast found GenTL ID: '" << genTlId << "'" << std::endl;
                candidates.push_back({genTlId, "unicast"});
            } else {
                std::cout << "  [test6] Unicast injection not available on this TL" << std::endl;
            }
            // Bare-IP: the AVT GigE TL may accept this on some versions
            if (candidates.empty() || candidates.front().first != *cam.ip)
                candidates.push_back({*cam.ip, "bare-IP"});
        }

        // MAC-based ID — always add as final fallback
        std::string macId = macToCameraId(cam.macAddress);
        if (std::find_if(candidates.begin(), candidates.end(),
                         [&](const Candidate& c){ return c.first == macId; }) == candidates.end())
            candidates.push_back({macId, "MAC"});

        VmbCPP::CameraPtr camera;
        bool opened = false;
        std::string usedLabel;
        for (const auto& [id, label] : candidates) {
            std::cout << "  [test6] Trying " << label << " id='" << id << "'" << std::endl;
            VmbErrorType openErr = vmb.OpenCameraByID(id.c_str(), VmbAccessModeFull, camera);
            if (openErr == VmbErrorSuccess) {
                opened = true;
                usedLabel = label;
                break;
            }
            std::cerr << "  [test6] Failed (err=" << openErr << ")" << std::endl;
        }

        if (!opened) {
            std::cerr << "  [test6] All candidates exhausted for " << cam.camUID
                      << " — camera unreachable" << std::endl;
            continue;
        }

        ++connected;
        std::cout << "  [test6] CONNECTED to " << cam.camUID
                  << " (via " << usedLabel << ")" << std::endl;

        // Grab one frame
        VmbUint32_t payloadSize = 0;
        VmbErrorType frameErr = camera->GetPayloadSize(payloadSize);
        if (frameErr != VmbErrorSuccess || payloadSize == 0) {
            std::cerr << "  [test6] WARNING: GetPayloadSize failed err=" << frameErr << std::endl;
        } else {
            VmbCPP::FramePtr frame(new VmbCPP::Frame(payloadSize));
            frameErr = camera->AcquireSingleImage(frame, /*timeoutMs=*/2000,
                          VmbCPP::FrameAllocationMode::FrameAllocation_AllocAndAnnounceFrame);
            if (frameErr == VmbErrorSuccess) {
                VmbUint32_t w = 0, h = 0;
                frame->GetWidth(w);
                frame->GetHeight(h);
                std::cout << "  [test6] Frame acquired: " << w << "x" << h << std::endl;
            } else {
                std::cerr << "  [test6] WARNING: AcquireSingleImage failed err=" << frameErr << std::endl;
            }
        }

        camera->Close();
        std::cout << "  [test6] Camera closed" << std::endl;
    }

    vmb.Shutdown();
    std::cout << "\n=== [test6] VimbaX shutdown ===" << std::endl;

    std::cout << "\n=== [test6] Done — attempted=" << attempted
              << "  connected=" << connected << " ===" << std::endl;
#else
    std::cout << "\n[test6] VimbaX not enabled in this build — "
                 "cannot connect to TYPE_CAM_VIMBA cameras." << std::endl;
    std::cout << "[test6] Rebuild with CONFIG+=vimbax to enable." << std::endl;
#endif

    return 0;
}
