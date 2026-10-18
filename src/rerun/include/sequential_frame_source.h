#pragma once

#include <liblbt/iframe_source.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Minimal IFrameSource that streams frames from a directory of already-captured
// images, one at a time, in ascending filename-timestamp order. Unlike
// DiskImageFrameSource (liblbt), this does not preload images into RAM and has
// no size cap — needed because rerun operates on full records that can exceed
// DiskImageFrameSource's DISK_IMAGE_MAX_PRELOAD (1000) frames.
class SequentialFrameSource : public IFrameSource {
public:
    explicit SequentialFrameSource(const std::string& directory, const std::string& pattern = "*.jpg");

    std::unique_ptr<FrameMemoryObject> nextFrame() override;
    bool isReady() const override;

    size_t getFrameCount() const { return imagePaths_.size(); }
    size_t getCurrentIndex() const { return currentIndex_; }

private:
    std::vector<std::filesystem::path> imagePaths_;
    size_t currentIndex_{0};
};
