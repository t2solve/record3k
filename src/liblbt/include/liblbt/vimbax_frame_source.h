#pragma once

// ---------------------------------------------------------------------------
// Local detection mode switch
//
// Define DETECT_UNI_CAST_ONLY to skip broadcast-based strategies (1-3) and
// go straight to Strategy 4 (IP unicast).  Useful when the local network
// blocks multicast/broadcast GigE discovery.
//
// Can also be injected from the build system with: -DDETECT_UNI_CAST_ONLY
// Uncomment the line below to activate:
//#define DETECT_UNI_CAST_ONLY
// ---------------------------------------------------------------------------

#include "iframe_source.h"
#include <memory>
#include <string>
#include <mutex>

#ifdef VIMBAX_ENABLED
#include <VmbCPP/VmbCPP.h>
#include <VmbCPP/SharedPointerDefines.h>
#include <VmbCPP/IFrameObserver.h>
#include <VmbCPP/Frame.h>
#include <VmbCPP/Camera.h>
#include <VmbCPP/VmbCPPCommon.h>

// Async frame source backed by Vimba X; selects camera by serial and/or interface ID (MAC)
class VimbaXFrameSource : public IFrameSource {
public:
    // Either parameter can be empty to act as a wildcard.
    // cameraIp is used as a fallback (Strategy 4) when broadcast discovery is blocked.
    VimbaXFrameSource(const std::string& cameraSerial,
                      const std::string& interfaceIdOrMac,
                      bool debugLog = false,
                      const std::string& cameraIp = "");
    ~VimbaXFrameSource() override;

    std::unique_ptr<FrameMemoryObject> nextFrame() override;
    bool isReady() const override;

private:
    // Setup/teardown
    bool initialize(const std::string& cameraSerial, const std::string& interfaceIdOrMac, const std::string& cameraIp);
    void shutdown();
    
    // Ensure pre-allocated frame buffer exists
    bool ensurePreallocFrame();

    // Vimba handles
    VmbCPP::VmbSystem& system_;
    VmbCPP::CameraPtr camera_;
    VmbCPP::FramePtr preallocFrame_;

    // State
    bool ready_{false};
    bool debug_{false};
    mutable std::mutex mtx_;
};

#else

// Stub when Vimba X is not enabled to keep builds green
class VimbaXFrameSource : public IFrameSource {
public:
    VimbaXFrameSource(const std::string&, const std::string&, bool=false, const std::string& = "") {}
    std::unique_ptr<FrameMemoryObject> nextFrame() override { return nullptr; }
    bool isReady() const override { return false; }
};

#endif // VIMBAX_ENABLED
