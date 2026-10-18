#include "sequential_frame_source.h"

#include <liblbt/frame_memory_object.h>

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cctype>
#include <iostream>

namespace {

// Same convention as DiskImageFrameSource::extractNumericKey: trailing digit
// run before the extension (i.e. the embedded timestamp in
// frame_<index>_<timestampNs>.jpg), used only to sort files into capture order.
long long extractNumericKey(const std::filesystem::path& p) {
    std::string name = p.stem().string();
    int i = static_cast<int>(name.size()) - 1;
    while (i >= 0 && !std::isdigit(static_cast<unsigned char>(name[i]))) --i;
    if (i < 0) return -1;
    int end = i;
    while (i >= 0 && std::isdigit(static_cast<unsigned char>(name[i]))) --i;
    int start = i + 1;
    std::string digits = name.substr(start, end - start + 1);
    try {
        return std::stoll(digits);
    } catch (...) {
        return -1;
    }
}

bool endsWithCaseInsensitive(const std::string& s, const std::string& suf) {
    if (s.size() < suf.size()) return false;
    auto toLower = [](std::string x) {
        std::transform(x.begin(), x.end(), x.begin(), [](unsigned char c) { return std::tolower(c); });
        return x;
    };
    return toLower(s.substr(s.size() - suf.size())) == toLower(suf);
}

} // namespace

SequentialFrameSource::SequentialFrameSource(const std::string& directory, const std::string& pattern) {
    const bool wildcardSuffix = (!pattern.empty() && pattern.size() > 2 && pattern[0] == '*' && pattern[1] == '.');
    const std::string suffix = wildcardSuffix ? pattern.substr(1) : pattern;

    std::vector<std::pair<long long, std::filesystem::path>> collected;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        const auto pathStr = entry.path().string();
        if (pattern == "*" || pattern.empty()) {
            collected.emplace_back(extractNumericKey(entry.path()), entry.path());
        } else if (wildcardSuffix) {
            if (endsWithCaseInsensitive(pathStr, suffix)) {
                collected.emplace_back(extractNumericKey(entry.path()), entry.path());
            }
        } else if (pathStr.find(pattern) != std::string::npos) {
            collected.emplace_back(extractNumericKey(entry.path()), entry.path());
        }
    }

    // Sort by numeric key ascending (matches DiskImageFrameSource's ordering rule)
    std::sort(collected.begin(), collected.end(), [](const auto& a, const auto& b) {
        if (a.first == b.first) return a.second.string() < b.second.string();
        if (a.first < 0) return false;
        if (b.first < 0) return true;
        return a.first < b.first;
    });

    imagePaths_.reserve(collected.size());
    for (auto& kv : collected) {
        imagePaths_.push_back(kv.second);
    }

    std::cout << "[SequentialFrameSource] Found " << imagePaths_.size()
              << " frame(s) in " << directory << std::endl;
}

bool SequentialFrameSource::isReady() const {
    return !imagePaths_.empty();
}

std::unique_ptr<FrameMemoryObject> SequentialFrameSource::nextFrame() {
    while (currentIndex_ < imagePaths_.size()) {
        const std::filesystem::path path = imagePaths_[currentIndex_++];
        cv::Mat img = cv::imread(path.string(), cv::IMREAD_UNCHANGED);
        if (img.empty()) {
            std::cerr << "[SequentialFrameSource] Failed to read frame: " << path << " - skipping" << std::endl;
            continue;
        }

        auto retValue = std::make_unique<FrameMemoryObject>(img);

        // Parse timestamp between the last '_' and the extension, same convention
        // as DiskImageFrameSource::nextFrame().
        const std::string filename = path.filename().string();
        size_t lastUnderscore = filename.rfind('_');
        size_t dotPos = filename.rfind('.');
        if (lastUnderscore != std::string::npos && dotPos != std::string::npos &&
            lastUnderscore < dotPos && (dotPos - lastUnderscore) > 1) {
            std::string timestampStr = filename.substr(lastUnderscore + 1, dotPos - lastUnderscore - 1);
            try {
                retValue->setTimestampNs(std::stoull(timestampStr));
            } catch (const std::exception&) {
                retValue->touchTimestampNow();
            }
        } else {
            retValue->touchTimestampNow();
        }

        return retValue;
    }
    return nullptr;
}
