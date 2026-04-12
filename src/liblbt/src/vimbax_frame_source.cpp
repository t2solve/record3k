#include "vimbax_frame_source.h"
// DETECT_UNI_CAST_ONLY switch is controlled in vimbax_frame_source.h

#ifdef VIMBAX_ENABLED

#include "frame_converter.h"
#include <liblbt/vimbax_unicast_discovery.h>
#include <QDebug>
#include <opencv2/opencv.hpp>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <thread>

using namespace VmbCPP;

// Pull-based single-shot acquisition; no observer used

VimbaXFrameSource::VimbaXFrameSource(const std::string& cameraSerial,
                                     const std::string& interfaceIdOrMac,
                                     bool debugLog,
                                     const std::string& cameraIp)
    : system_(VmbSystem::GetInstance()), debug_(debugLog) {
    std::cout << "[VimbaXFrameSource] Constructor called (serial='" << cameraSerial 
              << "', interface='" << interfaceIdOrMac << "', ip='" << cameraIp << "')" << std::endl;
    ready_ = initialize(cameraSerial, interfaceIdOrMac, cameraIp);
    std::cout << "[VimbaXFrameSource] Constructor complete, ready=" << ready_ << std::endl;
}

VimbaXFrameSource::~VimbaXFrameSource() {
    shutdown();
}

