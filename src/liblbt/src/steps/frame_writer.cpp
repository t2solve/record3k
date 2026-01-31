#include "steps/frame_writer.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>

FrameWriterCPUStep::FrameWriterCPUStep() : frameCounter_(0) {}


// Helper: format nanoseconds timestamp as string (just use the raw nanosecond value)
static std::string formatTimestamp(uint64_t tsNs) {
    if (tsNs == 0) return "0";
    // Simply return the nanosecond value as a string
    return std::to_string(tsNs);
}

FrameMemoryObject FrameWriterCPUStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    // Check if frame writing is enabled (default: true for backward compatibility)
    bool enabled = config.getParameter("enabled", true);
    if (!enabled) {
        // Skip writing if disabled, just pass through
        return input;
    }
    
    // Get output directory from config, default to "output_frames"
    std::string outputDir = config.getParameter("output_dir", std::string("output_frames"));
    
    // Get format from config, default to "png"
    std::string format = config.getParameter("format", std::string("png"));
    
    // Create output directory if it doesn't exist
    std::filesystem::path outPath(outputDir);
    if (!std::filesystem::exists(outPath)) {
        std::filesystem::create_directories(outPath);
        logInfo("[FrameWriter] Created output directory: " + outPath.string());
    }
    
    // Increment frame counter
    int currentFrame = frameCounter_.fetch_add(1);
    
    // Ensure frame is on CPU for writing
    cv::Mat cpuFrame = input.getCpuMat();
    
    // Verify frame is valid before writing
    if (cpuFrame.empty()) {
        std::stringstream ss;
        ss << "[FrameWriter] ERROR: Frame " << currentFrame << " is empty! "
           << "Input isEmpty=" << input.isEmpty() 
           << ", memoryLocation=" << static_cast<int>(input.getMemoryLocation());
        logError(ss.str());
        // Return input unchanged even on error
        return input;
    }
    
    // Use frame timestamp if available
    uint64_t tsNs = input.timestampNs();
    std::stringstream filename;
    filename << "frame_"
             << std::setfill('0') << std::setw(6) << currentFrame
             << "_" << formatTimestamp(tsNs)
             << "." << format;
    std::filesystem::path fullPath = outPath / filename.str();
    
    // Write frame to disk
    try {
        if (!cv::imwrite(fullPath.string(), cpuFrame)) {
            logError("[FrameWriter] Failed to write frame: " + fullPath.string());
        } else if (currentFrame % 100 == 0) {
            // Log progress every 100 frames
            logInfo("[FrameWriter] Wrote frame " + std::to_string(currentFrame) + " to " + outputDir);
        }
    } catch (const cv::Exception& e) {
        logError("[FrameWriter] OpenCV exception writing frame: " + std::string(e.what()));
    }
    
    // Return input unchanged (pass-through)
    return input;
}
