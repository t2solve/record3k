#pragma once
#include <liblbt/json_store.h>
#include <json/json.h>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <optional>

class InfoDataManager {
public:
    explicit InfoDataManager(std::filesystem::path baseDir);
    void start();
    void stop();
    
    // Basic CRUD operations
    Json::Value list(const std::string& category);
    std::optional<Json::Value> get(const std::string& category, const std::string& key);
    bool put(const std::string& category, const std::string& key, const Json::Value& value);
    
    // ============ Specialized Record Operations ============
    
    /**
     * Create record info with automatic timestamp
     */
    bool createRecordInfo(const Json::Value& recordJson);
    
    /**
     * Update existing record info (merges updates into existing record)
     */
    bool updateRecordInfo(const std::string& recordUID, const Json::Value& updates);
    
    /**
     * Get record info by UID
     */
    std::optional<Json::Value> getRecordInfo(const std::string& recordUID);
    
    // ============ Specialized File Operations ============
    
    /**
     * Create file info for a packed tar.gz archive
     */
    bool createFileInfo(const std::string& fileUID,
                       const std::filesystem::path& archivePath,
                       const std::string& recordUID,
                       const Json::Value& metadata = Json::Value::null);
    
    // ============ File Copy Operations ============
    
    /**
     * Copy study file to record directory
     */
    bool copyStudyToRecord(const std::string& studyUID,
                          const std::filesystem::path& recordDir,
                          const std::string& recordUID);
    
    /**
     * Copy calibration file to record directory
     */
    bool copyCalibrationToRecord(const std::string& calibrationUID,
                                const std::filesystem::path& recordDir,
                                const std::string& recordUID);
    
    /**
     * Get path to calibration file from calibration info
     */
    std::optional<std::filesystem::path> getCalibrationFilePath(const std::string& calibrationUID);
    
private:
    struct Cat { std::unique_ptr<JsonStore> store; };
    std::filesystem::path baseDir_;
    std::unordered_map<std::string, Cat> cats_;
    std::mutex mtx_;
    void ensureStartedUnlocked();
};

InfoDataManager& infoDataManager();
