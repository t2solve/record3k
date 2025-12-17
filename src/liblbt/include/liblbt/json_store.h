#pragma once

#include <json/json.h>
#include <filesystem>
#include <optional>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <map>
#include <atomic>
#include <chrono>

// JsonStore: directory-backed JSON document store with inotify-based hot reload.
// Moved into liblbt (single source of truth). Thread-safe for concurrent readers/writers.
class JsonStore {
public:
    using Clock = std::chrono::steady_clock;

    struct Entry {
        Json::Value data;
        std::filesystem::file_time_type mtime;
    };

    explicit JsonStore(std::filesystem::path dir);
    ~JsonStore();

    // Utilities
    static bool isJsonFile(const std::filesystem::path& p);
    static std::string keyFromPath(const std::filesystem::path& p);
    std::filesystem::path pathFromKey(const std::string& key) const;

    static bool readJsonFile(const std::filesystem::path& file, Json::Value& out);
    static bool writeJsonFileAtomic(const std::filesystem::path& file, const Json::Value& val);

    // Lifecycle
    void start();
    void stop();

    // Basic API
    std::vector<std::string> keys() const;
    std::optional<Json::Value> get(const std::string& key) const;
    bool put(const std::string& key, const Json::Value& value);
    bool erase(const std::string& key);

private:
    void loadAllUnlocked();
    void noteSelfWrite(const std::string& key);
    bool isRecentSelfWrite(const std::string& key);
    void handleFileChange(const std::filesystem::path& p, bool deleted);
    void watchLoop();

    std::filesystem::path dir_;
    mutable std::mutex mtx_;
    std::map<std::string, Entry> map_;
    std::map<std::string, Clock::time_point> selfWrites_;

    // inotify watcher
    int inotifyFd_{-1};
    int watch_{-1};
    std::thread watcher_;
    std::atomic<bool> running_{false};
};