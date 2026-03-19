#include "vimbax_frame_source.h"
// DETECT_UNI_CAST_ONLY switch is controlled in vimbax_frame_source.h

#ifdef VIMBAX_ENABLED

#include "frame_converter.h"
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

    // Strategy 4: IP-based GigE discovery (fallback when broadcast/multicast is blocked)
    // Iterates all GigE interfaces, triggers device list update, then matches on
    // GevDeviceIPAddress. Verifies at least one of MAC or serial matches before accepting.
    if (!camera_ && !cameraIp.empty()) {
        std::cout << "[VimbaXFrameSource] Strategy 4: IP-based discovery for " << cameraIp << "..." << std::endl;

        // Convert dotted-decimal IP to integer
        unsigned int ipa, ipb, ipc, ipd;
        VmbInt64_t targetIpInt = 0;
        if (sscanf(cameraIp.c_str(), "%u.%u.%u.%u", &ipa, &ipb, &ipc, &ipd) == 4) {
            targetIpInt = (VmbInt64_t)(((VmbInt64_t)ipa << 24) | (ipb << 16) | (ipc << 8) | ipd);
        } else {
            std::cerr << "[VimbaXFrameSource] Strategy 4: Invalid IP address '" << cameraIp << "', skipping" << std::endl;
        }

        if (targetIpInt != 0) {
            InterfacePtrVector interfaces;
            VmbErrorType ifErr = system_.GetInterfaces(interfaces);
            if (ifErr != VmbErrorSuccess) {
                std::cerr << "[VimbaXFrameSource] Strategy 4: GetInterfaces() failed, err=" << ifErr << std::endl;
            } else if (interfaces.empty()) {
                std::cerr << "[VimbaXFrameSource] Strategy 4: GetInterfaces() returned 0 interfaces — VimbaX transport layer may not be loaded (check GENICAM_GENTL64_PATH)" << std::endl;
            } else {
                std::cout << "[VimbaXFrameSource] Strategy 4: " << interfaces.size() << " interface(s) found" << std::endl;
                for (auto& iface : interfaces) {
                    if (camera_) break;

                    std::string ifaceId;
                    iface->GetID(ifaceId);

                    VmbTransportLayerType ifaceType{};
                    if (iface->GetType(ifaceType) != VmbErrorSuccess) {
                        std::cout << "[VimbaXFrameSource] Strategy 4:   iface='" << ifaceId << "' type query failed — skipping" << std::endl;
                        continue;
                    }
                    if (ifaceType != VmbTransportLayerTypeGEV) {
                        std::cout << "[VimbaXFrameSource] Strategy 4:   iface='" << ifaceId << "' type=" << ifaceType << " (not GEV) — skipping" << std::endl;
                        continue;
                    }
                    std::cout << "[VimbaXFrameSource] Strategy 4:   GEV iface='" << ifaceId << "' — triggering unicast rescan..." << std::endl;

                    // --- Explicit unicast DISCOVERY_CMD ---
                    // Send a directed GigE Vision discovery packet straight to the target IP.
                    // Different TL vendors expose this under different feature names; try all known ones.
                    // This is the most reliable approach when broadcast is blocked.
                    bool unicastTriggered = false;

                    // Attempt 1: GevDeviceForceIP (VimbaX / AVT TL)
                    {
                        FeaturePtr f;
                        if (iface->GetFeatureByName("GevDeviceForceIPAddress", f) == VmbErrorSuccess &&
                            f->SetValue(targetIpInt) == VmbErrorSuccess) {
                            FeaturePtr cmd;
                            if (iface->GetFeatureByName("GevDeviceForceIP", cmd) == VmbErrorSuccess &&
                                cmd->RunCommand() == VmbErrorSuccess) {
                                std::cout << "[VimbaXFrameSource] Strategy 4:   GevDeviceForceIP sent to " << cameraIp << std::endl;
                                unicastTriggered = true;
                                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                            }
                        }
                    }

                    // Attempt 2: ActionDestinationIPAddress + ActionDeviceKey  (some TLs)
                    if (!unicastTriggered) {
                        FeaturePtr addrFeat, cmdFeat;
                        if (iface->GetFeatureByName("ActionDestinationIPAddress", addrFeat) == VmbErrorSuccess &&
                            addrFeat->SetValue(cameraIp.c_str()) == VmbErrorSuccess) {
                            if (iface->GetFeatureByName("ActionCommand", cmdFeat) == VmbErrorSuccess &&
                                cmdFeat->RunCommand() == VmbErrorSuccess) {
                                std::cout << "[VimbaXFrameSource] Strategy 4:   ActionCommand unicast sent to " << cameraIp << std::endl;
                                unicastTriggered = true;
                                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                            }
                        }
                    }

                    // Attempt 3: DeviceDiscoveryDestinationIPAddress (Pleora / eBUS style)
                    if (!unicastTriggered) {
                        FeaturePtr addrFeat, cmdFeat;
                        if (iface->GetFeatureByName("DeviceDiscoveryDestinationIPAddress", addrFeat) == VmbErrorSuccess &&
                            addrFeat->SetValue(cameraIp.c_str()) == VmbErrorSuccess) {
                            if (iface->GetFeatureByName("DeviceDiscovery", cmdFeat) == VmbErrorSuccess &&
                                cmdFeat->RunCommand() == VmbErrorSuccess) {
                                std::cout << "[VimbaXFrameSource] Strategy 4:   DeviceDiscovery unicast sent to " << cameraIp << std::endl;
                                unicastTriggered = true;
                                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                            }
                        }
                    }

                    if (!unicastTriggered) {
                        std::cout << "[VimbaXFrameSource] Strategy 4:   No explicit unicast discovery feature found — falling back to DeviceUpdateList" << std::endl;
                    }

                    // Trigger unicast re-scan on this interface
                    FeaturePtr updateFeature;
                    if (iface->GetFeatureByName("DeviceUpdateList", updateFeature) == VmbErrorSuccess) {
                        updateFeature->RunCommand();
                        // Poll IsCommandDone up to 2000ms; fall back to fixed wait if feature absent
                        FeaturePtr doneFeature;
                        bool completed = false;
                        auto waitStart = std::chrono::steady_clock::now();
                        if (iface->GetFeatureByName("IsCommandDone", doneFeature) == VmbErrorSuccess) {
                            while (!completed &&
                                   std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - waitStart).count() < 2000) {
                                bool val = false;
                                if (doneFeature->GetValue(val) == VmbErrorSuccess && val)
                                    completed = true;
                                else
                                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            }
                            std::cout << "[VimbaXFrameSource] Strategy 4:   DeviceUpdateList "
                                      << (completed ? "completed" : "timed out — proceeding anyway") << std::endl;
                        } else {
                            // IsCommandDone not available — just wait
                            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                            std::cout << "[VimbaXFrameSource] Strategy 4:   DeviceUpdateList wait done (IsCommandDone unavailable)" << std::endl;
                        }
                    } else {
                        std::cout << "[VimbaXFrameSource] Strategy 4:   DeviceUpdateList not available on this interface" << std::endl;
                    }

                    // Need DeviceSelector for both range fallback and the per-slot loop — get it once
                    FeaturePtr deviceSelectorFeature;
                    bool hasSelectorFeature =
                        (iface->GetFeatureByName("DeviceSelector", deviceSelectorFeature) == VmbErrorSuccess);

                    // Determine number of devices via DeviceCount (preferred) or DeviceSelector range
                    VmbInt64_t devMin = 0, devMax = -1;
                    FeaturePtr deviceCountFeature;
                    if (iface->GetFeatureByName("DeviceCount", deviceCountFeature) == VmbErrorSuccess) {
                        VmbInt64_t count = 0;
                        if (deviceCountFeature->GetValue(count) == VmbErrorSuccess && count > 0) {
                            devMax = count - 1;
                            std::cout << "[VimbaXFrameSource] Strategy 4:   DeviceCount=" << count << std::endl;
                        } else {
                            std::cout << "[VimbaXFrameSource] Strategy 4:   DeviceCount=" << count << " — no devices on this interface after rescan" << std::endl;
                            continue;
                        }
                    } else if (hasSelectorFeature) {
                        // Fall back to DeviceSelector range
                        if (deviceSelectorFeature->GetRange(devMin, devMax) != VmbErrorSuccess) {
                            std::cout << "[VimbaXFrameSource] Strategy 4:   DeviceSelector range query failed and DeviceCount unavailable — no devices visible on this interface" << std::endl;
                            continue;
                        }
                    } else {
                        std::cout << "[VimbaXFrameSource] Strategy 4:   DeviceSelector not available — skipping interface" << std::endl;
                        continue;
                    }

                    if (devMax < devMin) {
                        std::cout << "[VimbaXFrameSource] Strategy 4:   No devices found on interface after rescan" << std::endl;
                        continue;
                    }
                    std::cout << "[VimbaXFrameSource] Strategy 4:   Scanning device slots [" << devMin << ".." << devMax << "]..." << std::endl;

                    if (!hasSelectorFeature) {
                        std::cout << "[VimbaXFrameSource] Strategy 4:   DeviceSelector not available — cannot iterate slots" << std::endl;
                        continue;
                    }

                    for (VmbInt64_t devIdx = devMin; devIdx <= devMax; ++devIdx) {
                        if (deviceSelectorFeature->SetValue(devIdx) != VmbErrorSuccess) continue;

                        FeaturePtr ipFeature;
                        if (iface->GetFeatureByName("GevDeviceIPAddress", ipFeature) != VmbErrorSuccess) continue;

                        VmbInt64_t devIp = 0;
                        if (ipFeature->GetValue(devIp) != VmbErrorSuccess) continue;

                        std::cout << "[VimbaXFrameSource] Strategy 4:   slot " << devIdx << " ip="
                                  << ((devIp>>24)&0xFF) << "." << ((devIp>>16)&0xFF)
                                  << "." << ((devIp>>8)&0xFF) << "." << (devIp&0xFF) << std::endl;

                        if (devIp != targetIpInt) continue;

                        std::cout << "[VimbaXFrameSource] Strategy 4: IP match at device slot " << devIdx << std::endl;

                        // Retrieve the GenTL device ID
                        FeaturePtr idFeature;
                        if (iface->GetFeatureByName("DeviceID", idFeature) != VmbErrorSuccess) {
                            std::cerr << "[VimbaXFrameSource] Strategy 4: DeviceID feature not available" << std::endl;
                            continue;
                        }

                        std::string cameraId;
                        if (idFeature->GetValue(cameraId) != VmbErrorSuccess) continue;

                        // Look up the camera object by its ID
                        CameraPtr candidate;
                        if (system_.GetCameraByID(cameraId.c_str(), candidate) != VmbErrorSuccess) {
                            std::cerr << "[VimbaXFrameSource] Strategy 4: IP matched (ID=" << cameraId
                                      << ") but GetCameraByID failed — camera may not be fully discovered yet" << std::endl;
                            continue;
                        }

                        // Double-check: verify MAC and/or serial to guard against IP collisions
                        bool macOk = false, serialOk = false;

                        if (!cameraSerial.empty()) {
                            std::string ser;
                            if (candidate->GetSerialNumber(ser) == VmbErrorSuccess)
                                serialOk = (ser == cameraSerial);
                        }

                        if (!interfaceIdOrMac.empty()) {
                            FeaturePtr macFeat;
                            if (candidate->GetFeatureByName("GevDeviceMACAddress", macFeat) == VmbErrorSuccess) {
                                VmbInt64_t macValue = 0;
                                if (macFeat->GetValue(macValue) == VmbErrorSuccess) {
                                    std::ostringstream oss;
                                    oss << std::hex << std::setfill('0') << std::uppercase;
                                    oss << std::setw(2) << ((macValue >> 40) & 0xFF) << ":"
                                        << std::setw(2) << ((macValue >> 32) & 0xFF) << ":"
                                        << std::setw(2) << ((macValue >> 24) & 0xFF) << ":"
                                        << std::setw(2) << ((macValue >> 16) & 0xFF) << ":"
                                        << std::setw(2) << ((macValue >> 8)  & 0xFF) << ":"
                                        << std::setw(2) << (macValue & 0xFF);
                                    macOk = (oss.str() == interfaceIdOrMac);
                                }
                            }
                            if (!macOk) {
                                std::string macNoSep;
                                for (char ch : interfaceIdOrMac)
                                    if (ch != ':' && ch != '-') macNoSep += std::toupper((unsigned char)ch);
                                macOk = (cameraId == "DEV_" + macNoSep);
                            }
                        }

                        bool noVerifyCriteria = cameraSerial.empty() && interfaceIdOrMac.empty();
                        if (noVerifyCriteria || macOk || serialOk) {
                            camera_ = candidate;
                            std::cout << "[VimbaXFrameSource] ✓ Strategy 4: matched camera by IP " << cameraIp
                                      << " (ID=" << cameraId << ")";
                            if (macOk)    std::cout << " [MAC verified]";
                            if (serialOk) std::cout << " [serial verified]";
                            std::cout << std::endl;
                            break;
                        } else {
                            std::cerr << "[VimbaXFrameSource] Strategy 4: IP matched (ID=" << cameraId
                                      << ") but MAC/serial verification failed — skipping" << std::endl;
                        }
                    }
                }
            }
        }
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
    int frameTimeoutMs = 1000;
    if (!ready_) return nullptr;
    if (!ensurePreallocFrame()) return nullptr;

    // Blocking grab with timeout
    VmbErrorType err = camera_->AcquireSingleImage(preallocFrame_, frameTimeoutMs, FrameAllocationMode::FrameAllocation_AllocAndAnnounceFrame);
    if (err != VmbErrorSuccess) {
        if (err == VmbErrorTimeout) {
            if (debug_) qDebug() << "[VimbaXFrameSource] AcquireSingleImage timeout";
        } else {
            qWarning() << "[VimbaXFrameSource] AcquireSingleImage failed, err=" << err;
        }
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
