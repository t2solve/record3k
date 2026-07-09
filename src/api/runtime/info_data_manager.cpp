#include <api/runtime/info_data_manager.h>
#include <api/runtime/app_config.h>
#include <trantor/utils/Logger.h>
#include <iostream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <unordered_map>
#include <liblbt/factory/model_factory.h>

static std::unique_ptr<InfoDataManager> gInfoMgr;
static std::mutex gInitMtx;

InfoDataManager::InfoDataManager(std::filesystem::path baseDir) : baseDir_(std::move(baseDir)) {
    const char* cats[] = {"cameras","pipelines","calibrations","records","files","studies"};
    for (auto c : cats) {
        auto dir = baseDir_ / c;
        cats_.emplace(c, Cat{std::make_unique<JsonStore>(dir)});
    }
}

// Check that each JSON file's UID field matches its filename (the store key).
// A mismatch means /get/<uid> and /do/camera/<uid> lookups will 404.
static void warnOnUIDMismatch(const std::string& cat, JsonStore& store) {
    // Map of category name -> the JSON field that must equal the filename stem
    static const std::unordered_map<std::string, std::string> uidFields = {
        {"cameras",    "camUID"},
        {"pipelines",  "pipelineUID"},
    };
    auto fieldIt = uidFields.find(cat);
    if (fieldIt == uidFields.end()) return; // no strict UID field for this category

    const std::string& uidField = fieldIt->second;
    for (const auto& key : store.keys()) {
        auto v = store.get(key);
        if (!v) continue;
        if (!v->isMember(uidField)) {
            LOG_WARN << "[DataIntegrity] " << cat << "/" << key << ".json is missing field \""
                     << uidField << "\" — lookups by UID will fail";
            continue;
        }
        const std::string jsonUID = (*v)[uidField].asString();
        if (jsonUID != key) {
            LOG_WARN << "[DataIntegrity] " << cat << "/" << key
                     << ".json: filename key \"" << key
                     << "\" != " << uidField << " \"" << jsonUID
                     << "\" — rename the file to " << jsonUID
                     << ".json or the API will return 404 when queried by UID";
        }
    }
}

void InfoDataManager::start() {
    std::lock_guard lk(mtx_);
    ensureStartedUnlocked();
    for (auto& kv : cats_) {
        warnOnUIDMismatch(kv.first, *kv.second.store);
    }
}
void InfoDataManager::stop() { std::lock_guard lk(mtx_); for (auto& kv : cats_) kv.second.store->stop(); }
void InfoDataManager::ensureStartedUnlocked() { for (auto& kv : cats_) kv.second.store->start(); }

static bool validateWithFactory(const std::string& cat, const Json::Value& j) {
    using lbt::StatusOr;
    using lbt::ModelFactory;
    
    // Only validate cameras and pipelines with strict schemas
    // Records, calibrations, files, and studies use flexible schemas
    if (cat == "cameras") { 
        auto r = ModelFactory::fromJsonCamera(j); 
        if (!r.ok()) LOG_WARN << "Invalid camera JSON: " << r.error();
        return r.ok(); 
    }
    if (cat == "pipelines") { 
        auto r = ModelFactory::fromJsonPipeline(j); 
        if (!r.ok()) LOG_WARN << "Invalid pipeline JSON: " << r.error();
        return r.ok(); 
    }
    
    // Skip validation for flexible schema categories
    return true;
}

Json::Value InfoDataManager::list(const std::string& category) {
    std::lock_guard lk(mtx_);
    LOG_DEBUG << "Listing category: " << category;
    auto it = cats_.find(category); 
    Json::Value arr(Json::arrayValue); 
    if (it == cats_.end()) {
        LOG_WARN << "Category not found: " << category;
        return arr;
    }
    
    int validCount = 0, invalidCount = 0;
    for (auto& k : it->second.store->keys()) {
        if (auto v = it->second.store->get(k)) {
            if (validateWithFactory(category, *v)) {
                arr.append(*v);
                validCount++;
            } else {
                invalidCount++;
                LOG_DEBUG << "Skipping invalid entry in " << category << ": " << k;
            }
        }
    }
    LOG_INFO << "Listed " << validCount << " valid entries from " << category 
             << (invalidCount > 0 ? " (skipped " + std::to_string(invalidCount) + " invalid)" : "");
    return arr;
}

std::optional<Json::Value> InfoDataManager::get(const std::string& category, const std::string& key) {
    std::lock_guard lk(mtx_);
    LOG_DEBUG << "Getting " << category << "/" << key;
    auto it = cats_.find(category); 
    if (it == cats_.end()) {
        LOG_WARN << "Category not found: " << category;
        return std::nullopt;
    }
    auto v = it->second.store->get(key);
    if (!v) {
        LOG_DEBUG << "Key not found: " << category << "/" << key;
        return std::nullopt;
    }
    if (!validateWithFactory(category, *v)) {
        LOG_WARN << "Validation failed for " << category << "/" << key;
        return std::nullopt;
    }
    LOG_DEBUG << "Successfully retrieved " << category << "/" << key;
    return v;
}

