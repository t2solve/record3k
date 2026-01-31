#pragma once

#include <string>
#include <filesystem>
#include <fstream>
#include <memory>
#include <opencv2/opencv.hpp>
#include <liblbt/frame_memory_object.h>

namespace lbt {

/**
 * ResultLogger handles logging of frame processing results to disk.
 * Supports different logging modes for debugging and production use.
 */
class ResultLogger {
public:
    enum class LogMode {
        NONE,           // No logging
        DEBUG,          // Full debug: input frames, all metadata, debug images
        PRODUCTION      // Production: frames only, minimal metadata
    };

    /**
     * Configuration for ResultLogger
     */
    struct Config {
        std::filesystem::path recordDir;       // Base record directory (file-rec-XXX)
        std::filesystem::path framesDir;       // Frames subdirectory
        std::filesystem::path othersDir;       // Others subdirectory
        LogMode mode = LogMode::NONE;          // Logging mode
        bool saveMetadata = true;              // Save metadata to files
        bool saveDebugImages = true;           // Save debug visualization images
    };

    /**
     * Constructor
     * @param config Logger configuration
     */
    explicit ResultLogger(const Config& config);

    /**
     * Destructor - ensures log files are closed
     */
    ~ResultLogger();

    /**
     * Log a processed frame and its input
     * @param frameIndex Frame number/index
     * @param inputFrame Original input frame (before processing)
     * @param resultFrame Processed frame result (after pipeline)
     */
    void logFrame(int frameIndex, 
                  const FrameMemoryObject& inputFrame,
                  const FrameMemoryObject& resultFrame);

    /**
     * Log metadata from a frame to metadata log file
     * @param frameIndex Frame number
     * @param frame Frame containing metadata
     */
    void logMetadata(int frameIndex, const FrameMemoryObject& frame);

    /**
     * Write final summary statistics
     * @param totalFrames Total frames logged
     * @param duration Duration in seconds
     */
    void writeSummary(int totalFrames, double duration);

private:
    Config m_config;
    std::ofstream m_metadataLog;
    std::filesystem::path m_debugDir;

    void initializeDirectories();
    void openLogFiles();
    void saveFrameImage(const std::filesystem::path& path, const FrameMemoryObject& frame);
    std::string metadataToString(const FrameMemoryObject& frame);
};

} // namespace lbt
