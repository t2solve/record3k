#pragma once
#include "iframe_source.h"
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <memory>
#include <filesystem>

// DiskImageFrameSource source("/path/to/images", "*.jpg");
// while (auto frame = source.nextFrame()) {
//     // process frame
// }

class DiskImageFrameSource : public IFrameSource {
public:
    DiskImageFrameSource(const std::string& directory, const std::string& pattern = "*.jpg")
    {
        // Collect all matching image files in the directory
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file()) {
                auto path = entry.path().string();
                // Simple pattern match (could use regex for more flexibility)
                if (pattern == "*" || path.find(pattern.substr(1)) != std::string::npos) {
                    files_.push_back(path);
                }
            }
        }
        current_ = files_.begin();
    }

    std::unique_ptr<FrameMemoryObject> nextFrame() override
    {
        if (current_ == files_.end())
            return nullptr;

        cv::Mat img = cv::imread(*current_, cv::IMREAD_UNCHANGED);
        ++current_;
        if (img.empty())
            return nullptr;
        return std::make_unique<FrameMemoryObject>(img);
    }

private:
    std::vector<std::string> files_;
    std::vector<std::string>::iterator current_;
};