bool InfoDataManager::put(const std::string& category, const std::string& key, const Json::Value& value) {
    std::lock_guard lk(mtx_);
    LOG_DEBUG << "Putting " << category << "/" << key;
    auto it = cats_.find(category);
    if (it == cats_.end()) {
        LOG_WARN << "Category not found: " << category;
        return false;
    }
    if (!validateWithFactory(category, value)) {
        LOG_WARN << "Validation failed for " << category << "/" << key;
        return false;
    }
    bool success = it->second.store->put(key, value);
    if (success) {
        LOG_INFO << "Successfully stored " << category << "/" << key;
    } else {
        LOG_ERROR << "Failed to store " << category << "/" << key;
    }
    return success;
}

InfoDataManager& infoDataManager() {
    std::lock_guard lk(gInitMtx);
    if (!gInfoMgr) {
        auto infoPath = api::AppConfig::instance().getDataRootPath();
        LOG_INFO << "Initializing InfoDataManager with base dir: " << infoPath;
        gInfoMgr = std::make_unique<InfoDataManager>(infoPath);
        gInfoMgr->start();
        LOG_INFO << "InfoDataManager initialized and started";
    }
    return *gInfoMgr;
}

// ============ Specialized Record Operations ============

bool InfoDataManager::createRecordInfo(const Json::Value& recordJson) {
    if (!recordJson.isMember("recordUID")) {
        LOG_ERROR << "Record JSON missing recordUID";
        return false;
    }
    
    std::string recordUID = recordJson["recordUID"].asString();
    return put("records", recordUID, recordJson);
}

bool InfoDataManager::updateRecordInfo(const std::string& recordUID, const Json::Value& updates) {
    std::lock_guard lk(mtx_);
    
    auto it = cats_.find("records");
    if (it == cats_.end()) {
        LOG_ERROR << "Records category not found";
        return false;
    }
    
    // Get existing record
    auto existingOpt = it->second.store->get(recordUID);
    if (!existingOpt) {
        LOG_ERROR << "Record not found for update: " << recordUID;
        return false;
    }
    
    Json::Value existing = *existingOpt;
    
    // Merge updates into existing
    for (const auto& key : updates.getMemberNames()) {
        existing[key] = updates[key];
    }
    
    // Update timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    gmtime_r(&time_t, &tm_buf);
    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
    existing["lastUpdated"] = ss.str();
    
    bool success = it->second.store->put(recordUID, existing);
    
    if (success) {
        LOG_INFO << "Updated record info: " << recordUID;
    } else {
        LOG_ERROR << "Failed to update record info: " << recordUID;
    }
    
    return success;
}

std::optional<Json::Value> InfoDataManager::getRecordInfo(const std::string& recordUID) {
    return get("records", recordUID);
}

bool InfoDataManager::deleteRecord(const std::string& recordUID) {
    std::lock_guard lk(mtx_);

    auto recordsIt = cats_.find("records");
    auto filesIt = cats_.find("files");
    if (recordsIt == cats_.end() || filesIt == cats_.end()) {
        LOG_ERROR << "Records or files category not found";
        return false;
    }

    auto recordOpt = recordsIt->second.store->get(recordUID);
    if (!recordOpt) {
        LOG_WARN << "Record not found for deletion: " << recordUID;
        return false;
    }

    // Resolve the fileUID associated with this record (fall back to convention)
    std::string fileUID = recordOpt->isMember("fileUID") ? (*recordOpt)["fileUID"].asString() : "";
    if (fileUID.empty() || fileUID == "file-rec-placeholder") {
        fileUID = "file-rec-" + recordUID;
    }

    // Resolve the archive path: prefer the FileInfo entry, fall back to the conventional bin path
    auto fileInfoOpt = filesIt->second.store->get(fileUID);
    std::filesystem::path archivePath = (fileInfoOpt && fileInfoOpt->isMember("archivePath"))
        ? std::filesystem::path((*fileInfoOpt)["archivePath"].asString())
        : baseDir_ / "files" / "bin" / (fileUID + ".tar.gz");

    // Delete the tar.gz archive from data/files/bin
    if (std::filesystem::exists(archivePath)) {
        std::error_code ec;
        if (!std::filesystem::remove(archivePath, ec)) {
            LOG_ERROR << "Failed to delete archive " << archivePath << ": " << ec.message();
            return false;
        }
        LOG_INFO << "Deleted archive: " << archivePath;
    } else {
        LOG_WARN << "Archive not found on disk (continuing): " << archivePath;
    }

    // Remove the FileInfo json entry (data/files/{fileUID}.json)
    if (fileInfoOpt) {
        if (!filesIt->second.store->erase(fileUID)) {
            LOG_ERROR << "Failed to delete file info entry: " << fileUID;
            return false;
        }
        LOG_INFO << "Deleted file info entry: " << fileUID;
    }

    // Remove the record json entry (data/records/{recordUID}.json)
    bool erased = recordsIt->second.store->erase(recordUID);
    if (erased) {
        LOG_INFO << "Deleted record info entry: " << recordUID;
    } else {
        LOG_ERROR << "Failed to delete record info entry: " << recordUID;
    }

    return erased;
}

