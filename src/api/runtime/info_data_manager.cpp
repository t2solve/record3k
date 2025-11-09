#include <api/runtime/info_data_manager.h>
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
    if (cat == "cameras") { auto r = ModelFactory::fromJsonCamera(j); return r.ok(); }
    if (cat == "pipelines") { auto r = ModelFactory::fromJsonPipeline(j); return r.ok(); }
    if (cat == "calibrations") { auto r = ModelFactory::fromJsonCalibration(j); return r.ok(); }
    if (cat == "files") { auto r = ModelFactory::fromJsonFileInfo(j); return r.ok(); }
    if (cat == "studies") { auto r = ModelFactory::fromJsonStudy(j); return r.ok(); }
    if (cat == "records") { auto r = ModelFactory::fromJsonRecord(j); return r.ok(); }
    return true; // unknown categories pass through
}

Json::Value InfoDataManager::list(const std::string& category) {
    std::lock_guard lk(mtx_);
    auto it = cats_.find(category); Json::Value arr(Json::arrayValue); if (it == cats_.end()) return arr;
    for (auto& k : it->second.store->keys()) {
        if (auto v = it->second.store->get(k)) {
            if (validateWithFactory(category, *v)) arr.append(*v);
        }
    }
    return arr;
}

std::optional<Json::Value> InfoDataManager::get(const std::string& category, const std::string& key) {
    std::lock_guard lk(mtx_);
    auto it = cats_.find(category); if (it == cats_.end()) return std::nullopt;
    auto v = it->second.store->get(key);
    if (!v) return std::nullopt;
    if (!validateWithFactory(category, *v)) return std::nullopt;
    return v;
}

InfoDataManager& infoDataManager() {
    std::lock_guard lk(gInitMtx);
    if (!gInfoMgr) {
        gInfoMgr = std::make_unique<InfoDataManager>(std::filesystem::path("data/info"));
        gInfoMgr->start();
    }
    return *gInfoMgr;
}
