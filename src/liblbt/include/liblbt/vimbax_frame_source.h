#pragma once

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
    // Either parameter can be empty to act as a wildcard
    VimbaXFrameSource(const std::string& cameraSerial,
                      const std::string& interfaceIdOrMac,
                      bool debugLog = false);
    ~VimbaXFrameSource() override;

    std::unique_ptr<FrameMemoryObject> nextFrame() override;
    bool isReady() const override;

private:
    // Setup/teardown
    bool initialize(const std::string& cameraSerial, const std::string& interfaceIdOrMac);
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
    VimbaXFrameSource(const std::string&, const std::string&, bool=false) {}
    std::unique_ptr<FrameMemoryObject> nextFrame() override { return nullptr; }
    bool isReady() const override { return false; }
};

#endif // VIMBAX_ENABLED