// ============ Specialized File Operations ============

bool InfoDataManager::createFileInfo(const std::string& fileUID,
                                     const std::filesystem::path& archivePath,
                                     const std::string& recordUID,
                                     const Json::Value& metadata) {
    try {
        Json::Value fileInfo;
        fileInfo["fileUID"] = fileUID;
        fileInfo["recordUID"] = recordUID;
        fileInfo["filePath"] = archivePath.string();
        fileInfo["fileName"] = archivePath.filename().string();
        
        // Get file size
        if (std::filesystem::exists(archivePath)) {
            fileInfo["fileSize"] = static_cast<Json::Int64>(std::filesystem::file_size(archivePath));
        } else {
            LOG_ERROR << "Archive file not found: " << archivePath;
            return false;
        }
        
        // Add timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf;
        gmtime_r(&time_t, &tm_buf);
        std::stringstream ss;
        ss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
        fileInfo["created"] = ss.str();
        
        // Add file type
        fileInfo["fileType"] = "tar.gz";
        fileInfo["contentType"] = "application/gzip";
        
        // Merge additional metadata if provided
        if (!metadata.isNull() && metadata.isObject()) {
            for (const auto& key : metadata.getMemberNames()) {
                fileInfo[key] = metadata[key];
            }
        }
        
        return put("files", fileUID, fileInfo);
        
    } catch (const std::exception& e) {
        LOG_ERROR << "Exception creating file info: " << e.what();
        return false;
    }
}

// ============ File Copy Operations ============

bool InfoDataManager::copyStudyToRecord(const std::string& studyUID,
                                       const std::filesystem::path& recordDir,
                                       const std::string& recordUID) {
    auto studyOpt = get("studies", studyUID);
    if (!studyOpt) {
        LOG_ERROR << "Study not found: " << studyUID;
        return false;
    }
    
    try {
        std::filesystem::path dstPath = recordDir / ("study-" + recordUID + ".json");
        
        // Write study info to record directory
        if (!JsonStore::writeJsonFileAtomic(dstPath, *studyOpt)) {
            LOG_ERROR << "Failed to write study file to: " << dstPath;
            return false;
        }
        
        LOG_INFO << "Copied study to record: " << dstPath;
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR << "Exception copying study: " << e.what();
        return false;
    }
}

bool InfoDataManager::copyCalibrationToRecord(const std::string& calibrationUID,
                                              const std::filesystem::path& recordDir,
                                              const std::string& recordUID) {
    auto calPathOpt = getCalibrationFilePath(calibrationUID);
    if (!calPathOpt) {
        LOG_ERROR << "Calibration file path not found for: " << calibrationUID;
        return false;
    }
    
    std::filesystem::path srcPath = *calPathOpt;
    if (!std::filesystem::exists(srcPath)) {
        LOG_ERROR << "Calibration file does not exist: " << srcPath;
        return false;
    }
    
    try {
        std::filesystem::path othersDir = recordDir / "others";
        std::filesystem::create_directories(othersDir);
        
        std::filesystem::path dstPath = othersDir / ("calibration-" + recordUID + srcPath.extension().string());
        std::filesystem::copy_file(srcPath, dstPath, std::filesystem::copy_options::overwrite_existing);
        
        LOG_INFO << "Copied calibration file to: " << dstPath;
        return true;
        
    } catch (const std::filesystem::filesystem_error& e) {
        LOG_ERROR << "Failed to copy calibration file: " << e.what();
        return false;
    }
}

std::optional<std::filesystem::path> InfoDataManager::getCalibrationFilePath(const std::string& calibrationUID) {
    auto calInfoOpt = get("calibrations", calibrationUID);
    if (!calInfoOpt) {
        return std::nullopt;
    }
    
    Json::Value calInfo = *calInfoOpt;
    
    // Check for fileUID field and resolve to file path
    if (calInfo.isMember("fileUID")) {
        std::string fileUID = calInfo["fileUID"].asString();
        
        // Try to get the tar.gz path from the temp output
        // Calibration files are typically in file-rec-{calUID} directories
        std::filesystem::path archivePath = baseDir_.parent_path() / "files" / "bin" / (fileUID + ".tar.gz");
        
        if (std::filesystem::exists(archivePath)) {
            return archivePath;
        }
    }
    
    // Fallback: check for direct filePath in calibration info
    if (calInfo.isMember("filePath")) {
        std::filesystem::path path = calInfo["filePath"].asString();
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    
    return std::nullopt;
}
