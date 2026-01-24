#include <liblbt/record_manager_locker.h>
#include <iostream>

namespace lbt {

RecordManagerLocker& RecordManagerLocker::instance() {
    static RecordManagerLocker instance;
    return instance;
}

bool RecordManagerLocker::tryLock(const std::string& cameraUID, const std::string& recordUID) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Check if already locked
    auto it = m_locks.find(cameraUID);
    if (it != m_locks.end()) {
        std::cerr << "[RecordManagerLocker] Camera " << cameraUID 
                  << " is already locked by recording " << it->second.recordUID 
                  << " (state: " << it->second.state << ")" << std::endl;
        return false;
    }
    
    // Acquire lock
    LockInfo info;
    info.recordUID = recordUID;
    info.cameraUID = cameraUID;
    info.lockedAt = std::chrono::system_clock::now();
    info.state = "recording";
    info.errorMessage = "";
    
    m_locks[cameraUID] = info;
    
    std::cout << "[RecordManagerLocker] Camera " << cameraUID 
              << " locked by recording " << recordUID << std::endl;
    
    return true;
}

void RecordManagerLocker::unlock(const std::string& cameraUID) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_locks.find(cameraUID);
    if (it != m_locks.end()) {
        std::cout << "[RecordManagerLocker] Camera " << cameraUID 
                  << " unlocked (was locked by " << it->second.recordUID 
                  << ", final state: " << it->second.state << ")" << std::endl;
        m_locks.erase(it);
    }
}

bool RecordManagerLocker::isLocked(const std::string& cameraUID) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_locks.find(cameraUID) != m_locks.end();
}

std::optional<RecordManagerLocker::LockInfo> RecordManagerLocker::getLockInfo(const std::string& cameraUID) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_locks.find(cameraUID);
    if (it != m_locks.end()) {
        return it->second;
    }
    
    return std::nullopt;
}

void RecordManagerLocker::updateState(const std::string& cameraUID, const std::string& state, const std::string& errorMessage) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_locks.find(cameraUID);
    if (it != m_locks.end()) {
        it->second.state = state;
        it->second.errorMessage = errorMessage;
        
        std::cout << "[RecordManagerLocker] Camera " << cameraUID 
                  << " state updated to: " << state;
        if (!errorMessage.empty()) {
            std::cout << " (error: " << errorMessage << ")";
        }
        std::cout << std::endl;
    } else {
        std::cerr << "[RecordManagerLocker] WARNING: Tried to update state for unlocked camera: " 
                  << cameraUID << std::endl;
    }
}

std::unordered_map<std::string, RecordManagerLocker::LockInfo> RecordManagerLocker::getAllLocks() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_locks;
}

void RecordManagerLocker::unlockAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::cout << "[RecordManagerLocker] Force unlocking all " << m_locks.size() << " cameras" << std::endl;
    m_locks.clear();
}

} // namespace lbt
