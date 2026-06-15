#pragma once

#include "../process_step.h"
#include <opencv2/opencv.hpp>
#include <vector>
#include <mutex>
#include <string>
#include <cstdint>

struct FrameLogEntry {
    int frame_idx;
    uint64_t timestamp_ns;
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Point2f> centers;
    std::vector<cv::RotatedRect> rects;
};

// Pass-through step that collects contour/center metadata each frame into RAM.
// Call flush(dbPath) at the end of a recording to persist everything to SQLite.
class DebugLoggerStep : public ProcessStep {
public:
    DebugLoggerStep();

    FrameMemoryObject process(const FrameMemoryObject& input, const FilterConfig& config) override;
    std::string getName() const override { return "DebugLogger_CPU"; }
    FilterType getFilterType() const override { return FilterType::DEBUG_LOGGER; }
    bool isAvailable() const override { return true; }
    ProcessingMode getMode() const override { return ProcessingMode::CPU_ONLY; }
    MemoryLocation getPreferredMemoryLocation() const override { return MemoryLocation::CPU; }

    // Write accumulated entries to a SQLite file. Thread-safe.
    // Returns true on success.
    bool flush(const std::string& dbPath);

    size_t entryCount() const;

private:
    mutable std::mutex m_mutex;
    std::vector<FrameLogEntry> m_entries;
    int m_frameCounter{0};
};
