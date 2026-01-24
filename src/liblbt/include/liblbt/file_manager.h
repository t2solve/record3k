#pragma once

#include <string>
#include <filesystem>
#include <memory>
#include <functional>
#include <mutex>
#include <map>
#include <optional>
#include <json/json.h>
#include <liblbt/api/api_models.h>
#include <liblbt/json_store.h>

namespace lbt {

// Forward declare
class RecordManager;

/**
 * FileManager - Singleton for managing recording file lifecycle
 * 
 * Responsibilities:
 * - Centralized path management for recordings
 * - Observer pattern for RecordManager state changes
 * - Automatic packing of completed recordings
 * - Creation and management of api::FileInfo entries
 * - Integration with JsonStore for persistent file metadata
 */
class FileManager {
public:
    /**
     * Get singleton instance
     */
    static FileManager& instance();
    
    /**
     * Initialize the FileManager with data paths
     * @param dataRootPath Root directory for data (default: "data")
     * @param tempOutputPath Temporary output directory for recordings (default: "/tmp/record3k")
     */
    void initialize(const std::filesystem::path& dataRootPath = "data",
                   const std::filesystem::path& tempOutputPath = "/tmp/record3k");
    
    /**
     * Get the temporary output directory for new recordings
     */
    std::filesystem::path getTempOutputPath() const;
    
    /**
     * Get the archive directory where packed files are stored (data/files/bin)
     */
    std::filesystem::path getArchivePath() const;
    
    /**
     * Get the metadata directory where FileInfo JSONs are stored (data/files)
     */
    std::filesystem::path getFileMetadataPath() const;
    
    /**
     * Register a RecordManager for observation
     * FileManager will monitor state changes and auto-pack when recording stops
     * @param recordUID Unique identifier for the recording
     * @param recordManager Shared pointer to the RecordManager
     * @param cameraUID Camera identifier for metadata
     */
    void registerRecording(const std::string& recordUID, 
                          std::shared_ptr<RecordManager> recordManager,
                          const std::string& cameraUID = "");
    
    /**
     * Manually trigger packing of a recording
     * @param recordUID Unique identifier for the recording
     * @return Optional path to the packed tar.gz file, or nullopt on failure
     */
    std::optional<std::filesystem::path> packRecording(const std::string& recordUID);
    
    /**
     * Create a FileInfo entry in the metadata store
     * @param fileUID Unique identifier for the file (typically "file-rec-{recordUID}")
     * @param archivePath Path to the packed tar.gz archive
     * @param recordUID Associated record identifier
     * @param cameraUID Associated camera identifier
     * @return true if FileInfo was successfully created and persisted
     */
    bool createFileInfo(const std::string& fileUID,
                       const std::filesystem::path& archivePath,
                       const std::string& recordUID = "",
                       const std::string& cameraUID = "");
    
    /**
     * Get FileInfo by UID
     * @param fileUID File identifier
     * @return Optional FileInfo object
     */
    std::optional<api::FileInfo> getFileInfo(const std::string& fileUID) const;
    
    /**
     * List all file entries
     * @return Vector of FileInfo objects
     */
    std::vector<api::FileInfo> listFiles() const;
    
    /**
     * Check recording state and auto-pack if needed
     * Should be called periodically or when state changes are detected
     */
    void checkAndPackCompletedRecordings();

    // Disable copy and move
    FileManager(const FileManager&) = delete;
    FileManager& operator=(const FileManager&) = delete;
    FileManager(FileManager&&) = delete;
    FileManager& operator=(FileManager&&) = delete;

private:
    FileManager() = default;
    ~FileManager() = default;
    
    struct RecordingInfo {
        std::shared_ptr<RecordManager> manager;
        std::string cameraUID;
        bool packed = false;
    };
    
    mutable std::mutex m_mutex;
    std::filesystem::path m_dataRootPath;
    std::filesystem::path m_tempOutputPath;
    std::map<std::string, RecordingInfo> m_recordings;  // recordUID -> RecordingInfo
    std::unique_ptr<JsonStore> m_fileStore;  // Metadata store for FileInfo entries
};

} // namespace lbt
