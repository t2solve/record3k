#include "vimbax_frame_source.h"
// DETECT_UNI_CAST_ONLY switch is controlled in vimbax_frame_source.h

#ifdef VIMBAX_ENABLED

#include <VmbC/VmbC.h>      // VmbCaptureFrameWait, VmbCaptureQueueFlush
#include "frame_converter.h"
#include <liblbt/vimbax_unicast_discovery.h>
#include <QDebug>
#include <opencv2/opencv.hpp>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <thread>

using namespace VmbCPP;

// ---------------------------------------------------------------------------
// VimbaXFrameSource::FrameObserver
// ---------------------------------------------------------------------------

VimbaXFrameSource::FrameObserver::FrameObserver(CameraPtr cam)
    : IFrameObserver(cam) {}

void VimbaXFrameSource::FrameObserver::FrameReceived(FramePtr frame) {
    VmbUint64_t ticks = 0;
    // Timestamp via TimestampLatch only.
    // GetTimestamp() (GVSP-embedded, Method 1) is intentionally NOT used:
    // it fires on only ~5 % of frames.  Mixing it with the latch (Method 2)
    // introduces ~1 RTT apparent order inversions because the two methods
    // measure different points in time.  Using the latch exclusively keeps
    // all timestamps on the same reference and preserves strict monotonicity.
    if (m_pCamera) {
        FeaturePtr latchCmd, tickVal;
        // GEV 1.x feature names (older cameras / legacy TL)
        if (m_pCamera->GetFeatureByName("GevTimestampControlLatch", latchCmd) == VmbErrorSuccess &&
            m_pCamera->GetFeatureByName("GevTimestampValue",        tickVal)  == VmbErrorSuccess) {
            latchCmd->RunCommand();
            VmbInt64_t t = 0;
            tickVal->GetValue(t);
            ticks = static_cast<VmbUint64_t>(t);
        }
        // SFNC 2.x feature names (e.g. Allied Vision Alvium with SFNC 2.7+):
        //   TimestampLatch (Command) + TimestampLatchValue (Int64)
        // GevTimestampControlLatch/GevTimestampValue are absent on these cameras.
        if (ticks == 0) {
            FeaturePtr sfncLatch, sfncVal;
            if (m_pCamera->GetFeatureByName("TimestampLatch",      sfncLatch) == VmbErrorSuccess &&
                m_pCamera->GetFeatureByName("TimestampLatchValue", sfncVal)   == VmbErrorSuccess) {
                sfncLatch->RunCommand();
                VmbInt64_t t = 0;
                sfncVal->GetValue(t);
                ticks = static_cast<VmbUint64_t>(t);
            }
        }
    }

#ifdef VIMBAX_TIMESTAMP_STATS
    // Method 1 (GVSP-embedded) is disabled — all frames go through Method 2.
    if (ticks != 0) ++statsMethod2_;
    else            ++statsMissed_;
#endif

    std::lock_guard<std::mutex> lock(mtx_);
    // Monotonicity guard: ticks must never go backwards.
    // A regression means the SDK callback thread delivered frames out of order,
    // which would silently corrupt any downstream timestamp-based ordering.
    if (ticks != 0 && lastTicks_ != 0 && ticks < lastTicks_) {
        std::cerr << "[VimbaXFrameSource] WARNING: timestamp regression detected! "
                  << "prev=" << lastTicks_ << " cur=" << ticks
                  << " (delta=-" << (lastTicks_ - ticks) << " ticks) "
                  << "— frame order may be violated" << std::endl;
    }
    if (ticks != 0) lastTicks_ = ticks;
    queue_.push({frame, ticks});
    cv_.notify_one();
}

std::pair<VmbCPP::FramePtr, VmbUint64_t>
VimbaXFrameSource::FrameObserver::waitForFrame(uint32_t timeoutMs) {
    std::unique_lock<std::mutex> lock(mtx_);
    if (!cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                      [this] { return !queue_.empty(); }))
        return {{}, 0};
    auto entry = queue_.front();
    queue_.pop();
    return entry;
}