bool VimbaXFrameSource::initialize(const std::string& cameraSerial, const std::string& interfaceIdOrMac, const std::string& cameraIp) {
    VmbErrorType err = VmbErrorSuccess;
    
    std::cout << "[VimbaXFrameSource] initialize() started" << std::endl;
    std::cout << "[VimbaXFrameSource] Requested: serial='" << cameraSerial << "', interface/MAC='" << interfaceIdOrMac << "', ip='" << cameraIp << "'" << std::endl;
    
    // Note: We don't call Startup() here anymore since CameraManagerVimbax singleton manages system lifecycle
    // The system should already be initialized by CameraManagerVimbax::startup()
    if (debug_) qDebug() << "[VimbaXFrameSource] Using already initialized VmbSystem";

    // Wait a moment for camera discovery to complete (GigE cameras may take time)
    std::cout << "[VimbaXFrameSource] Waiting 200ms for camera discovery..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

#ifndef DETECT_UNI_CAST_ONLY
    // Pick camera by serial and/or interface id (MAC)
    std::cout << "[VimbaXFrameSource] Calling GetCameras()..." << std::endl;
    CameraPtrVector cameras;
    err = system_.GetCameras(cameras);
    std::cout << "[VimbaXFrameSource] GetCameras() returned, err=" << err << ", count=" << cameras.size() << std::endl;
    if (err != VmbErrorSuccess) {
        std::cerr << "[VimbaXFrameSource] GetCameras failed with error: " << err << std::endl;
        if (debug_) qWarning() << "[VimbaXFrameSource] GetCameras failed:" << err;
        // In unicast-only builds this path is never reached, but keep the
        // variable in scope to avoid compiler complaints below.
        return false;
    }
    
    if (cameras.empty()) {
        std::cout << "[VimbaXFrameSource] No cameras found by GetCameras() — will still attempt Strategy 4 (IP unicast) if ip is set" << std::endl;
        if (debug_) qDebug() << "[VimbaXFrameSource] GetCameras returned empty — falling through to Strategy 4";
        // Do NOT return here: fall through so Strategy 4 (IP-based) gets a chance to run
    }

    std::cout << "[VimbaXFrameSource] Searching for matching camera..." << std::endl;
    std::cout << "[VimbaXFrameSource] Search criteria: serial='" << cameraSerial << "', interface/MAC='" << interfaceIdOrMac << "'" << std::endl;
    
    // First, list all detected cameras for debugging
    std::cout << "[VimbaXFrameSource] Available cameras:" << std::endl;
    for (size_t i = 0; i < cameras.size(); i++) {
        std::string camId, camSerial, camModel, camInterface, camName;
        cameras[i]->GetID(camId);
        cameras[i]->GetSerialNumber(camSerial);
        cameras[i]->GetModel(camModel);
        cameras[i]->GetInterfaceID(camInterface);
        cameras[i]->GetName(camName);
        
        // Try to get MAC address
        std::string macAddr = "N/A";
        FeaturePtr macFeature;
        if (cameras[i]->GetFeatureByName("GevDeviceMACAddress", macFeature) == VmbErrorSuccess) {
            VmbInt64_t macValue;
            if (macFeature->GetValue(macValue) == VmbErrorSuccess) {
                std::ostringstream oss;
                oss << std::hex << std::setfill('0') << std::uppercase;
                oss << std::setw(2) << ((macValue >> 40) & 0xFF) << ":"
                    << std::setw(2) << ((macValue >> 32) & 0xFF) << ":"
                    << std::setw(2) << ((macValue >> 24) & 0xFF) << ":"
                    << std::setw(2) << ((macValue >> 16) & 0xFF) << ":"
                    << std::setw(2) << ((macValue >> 8) & 0xFF) << ":"
                    << std::setw(2) << (macValue & 0xFF);
                macAddr = oss.str();
            }
        }
        
        std::cout << "[VimbaXFrameSource]   Camera[" << i << "]:" << std::endl;
        std::cout << "[VimbaXFrameSource]     Name: " << camName << std::endl;
        std::cout << "[VimbaXFrameSource]     ID: " << camId << std::endl;
        std::cout << "[VimbaXFrameSource]     Serial: " << camSerial << std::endl;
        std::cout << "[VimbaXFrameSource]     Model: " << camModel << std::endl;
        std::cout << "[VimbaXFrameSource]     Interface: " << camInterface << std::endl;
        std::cout << "[VimbaXFrameSource]     MAC: " << macAddr << std::endl;
    }
    
    // Strategy 1: Try exact match on both serial and interface
    std::cout << "[VimbaXFrameSource] Strategy 1: Trying exact match on serial AND interface..." << std::endl;
    if (!cameraSerial.empty() && !interfaceIdOrMac.empty()) {
        for (auto& cam : cameras) {
            std::string ser, iid;
            cam->GetSerialNumber(ser);
            cam->GetInterfaceID(iid);
            if (ser == cameraSerial && iid.find(interfaceIdOrMac) != std::string::npos) {
                camera_ = cam;
                std::cout << "[VimbaXFrameSource] ✓ Found exact match: " << ser << " on " << iid << std::endl;
                break;
            }
        }
    }
    
    // Strategy 2: Try match by serial only
    if (!camera_ && !cameraSerial.empty()) {
        std::cout << "[VimbaXFrameSource] Strategy 2: Trying match by serial only..." << std::endl;
        for (auto& cam : cameras) {
            std::string ser;
            cam->GetSerialNumber(ser);
            if (ser == cameraSerial) {
                camera_ = cam;
                std::cout << "[VimbaXFrameSource] ✓ Found match by serial: " << ser << std::endl;
                break;
            }
        }
    }
    
    // Strategy 3: Try match by interface/MAC only
    if (!camera_ && !interfaceIdOrMac.empty()) {
        std::cout << "[VimbaXFrameSource] Strategy 3: Trying match by interface/MAC only..." << std::endl;
        
        // Prepare MAC address without separators for Camera ID matching
        // e.g., "00:0A:47:37:27:77" -> "000A47372777"
        std::string macNoSeparators;
        for (char c : interfaceIdOrMac) {
            if (c != ':' && c != '-') {
                macNoSeparators += std::toupper(c);
            }
        }
        std::string expectedCameraId = "DEV_" + macNoSeparators;
        std::cout << "[VimbaXFrameSource]   Expected Camera ID from MAC: " << expectedCameraId << std::endl;
        
        for (auto& cam : cameras) {
            std::string iid, camId;
            cam->GetInterfaceID(iid);
            cam->GetID(camId);
            
            // Check Camera ID (contains MAC without separators)
            bool matchCameraId = (camId == expectedCameraId);
            
            // Also check MAC address from feature (if available)
            FeaturePtr macFeature;
            std::string macAddr;
            if (cam->GetFeatureByName("GevDeviceMACAddress", macFeature) == VmbErrorSuccess) {
                VmbInt64_t macValue;
                if (macFeature->GetValue(macValue) == VmbErrorSuccess) {
                    std::ostringstream oss;
                    oss << std::hex << std::setfill('0') << std::uppercase;
                    oss << std::setw(2) << ((macValue >> 40) & 0xFF) << ":"
                        << std::setw(2) << ((macValue >> 32) & 0xFF) << ":"
                        << std::setw(2) << ((macValue >> 24) & 0xFF) << ":"
                        << std::setw(2) << ((macValue >> 16) & 0xFF) << ":"
                        << std::setw(2) << ((macValue >> 8) & 0xFF) << ":"
                        << std::setw(2) << (macValue & 0xFF);
                    macAddr = oss.str();
                }
            }
            
            bool matchInterface = iid.find(interfaceIdOrMac) != std::string::npos;
            bool matchMAC = !macAddr.empty() && macAddr == interfaceIdOrMac;
            
            if (matchCameraId || matchInterface || matchMAC) {
                camera_ = cam;
                if (matchCameraId) {
                    std::cout << "[VimbaXFrameSource] ✓ Found match by Camera ID: " << camId << std::endl;
                } else if (matchInterface) {
                    std::cout << "[VimbaXFrameSource] ✓ Found match by interface: " << iid << std::endl;
                } else {
                    std::cout << "[VimbaXFrameSource] ✓ Found match by MAC: " << macAddr << std::endl;
                }
                break;
            }
        }
    }
    
    // // Strategy 4: Use first available camera (only if no specific criteria were given)
    // if (!camera_) {
    //     // Only use first available if BOTH serial and interface/MAC are empty
    //     // Otherwise, it means we failed to find the specific camera requested
    //     if (cameraSerial.empty() && interfaceIdOrMac.empty()) {
    //         std::cout << "[VimbaXFrameSource] Strategy 4: Using first available camera..." << std::endl;
    //         camera_ = cameras.front();
    //         std::string camId, camSerial;
    //         camera_->GetID(camId);
    //         camera_->GetSerialNumber(camSerial);
    //         std::cout << "[VimbaXFrameSource] ✓ Using first camera: " << camId << " (S/N: " << camSerial << ")" << std::endl;
    //         if (debug_) qWarning() << "[VimbaXFrameSource] No match found, using first available camera";
    //     } else {
    //         std::cerr << "[VimbaXFrameSource] ✗ Failed to find camera matching criteria" << std::endl;
    //         std::cerr << "[VimbaXFrameSource]   Requested serial: '" << cameraSerial << "'" << std::endl;
    //         std::cerr << "[VimbaXFrameSource]   Requested interface/MAC: '" << interfaceIdOrMac << "'" << std::endl;
    //         std::cerr << "[VimbaXFrameSource]   Will NOT fallback to first available camera" << std::endl;
    //         return false;  // Return false instead of using first available camera
    //     }
    // }

#else // DETECT_UNI_CAST_ONLY
    std::cout << "[VimbaXFrameSource] DETECT_UNI_CAST_ONLY — broadcast strategies 1-3 skipped" << std::endl;
#endif // !DETECT_UNI_CAST_ONLY

    // Strategy 4: IP-based — unicast GVCP probe (reachability check) then bare-IP open.
    //   a) Send a GVCP DISCOVERY_CMD and log the camera's network-level response.
    //      This confirms whether the camera is reachable on UDP:3956 without any SDK
    //      broadcast and works on managed networks / VPN where broadcast is blocked.
    //   b) Try OpenCameraByID(ip) — the AVT GigE TL accepts a bare IP string as a
    //      camera ID on older transport layers that do not support writable DeviceIPAddress.
    if (!camera_ && !cameraIp.empty()) {
        std::cout << "[VimbaXFrameSource] Strategy 4: IP unicast — probing " << cameraIp << " ..." << std::endl;

        // a) GVCP unicast probe (pure C++, no SDK)
        const auto gev = lbt::VimbaXUnicastDiscovery::probe(cameraIp);
        if (gev.responded) {
            std::cout << "[VimbaXFrameSource] Strategy 4: DISCOVERY_ACK received"
                      << "  from=" << gev.fromIp
                      << "  devIP=" << gev.deviceIp
                      << "  MAC=" << gev.deviceMac
                      << "  subnet=" << gev.subnet << std::endl;
        } else {
            std::cout << "[VimbaXFrameSource] Strategy 4: No GVCP response from " << cameraIp
                      << " — camera may still be addressable directly via SDK" << std::endl;
        }

        // b) Bare-IP open via VimbaX transport layer
        //    OpenCameraByID both locates and opens the camera in one call, so we
        //    return early to skip the camera_->Open() below (it is already open).
        std::cout << "[VimbaXFrameSource] Strategy 4: Trying OpenCameraByID(ip='"
                  << cameraIp << "')..." << std::endl;
        VmbErrorType s4err = system_.OpenCameraByID(cameraIp.c_str(), VmbAccessModeFull, camera_);
        if (s4err == VmbErrorSuccess) {
            std::cout << "[VimbaXFrameSource] ✓ Strategy 4: Opened camera via bare IP" << std::endl;
            // Camera is already open — skip camera_->Open() and go straight to frame setup
            return ensurePreallocFrame();
        }
        std::cerr << "[VimbaXFrameSource] ✗ Strategy 4: OpenCameraByID failed (err=" << s4err << ")" << std::endl;
    }

    if (!camera_) {
        std::cerr << "[VimbaXFrameSource] ✗ All strategies exhausted — camera not found" << std::endl;
        std::cerr << "[VimbaXFrameSource]   serial='" << cameraSerial << "', MAC='" << interfaceIdOrMac
                  << "', ip='" << cameraIp << "'" << std::endl;
        return false;
    }
    std::cout << "[VimbaXFrameSource] Opening camera..." << std::endl;
    err = camera_->Open(VmbAccessModeFull);
    std::cout << "[VimbaXFrameSource] Camera open returned, err=" << err << std::endl;
    if (err != VmbErrorSuccess) {
        if (debug_) qWarning() << "[VimbaXFrameSource] Camera open failed:" << err;
        return false;
    }

    // --- GigE stream packet size negotiation ---
    // Run GVSPAdjustPacketSize so the transport layer auto-negotiates the largest
    // safe packet size for this network path.  Without this, the camera may send
    // frames in packets that are larger than the NIC/switch MTU, causing silent
    // packet loss and AcquireSingleImage timeouts.
    {
        StreamPtrVector streams;
        if (camera_->GetStreams(streams) == VmbErrorSuccess && !streams.empty()) {
            FeaturePtr pktFeature;
            if (streams[0]->GetFeatureByName("GVSPAdjustPacketSize", pktFeature) == VmbErrorSuccess) {
                VmbErrorType pktErr = pktFeature->RunCommand();
                if (pktErr == VmbErrorSuccess) {
                    // Wait for the command to complete
                    bool isDone = false;
                    for (int i = 0; i < 50 && !isDone; ++i) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        pktFeature->IsCommandDone(isDone);
                    }
                    std::cout << "[VimbaXFrameSource] GVSPAdjustPacketSize done" << std::endl;
                } else {
                    std::cout << "[VimbaXFrameSource] GVSPAdjustPacketSize returned err=" << pktErr << " (non-fatal)" << std::endl;
                }
            }
        }
    }

    // --- Disable hardware trigger so the camera runs in free-run mode ---
    // If TriggerMode is left On from a previous session or firmware default,
    // AcquireSingleImage blocks forever waiting for a hardware trigger signal.
    {
        FeaturePtr trigFeature;
        if (camera_->GetFeatureByName("TriggerMode", trigFeature) == VmbErrorSuccess) {
            VmbErrorType trigErr = trigFeature->SetValue("Off");
            if (trigErr == VmbErrorSuccess) {
                std::cout << "[VimbaXFrameSource] TriggerMode set to Off" << std::endl;
            } else {
                std::cout << "[VimbaXFrameSource] TriggerMode SetValue('Off') err=" << trigErr << " (non-fatal)" << std::endl;
            }
        }
    }

    std::cout << "[VimbaXFrameSource] Ensuring preallocated frame..." << std::endl;
    // Pre-allocate a single frame buffer for AcquireSingleImage
    bool result = ensurePreallocFrame();
    std::cout << "[VimbaXFrameSource] initialize() complete, result=" << result << std::endl;
    return result;
}

