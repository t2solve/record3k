#pragma once
#include "iframe_source.h"
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <memory>
#include <filesystem>

class DiskImageFrameSource : public IFrameSource {
public:
    DiskImageFrameSource(const std::string& directory, const std::string& pattern = "*.jpg");

    std::unique_ptr<FrameMemoryObject> nextFrame() override;
    bool isReady() const override;

private:
    std::vector<std::string> files_;
    std::vector<std::string>::iterator current_;
};