#include "steps/frame_writer.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>

FrameWriterCPUStep::FrameWriterCPUStep() : frameCounter_(0) {}


// Helper: format nanoseconds timestamp as string (YYYYMMDD_HHMMSS_mmm)
static std::string formatTimestamp(uint64_t tsNs) {
    if (tsNs == 0) return "notimestamp";
    std::time_t t = tsNs / 1000000000ULL;
    uint64_t ms = (tsNs / 1000000ULL) % 1000ULL;
    std::stringstream ss;
    ss << std::put_time(std::localtime(&t), "%Y%m%d_%H%M%S")
       << "_" << std::setfill('0') << std::setw(3) << ms;
    return ss.str();
}

FrameMemoryObject FrameWriterCPUStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    // Get output directory from config, default to "output_frames"
    std::string outputDir = config.getParameter("output_dir", std::string("output_frames"));
    
    // Get format from config, default to "png"
    std::string format = config.getParameter("format", std::string("png"));
    
    // Create output directory if it doesn't exist
    std::filesystem::path outPath(outputDir);
    if (!std::filesystem::exists(outPath)) {
        std::filesystem::create_directories(outPath);
    }
    
    // Increment frame counter
    int currentFrame = frameCounter_.fetch_add(1);
    // Use frame timestamp if available
    uint64_t tsNs = input.timestampNs();
    std::stringstream filename;
    filename << "frame_"
             << std::setfill('0') << std::setw(6) << currentFrame
             << "_" << formatTimestamp(tsNs)
             << "." << format;
    std::filesystem::path fullPath = outPath / filename.str();
    
    // Ensure frame is on CPU for writing
    cv::Mat cpuFrame = input.getCpuMat();
    
    // Write frame to disk
    try {
        if (!cv::imwrite(fullPath.string(), cpuFrame)) {
            std::cerr << "[FrameWriter] Failed to write frame: " << fullPath << std::endl;
        }
    } catch (const cv::Exception& e) {
        std::cerr << "[FrameWriter] OpenCV exception writing frame: " << e.what() << std::endl;
    }
    
    // Return input unchanged (pass-through)
    return input;
}
