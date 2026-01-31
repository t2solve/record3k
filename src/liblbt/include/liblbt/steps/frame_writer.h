#pragma once

#include "../process_step.h"
#include <opencv2/opencv.hpp>
#include <atomic>
#include <filesystem>

class FrameWriterCPUStep : public ProcessStep {
public:
    FrameWriterCPUStep();
    
    FrameMemoryObject process(const FrameMemoryObject& input, const FilterConfig& config) override;
    std::string getName() const override { return "FrameWriter_CPU"; }
    FilterType getFilterType() const override { return FilterType::FRAME_WRITER; }
    bool isAvailable() const override { return true; }
    ProcessingMode getMode() const override { return ProcessingMode::CPU_ONLY; }
    MemoryLocation getPreferredMemoryLocation() const override { return MemoryLocation::CPU; }
    
private:
    std::atomic<int> frameCounter_{0};
    std::string getTimestamp() const;
};
