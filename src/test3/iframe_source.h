#pragma once
#include <liblbt/frame_memory_object.h>

class IFrameSource {
public:
    virtual ~IFrameSource() = default;
    // Returns next frame, or nullptr if done
    virtual std::unique_ptr<FrameMemoryObject> nextFrame() = 0;
    // Check if the source is ready (configured and able to provide frames)
    virtual bool isReady() const = 0;
};