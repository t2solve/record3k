#include <liblbt/record_manager.h>
#include <liblbt/processing_pipeline.h>
#include <liblbt/frame_memory_object.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <json/json.h>

namespace lbt {

RecordManager::RecordManager(RecordConfig config)
    : m_config(std::move(config))
{
    m_state = State::PREPARATION;
    
    // Create output paths
    m_recordDir = std::filesystem::path(m_config.outputDir) / ("file-rec-" + m_config.recordUID);
    m_framesDir = m_recordDir / "frames";
    m_othersDir = m_recordDir / "others";
}

RecordManager::~RecordManager() {
    stop();
}

bool RecordManager::start() {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    
    if (m_state != State::PREPARATION && m_state != State::STOPPED) {
        std::cerr << "[RecordManager] Cannot start: invalid state " << getStateString() << std::endl;
        return false;
    }
    
    // Validate configuration
    if (!m_config.frameSource) {
        std::cerr << "[RecordManager] No frame source configured" << std::endl;
        m_state = State::FAILURE;
        return false;
    }
    
    if (!m_config.frameSource->isReady()) {
        std::cerr << "[RecordManager] Frame source is not ready" << std::endl;
        m_state = State::FAILURE;
        return false;
    }
    
    if (m_config.pipeline.steps.empty()) {
        std::cerr << "[RecordManager] No pipeline steps configured" << std::endl;
        m_state = State::FAILURE;
        return false;
    }
    
    // Create output directory structure
    if (!createOutputStructure()) {
        std::cerr << "[RecordManager] Failed to create output directory structure" << std::endl;
        m_state = State::FAILURE;
        return false;
    }
    
    // Reset state
    m_stopRequested = false;
    {
        std::lock_guard<std::mutex> statsLock(m_statsMutex);
        m_stats = Statistics();
        m_stats.startTime = std::chrono::steady_clock::now();
    }
    
    // Start recording thread
    m_recordingThread = std::make_unique<std::thread>(&RecordManager::recordingLoop, this);
    
    // Brief wait to ensure recording thread has started
    // This avoids race conditions when checking state immediately after start()
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    std::cout << "[RecordManager] Recording started for " << m_config.recordUID << std::endl;
    return true;
}

void RecordManager::stop() {
    m_stopRequested = true;
    
    if (m_recordingThread && m_recordingThread->joinable()) {
        std::cout << "[RecordManager] Waiting for recording thread to finish..." << std::endl;
        m_recordingThread->join();
        std::cout << "[RecordManager] Recording thread stopped" << std::endl;
    }
}

void RecordManager::recordingLoop() {
    m_state = State::RECORDING;
    
    try {
        // Calculate timing parameters
        auto interval = std::chrono::milliseconds(static_cast<int>(1000.0 / m_config.targetFPS));
        auto recordDuration = std::chrono::duration<double>(m_config.totalTimeSeconds);
        
        auto startTime = std::chrono::steady_clock::now();
        auto lastFrameTime = startTime;
        
        double avgFPS = 0.0;
        const double alpha = 0.1; // Exponential moving average factor
        int frameCounter = 0;
        
        std::cout << "[RecordManager] Recording loop started - Target FPS: " << m_config.targetFPS 
                  << ", Duration: " << m_config.totalTimeSeconds << "s" << std::endl;
        
        // Main recording loop
        while (!m_stopRequested) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = now - startTime;
            
            // Check if we've reached the target duration
            if (elapsed >= recordDuration) {
                std::cout << "[RecordManager] Target duration reached" << std::endl;
                break;
            }
            
            // Acquire frame
            auto frame = m_config.frameSource->nextFrame();
            if (!frame) {
                std::cerr << "[RecordManager] Failed to acquire frame, stopping" << std::endl;
                m_state = State::FAILURE;
                break;
            }
            
            // Process frame through pipeline
            try {
                FrameMemoryObject result = ProcessingPipeline::processLine(
                    m_config.pipeline.steps,
                    *frame,
                    m_config.pipeline.configs
                );
                frameCounter++;
            } catch (const std::exception& e) {
                std::cerr << "[RecordManager] Pipeline processing error: " << e.what() << std::endl;
                m_state = State::FAILURE;
                break;
            }
            
            // Calculate FPS
            auto frameEndTime = std::chrono::steady_clock::now();
            double dt = std::chrono::duration<double>(frameEndTime - lastFrameTime).count();
            lastFrameTime = frameEndTime;
            
            double instantaneousFPS = (dt > 0.0) ? (1.0 / dt) : 0.0;
            avgFPS = (frameCounter == 1) ? instantaneousFPS : (1.0 - alpha) * avgFPS + alpha * instantaneousFPS;
            
            // Update statistics
            {
                std::lock_guard<std::mutex> lock(m_statsMutex);
                m_stats.framesRecorded = frameCounter;
                m_stats.actualFPS = avgFPS;
            }
            
            // Log progress periodically
            if (frameCounter % 100 == 0) {
                std::cout << "[RecordManager] Progress: " << frameCounter << " frames, "
                          << "Avg FPS: " << std::fixed << std::setprecision(2) << avgFPS
                          << ", Elapsed: " << std::chrono::duration<double>(elapsed).count() << "s" << std::endl;
            }
            
            // Sleep to maintain target FPS
            auto processingTime = std::chrono::steady_clock::now() - now;
            auto sleepTime = interval - std::chrono::duration_cast<std::chrono::milliseconds>(processingTime);
            
            if (sleepTime.count() > 0) {
                std::this_thread::sleep_for(sleepTime);
            }
        }
        
        // Recording finished
        {
            std::lock_guard<std::mutex> lock(m_statsMutex);
            m_stats.endTime = std::chrono::steady_clock::now();
            m_stats.completed = (m_state != State::FAILURE);
        }
        
        if (m_state != State::FAILURE) {
            m_state = State::STOPPED;
            std::cout << "[RecordManager] Recording completed successfully: " 
                      << frameCounter << " frames recorded" << std::endl;
            
            // Save metadata
            if (!saveRecordMetadata()) {
                std::cerr << "[RecordManager] Warning: Failed to save record metadata" << std::endl;
            }
            
            // Copy optional files
            copyOptionalFiles();
            
            // Save pipeline configuration
            try {
                std::string pipelineJsonPath = (m_recordDir / ("pipeline-" + m_config.recordUID + ".json")).string();
                PipelineConfigLoader::saveToJSON(pipelineJsonPath, m_config.pipeline);
            } catch (const std::exception& e) {
                std::cerr << "[RecordManager] Warning: Failed to save pipeline config: " << e.what() << std::endl;
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[RecordManager] Recording loop exception: " << e.what() << std::endl;
        m_state = State::FAILURE;
    }
}

bool RecordManager::createOutputStructure() {
    try {
        // Create main record directory
        std::filesystem::create_directories(m_recordDir);
        
        // Create frames subdirectory
        std::filesystem::create_directories(m_framesDir);
        
        // Create others subdirectory
        std::filesystem::create_directories(m_othersDir);
        
        std::cout << "[RecordManager] Created output structure at: " << m_recordDir << std::endl;
        return true;
        
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "[RecordManager] Failed to create directory structure: " << e.what() << std::endl;
        return false;
    }
}

bool RecordManager::saveRecordMetadata() {
    try {
        Json::Value recordJson;
        
        // Basic information
        recordJson["recordUID"] = m_config.recordUID;
        recordJson["targetFPS"] = m_config.targetFPS;
        recordJson["targetDurationSeconds"] = m_config.totalTimeSeconds;
        
        // Statistics
        Statistics stats = getStatistics();
        recordJson["framesRecorded"] = stats.framesRecorded;
        recordJson["actualFPS"] = stats.actualFPS;
        recordJson["completed"] = stats.completed;
        
        // Timestamps (ISO 8601 format)
        auto formatTime = [](const std::chrono::steady_clock::time_point& tp) -> std::string {
            auto systemTime = std::chrono::system_clock::now() + 
                            std::chrono::duration_cast<std::chrono::system_clock::duration>(
                                tp - std::chrono::steady_clock::now());
            auto time_t = std::chrono::system_clock::to_time_t(systemTime);
            std::stringstream ss;
            ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
            return ss.str();
        };
        
        recordJson["datetimeStart"] = formatTime(stats.startTime);
        if (stats.completed) {
            recordJson["datetimeEnd"] = formatTime(stats.endTime);
        }
        
        // Optional references
        if (m_config.calibrationFile) {
            recordJson["calibrationFile"] = *m_config.calibrationFile;
        }
        if (m_config.studyFile) {
            recordJson["studyFile"] = *m_config.studyFile;
        }
        
        // State
        recordJson["state"] = getStateString();
        
        // Write to file
        std::string recordJsonPath = (m_recordDir / ("record-" + m_config.recordUID + ".json")).string();
        std::ofstream file(recordJsonPath);
        if (!file.is_open()) {
            std::cerr << "[RecordManager] Failed to open file for writing: " << recordJsonPath << std::endl;
            return false;
        }
        
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "  ";
        std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
        writer->write(recordJson, &file);
        
        std::cout << "[RecordManager] Saved record metadata to: " << recordJsonPath << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[RecordManager] Exception saving metadata: " << e.what() << std::endl;
        return false;
    }
}

void RecordManager::copyOptionalFiles() {
    try {
        // Copy calibration file if provided
        if (m_config.calibrationFile && std::filesystem::exists(*m_config.calibrationFile)) {
            std::filesystem::path srcPath(*m_config.calibrationFile);
            std::filesystem::path dstPath = m_othersDir / ("calibration-" + m_config.recordUID + srcPath.extension().string());
            std::filesystem::copy_file(srcPath, dstPath, std::filesystem::copy_options::overwrite_existing);
            std::cout << "[RecordManager] Copied calibration file to: " << dstPath << std::endl;
        }
        
        // Copy study file if provided
        if (m_config.studyFile && std::filesystem::exists(*m_config.studyFile)) {
            std::filesystem::path srcPath(*m_config.studyFile);
            std::filesystem::path dstPath = m_recordDir / ("study-" + m_config.recordUID + ".json");
            std::filesystem::copy_file(srcPath, dstPath, std::filesystem::copy_options::overwrite_existing);
            std::cout << "[RecordManager] Copied study file to: " << dstPath << std::endl;
        }
        
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "[RecordManager] Warning: Failed to copy optional files: " << e.what() << std::endl;
    }
}

std::optional<std::filesystem::path> RecordManager::pack() {
    if (m_state != State::STOPPED) {
        std::cerr << "[RecordManager] Cannot pack: recording must be in STOPPED state, current state: " 
                  << getStateString() << std::endl;
        return std::nullopt;
    }
    
    try {
        // Create data/files directory if it doesn't exist
        std::filesystem::path archiveDir = "data/files";
        std::filesystem::create_directories(archiveDir);
        
        // Archive filename
        std::string archiveName = "file-rec-" + m_config.recordUID + ".tar.gz";
        std::filesystem::path archivePath = archiveDir / archiveName;
        
        // Build tar command
        // tar -czf <archive> -C <parent_dir> <dir_name>
        std::string parentDir = m_recordDir.parent_path().string();
        std::string dirName = m_recordDir.filename().string();
        
        std::stringstream cmd;
        cmd << "tar -czf " << archivePath.string() 
            << " -C " << parentDir 
            << " " << dirName;
        
        std::cout << "[RecordManager] Packing: " << cmd.str() << std::endl;
        
        int result = std::system(cmd.str().c_str());
        if (result != 0) {
            std::cerr << "[RecordManager] tar command failed with exit code: " << result << std::endl;
            m_state = State::FAILURE;
            return std::nullopt;
        }
        
        // Verify archive was created
        if (!std::filesystem::exists(archivePath)) {
            std::cerr << "[RecordManager] Archive not found after packing: " << archivePath << std::endl;
            m_state = State::FAILURE;
            return std::nullopt;
        }
        
        std::cout << "[RecordManager] Successfully packed to: " << archivePath << std::endl;
        std::cout << "[RecordManager] Archive size: " << std::filesystem::file_size(archivePath) << " bytes" << std::endl;
        
        m_state = State::PACKED;
        return archivePath;
        
    } catch (const std::exception& e) {
        std::cerr << "[RecordManager] Exception during packing: " << e.what() << std::endl;
        m_state = State::FAILURE;
        return std::nullopt;
    }
}

RecordManager::State RecordManager::getState() const {
    return m_state.load();
}

std::string RecordManager::getStateString() const {
    switch (m_state.load()) {
        case State::UNKNOWN:      return "UNKNOWN";
        case State::PREPARATION:  return "PREPARATION";
        case State::RECORDING:    return "RECORDING";
        case State::STOPPED:      return "STOPPED";
        case State::FAILURE:      return "FAILURE";
        case State::PACKED:       return "PACKED";
        default:                  return "INVALID";
    }
}

std::filesystem::path RecordManager::getOutputPath() const {
    return m_recordDir;
}

RecordManager::Statistics RecordManager::getStatistics() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_stats;
}

} // namespace lbt
