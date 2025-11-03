#include "disk_image_frame_source.h"

DiskImageFrameSource::DiskImageFrameSource(const std::string& directory, const std::string& pattern)
{
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

std::unique_ptr<FrameMemoryObject> DiskImageFrameSource::nextFrame()
{
    if (current_ == files_.end())
        return nullptr;

    cv::Mat img = cv::imread(*current_, cv::IMREAD_UNCHANGED);
    ++current_;
    if (img.empty())
        return nullptr;
    return std::make_unique<FrameMemoryObject>(img);
}

bool DiskImageFrameSource::isReady() const
{
    return !files_.empty();
}