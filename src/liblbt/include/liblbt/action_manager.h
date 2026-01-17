#pragma once

#include <liblbt/api/api_models.h>
#include <liblbt/iframe_source.h>
#include <liblbt/processing_pipeline.h>
#include <liblbt/process_step.h>
#include <liblbt/json_store.h>
#include <liblbt/camera_manager.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <atomic>
#include <optional>

namespace lbt {

// ActionManager orchestrates recording sessions: calibration records, pipeline tests, and actual recordings.
// Each record runs in its own thread with its own pipeline and frame source.
class ActionManager {
public:
    enum class RecordType {
        Calibration,
        PipelineTest,
        Recording
    };

    struct RecordSession {
        std::string recordUID;
        RecordType type;
        api::RecordInfo info;
        std::unique_ptr<IFrameSource> frameSource;
        std::vector<std::shared_ptr<ProcessStep>> pipeline;
        std::unique_ptr<std::thread> thread;
        std::atomic<bool> stopRequested{false};
        std::atomic<bool> running{false};
        std::filesystem::path outputPath;
    };

    explicit ActionManager(std::filesystem::path recordsDir, std::filesystem::path outputDir);
    ~ActionManager();

    // Start a calibration recording session
    // Returns recordUID or nullopt on failure
    std::optional<std::string> startCalibration(
        const std::string& camUID,
        const std::string& calibrationUID,
        const std::string& pipelineUID,
        double durationInSeconds  =180,
        const std::string& description = "");

    // Start a pipeline test recording session
    // Returns recordUID or nullopt on failure
    std::optional<std::string> startPipelineTest(
        const std::string& camUID,
        const std::string& pipelineUID,
        double durationInSeconds,
        const std::string& description = "");

    // Start a regular recording session
    // Returns recordUID or nullopt on failure
    std::optional<std::string> startRecording(
        const std::string& camUID,
        const std::string& calibrationUID,
        const std::string& pipelineUID,
        const std::string& studyUID,
        double durationInSeconds,
        const std::string& description = "");

    // Stop a recording session by recordUID
    bool stopRecording(const std::string& recordUID);

    // Get current status of a recording
    std::optional<api::RecordInfo> getRecordInfo(const std::string& recordUID) const;

    // Get list of all active recordings
    std::vector<api::RecordInfo> getActiveRecordings() const;

    // Clean up finished sessions
    void cleanupFinished();

private:
    std::filesystem::path recordsDir_;
    std::filesystem::path outputDir_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<RecordSession>> sessions_;

    // Generate unique record UID
    std::string generateRecordUID(RecordType type) const;

    // Create frame source for given camera
    std::unique_ptr<IFrameSource> createFrameSource(const std::string& camUID);

    // Create Default pipeline for test and calibration
    std::vector<std::shared_ptr<ProcessStep>> createDefaultPipeline();

    // Load pipeline from configuration
    std::vector<std::shared_ptr<ProcessStep>> loadPipeline(const std::string& pipelineUID);

    // Recording thread worker function
    void recordingWorker(const std::string& recordUID);

    // Persist record info to disk
    void persistRecordInfo(const api::RecordInfo& info);

    // Update record status
    void updateRecordStatus(const std::string& recordUID, const std::string& status);
};

} // namespace lbt
