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
#define DETECT_UNI_CAST_ONLY
//#define VIMBAX_TIMESTAMP_STATS  // Enable timestamp method stats collection and logging
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
#include <vector>
#include <queue>
#include <condition_variable>
#include <cstdint>
#include <atomic>

// Continuous-streaming frame source backed by Vimba X.
// Selects camera by serial and/or interface ID (MAC).
// Keeps the GigE DMA stream permanently alive between frames (no per-frame
// teardown) using AnnounceFrame / StartCapture / QueueFrame + IFrameObserver.
class VimbaXFrameSource : public IFrameSource {
public:
    // Either serial/MAC can be empty to act as a wildcard.
    // cameraIp is used as fallback (Strategy 4) when broadcast is blocked.
    // targetFps is set on the camera hardware so it gates frames at that rate.
    VimbaXFrameSource(const std::string& cameraSerial,
                      const std::string& interfaceIdOrMac,
                      bool debugLog = false,
                      const std::string& cameraIp = "",
                      double targetFps = 10.0);
    ~VimbaXFrameSource() override;

    std::unique_ptr<FrameMemoryObject> nextFrame() override;
    bool isReady() const override;

private:
    // Observer that receives filled frames asynchronously and buffers them
    // in a thread-safe queue so nextFrame() can pull them synchronously.
    // Each entry is {FramePtr, camera_ticks} where camera_ticks is the raw
    // hardware tick counter embedded in the GVSP stream by the camera (unit:
    // GevTimestampTickFrequency ticks since camera power-on; Allied Vision
    // GigE = 1 GHz so 1 tick = 1 ns).  Falls back to an immediate
    // GevTimestampControlLatch when the transport layer does not set the
    // VmbFrameFlagsTimestamp flag (embedded timestamp == 0).
    class FrameObserver : public VmbCPP::IFrameObserver {
    public:
        explicit FrameObserver(VmbCPP::CameraPtr cam);

        void FrameReceived(VmbCPP::FramePtr frame) override;

        // Block until a frame is available or timeout expires.
        // Returns {empty FramePtr, 0} on timeout.
        std::pair<VmbCPP::FramePtr, VmbUint64_t> waitForFrame(uint32_t timeoutMs);

        void clear();

#ifdef VIMBAX_TIMESTAMP_STATS
        // Log a per-session summary of which timestamp method was used.
        // Call at end of recording to quantify precision in error discussion.
        // See doc/FRAME_TIMESTAMP_DISCUSSION.md.
        void logStats(const std::string& prefix = "") const;
#endif

    private:
        std::mutex mtx_;
        std::condition_variable cv_;
        std::queue<std::pair<VmbCPP::FramePtr, VmbUint64_t>> queue_;
        // Last non-zero tick seen — used to detect unexpected SDK callback reordering.
        VmbUint64_t lastTicks_{0};

#ifdef VIMBAX_TIMESTAMP_STATS
        // Counters incremented atomically from the DMA callback thread.
        // Method 1 (GVSP-embedded) is disabled; only latch and missed are tracked.
        // Latch: GevTimestampControlLatch / TimestampLatch fallback.
        // Missed:  latch returned 0 — timestamp is unavailable.
        std::atomic<uint64_t> statsMethod1_{0};  // always 0; kept for ABI compat
        std::atomic<uint64_t> statsMethod2_{0};
        std::atomic<uint64_t> statsMissed_{0};
#endif
    };

    // Setup / teardown
    bool initialize(const std::string& cameraSerial, const std::string& interfaceIdOrMac, const std::string& cameraIp);
    void shutdown();

    // Announce frame pool, start DMA capture and set hw FPS feature.
    // Called at the end of initialize() once the camera is open.
    bool startContinuousCapture();

    // Vimba handles
    VmbCPP::VmbSystem& system_;
    VmbCPP::CameraPtr  camera_;

    // Pool of pre-announced frame buffers; keeps shared_ptr refs alive for
    // the lifetime of the stream so RevokeAllFrames works correctly.
    // 8 slots @ 10 fps gives ~800 ms of headroom before frames start dropping.
    // Never reduce below 3 (VimbaX minimum for continuous streaming).
    static constexpr size_t kFramePoolSize = 8;
    std::vector<VmbCPP::FramePtr> framePool_;

    // Observer that bridges async DMA callbacks to the pull-based nextFrame() API.
    VmbCPP::shared_ptr<FrameObserver> observer_;

    // State
    bool captureStarted_{false};
    bool ready_{false};
    bool debug_{false};
    double targetFps_{10.0};

    // Camera tick frequency (GevTimestampTickFrequency, Hz).
    // Allied Vision GigE = 1,000,000,000 Hz → 1 tick = 1 ns.
    // Read at startup; used to convert raw camera ticks to nanoseconds.
    uint64_t tickFreqHz_{1000000000ULL};
};

#else

// Stub when Vimba X is not enabled to keep builds green
class VimbaXFrameSource : public IFrameSource {
public:
    VimbaXFrameSource(const std::string&, const std::string&, bool=false, const std::string& = "", double = 10.0) {}
    std::unique_ptr<FrameMemoryObject> nextFrame() override { return nullptr; }
    bool isReady() const override { return false; }
};

#endif // VIMBAX_ENABLED
