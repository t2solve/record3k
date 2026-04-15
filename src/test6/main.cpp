
// test6 — Load camera configs from JSON, start VimbaX without broadcast autodiscovery,
// then connect directly to each TYPE_CAM_VIMBA camera.
//
// Discovery strategy (in order of preference):
//   1. Unicast GVCP DISCOVERY_CMD via raw UDP (requires cam.ip in JSON):
//      Sends a GVCP DISCOVERY_CMD to cam.ip:3956, retries up to 5 times, parses the
//      DISCOVERY_ACK to extract the camera MAC → GenTL ID "DEV_<MAC>".
//      Falls back to bare-IP and MAC-based IDs if no ACK is received.
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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <array>
#include <sstream>
#include <thread>
#include <chrono>
#include <map>

#include "VimbaXUnicastDiscovery.h"

#include <liblbt/camera_manager.h>
#include <liblbt/api/api_models.h>

#ifdef VIMBAX_ENABLED
#include <VmbCPP/VmbCPP.h>
#include <VmbCPP/Frame.h>
#include <VmbC/VmbC.h>
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

#ifdef VIMBAX_ENABLED
// ---------------------------------------------------------------------------
// VimbaX cross-subnet registration helpers (VmbCPP API)
//
// When the host machine is on a different subnet from the cameras, VimbaX's
// own GigE broadcast discovery (executed during Startup()) is blocked by the
// router.  The cameras never appear in VimbaX's internal device list, so every
// subsequent OpenCameraByID() call returns VmbErrorNotFound (-3).
//
// Fix: after Startup() succeeds, write the target IP into the
// "DeviceIPAddress" feature of every GigE TL interface and run
// "DeviceUpdateList".  That causes the TL to send a directed unicast
// GVCP DISCOVERY_CMD directly to the camera and register the reply.
//
// Notes:
//  - The VimbaX TL owns UDP port 3956 once Startup() has run.  Never open
//    our own POSIX socket to port 3956 concurrently (it will block recv()).
//  - Injection is idempotent: repeating it for an already-known camera is safe.
//  - Non-GigE interfaces silently return false from injectIpIntoInterface()
//    because they do not expose the DeviceIPAddress feature.
// ---------------------------------------------------------------------------

// Convert a dotted-decimal IP string to a host-order 32-bit integer.
static unsigned int ipStringToUint32(const std::string& ipStr)
{
    struct in_addr addr{};
    if (::inet_pton(AF_INET, ipStr.c_str(), &addr) != 1) return 0u;
    return ntohl(addr.s_addr);
}

