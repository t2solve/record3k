#pragma once

#include "iframe_source.h"

#include <filesystem>
#include <memory>
#include <opencv2/core.hpp>
#include <string>
#include <vector>

// IFrameSource that decodes every matching frame in a directory into RAM once, up
// front, with no size cap by default (unlike DiskImageFrameSource's
// DISK_IMAGE_MAX_PRELOAD=1000) -- pass maxFrames to cap it explicitly.
// Two ways to consume it:
//  - IFrameSource interface (nextFrame()/isReady()): one-shot sequential streaming,
//    same as DiskImageFrameSource/SequentialFrameSource -- reset() rewinds the cursor
//    if you want a second full pass through it this way.
//  - Random access (frameCount()/frameAt()/timestampAt()): lets a caller replay the
//    *same* decoded buffer once per pipeline config in a sweep without re-decoding --
//    this is the reason it exists (src/pipeoptimizer/plan.md step 2: decode once, run
//    every swept config against the same in-memory frames).
// Frames are read-only cv::Mat after construction; safe to share (read-only) across
// worker threads.
class BufferedFrameSource : public IFrameSource {
public:
    // maxFrames caps how many frames (in sorted capture order) get decoded -- 0 means
    // no limit. Frames beyond the cap are never cv::imread'd or held in RAM, unlike
    // decoding everything and discarding the tail -- real records can run 5000+
    // frames, and only decoding what will actually be scored keeps memory bounded.
    explicit BufferedFrameSource(const std::string& directory, const std::string& pattern = "*.jpg",
                                  size_t maxFrames = 0);

    // IFrameSource
    std::unique_ptr<FrameMemoryObject> nextFrame() override;
    bool isReady() const override;

    // Random access into the full in-RAM buffer.
    size_t frameCount() const { return frames_.size(); }
    const cv::Mat& frameAt(size_t index) const { return frames_.at(index); }
    uint64_t timestampAt(size_t index) const { return timestamps_.at(index); }
    const std::filesystem::path& pathAt(size_t index) const { return paths_.at(index); }

    // Rewind the IFrameSource-style sequential cursor to the start.
    void reset() { currentIndex_ = 0; }

private:
    std::vector<cv::Mat> frames_;
    std::vector<uint64_t> timestamps_;
    std::vector<std::filesystem::path> paths_;
    size_t currentIndex_{0};
};
