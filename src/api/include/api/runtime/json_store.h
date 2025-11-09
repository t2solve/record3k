#pragma once
#include <json/json.h>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

class JsonStore {
public:
    explicit JsonStore(std::filesystem::path dir);
    ~JsonStore();
    void start();
    void stop();
    std::vector<std::string> keys() const;
    std::optional<Json::Value> get(const std::string& key) const;
    bool put(const std::string& key, const Json::Value& value);
    bool erase(const std::string& key);
private:
    struct Entry { Json::Value data; std::filesystem::file_time_type mtime{}; };
    std::filesystem::path dir_;
    mutable std::mutex mtx_;
    std::unordered_map<std::string, Entry> map_;
    int inotifyFd_{-1};
    int watch_{-1};
    std::thread watcher_;
    std::atomic<bool> running_{false};
    using Clock = std::chrono::steady_clock;
    std::unordered_map<std::string, Clock::time_point> selfWrites_;
    static bool isJsonFile(const std::filesystem::path& p);
    static std::string keyFromPath(const std::filesystem::path& p);
    std::filesystem::path pathFromKey(const std::string& key) const;
    static bool readJsonFile(const std::filesystem::path& file, Json::Value& out);
    static bool writeJsonFileAtomic(const std::filesystem::path& file, const Json::Value& val);
    void loadAllUnlocked();
    void watchLoop();
    void handleFileChange(const std::filesystem::path& p, bool deleted);
    void noteSelfWrite(const std::string& key);
    bool isRecentSelfWrite(const std::string& key);
};