// Inject one IP into one VmbCPP Interface.
// Returns true when the interface supported DeviceIPAddress + DeviceUpdateList.
static bool injectIpIntoInterface(VmbCPP::InterfacePtr& iface,
                                  unsigned int          ipHostOrder,
                                  const std::string&    ifId)
{
    VmbCPP::FeaturePtr ipFeat;
    if (iface->GetFeatureByName("DeviceIPAddress", ipFeat) != VmbErrorSuccess)
        return false;   // not a GigE TL interface — silently skip

    if (ipFeat->SetValue(static_cast<VmbInt64_t>(ipHostOrder)) != VmbErrorSuccess) {
        std::cerr << "    [inject] DeviceIPAddress SetValue failed on " << ifId << "\n";
        return false;
    }

    VmbCPP::FeaturePtr cmdFeat;
    if (iface->GetFeatureByName("DeviceUpdateList", cmdFeat) != VmbErrorSuccess) {
        std::cerr << "    [inject] DeviceUpdateList feature not found on " << ifId << "\n";
        return false;
    }
    if (cmdFeat->RunCommand() != VmbErrorSuccess) {
        std::cerr << "    [inject] DeviceUpdateList RunCommand failed on " << ifId << "\n";
        return false;
    }

    // Poll until the directed discovery completes — max 3 s
    for (int ms = 0; ms < 3000; ms += 100) {
        bool done = false;
        if (cmdFeat->IsCommandDone(done) == VmbErrorSuccess && done)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "    [inject] IP injected via interface '" << ifId << "'\n";
    return true;
}

// Enumerate all VimbaX interfaces and print their IDs.
// Called once after Startup() to confirm the GigE TL was loaded.
static void printVimbaXInterfaces(VmbCPP::VmbSystem& vmb)
{
    VmbCPP::InterfacePtrVector ifaces;
    const VmbErrorType rc = vmb.GetInterfaces(ifaces);
    if (rc != VmbErrorSuccess) {
        std::cerr << "  [interfaces] GetInterfaces() failed, err=" << rc << "\n";
        return;
    }
    std::cout << "  [interfaces] Total interfaces: " << ifaces.size() << "\n";
    for (auto& iface : ifaces) {
        std::string ifId, ifName;
        iface->GetID(ifId);
        iface->GetName(ifName);
        // Probe whether this interface exposes the GigE-specific feature
        VmbCPP::FeaturePtr probe;
        const bool hasGev =
            (iface->GetFeatureByName("DeviceIPAddress", probe) == VmbErrorSuccess);
        std::cout << "  [interfaces]   id='" << ifId
                  << "'  name='" << ifName
                  << "'  GigE_TL=" << (hasGev ? "YES" : "no") << "\n";
        // List ALL feature names so we can see what the TL actually exposes
        // on this interface (useful when DeviceIPAddress is not found).
        VmbCPP::FeaturePtrVector features;
        if (iface->GetFeatures(features) == VmbErrorSuccess && !features.empty()) {
            std::cout << "  [interfaces]   Features (" << features.size() << "): ";
            for (std::size_t i = 0; i < features.size(); ++i) {
                std::string fname;
                features[i]->GetName(fname);
                std::cout << fname;
                if (i + 1 < features.size()) std::cout << ", ";
            }
            std::cout << "\n";
        } else {
            std::cout << "  [interfaces]   (no features accessible or GetFeatures failed)\n";
        }
    }
}

// Inject cameraIp into every VimbaX interface that supports DeviceIPAddress.
// Returns the number of interfaces on which injection was performed.
static int vimbaXRegisterCamera(VmbCPP::VmbSystem& vmb, const std::string& cameraIp)
{
    const unsigned int ip = ipStringToUint32(cameraIp);
    if (ip == 0) {
        std::cerr << "    [inject] Invalid IP: " << cameraIp << "\n";
        return 0;
    }

    VmbCPP::InterfacePtrVector interfaces;
    const VmbErrorType rc = vmb.GetInterfaces(interfaces);
    if (rc != VmbErrorSuccess) {
        std::cerr << "    [inject] GetInterfaces() failed, err=" << rc << "\n";
        return 0;
    }
    if (interfaces.empty()) {
        std::cerr << "    [inject] GetInterfaces() returned 0 interfaces\n";
        return 0;
    }

    int count = 0;
    for (auto& iface : interfaces) {
        std::string ifId;
        iface->GetID(ifId);   // best-effort — empty string is safe
        const std::string label = ifId.empty() ? "<unnamed>" : ifId;
        VmbCPP::FeaturePtr probe;
        if (iface->GetFeatureByName("DeviceIPAddress", probe) != VmbErrorSuccess) {
            std::cout << "    [inject] skip '" << label << "' (no DeviceIPAddress — not GigE TL)\n";
            continue;
        }
        if (injectIpIntoInterface(iface, ip, label))
            ++count;
    }
    return count;
}
#endif // VIMBAX_ENABLED

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
// ICMP ping helper — runs system ping command, mirrors probe_ping() in
// debug_camera_network.py.  Returns true when at least one reply is received.
// Requires NET_RAW capability + seccomp=unconfined inside a container.
// ---------------------------------------------------------------------------
static bool probePing(const std::string& ip)
{
    // -c 2 : send 2 packets   -W 2 : 2 s timeout per packet   -q : quiet summary
    const std::string cmd = "ping -c 2 -W 2 -q " + ip + " 2>&1";
    FILE* fp = ::popen(cmd.c_str(), "r");
    if (!fp) {
        std::cerr << "    [ping] popen failed: " << std::strerror(errno) << "\n";
        return false;
    }
    std::string output;
    std::array<char, 256> buf{};
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), fp))
        output += buf.data();
    const int rc = ::pclose(fp);
    const bool ok = (rc == 0);
    // Print the last non-empty line (rtt summary or error)
    std::string summary;
    std::istringstream ss(output);
    std::string line;
    while (std::getline(ss, line))
        if (!line.empty()) summary = line;
    std::cout << "    [ping] " << (ok ? "OK  " : "FAIL") << "  " << summary << "\n";
    return ok;
}

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

    // ------------------------------------------------------------------
    // 2. ICMP ping pre-check
    //    Uses the system ping command (2 packets, 2 s timeout).
    //    Requires NET_RAW capability + seccomp=unconfined in a container.
    // ------------------------------------------------------------------
    std::cout << "\n=== [test6] ICMP ping pre-check ===" << std::endl;
    for (const auto& cam : cameras) {
        if (cam.cameraType != "TYPE_CAM_VIMBA") continue;
        if (!args.targetCamUID.empty() && cam.camUID != args.targetCamUID) continue;
        if (!cam.ip || cam.ip->empty()) {
            std::cout << "  " << cam.camUID << ": no IP configured — skipping ping\n";
            continue;
        }
        std::cout << "  Pinging " << cam.camUID << " (" << *cam.ip << ") ...\n";
        probePing(*cam.ip);
    }

    // ------------------------------------------------------------------
    // 3. GVCP unicast pre-check (pure C++, no VimbaX SDK)
    //    Sends a GVCP DISCOVERY_CMD to each configured camera IP and
    //    reports whether the camera responds on the network level before
    //    any VimbaX SDK call is made.
    //
    //    Results are stored in gevProbeResults and reused in section 6 so
    //    that we never open a competing raw UDP socket after VimbaX Startup()
    //    (the GigE TL takes over port 3956 and would cause recv() to block).
    // ------------------------------------------------------------------
    std::cout << "\n=== [test6] GVCP unicast pre-check (UDP port 3956) ===" << std::endl;
    std::map<std::string, GevDiscoveryResult> gevProbeResults; // ip -> result
    for (const auto& cam : cameras) {
        if (cam.cameraType != "TYPE_CAM_VIMBA") continue;
        if (!args.targetCamUID.empty() && cam.camUID != args.targetCamUID) continue;
        if (!cam.ip || cam.ip->empty()) {
            std::cout << "  " << cam.camUID << ": no IP configured — skipping probe\n";
            continue;
        }
        std::cout << "  Probing " << cam.camUID << " (ip=" << *cam.ip << ") ... " << std::flush;
        const auto gev = VimbaXUnicastDiscovery::probe(*cam.ip);
        gevProbeResults[*cam.ip] = gev;
        if (gev.responded) {
            std::cout << "DISCOVERY_ACK";
            if (!gev.fromIp.empty())    std::cout << "  from="   << gev.fromIp;
            if (!gev.deviceIp.empty())  std::cout << "  devIP="  << gev.deviceIp;
            if (!gev.deviceMac.empty()) std::cout << "  MAC="    << gev.deviceMac;
            if (!gev.subnet.empty())    std::cout << "  subnet=" << gev.subnet;
            std::cout << "\n";
        } else {
            std::cout << "NO RESPONSE (camera offline or UDP:3956 blocked)\n";
        }
    }

