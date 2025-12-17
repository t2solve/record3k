#pragma once
#include "iframe_source.h"
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <memory>
#include <filesystem>

const size_t DISK_IMAGE_MAX_PRELOAD = 1000; // Max number of images to preload for stable profiling
class DiskImageFrameSource : public IFrameSource {
public:
    DiskImageFrameSource(const std::string& directory, const std::string& pattern = "*.jpg");

    std::unique_ptr<FrameMemoryObject> nextFrame() override;
    bool isReady() const override;

private:
    // Preloaded images (up to a cap) for stable profiling without disk I/O jitter
    std::vector<cv::Mat> images_;
    size_t currentIndex_ {0};
};