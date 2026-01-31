#include <liblbt/file_manager.h>
#include <liblbt/record_manager.h>
#include <liblbt/factory/model_factory.h>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace lbt {

static std::string getCurrentTimeISO8601() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t_now), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

FileManager& FileManager::instance() {
    static FileManager inst;
    return inst;
}

void FileManager::initialize(const std::filesystem::path& dataRootPath,
                            const std::filesystem::path& tempOutputPath) {
    m_dataRootPath = dataRootPath;
    m_tempOutputPath = tempOutputPath;
    
    // Initialize JsonStore for file metadata
    m_fileStore = std::make_unique<JsonStore>(m_dataRootPath / "files");
    m_fileStore->start();
    
    std::cout << "[FileManager] Initialized" << std::endl;
    std::cout << "[FileManager]   Data root: " << m_dataRootPath << std::endl;
    std::cout << "[FileManager]   Temp output: " << m_tempOutputPath << std::endl;
    std::cout << "[FileManager]   Archive path: " << (m_dataRootPath / "files" / "bin").string() << std::endl;
}

void FileManager::setFileInfoCreator(FileInfoCreator creator) {
    m_fileInfoCreator = std::move(creator);
    std::cout << "[FileManager] FileInfoCreator callback registered" << std::endl;
}

std::filesystem::path FileManager::getTempOutputPath() const {
    return m_tempOutputPath;
}

std::filesystem::path FileManager::getArchivePath() const {
    return m_dataRootPath / "files" / "bin";
}

std::filesystem::path FileManager::getFileMetadataPath() const {
    return m_dataRootPath / "files";
}

void FileManager::registerRecording(const std::string& recordUID,
                                    std::shared_ptr<RecordManager> recordManager,
                                    const std::string& cameraUID) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    RecordingInfo info;
    info.manager = recordManager;
    info.cameraUID = cameraUID;
    info.packed = false;
    
    m_recordings[recordUID] = info;
    
    std::cout << "[FileManager] Registered recording: " << recordUID 
              << " (camera: " << cameraUID << ")" << std::endl;
}

std::optional<std::filesystem::path> FileManager::packRecording(const std::string& recordUID) {
    std::shared_ptr<RecordManager> manager;
    std::string cameraUID;
    
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_recordings.find(recordUID);
        if (it == m_recordings.end()) {
            std::cerr << "[FileManager] Recording not found: " << recordUID << std::endl;
            return std::nullopt;
        }
        
        if (it->second.packed) {
            std::cout << "[FileManager] Recording already packed: " << recordUID << std::endl;
            return std::nullopt;
        }
        
        manager = it->second.manager;
        cameraUID = it->second.cameraUID;
    }
    
    // RAII guard to ensure RecordManager is always released when function exits
    auto cleanup = [this, &recordUID]() {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_recordings.find(recordUID);
        if (it != m_recordings.end() && it->second.manager) {
            std::cout << "[FileManager] Releasing RecordManager for " << recordUID << " to free camera resources" << std::endl;
            it->second.manager.reset();
            it->second.packed = true;
            std::cout << "[FileManager] RecordManager released for " << recordUID << std::endl;
        }
    };
    std::shared_ptr<void> cleanupGuard(nullptr, [&cleanup](void*) { cleanup(); });
    
    if (!manager) {
        std::cerr << "[FileManager] RecordManager not available for: " << recordUID << std::endl;
        return std::nullopt;
    }
    
    // Check if recording is in a packable state
    auto state = manager->getState();
    if (state != RecordManager::State::STOPPED && state != RecordManager::State::FAILURE) {
        std::cerr << "[FileManager] Recording not ready to pack (state: " 
                  << manager->getStateString() << "): " << recordUID << std::endl;
        return std::nullopt;
    }
    
    std::cout << "[FileManager] Packing recording: " << recordUID << std::endl;
    
    // Call RecordManager's pack method
    auto archivePath = manager->pack();
    if (!archivePath) {
        std::cerr << "[FileManager] Failed to pack recording: " << recordUID << std::endl;
        return std::nullopt;
    }
    
    // Security validation: verify archive path
    if (!std::filesystem::exists(*archivePath)) {
        std::cerr << "[FileManager] Archive does not exist: " << *archivePath << std::endl;
        return std::nullopt;
    }
    
    if (!std::filesystem::is_regular_file(*archivePath)) {
        std::cerr << "[FileManager] Archive is not a regular file: " << *archivePath << std::endl;
        return std::nullopt;
    }
    
    // Security validation: check filename doesn't contain path traversal
    std::string filename = archivePath->filename().string();
    if (filename.find("..") != std::string::npos || 
        filename.find('/') != std::string::npos || 
        filename.find('\\') != std::string::npos) {
        std::cerr << "[FileManager] Invalid filename (path traversal attempt): " << filename << std::endl;
        return std::nullopt;
    }
    
    // Move archive from temp location to data/files/bin
    std::filesystem::path targetDir = getArchivePath();
    
    // Security validation: ensure target directory exists and is a directory
    if (!std::filesystem::exists(targetDir) || !std::filesystem::is_directory(targetDir)) {
        std::cerr << "[FileManager] Target directory invalid: " << targetDir << std::endl;
        return std::nullopt;
    }
    
    std::filesystem::path targetPath = targetDir / filename;
    
    // Security validation: verify target path stays within target directory
    std::error_code ec;
    auto canonicalTarget = std::filesystem::canonical(targetDir, ec);
    if (ec) {
        std::cerr << "[FileManager] Cannot resolve target directory: " << ec.message() << std::endl;
        return std::nullopt;
    }
    
    auto targetPathParent = targetPath.parent_path();
    auto canonicalTargetParent = std::filesystem::canonical(targetPathParent, ec);
    if (ec || canonicalTargetParent != canonicalTarget) {
        std::cerr << "[FileManager] Security violation: target path outside allowed directory" << std::endl;
        return std::nullopt;
    }
    
    // Check if target file already exists to avoid overwriting
    if (std::filesystem::exists(targetPath)) {
        std::cerr << "[FileManager] Target file already exists: " << targetPath << std::endl;
        return std::nullopt;
    }
    
    // Archive is already in target directory (RecordManager creates it there)
    // Verify final path matches expected location
    if (archivePath->parent_path() == targetDir && *archivePath == targetPath) {
        std::cout << "[FileManager] Archive already in correct location: " << targetPath << std::endl;
    } else if (archivePath->parent_path() != targetDir) {
        // Need to move from temp location to final destination
        try {
            std::filesystem::rename(*archivePath, targetPath);
            std::cout << "[FileManager] Moved archive from " << *archivePath << " to: " << targetPath << std::endl;
            archivePath = targetPath;
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "[FileManager] Failed to move archive: " << e.what() << std::endl;
            return std::nullopt;
        }
    } else {
        std::cerr << "[FileManager] Unexpected archive location: " << *archivePath << std::endl;
        return std::nullopt;
    }
    
    // Create FileInfo entry
    std::string fileUID = "file-rec-" + recordUID;
    bool fileInfoCreated = createFileInfo(fileUID, *archivePath, recordUID, cameraUID);
    
    // Note: RecordManager cleanup is handled automatically by the RAII guard at function exit
    
    if (fileInfoCreated) {
        std::cout << "[FileManager] Successfully packed and registered: " << fileUID << std::endl;
    } else {
        std::cerr << "[FileManager] Warning: Packed but failed to create FileInfo for: " << fileUID << std::endl;
    }
    
    return archivePath;
}

