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
    imagePaths_.reserve(std::min(preloadCap, collected.size()));
    size_t count = 0;
    for (auto& kv : collected) {
        if (count >= preloadCap) break;
        cv::Mat img = cv::imread(kv.second, cv::IMREAD_UNCHANGED);
        if (!img.empty()) {
            images_.push_back(std::move(img));
            imagePaths_.push_back(std::filesystem::path(kv.second));
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
    const cv::Mat& img = images_[currentIndex_];
    auto retValue = std::make_unique<FrameMemoryObject>(img);
    
    // Try to extract timestamp from filename (e.g., "frame_000000_190007170444255.jpg")
    if (currentIndex_ < imagePaths_.size()) {
        const std::string filename = imagePaths_[currentIndex_].filename().string();
        
        // DEBUG: Log first few filenames for debugging
        if (currentIndex_ < 5) {
            std::cout << "[DiskImageFrameSource] Frame " << currentIndex_ 
                      << " filename: " << filename << std::endl;
        }
        
        // Find the last underscore before the extension
        size_t lastUnderscore = filename.rfind('_');
        size_t dotPos = filename.rfind('.');
        
        if (lastUnderscore != std::string::npos && dotPos != std::string::npos && 
            lastUnderscore < dotPos && (dotPos - lastUnderscore) > 1) {
            // Extract timestamp string between last underscore and extension
            std::string timestampStr = filename.substr(lastUnderscore + 1, dotPos - lastUnderscore - 1);
            
            // Try to parse as uint64_t (nanoseconds)
            try {
                uint64_t timestamp = std::stoull(timestampStr);
                retValue->setTimestampNs(timestamp);
                
                // DEBUG: Log first few parsed timestamps
                if (currentIndex_ < 5) {
                    std::cout << "[DiskImageFrameSource] Parsed timestamp: " << timestamp 
                              << " from string: '" << timestampStr << "'" << std::endl;
                }
            } catch (const std::exception& e) {
                // Parsing failed, use current time as fallback
                if (currentIndex_ < 5) {
                    std::cout << "[DiskImageFrameSource] Failed to parse timestamp from: '" 
                              << timestampStr << "', error: " << e.what() << std::endl;
                }
                retValue->touchTimestampNow();
            }
        } else {
            // Filename doesn't match expected pattern, use current time
            if (currentIndex_ < 5) {
                std::cout << "[DiskImageFrameSource] Filename pattern mismatch "
                          << "(lastUnderscore=" << lastUnderscore << ", dotPos=" << dotPos << ")" << std::endl;
            }
            retValue->touchTimestampNow();
        }
    } else {
        // No path info available, use current time
        std::cout << "[DiskImageFrameSource] WARNING: No path info for index " << currentIndex_ << std::endl;
        retValue->touchTimestampNow();
    }
    
    currentIndex_++;
    return retValue;
}

bool DiskImageFrameSource::isReady() const
{
    return !images_.empty();
}

void DiskImageFrameSource::seekToFrame(size_t index)
{
    if (index < images_.size()) {
        currentIndex_ = index;
    }
}