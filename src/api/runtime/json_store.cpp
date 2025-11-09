#include <api/runtime/json_store.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <fstream>
#include <iostream>

using namespace std::chrono_literals;

JsonStore::JsonStore(std::filesystem::path dir) : dir_(std::move(dir)) {}
JsonStore::~JsonStore() { stop(); }

bool JsonStore::isJsonFile(const std::filesystem::path& p) { return p.has_extension() && p.extension() == ".json"; }
std::string JsonStore::keyFromPath(const std::filesystem::path& p) { return p.stem().string(); }
std::filesystem::path JsonStore::pathFromKey(const std::string& key) const { return dir_ / (key + ".json"); }

bool JsonStore::readJsonFile(const std::filesystem::path& file, Json::Value& out) {
    std::ifstream ifs(file, std::ios::in | std::ios::binary); if (!ifs) return false; Json::CharReaderBuilder b; std::string errs; return Json::parseFromStream(b, ifs, &out, &errs);
}
bool JsonStore::writeJsonFileAtomic(const std::filesystem::path& file, const Json::Value& val) {
    auto tmp = file; tmp += ".tmp"; {
        std::ofstream ofs(tmp, std::ios::out | std::ios::binary | std::ios::trunc); if (!ofs) return false; Json::StreamWriterBuilder wb; wb["indentation"] = "  "; std::unique_ptr<Json::StreamWriter> w(wb.newStreamWriter()); w->write(val, &ofs); ofs.flush(); if (!ofs) return false; }
    std::error_code ec; std::filesystem::rename(tmp, file, ec); if (ec) { std::filesystem::remove(file, ec); std::filesystem::rename(tmp, file, ec); } return !ec;
}
void JsonStore::loadAllUnlocked() {
    std::error_code ec; std::filesystem::create_directories(dir_, ec); for (auto& p : std::filesystem::directory_iterator(dir_, ec)) { if (p.is_regular_file() && isJsonFile(p.path())) { Json::Value j; if (readJsonFile(p.path(), j)) { Entry e; e.data = std::move(j); e.mtime = std::filesystem::last_write_time(p.path(), ec); map_[keyFromPath(p.path())] = std::move(e); } } }
}
void JsonStore::start() {
    std::lock_guard lk(mtx_); if (running_) return; loadAllUnlocked(); inotifyFd_ = ::inotify_init1(IN_NONBLOCK); if (inotifyFd_ < 0) { std::cerr << "JsonStore: inotify_init1 failed\n"; return; }
    uint32_t mask = IN_CREATE | IN_MODIFY | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE; watch_ = ::inotify_add_watch(inotifyFd_, dir_.c_str(), mask); if (watch_ < 0) { std::cerr << "JsonStore: inotify_add_watch failed for " << dir_ << "\n"; ::close(inotifyFd_); inotifyFd_ = -1; return; }
    running_ = true; watcher_ = std::thread([this]{ watchLoop(); });
}
void JsonStore::stop() {
    bool wasRunning = running_.exchange(false); if (wasRunning) { if (watch_ >= 0) { ::inotify_rm_watch(inotifyFd_, watch_); watch_ = -1; } if (inotifyFd_ >= 0) { ::close(inotifyFd_); inotifyFd_ = -1; } if (watcher_.joinable()) watcher_.join(); }
}
std::vector<std::string> JsonStore::keys() const { std::lock_guard lk(mtx_); std::vector<std::string> out; out.reserve(map_.size()); for (auto& kv : map_) out.push_back(kv.first); return out; }
std::optional<Json::Value> JsonStore::get(const std::string& key) const { std::lock_guard lk(mtx_); auto it = map_.find(key); if (it == map_.end()) return std::nullopt; return it->second.data; }
bool JsonStore::put(const std::string& key, const Json::Value& value) {
    std::lock_guard lk(mtx_); std::error_code ec; std::filesystem::create_directories(dir_, ec); auto file = pathFromKey(key); if (!writeJsonFileAtomic(file, value)) return false; Entry e; e.data = value; e.mtime = std::filesystem::last_write_time(file, ec); map_[key] = std::move(e); noteSelfWrite(key); return true;
}
bool JsonStore::erase(const std::string& key) {
    std::lock_guard lk(mtx_); auto file = pathFromKey(key); std::error_code ec; bool ok = std::filesystem::remove(file, ec); map_.erase(key); noteSelfWrite(key); return ok;
}
void JsonStore::noteSelfWrite(const std::string& key) { selfWrites_[key] = Clock::now(); }
bool JsonStore::isRecentSelfWrite(const std::string& key) { auto it = selfWrites_.find(key); if (it == selfWrites_.end()) return false; bool recent = (Clock::now() - it->second) < 2s; if (!recent) selfWrites_.erase(it); return recent; }
void JsonStore::handleFileChange(const std::filesystem::path& p, bool deleted) {
    if (!isJsonFile(p)) return; const auto key = keyFromPath(p); if (isRecentSelfWrite(key)) return; std::error_code ec; if (deleted) { map_.erase(key); return; } Json::Value j; if (readJsonFile(p, j)) { Entry e; e.data = std::move(j); e.mtime = std::filesystem::last_write_time(p, ec); map_[key] = std::move(e); }
}
void JsonStore::watchLoop() {
    constexpr size_t BufLen = 64 * (sizeof(struct inotify_event) + NAME_MAX + 1); std::vector<char> buf(BufLen); while (running_) { fd_set rfds; FD_ZERO(&rfds); FD_SET(inotifyFd_, &rfds); struct timeval tv {1,0}; int ready = ::select(inotifyFd_ + 1, &rfds, nullptr, nullptr, &tv); if (ready <= 0) continue; ssize_t len = ::read(inotifyFd_, buf.data(), buf.size()); if (len <= 0) continue; size_t i = 0; while (i < static_cast<size_t>(len)) { auto* ev = reinterpret_cast<inotify_event*>(buf.data() + i); std::filesystem::path p = dir_ / (ev->len ? ev->name : ""); bool isDelete = (ev->mask & (IN_DELETE | IN_MOVED_FROM)) != 0; bool isWriteDone = (ev->mask & IN_CLOSE_WRITE) != 0; bool isModify = (ev->mask & IN_MODIFY) != 0; bool isCreateOrMoveTo = (ev->mask & (IN_CREATE | IN_MOVED_TO)) != 0; std::lock_guard lk(mtx_); if (isDelete) { handleFileChange(p, true); } else if (isWriteDone || isCreateOrMoveTo || isModify) { handleFileChange(p, false); } i += sizeof(inotify_event) + ev->len; } }
}
