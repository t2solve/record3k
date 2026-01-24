#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <chrono>

namespace lbt {

/**
 * Singleton to manage locks on cameras being used for recording.
 * Prevents multiple simultaneous recordings from the same camera.
 * Tracks recording state and error information.
 */
class RecordManagerLocker {
public:
    struct LockInfo {
        std::string recordUID;           // Which recording session has the lock
        std::string cameraUID;           // Camera identifier
        std::chrono::system_clock::time_point lockedAt; // When was it locked
        std::string state;               // Current state: "recording", "stopped", "error"
        std::string errorMessage;        // Error details if state == "error"
    };

    /**
     * Get singleton instance
     */
    static RecordManagerLocker& instance();

    /**
     * Try to acquire lock for a camera
     * @param cameraUID Camera unique identifier
     * @param recordUID Recording session identifier
     * @return true if lock acquired, false if camera already locked
     */
    bool tryLock(const std::string& cameraUID, const std::string& recordUID);

    /**
     * Release lock for a camera
     * @param cameraUID Camera unique identifier
     */
    void unlock(const std::string& cameraUID);

    /**
     * Check if camera is currently locked
     * @param cameraUID Camera unique identifier
     * @return true if locked, false otherwise
     */
    bool isLocked(const std::string& cameraUID) const;

    /**
     * Get lock information for a camera
     * @param cameraUID Camera unique identifier
     * @return LockInfo if locked, nullopt otherwise
     */
    std::optional<LockInfo> getLockInfo(const std::string& cameraUID) const;

    /**
     * Update the state of a locked camera recording
     * @param cameraUID Camera unique identifier
     * @param state New state ("recording", "stopped", "error")
     * @param errorMessage Optional error message if state == "error"
     */
    void updateState(const std::string& cameraUID, const std::string& state, const std::string& errorMessage = "");

    /**
     * Get all currently locked cameras
     * @return Map of cameraUID -> LockInfo
     */
    std::unordered_map<std::string, LockInfo> getAllLocks() const;

    /**
     * Force unlock all cameras (for cleanup/debugging)
     */
    void unlockAll();

private:
    RecordManagerLocker() = default;
    ~RecordManagerLocker() = default;
    
    RecordManagerLocker(const RecordManagerLocker&) = delete;
    RecordManagerLocker& operator=(const RecordManagerLocker&) = delete;

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, LockInfo> m_locks;
};

} // namespace lbt
