#include "disk_image_frame_source.h"
#include <algorithm>
#include <cctype>

DiskImageFrameSource::DiskImageFrameSource(const std::string& directory, const std::string& pattern)
{
    const bool wildcardSuffix = (!pattern.empty() && pattern.size() > 2 && pattern[0] == '*' && pattern[1] == '.');
    const std::string suffix = wildcardSuffix ? pattern.substr(1) : pattern; // e.g. ".jpg"

    auto toLower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
        return s;
    };

    auto endsWithCaseInsensitive = [&](const std::string& s, const std::string& suf) {
        if (s.size() < suf.size()) return false;
        std::string a = toLower(s.substr(s.size() - suf.size()));
        std::string b = toLower(suf);
        return a == b;
    };

    auto extractNumericKey = [&](const std::filesystem::path& p) -> long long {
        // Extract the last continuous digit run before extension from the filename
        std::string name = p.stem().string(); // filename without extension
        // scan backwards to find last digit run
        int i = static_cast<int>(name.size()) - 1;
        while (i >= 0 && !std::isdigit(static_cast<unsigned char>(name[i]))) --i;
        if (i < 0) return -1; // no digits
        int end = i;
        while (i >= 0 && std::isdigit(static_cast<unsigned char>(name[i]))) --i;
        int start = i + 1;
        std::string digits = name.substr(start, end - start + 1);
        try {
            return std::stoll(digits);
        } catch (...) {
            return -1;
        }
    };

    std::vector<std::pair<long long, std::string>> collected;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        const auto pathStr = entry.path().string();
        if (pattern == "*" || pattern.empty()) {
            collected.emplace_back(extractNumericKey(entry.path()), pathStr);
        } else if (wildcardSuffix) {
            if (endsWithCaseInsensitive(pathStr, suffix)) {
                collected.emplace_back(extractNumericKey(entry.path()), pathStr);
            }
        } else {
            // Fallback simple substring match for custom patterns
            if (pathStr.find(pattern) != std::string::npos) {
                collected.emplace_back(extractNumericKey(entry.path()), pathStr);
            }
        }
    }

    // Sort by numeric key ascending; fallback to lexicographic if equal or missing key
    std::sort(collected.begin(), collected.end(), [](const auto& a, const auto& b){
        if (a.first == b.first) return a.second < b.second;
        if (a.first < 0) return false; // put non-numeric after numeric
        if (b.first < 0) return true;
        return a.first < b.first;
    });

    // Preload up to DISK_IMAGE_MAX_PRELOAD images into RAM for profiling stability
    const size_t preloadCap = DISK_IMAGE_MAX_PRELOAD;
    images_.reserve(std::min(preloadCap, collected.size()));
    size_t count = 0;
    for (auto& kv : collected) {
        if (count >= preloadCap) break;
        cv::Mat img = cv::imread(kv.second, cv::IMREAD_UNCHANGED);
        if (!img.empty()) {
            images_.push_back(std::move(img));
            ++count;
        }
    }
    currentIndex_ = 0;
}

std::unique_ptr<FrameMemoryObject> DiskImageFrameSource::nextFrame()
{
    if (currentIndex_ >= images_.size())
        return nullptr;
    // Construct a FrameMemoryObject that references the preloaded cv::Mat (no copy)
    const cv::Mat& img = images_[currentIndex_++];
    auto retValue = std::make_unique<FrameMemoryObject>(img);
    
    retValue->touchTimestampNow(); // set current timestamp

    return retValue;
}

bool DiskImageFrameSource::isReady() const
{
    return !images_.empty();
}