#include <api/runtime/info_data_manager.h>
#include <api/runtime/app_config.h>
#include <trantor/utils/Logger.h>
#include <iostream>
#include <mutex>
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

void InfoDataManager::start() { std::lock_guard lk(mtx_); ensureStartedUnlocked(); }
void InfoDataManager::stop() { std::lock_guard lk(mtx_); for (auto& kv : cats_) kv.second.store->stop(); }
void InfoDataManager::ensureStartedUnlocked() { for (auto& kv : cats_) kv.second.store->start(); }

static bool validateWithFactory(const std::string& cat, const Json::Value& j) {
    using lbt::StatusOr;
    using lbt::ModelFactory;
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
    if (cat == "calibrations") { 
        auto r = ModelFactory::fromJsonCalibration(j); 
        if (!r.ok()) LOG_WARN << "Invalid calibration JSON: " << r.error();
        return r.ok(); 
    }
    if (cat == "files") { 
        auto r = ModelFactory::fromJsonFileInfo(j); 
        if (!r.ok()) LOG_WARN << "Invalid file JSON: " << r.error();
        return r.ok(); 
    }
    if (cat == "studies") { 
        auto r = ModelFactory::fromJsonStudy(j); 
        if (!r.ok()) LOG_WARN << "Invalid study JSON: " << r.error();
        return r.ok(); 
    }
    if (cat == "records") { 
        auto r = ModelFactory::fromJsonRecord(j); 
        if (!r.ok()) LOG_WARN << "Invalid record JSON: " << r.error();
        return r.ok(); 
    }
    return true; // unknown categories pass through
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