bool FileManager::createFileInfo(const std::string& fileUID,
                                const std::filesystem::path& archivePath,
                                const std::string& recordUID,
                                const std::string& cameraUID) {
    if (!m_fileStore) {
        std::cerr << "[FileManager] FileStore not initialized" << std::endl;
        return false;
    }
    
    try {
        // Use ModelFactory to create validated FileInfo
        auto fileInfoResult = ModelFactory::makeFileInfo(
            fileUID,
            getCurrentTimeISO8601(),
            "active",
            std::nullopt
        );
        
        if (!fileInfoResult.ok()) {
            std::cerr << "[FileManager] Failed to create FileInfo: " << fileInfoResult.error() << std::endl;
            return false;
        }
        
        // Convert to JSON and add custom fields
        Json::Value fileJson = fileInfoResult.value().toJson();
        fileJson["archivePath"] = archivePath.string();
        fileJson["archiveSize"] = static_cast<Json::Int64>(std::filesystem::file_size(archivePath));
        
        // Optional metadata
        if (!recordUID.empty()) {
            fileJson["recordUID"] = recordUID;
        }
        if (!cameraUID.empty()) {
            fileJson["cameraUID"] = cameraUID;
        }
        
        // Use external data manager callback if provided
        bool mdSuccess = true;
        if (m_fileInfoCreator) {
            mdSuccess = m_fileInfoCreator(fileUID, archivePath, recordUID, fileJson);
        }
        
        bool success = m_fileStore->put(fileUID, fileJson);
        if (success) {
            std::cout << "[FileManager] Created FileInfo: " << fileUID << std::endl;
        } else {
            std::cerr << "[FileManager] Failed to persist FileInfo: " << fileUID << std::endl;
        }
        return success && mdSuccess;
        
    } catch (const std::exception& e) {
        std::cerr << "[FileManager] Exception creating FileInfo: " << e.what() << std::endl;
        return false;
    }
}

std::optional<api::FileInfo> FileManager::getFileInfo(const std::string& fileUID) const {
    if (!m_fileStore) {
        return std::nullopt;
    }
    
    auto jsonOpt = m_fileStore->get(fileUID);
    if (!jsonOpt) {
        return std::nullopt;
    }
    
    return api::FileInfo::fromJson(*jsonOpt);
}

std::vector<api::FileInfo> FileManager::listFiles() const {
    std::vector<api::FileInfo> files;
    
    if (!m_fileStore) {
        return files;
    }
    
    auto keys = m_fileStore->keys();
    for (const auto& key : keys) {
        auto jsonOpt = m_fileStore->get(key);
        if (jsonOpt) {
            files.push_back(api::FileInfo::fromJson(*jsonOpt));
        }
    }
    
    return files;
}

void FileManager::checkAndPackCompletedRecordings() {
    std::vector<std::string> recordingsToPack;
    
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& [recordUID, info] : m_recordings) {
            if (info.packed || !info.manager) {
                continue;
            }
            
            auto state = info.manager->getState();
            if (state == RecordManager::State::STOPPED || state == RecordManager::State::FAILURE) {
                recordingsToPack.push_back(recordUID);
            }
        }
    }
    
    // Pack recordings (outside lock to avoid deadlock)
    for (const auto& recordUID : recordingsToPack) {
        std::cout << "[FileManager] Auto-packing completed recording: " << recordUID << std::endl;
        packRecording(recordUID);
    }
}

} // namespace lbt