void VimbaXFrameSource::FrameObserver::clear() {
    std::lock_guard<std::mutex> lock(mtx_);
    while (!queue_.empty()) queue_.pop();
    lastTicks_ = 0;
}

#ifdef VIMBAX_TIMESTAMP_STATS
void VimbaXFrameSource::FrameObserver::logStats(const std::string& prefix) const {
    const uint64_t m1    = statsMethod1_.load(std::memory_order_relaxed);
    const uint64_t m2    = statsMethod2_.load(std::memory_order_relaxed);
    const uint64_t miss  = statsMissed_.load(std::memory_order_relaxed);
    const uint64_t total = m1 + m2 + miss;
    auto pct = [&](uint64_t n) -> double {
        return total > 0 ? 100.0 * static_cast<double>(n) / static_cast<double>(total) : 0.0;
    };
    const std::string tag = prefix.empty() ? "[VimbaXFrameSource]" : prefix;
    std::cout << tag << " === Timestamp method stats (" << total << " frames) ==="  << std::endl;
    std::cout << tag << "   Latch (GevTimestampControlLatch / TimestampLatch)   : "
              << m2   << " frames (" << pct(m2)   << "%)" << std::endl;
    std::cout << tag << "   Missed — latch returned 0                          : "
              << miss << " frames (" << pct(miss) << "%)" << std::endl;
    if (miss > 0)
        std::cout << tag << "   WARNING: " << miss << " frames have no camera timestamp at all."
                  << " Downstream analysis must treat these as missing data." << std::endl;
}
#endif

// ---------------------------------------------------------------------------
// VimbaXFrameSource

VimbaXFrameSource::VimbaXFrameSource(const std::string& cameraSerial,
                                     const std::string& interfaceIdOrMac,
                                     bool debugLog,
                                     const std::string& cameraIp,
                                     double targetFps)
    : system_(VmbSystem::GetInstance()), debug_(debugLog), targetFps_(targetFps) {
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
            return startContinuousCapture();
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

    std::cout << "[VimbaXFrameSource] Starting continuous capture..." << std::endl;
    bool result = startContinuousCapture();
    std::cout << "[VimbaXFrameSource] initialize() complete, result=" << result << std::endl;
    return result;
}

void VimbaXFrameSource::shutdown() {
    try {
        if (camera_ && captureStarted_) {
            FeaturePtr feat;
            if (camera_->GetFeatureByName("AcquisitionStop", feat) == VmbErrorSuccess)
                feat->RunCommand();
            // Clear observer queue before flushing so no stale frames are processed
            if (observer_) observer_->clear();
#ifdef VIMBAX_TIMESTAMP_STATS
            if (observer_) observer_->logStats("[VimbaXFrameSource][shutdown]");
#endif
            VmbCaptureQueueFlush(camera_->GetHandle());
            camera_->EndCapture();
            camera_->RevokeAllFrames();
        }
        observer_.reset();
        framePool_.clear();
        captureStarted_ = false;
        if (camera_)
            (void)camera_->Close();
    } catch (const std::exception& e) {
        qWarning() << "[VimbaXFrameSource] Exception during shutdown:" << e.what();
    } catch (...) {
        qWarning() << "[VimbaXFrameSource] Unknown exception during shutdown";
    }
    // Note: We don't call system_.Shutdown() — CameraManagerVimbax manages lifecycle
    camera_.reset();
    ready_ = false;
}