#ifdef VIMBAX_ENABLED
    // ------------------------------------------------------------------
    // 4. Start VimbaX system directly — skip broadcast autodiscovery
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

    // Enumerate all loaded transport layer interfaces immediately after Startup().
    // This confirms whether the GigE CTI was loaded and is the first thing to
    // check when injection or camera-open fails.
    std::cout << "[test6] Enumerating VimbaX interfaces (CTI check):" << std::endl;
    printVimbaXInterfaces(vmb);

    // ------------------------------------------------------------------
    // 5. Cross-subnet registration: inject camera IPs into VimbaX
    //
    //    VimbaX's Startup() runs a GigE broadcast discovery on every local
    //    network interface.  When the host is on a different subnet from the
    //    cameras, that broadcast is blocked by the router and the cameras
    //    never appear in VimbaX's internal device list — causing every later
    //    OpenCameraByID() to return VmbErrorNotFound (-3).
    //
    //    Fix: write the target IP into the "DeviceIPAddress" feature of each
    //    GigE TL interface and run "DeviceUpdateList".  The TL then sends a
    //    directed unicast GVCP DISCOVERY_CMD to that IP and registers the
    //    camera.  Same-subnet cameras are already in the list; injecting them
    //    again is idempotent.
    //
    //    IMPORTANT: The VimbaX TL owns UDP port 3956 now.  Do NOT open our
    //    own POSIX socket to port 3956 concurrently (it will block recv()).
    //    The pre-checks in sections 2–3 ran before Startup(), so there is no
    //    conflict here.
    // ------------------------------------------------------------------
    std::cout << "\n=== [test6] Cross-subnet injection: registering cameras in VimbaX ===" << std::endl;
    for (const auto& cam : cameras) {
        if (cam.cameraType != "TYPE_CAM_VIMBA") continue;
        if (!args.targetCamUID.empty() && cam.camUID != args.targetCamUID) continue;
        if (!cam.ip || cam.ip->empty()) {
            std::cout << "  " << cam.camUID << ": no IP configured — skipping injection\n";
            continue;
        }
        std::cout << "  Injecting " << cam.camUID << " (" << *cam.ip << ") ...\n";
        const int n = vimbaXRegisterCamera(vmb, *cam.ip);
        if (n > 0)
            std::cout << "  -> injection sent on " << n << " interface(s)\n";
        else
            std::cout << "  -> WARNING: injection found no GigE interfaces (all cameras "
                         "must be on the same subnet, or no GEV TL is loaded)\n";
    }

    // ------------------------------------------------------------------
    // 6. Connect to TYPE_CAM_VIMBA cameras
    //    Strategy:
    //      a) cam.ip present → unicast GVCP DISCOVERY_CMD (pure UDP, 5 retries)
    //                          → parse DISCOVERY_ACK → GenTL ID "DEV_<MAC>"
    //         (bare-IP always added as secondary fallback)
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

        // Build ordered candidate list:
        //   1. GenTL ID from section-3 pre-check MAC  (best — ID confirmed by camera)
        //   2. Bare-IP fallback (AVT GigE TL accepts this on some firmware versions)
        //   3. MAC from JSON config (always present as final fallback)
        //
        // NOTE: unicastDiscoverCameraId() is NOT called here because VimbaX
        // Startup() is already running and owns UDP port 3956.  Opening a
        // competing raw socket would cause recv() to block indefinitely.
        // The section-3 pre-check (before Startup) already captured any
        // DISCOVERY_ACK the camera sent; we reuse that result here.
        using Candidate = std::pair<std::string, std::string>; // {id, label}
        std::vector<Candidate> candidates;

        if (cam.ip && !cam.ip->empty()) {
            const auto it = gevProbeResults.find(*cam.ip);
            if (it != gevProbeResults.end() && it->second.responded
                && !it->second.deviceMac.empty()) {
                // Build "DEV_XXXXXXXXXXXX" from the MAC the camera reported
                std::string macClean = "DEV_";
                for (char c : it->second.deviceMac)
                    if (c != ':' && c != '-')
                        macClean += static_cast<char>(std::toupper(
                            static_cast<unsigned char>(c)));
                std::cout << "  [test6] Pre-check MAC → GenTL ID: '" << macClean << "'\n";
                candidates.push_back({macClean, "pre-check-MAC"});
            } else {
                std::cout << "  [test6] No pre-check DISCOVERY_ACK for "
                          << *cam.ip << " — skipping unicast GenTL ID\n";
            }
            // Bare-IP candidate
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
