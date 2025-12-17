#include "vimbax_frame_source.h"
#ifdef VIMBAX_ENABLED

#include "frame_converter.h"
#include <QDebug>
#include <opencv2/opencv.hpp>
#include <chrono>

using namespace VmbCPP;

// Pull-based single-shot acquisition; no observer used

VimbaXFrameSource::VimbaXFrameSource(const std::string& cameraSerial,
                                     const std::string& interfaceIdOrMac,
                                     bool debugLog)
    : system_(VmbSystem::GetInstance()), debug_(debugLog) {
    ready_ = initialize(cameraSerial, interfaceIdOrMac);
}

VimbaXFrameSource::~VimbaXFrameSource() {
    shutdown();
}

bool VimbaXFrameSource::initialize(const std::string& cameraSerial, const std::string& interfaceIdOrMac) {
    VmbErrorType err = VmbErrorSuccess;
    err = system_.Startup();
    if (err != VmbErrorSuccess) {
        if (debug_) qWarning() << "[VimbaXFrameSource] Vimba system Startup failed:" << err;
        return false;
    }

    // Pick camera by serial and/or interface id (MAC)
    CameraPtrVector cameras;
    err = system_.GetCameras(cameras);
    if (err != VmbErrorSuccess || cameras.empty()) {
        if (debug_) qWarning() << "[VimbaXFrameSource] No cameras found or GetCameras failed:" << err;
        return false;
    }

    auto matches = [&](const CameraPtr& cam) -> bool {
        std::string ser; (void)cam->GetSerialNumber(ser);
        std::string iid; (void)cam->GetInterfaceID(iid);
        bool okSerial = cameraSerial.empty() || ser == cameraSerial;
        bool okIface = interfaceIdOrMac.empty() || iid.find(interfaceIdOrMac) != std::string::npos;
        return okSerial && okIface;
    };

    for (auto& cam : cameras) {
        if (matches(cam)) { camera_ = cam; break; }
    }
    if (!camera_) {
        // fallback first camera
        camera_ = cameras.front();
        if (debug_) qWarning() << "[VimbaXFrameSource] Requested camera not found; using first available.";
    }

    // Open
    err = camera_->Open(VmbAccessModeFull);
    if (err != VmbErrorSuccess) {
        if (debug_) qWarning() << "[VimbaXFrameSource] Camera open failed:" << err;
        return false;
    }

    // Pre-allocate a single frame buffer for AcquireSingleImage
    return ensurePreallocFrame();
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
    try {
        system_.Shutdown();
    } catch (const std::exception& e) {
        qWarning() << "[VimbaXFrameSource] Exception during Vimba system shutdown:" << e.what();
    } catch (...) {
        qWarning() << "[VimbaXFrameSource] Unknown exception during Vimba system shutdown";
    }
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