std::unique_ptr<FrameMemoryObject> VimbaXFrameSource::nextFrame() {
    // DMA stream stays alive between calls — observer delivers filled frames.
    static constexpr uint32_t frameTimeoutMs = 3000;
    static constexpr int maxRetries = 3;

    if (!ready_ || !captureStarted_ || !observer_) return nullptr;

    for (int attempt = 0; attempt < maxRetries; ++attempt) {
        auto [frame, ticks] = observer_->waitForFrame(frameTimeoutMs);
        if (!frame) {
            std::cout << "[VimbaXFrameSource] CaptureFrameWait timeout (attempt "
                      << (attempt + 1) << "/" << maxRetries << ")" << std::endl;
            continue;
        }

        // Convert to FrameMemoryObject and attach timestamp
        FrameMemoryObject out = FrameConverter::convertVmbFrameToMemoryObject(frame, false);
        // ticks: raw camera hardware counter (GevTimestampTickFrequency ticks since
        // camera power-on; for Allied Vision GigE tickFreqHz_ = 1e9, so 1 tick = 1 ns).
        if (ticks != 0) {
            uint64_t tsNs = static_cast<uint64_t>(
                static_cast<double>(ticks) * 1e9 / static_cast<double>(tickFreqHz_));
            out.setTimestampNs(tsNs);
        } else {
            qWarning() << "[VimbaXFrameSource] Camera timestamp unavailable (ticks=0)";
        }

        // Re-queue the slot so the DMA engine can fill it again
        camera_->QueueFrame(frame);
        return std::make_unique<FrameMemoryObject>(std::move(out));
    }

    std::cout << "[VimbaXFrameSource] CaptureFrameWait failed after " << maxRetries << " attempts" << std::endl;
    return nullptr;
}

bool VimbaXFrameSource::isReady() const { return ready_; }

