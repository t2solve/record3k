#include <liblbt/buffered_frame_source.h>
#include <liblbt/frame_memory_object.h>

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <cctype>
#include <iostream>

namespace {

// Same convention as DiskImageFrameSource/SequentialFrameSource: trailing digit run
// before the extension (i.e. the embedded timestamp in frame_<index>_<timestampNs>.jpg),
// used only to sort files into capture order.
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

// Parses the timestamp between the last '_' and the extension, same convention as
// DiskImageFrameSource::nextFrame()/SequentialFrameSource::nextFrame().
uint64_t parseTimestampFromFilename(const std::filesystem::path& path) {
    const std::string filename = path.filename().string();
    size_t lastUnderscore = filename.rfind('_');
    size_t dotPos = filename.rfind('.');
    if (lastUnderscore != std::string::npos && dotPos != std::string::npos &&
        lastUnderscore < dotPos && (dotPos - lastUnderscore) > 1) {
        std::string timestampStr = filename.substr(lastUnderscore + 1, dotPos - lastUnderscore - 1);
        try {
            return std::stoull(timestampStr);
        } catch (const std::exception&) {
            return 0;
        }
    }
    return 0;
}

} // namespace

BufferedFrameSource::BufferedFrameSource(const std::string& directory, const std::string& pattern,
                                          size_t maxFrames) {
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

    if (maxFrames > 0 && collected.size() > maxFrames) {
        collected.resize(maxFrames);
    }

    frames_.reserve(collected.size());
    timestamps_.reserve(collected.size());
    paths_.reserve(collected.size());

    size_t failedReads = 0;
    for (auto& kv : collected) {
        cv::Mat img = cv::imread(kv.second.string(), cv::IMREAD_UNCHANGED);
        if (img.empty()) {
            ++failedReads;
            continue;
        }
        frames_.push_back(std::move(img));
        timestamps_.push_back(parseTimestampFromFilename(kv.second));
        paths_.push_back(kv.second);
    }

    std::cout << "[BufferedFrameSource] Decoded " << frames_.size() << " frame(s) into RAM from " << directory;
    if (failedReads > 0) {
        std::cout << " (" << failedReads << " file(s) failed to decode, skipped)";
    }
    std::cout << std::endl;
}

bool BufferedFrameSource::isReady() const {
    return !frames_.empty();
}

std::unique_ptr<FrameMemoryObject> BufferedFrameSource::nextFrame() {
    if (currentIndex_ >= frames_.size()) return nullptr;
    size_t i = currentIndex_++;
    auto retValue = std::make_unique<FrameMemoryObject>(frames_[i]);
    retValue->setTimestampNs(timestamps_[i]);
    return retValue;
}
