// Moved to include/api/runtime/info_data_manager.h
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

    Json::Value list(const std::string& category); // array of objects
    std::optional<Json::Value> get(const std::string& category, const std::string& key);

private:
    struct Cat { std::unique_ptr<JsonStore> store; };
    std::filesystem::path baseDir_;
    std::unordered_map<std::string, Cat> cats_;
    std::mutex mtx_;
    void ensureStartedUnlocked();
};

// Global accessor
InfoDataManager& infoDataManager();