bool VimbaXFrameSource::startContinuousCapture() {
    if (!camera_) return false;

    // 0. Enable chunk mode with the Timestamp chunk so Frame::GetTimestamp() in the
    //    FrameObserver returns the camera's hardware-clock capture tick (Method 1).
    //    Must happen before GetPayloadSize() because enabling chunks enlarges the
    //    per-frame payload.  Non-fatal: if the camera does not support chunks, Method 2
    //    (TimestampLatch fallback) takes over automatically.
    {
        FeaturePtr chunkModeActive;
        if (camera_->GetFeatureByName("ChunkModeActive", chunkModeActive) == VmbErrorSuccess) {
            if (chunkModeActive->SetValue(true) == VmbErrorSuccess) {
                FeaturePtr chunkSelector, chunkEnable;
                if (camera_->GetFeatureByName("ChunkSelector", chunkSelector) == VmbErrorSuccess &&
                    camera_->GetFeatureByName("ChunkEnable",   chunkEnable)   == VmbErrorSuccess) {
                    if (chunkSelector->SetValue("Timestamp") == VmbErrorSuccess &&
                        chunkEnable->SetValue(true)          == VmbErrorSuccess) {
                        std::cout << "[VimbaXFrameSource] Chunk mode ON (Timestamp chunk enabled)"
                                     " — Method 1 timestamps will be hardware-precise" << std::endl;
                    } else {
                        std::cout << "[VimbaXFrameSource] ChunkSelector/ChunkEnable setup failed (non-fatal)" << std::endl;
                    }
                } else {
                    std::cout << "[VimbaXFrameSource] ChunkSelector/ChunkEnable features not found (non-fatal)" << std::endl;
                }
            } else {
                std::cout << "[VimbaXFrameSource] ChunkModeActive SetValue(true) failed (non-fatal)" << std::endl;
            }
        } else {
            std::cout << "[VimbaXFrameSource] ChunkModeActive not found — camera may not support chunks" << std::endl;
        }
    }

    // 1. Determine frame payload size
    VmbUint32_t payloadSize = 0;
    VmbErrorType err = camera_->GetPayloadSize(payloadSize);
    if (err != VmbErrorSuccess || payloadSize == 0) {
        qWarning() << "[VimbaXFrameSource] GetPayloadSize failed:" << err;
        return false;
    }
    std::cout << "[VimbaXFrameSource] Payload size = " << payloadSize << " bytes" << std::endl;

    // 2. Create the frame observer (bridges async DMA callbacks to pull-based API)
    observer_.reset(new FrameObserver(camera_));
    VmbCPP::IFrameObserverPtr obsPtr = observer_;  // implicit VmbCPP::shared_ptr conversion

    // 3. Allocate frame pool: register observer, announce each frame
    framePool_.clear();
    framePool_.reserve(kFramePoolSize);
    for (size_t i = 0; i < kFramePoolSize; ++i) {
        FramePtr frame(new Frame(payloadSize));
        if (frame->RegisterObserver(obsPtr) != VmbErrorSuccess) {
            qWarning() << "[VimbaXFrameSource] RegisterObserver failed for slot" << i;
            return false;
        }
        err = camera_->AnnounceFrame(frame);
        if (err != VmbErrorSuccess) {
            qWarning() << "[VimbaXFrameSource] AnnounceFrame failed for slot" << i << "err=" << err;
            return false;
        }
        framePool_.push_back(frame);
    }

    // 4. Start DMA capture engine
    err = camera_->StartCapture();
    if (err != VmbErrorSuccess) {
        qWarning() << "[VimbaXFrameSource] StartCapture failed:" << err;
        return false;
    }

    // 5. Queue all frames so the DMA engine can fill them
    for (auto& frame : framePool_) {
        err = camera_->QueueFrame(frame);
        if (err != VmbErrorSuccess) {
            qWarning() << "[VimbaXFrameSource] QueueFrame failed:" << err;
            camera_->EndCapture();
            return false;
        }
    }

    // 6. Set hardware acquisition rate via camera features
    FeaturePtr feat;
    if (camera_->GetFeatureByName("AcquisitionFrameRateEnable", feat) == VmbErrorSuccess) {
        if (feat->SetValue(true) == VmbErrorSuccess)
            std::cout << "[VimbaXFrameSource] AcquisitionFrameRateEnable = true" << std::endl;
    }
    if (camera_->GetFeatureByName("AcquisitionFrameRate", feat) == VmbErrorSuccess) {
        if (feat->SetValue(targetFps_) == VmbErrorSuccess)
            std::cout << "[VimbaXFrameSource] AcquisitionFrameRate = " << targetFps_ << std::endl;
    }

    // 7. Issue AcquisitionStart command
    if (camera_->GetFeatureByName("AcquisitionStart", feat) != VmbErrorSuccess) {
        qWarning() << "[VimbaXFrameSource] Cannot get AcquisitionStart feature";
        camera_->EndCapture();
        return false;
    }
    err = feat->RunCommand();
    if (err != VmbErrorSuccess) {
        qWarning() << "[VimbaXFrameSource] AcquisitionStart command failed:" << err;
        camera_->EndCapture();
        return false;
    }

    captureStarted_ = true;
    std::cout << "[VimbaXFrameSource] Continuous capture started (pool=" << kFramePoolSize
              << ", fps=" << targetFps_ << ")" << std::endl;

    // 8. Read camera tick frequency so nextFrame() can convert ticks → ns correctly.
    //    GEV 1.x name: GevTimestampTickFrequency (absent on SFNC 2.x cameras)
    //    SFNC 2.x name: DeviceTimestampFrequency (Allied Vision Alvium etc.)
    //    Default 1 GHz is correct for Allied Vision GigE cameras either way.
    FeaturePtr tickFreqFeat;
    if (camera_->GetFeatureByName("GevTimestampTickFrequency", tickFreqFeat) == VmbErrorSuccess) {
        VmbInt64_t rawFreq = 0;
        if (tickFreqFeat->GetValue(rawFreq) == VmbErrorSuccess && rawFreq > 0) {
            tickFreqHz_ = static_cast<uint64_t>(rawFreq);
        }
    } else if (camera_->GetFeatureByName("DeviceTimestampFrequency", tickFreqFeat) == VmbErrorSuccess) {
        VmbInt64_t rawFreq = 0;
        if (tickFreqFeat->GetValue(rawFreq) == VmbErrorSuccess && rawFreq > 0) {
            tickFreqHz_ = static_cast<uint64_t>(rawFreq);
        }
    }
    std::cout << "[VimbaXFrameSource] Timestamp tick frequency = " << tickFreqHz_ << " Hz" << std::endl;

    return true;
}

#endif // VIMBAX_ENABLED