void VimbaXFrameSource::shutdown() {
    try {
        if (camera_) {
            (void)camera_->Close();
        }
    } catch (const std::exception& e) {
        qWarning() << "[VimbaXFrameSource] Exception while closing camera:" << e.what();
    } catch (...) {
        qWarning() << "[VimbaXFrameSource] Unknown exception while closing camera";
    }
    // Note: We don't call system_.Shutdown() here anymore since CameraManagerVimbax singleton manages system lifecycle
    preallocFrame_.reset();
    camera_.reset();
    ready_ = false;
}

std::unique_ptr<FrameMemoryObject> VimbaXFrameSource::nextFrame() {
    // GigE cameras can occasionally drop a packet — retry a few times before
    // giving up so that transient network hiccups don't abort the whole recording.
    static constexpr int frameTimeoutMs = 3000;
    static constexpr int maxRetries = 3;

    if (!ready_) return nullptr;
    if (!ensurePreallocFrame()) return nullptr;

    VmbErrorType err = VmbErrorSuccess;
    for (int attempt = 0; attempt < maxRetries; ++attempt) {
        err = camera_->AcquireSingleImage(preallocFrame_, frameTimeoutMs, FrameAllocationMode::FrameAllocation_AllocAndAnnounceFrame);
        if (err == VmbErrorSuccess) break;

        if (err == VmbErrorTimeout) {
            std::cout << "[VimbaXFrameSource] AcquireSingleImage timeout (attempt "
                      << (attempt + 1) << "/" << maxRetries << ")" << std::endl;
        } else {
            qWarning() << "[VimbaXFrameSource] AcquireSingleImage failed, err=" << err;
            return nullptr; // non-timeout errors are not worth retrying
        }
    }

    if (err != VmbErrorSuccess) {
        std::cout << "[VimbaXFrameSource] AcquireSingleImage failed after " << maxRetries << " attempts" << std::endl;
        return nullptr;
    }

    // Convert to FrameMemoryObject and attach timestamp
    FrameMemoryObject out = FrameConverter::convertVmbFrameToMemoryObject(preallocFrame_, false);
    VmbUint64_t tsNs = 0;
    if (preallocFrame_->GetTimestamp(tsNs) == VmbErrorSuccess && tsNs != 0) {
        out.setTimestampNs(static_cast<uint64_t>(tsNs));
    } else {
        qWarning() << "[VimbaXFrameSource] Frame timestamp is invalid; using current time.";
        //out.touchTimestampNow();
    }

    return std::make_unique<FrameMemoryObject>(std::move(out));
}

bool VimbaXFrameSource::isReady() const { return ready_; }

bool VimbaXFrameSource::ensurePreallocFrame() {
    if (preallocFrame_) return true;
    if (!camera_) return false;
    VmbUint32_t payloadSize = 0;
    VmbErrorType err = camera_->GetPayloadSize(payloadSize);
    if (err != VmbErrorSuccess || payloadSize == 0) {
        if (debug_) qWarning() << "[VimbaXFrameSource] GetPayloadSize failed:" << err;
        return false;
    }
    preallocFrame_.reset(new Frame(payloadSize));
    return true;
}

#endif // VIMBAX_ENABLED
