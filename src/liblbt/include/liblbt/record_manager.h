#pragma once

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <optional>
#include <filesystem>
#include <chrono>

#include <liblbt/iframe_source.h>
#include <liblbt/pipeline_config_loader.h>
#include <liblbt/process_step.h>
#include <liblbt/filter_config.h>
#include <liblbt/result_logger.h>

namespace lbt {

/**
 * RecordManager handles recording sessions with frame acquisition,
 * processing pipeline execution, and output management.
 * 
 * State flow: preparation -> recording -> stopped/failure -> packed
 */
class RecordManager {
public:
    enum class State {
        UNKNOWN,      // Default state
        PREPARATION,  // Initializing resources
        RECORDING,    // Actively recording
        STOPPED,      // Recording stopped successfully
        FAILURE,      // Recording failed
        PACKED        // Recording compressed and archived
    };

    /**
     * Configuration for a recording session
     */
    struct RecordConfig {
        std::shared_ptr<IFrameSource> frameSource;      // Source of frames
        PipelineConfigLoader::PipelineConfig pipeline;  // Processing pipeline
        double totalTimeSeconds;                         // Duration of recording
        double targetFPS;                                // Desired frame rate
        std::optional<std::string> calibrationFile;     // Optional calibration XML path
        std::optional<std::string> studyFile;           // Optional study JSON path
        std::string recordUID;                           // Unique identifier for this record
        std::string cameraUID;                           // Camera identifier (for locking)
        std::string outputDir = "/tmp/record3k";        // Base output directory
        ResultLogger::LogMode logMode = ResultLogger::LogMode::NONE;  // Result logging mode
    };

    /**
     * Constructor
     * @param config Recording configuration
     */
    explicit RecordManager(RecordConfig config);
    
    /**
     * Destructor - ensures recording is stopped
     */
    ~RecordManager();

    // Disable copy and move
    RecordManager(const RecordManager&) = delete;
    RecordManager& operator=(const RecordManager&) = delete;
    RecordManager(RecordManager&&) = delete;
    RecordManager& operator=(RecordManager&&) = delete;

    /**
     * Start the recording in a separate thread
     * Uses FPS timing logic to maintain target frame rate
     * @return true if recording started successfully, false otherwise
     */
    bool start();

    /**
     * Stop the recording gracefully
     * Waits for the recording thread to finish
     */
    void stop();

    /**
     * Pack the recording into a tar.gz archive and move to data/files
     * Creates structure:
     *   file-rec-<uid>/
     *     record-<uid>.json
     *     study-<uid>.json (if provided)
     *     frames/
     *     pipeline-<uid>.json
     *     others/
     *       calibration-<uid>.xml (if provided)
     * @return Path to the packed file, or nullopt on failure
     */
    std::optional<std::filesystem::path> pack();

    /**
     * Get current state of the recording
     */
    State getState() const;

    /**
     * Get human-readable state string
     */
    std::string getStateString() const;

    /**
     * Get the output directory path for this recording
     */
    std::filesystem::path getOutputPath() const;

    /**
     * Get recording statistics
     */
    struct Statistics {
        int framesRecorded = 0;
        double actualFPS = 0.0;
        std::chrono::steady_clock::time_point startTime;
        std::chrono::steady_clock::time_point endTime;
        bool completed = false;
    };
    
    Statistics getStatistics() const;

private:
    /**
     * Main recording loop executed in separate thread
     */
    void recordingLoop();

    /**
     * Create output directory structure
     */
    bool createOutputStructure();

    /**
     * Save recording metadata to JSON
     */
    bool saveRecordMetadata();

    /**
     * Copy calibration and study files if provided
     */
    void copyOptionalFiles();

    // Configuration
    RecordConfig m_config;

    // State management
    std::atomic<State> m_state{State::UNKNOWN};
    mutable std::mutex m_stateMutex;

    // Thread management
    std::unique_ptr<std::thread> m_recordingThread;
    std::atomic<bool> m_stopRequested{false};

    // Recording statistics
    Statistics m_stats;
    mutable std::mutex m_statsMutex;

    // Paths
    std::filesystem::path m_recordDir;      // /tmp/record3k/file-rec-<uid>
    std::filesystem::path m_framesDir;      // /tmp/record3k/file-rec-<uid>/frames
    std::filesystem::path m_othersDir;      // /tmp/record3k/file-rec-<uid>/others
    
    // Result logging
    std::unique_ptr<ResultLogger> m_resultLogger;
};

} // namespace lbt
