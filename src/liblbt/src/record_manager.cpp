#include <liblbt/record_manager.h>
#include <liblbt/record_manager_locker.h>
#include <liblbt/processing_pipeline.h>
#include <liblbt/frame_memory_object.h>
#include <liblbt/result_logger.h>
#include <liblbt/file_manager.h>

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

    // Build a stable per-instance log prefix for disambiguation in multi-camera runs.
    {
        std::ostringstream ss;
        ss << "[RecordManager|" << m_config.cameraUID << "|" << m_config.recordUID << "]";
        m_tag = ss.str();
    }
    
    // Create output paths
    m_recordDir = std::filesystem::path(m_config.outputDir) / ("file-rec-" + m_config.recordUID);
    m_framesDir = m_recordDir / "frames";
    m_othersDir = m_recordDir / "others";
    
    // Initialize ResultLogger if logging is enabled
    if (m_config.logMode != ResultLogger::LogMode::NONE) {
        ResultLogger::Config logConfig;
        logConfig.recordDir = m_recordDir;
        logConfig.framesDir = m_framesDir;
        logConfig.othersDir = m_othersDir;
        logConfig.mode = m_config.logMode;
        logConfig.saveMetadata = true;
        logConfig.saveDebugImages = true;
        
        m_resultLogger = std::make_unique<ResultLogger>(logConfig);
        std::cout << m_tag << " ResultLogger initialized in mode: " 
                  << (m_config.logMode == ResultLogger::LogMode::DEBUG ? "DEBUG" : "PRODUCTION") << std::endl;
    }
}

RecordManager::~RecordManager() {
    stop();
    
    // Release camera lock if still held
    if (!m_config.cameraUID.empty()) {
        RecordManagerLocker::instance().unlock(m_config.cameraUID);
    }
}

bool RecordManager::start() {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    
    if (m_state != State::PREPARATION && m_state != State::STOPPED) {
        std::cerr << m_tag << " Cannot start: invalid state " << getStateString() << std::endl;
        return false;
    }
    
    // Validate configuration
    if (!m_config.frameSource) {
        std::cerr << m_tag << " No frame source configured" << std::endl;
        m_state = State::FAILURE;
        return false;
    }
    
    if (!m_config.frameSource->isReady()) {
        std::cerr << m_tag << " Frame source is not ready" << std::endl;
        m_state = State::FAILURE;
        return false;
    }
    
    if (m_config.pipeline.steps.empty()) {
        std::cerr << m_tag << " No pipeline steps configured" << std::endl;
        m_state = State::FAILURE;
        return false;
    }
    
    // Try to acquire camera lock
    if (!m_config.cameraUID.empty()) {
        if (!RecordManagerLocker::instance().tryLock(m_config.cameraUID, m_config.recordUID)) {
            std::cerr << m_tag << " Camera " << m_config.cameraUID << " is already in use" << std::endl;
            auto lockInfo = RecordManagerLocker::instance().getLockInfo(m_config.cameraUID);
            if (lockInfo) {
                std::cerr << m_tag << " Locked by recording " << lockInfo->recordUID 
                          << " (state: " << lockInfo->state << ")" << std::endl;
            }
            m_state = State::FAILURE;
            return false;
        }
    }
    
    // Create output directory structure
    if (!createOutputStructure()) {
        std::cerr << m_tag << " Failed to create output directory structure" << std::endl;
        m_state = State::FAILURE;
        if (!m_config.cameraUID.empty()) {
            RecordManagerLocker::instance().unlock(m_config.cameraUID);
        }
        return false;
    }
    
    // Add FrameWriter only in DEBUG mode to save raw frames
    // In PRODUCTION mode, skip frame writing to save disk space
    if (m_config.logMode == ResultLogger::LogMode::DEBUG) {
        std::string framesPath = m_framesDir.string();
        bool added = ProcessingPipeline::addFrameWriterIfNotExists(
            m_config.pipeline.steps,
            m_config.pipeline.configs,
            ProcessingMode::CUDA_PREFERRED,
            framesPath,
            "jpg"
        );
        if (added) {
            std::cout << m_tag << " Added FrameWriter to pipeline for DEBUG mode (frames: " << framesPath << ")" << std::endl;
        } else {
            auto successFlag = false;
            // Reset path if FrameWriter already exists - find its index in the steps vector
            for (size_t i = 0; i < m_config.pipeline.steps.size(); ++i) {
                if (m_config.pipeline.steps[i] && 
                    m_config.pipeline.steps[i]->getFilterType() == FilterType::FRAME_WRITER) {
                    // Update the config at the same index
                    if (i < m_config.pipeline.configs.size()) {
                        m_config.pipeline.configs[i].setParameter("output_dir", framesPath);
                        std::cout << m_tag << " Updated existing FrameWriter output directory to: " << framesPath << std::endl;
                        successFlag = true;
                    }
                    //break;//we do all in case of multiple FrameWriters
                }
            }
            if (!successFlag) 
            {
                std::cerr << m_tag << " Warning: Failed to update existing FrameWriter output directory" << std::endl;
            }
            else 
            {
                std::cout << m_tag << " FrameWriter already exists in pipeline" << std::endl;
            }
        }
    } else {
        std::cout << m_tag << " PRODUCTION mode - skipping FrameWriter to save disk space" << std::endl;
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
    
    std::cout << m_tag << " Recording started for " << m_config.recordUID << std::endl;
    return true;
}

void RecordManager::stop() {
    m_stopRequested = true;
    
    if (m_recordingThread && m_recordingThread->joinable()) {
        std::cout << m_tag << " Waiting for recording thread to finish..." << std::endl;
        m_recordingThread->join();
        std::cout << m_tag << " Recording thread finished" << std::endl;
    }
    
    // Update state in locker and release lock
    if (!m_config.cameraUID.empty()) {
        if (m_state == State::FAILURE) {
            RecordManagerLocker::instance().updateState(m_config.cameraUID, "error", "Recording failed");
        } else if (m_state == State::STOPPED) {
            RecordManagerLocker::instance().updateState(m_config.cameraUID, "stopped", "");
        }
        RecordManagerLocker::instance().unlock(m_config.cameraUID);
    }
}

void RecordManager::recordingLoop() {
    m_state = State::RECORDING;

    // Extend the base tag with this recording thread's ID so concurrent camera
    // recording threads are distinguishable in the log even on the same camera.
    std::string rtag;
    {
        std::ostringstream ss;
        ss << rtag << "[TID:" << std::this_thread::get_id() << "]";
        rtag = ss.str();
    }

    // Open log file in others directory
    std::ofstream logFile((m_othersDir / "log.txt").string());
    auto logToFile = [&logFile](const std::string& message) {
        if (logFile.is_open()) {
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) % 1000;
            
            std::tm tm_buf;
            gmtime_r(&time_t, &tm_buf);
            
            logFile << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S")
                   << "." << std::setfill('0') << std::setw(3) << ms.count()
                   << "Z " << message << std::endl;
            logFile.flush();
        }
    };
    
    // Create step logger that writes to log file
    m_stepLogger = std::make_shared<StepLogger>([&logToFile](StepLogger::Level level, const std::string& message) {
        const char* prefix;
        switch (level) {
            case StepLogger::Level::DEBUG:   prefix = "[DEBUG] "; break;
            case StepLogger::Level::INFO:    prefix = "[INFO] "; break;
            case StepLogger::Level::WARNING: prefix = "[WARN] "; break;
            case StepLogger::Level::ERROR:   prefix = "[ERROR] "; break;
            default:                          prefix = "[LOG] "; break;
        }
        logToFile(std::string(prefix) + message);
    });
    
    // Inject logger into all pipeline steps
    for (auto& step : m_config.pipeline.steps) {
        if (step) {
            step->setLogger(m_stepLogger);
        }
    }
    
    try {
        // Calculate timing parameters
        auto interval = std::chrono::milliseconds(static_cast<int>(1000.0 / m_config.targetFPS));
        auto recordDuration = std::chrono::duration<double>(m_config.totalTimeSeconds);
        
        auto startTime = std::chrono::steady_clock::now();
        auto lastFrameTime = startTime;

        std::vector<cv::Point2f> mass_center_list;
        std::vector<cv::Point2d> world_center_list; 
        std::vector<uint64_t> frame_timestamps_list;
        
        double avgFPS = 0.0;
        const double alpha = 0.1; // Exponential moving average factor
        int frameCounter = 0;
        int consecutiveFailures = 0;
        const int maxConsecutiveFailures = 5; // tolerate transient GigE drops
        
        // Log start information
        std::stringstream startMsg;
        auto startTimeSys = std::chrono::system_clock::now();
        auto start_time_t = std::chrono::system_clock::to_time_t(startTimeSys);
        std::tm tm_buf;
        gmtime_r(&start_time_t, &tm_buf);
        startMsg << "Recording started - RecordUID: " << m_config.recordUID
                 << ", CameraUID: " << m_config.cameraUID
                 << ", Target FPS: " << m_config.targetFPS
                 << ", Duration: " << m_config.totalTimeSeconds << "s"
                 << ", Start Time: " << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
        
        std::cout << rtag << " " << startMsg.str() << std::endl;
        logToFile(startMsg.str());
        
        // Main recording loop
        while (!m_stopRequested) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = now - startTime;
            
            // Check if we've reached the target duration
            if (elapsed >= recordDuration) {
                std::cout << rtag << " Target duration reached" << std::endl;
                break;
            }
            
            // Acquire frame
            auto frame = m_config.frameSource->nextFrame();
            if (!frame) {
                consecutiveFailures++;
                std::cerr << rtag << " Failed to acquire frame ("
                          << consecutiveFailures << "/" << maxConsecutiveFailures << ")" << std::endl;
                logToFile("WARN: Failed to acquire frame (" + std::to_string(consecutiveFailures)
                          + "/" + std::to_string(maxConsecutiveFailures) + ")");
                if (consecutiveFailures >= maxConsecutiveFailures) {
                    std::string errMsg = "Too many consecutive frame failures, stopping";
                    std::cerr << rtag << " " << errMsg << std::endl;
                    logToFile("ERROR: " + errMsg);
                    m_state = State::FAILURE;
                    if (!m_config.cameraUID.empty()) {
                        RecordManagerLocker::instance().updateState(m_config.cameraUID, "error", "Failed to acquire frame");
                    }
                    break;
                }
                continue;
            }
            consecutiveFailures = 0;
            
            // Process frame through pipeline
            try {
                FrameMemoryObject result = ProcessingPipeline::processLine(
                    m_config.pipeline.steps,
                    *frame,
                    m_config.pipeline.configs
                );

                // Pipeline doesn't preserve timestamp, so copy from original frame to result
                uint64_t originalFrameTimestamp = frame->timestampNs();
                result.setTimestampNs(originalFrameTimestamp);
                
                // Use ResultLogger to log frame and metadata
                if (m_resultLogger) {
                    m_resultLogger->logFrame(frameCounter, *frame, result);
                }
                
                // Collect mass center data for analysis
                if (result.hasMetadata("mass_center_point_pixel")) {
                    cv::Point2f massCenter = result.getMetadata<cv::Point2f>("mass_center_point_pixel");
                    cv::Point2d worldPt = result.getMetadata<cv::Point2d>("mass_center_point_world");
                    if (massCenter.x >= 0 && massCenter.y >= 0) {
                        mass_center_list.push_back(massCenter);
                        world_center_list.push_back(worldPt);
                        // Use result timestamp (now fixed) for CSV
                        frame_timestamps_list.push_back(result.timestampNs());
                    }
                }
                frameCounter++;
            } catch (const std::exception& e) {
                std::string errMsg = std::string("Pipeline processing error: ") + e.what();
                std::cerr << rtag << " " << errMsg << std::endl;
                logToFile("ERROR: " + errMsg);
                m_state = State::FAILURE;
                if (!m_config.cameraUID.empty()) {
                    RecordManagerLocker::instance().updateState(m_config.cameraUID, "error", std::string("Pipeline error: ") + e.what());
                }
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
                std::stringstream progressMsg;
                progressMsg << "Progress: " << frameCounter << " frames, "
                          << "Avg FPS: " << std::fixed << std::setprecision(2) << avgFPS
                          << ", Elapsed: " << std::chrono::duration<double>(elapsed).count() << "s";
                std::cout << rtag << " " << progressMsg.str() << std::endl;
                logToFile(progressMsg.str());
            }
      
            // Sleep to maintain target FPS
            auto processingTime = std::chrono::steady_clock::now() - now;
            auto sleepTime = interval - std::chrono::duration_cast<std::chrono::milliseconds>(processingTime);
            
            // Log timing details periodically to verify FPS control
            if (frameCounter % 100 == 0) {
                double procMs = std::chrono::duration<double, std::milli>(processingTime).count();
                double sleepMs = sleepTime.count();
                std::stringstream timingMsg;
                timingMsg << "Timing: Processing=" << std::fixed << std::setprecision(2) << procMs 
                         << "ms, Sleep=" << sleepMs << "ms, Target=" << interval.count() << "ms";
                logToFile(timingMsg.str());
            }
            
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
            
            // Calculate final statistics
            auto endTimeSys = std::chrono::system_clock::now();
            auto end_time_t = std::chrono::system_clock::to_time_t(endTimeSys);
            std::tm tm_buf_end;
            gmtime_r(&end_time_t, &tm_buf_end);
            
            double totalDuration = std::chrono::duration<double>(m_stats.endTime - m_stats.startTime).count();
            
            std::stringstream finalMsg;
            finalMsg << "Recording completed successfully:\n"
                    << "  Frames recorded: " << frameCounter << "\n"
                    << "  Average FPS: " << std::fixed << std::setprecision(2) << avgFPS << "\n"
                    << "  Total duration: " << std::setprecision(3) << totalDuration << "s\n"
                    << "  End time: " << std::put_time(&tm_buf_end, "%Y-%m-%dT%H:%M:%SZ");
            
            std::cout << rtag << " Recording completed successfully: " 
                      << frameCounter << " frames recorded" << std::endl;
            logToFile(finalMsg.str());
            
            // Write trajectory file once at the end
            if (!mass_center_list.empty()) {
                std::ofstream trajFile((m_recordDir / "mass_center_trajectory.csv").string());
                trajFile << "timestamp_ns;world_x_mm;world_y_mm;"
                         << "mass_center_x_px;mass_center_y_px"
                         << "\n";
                for (size_t i = 0; i < mass_center_list.size(); ++i) {
                    const auto& p = mass_center_list[i];
                    const auto& p2 = world_center_list[i];
                    trajFile << frame_timestamps_list[i] << ";"
                             << std::fixed << std::setprecision(2)
                             << p2.x << ";" << p2.y << ";"
                             << std::setprecision(1)
                             << p.x << ";" << p.y
                             << "\n";
                }
                trajFile.close();
                std::cout << rtag << " Trajectory file written with " << mass_center_list.size() << " points" << std::endl;
                logToFile("Trajectory file written with " + std::to_string(mass_center_list.size()) + " points");
            }
            
            // Write ResultLogger summary
            if (m_resultLogger) {
                m_resultLogger->writeSummary(frameCounter, totalDuration);
            }
            
            // Save metadata
            if (!saveRecordMetadata()) {
                std::cerr << rtag << " Warning: Failed to save record metadata" << std::endl;
                logToFile("WARNING: Failed to save record metadata");
            }
            
            // Copy optional files
            copyOptionalFiles();
            
            // Save pipeline configuration
            try {
                std::string pipelineJsonPath = (m_recordDir / ("pipeline-" + m_config.recordUID + ".json")).string();
                PipelineConfigLoader::saveToJSON(pipelineJsonPath, m_config.pipeline);
                logToFile("Pipeline configuration saved");
            } catch (const std::exception& e) {
                std::string warnMsg = std::string("Failed to save pipeline config: ") + e.what();
                std::cerr << rtag << " Warning: " << warnMsg << std::endl;
                logToFile("WARNING: " + warnMsg);
            }
        }
        
        // Close log file
        if (logFile.is_open()) {
            logFile.close();
        }
        
    } catch (const std::exception& e) {
        std::string errMsg = std::string("Recording loop exception: ") + e.what();
        std::cerr << rtag << " " << errMsg << std::endl;
        logToFile("EXCEPTION: " + errMsg);
        m_state = State::FAILURE;
        
        // Update error state in locker
        if (!m_config.cameraUID.empty()) {
            RecordManagerLocker::instance().updateState(m_config.cameraUID, "error", e.what());
        }
        
        // Close log file
        if (logFile.is_open()) {
            logFile.close();
        }
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
        
        std::cout << m_tag << " Created output structure at: " << m_recordDir << std::endl;
        return true;
        
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << m_tag << " Failed to create directory structure: " << e.what() << std::endl;
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
            std::cerr << m_tag << " Failed to open file for writing: " << recordJsonPath << std::endl;
            return false;
        }
        
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "  ";
        std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
        writer->write(recordJson, &file);
        
        std::cout << m_tag << " Saved record metadata to: " << recordJsonPath << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << m_tag << " Exception saving metadata: " << e.what() << std::endl;
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
            std::cout << m_tag << " Copied calibration file to: " << dstPath << std::endl;
        } else if (m_config.calibrationFile) {
            std::cerr << m_tag << " Warning: Calibration file not found: " << *m_config.calibrationFile << std::endl;
        }
        
        // Copy study file if provided (will be included in packed archive)
        if (m_config.studyFile && std::filesystem::exists(*m_config.studyFile)) {
            std::filesystem::path srcPath(*m_config.studyFile);
            std::filesystem::path dstPath = m_recordDir / ("study-" + m_config.recordUID + ".json");
            std::filesystem::copy_file(srcPath, dstPath, std::filesystem::copy_options::overwrite_existing);
            std::cout << m_tag << " Copied study file to record directory: " << dstPath << std::endl;
            std::cout << m_tag << " Study info will be included in packed archive (tar.gz)" << std::endl;
        } else if (m_config.studyFile) {
            std::cerr << m_tag << " Warning: Study file not found: " << *m_config.studyFile << std::endl;
        }
        
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << m_tag << " Warning: Failed to copy optional files: " << e.what() << std::endl;
    }
}

std::optional<std::filesystem::path> RecordManager::pack() {
    if (m_state != State::STOPPED) {
        std::cerr << m_tag << " Cannot pack: recording must be in STOPPED state, current state: " 
                  << getStateString() << std::endl;
        return std::nullopt;
    }
    
    try {
        // Create archive directory (now using data/files/bin for packed archives)
        std::filesystem::path archiveDir = "data/files/bin";
        std::filesystem::create_directories(archiveDir);
        
        // Archive filename
        std::string archiveName = "file-rec-" + m_config.recordUID + ".tar.gz";
        std::filesystem::path archivePath = archiveDir / archiveName;
        
        // Security validation: check recordUID for command injection attempts
        if (m_config.recordUID.find('\'') != std::string::npos ||
            m_config.recordUID.find('"') != std::string::npos ||
            m_config.recordUID.find('$') != std::string::npos ||
            m_config.recordUID.find('`') != std::string::npos ||
            m_config.recordUID.find(';') != std::string::npos ||
            m_config.recordUID.find('|') != std::string::npos ||
            m_config.recordUID.find('&') != std::string::npos ||
            m_config.recordUID.find('\n') != std::string::npos ||
            m_config.recordUID.find('\r') != std::string::npos) {
            std::cerr << m_tag << " Security: Invalid characters in recordUID" << std::endl;
            m_state = State::FAILURE;
            return std::nullopt;
        }
        
        // Get parent directory and directory name
        std::string parentDir = m_recordDir.parent_path().string();
        std::string dirName = m_recordDir.filename().string();
        
        // Security validation: ensure paths don't contain dangerous characters
        if (parentDir.find('\'') != std::string::npos || parentDir.find('"') != std::string::npos ||
            dirName.find('\'') != std::string::npos || dirName.find('"') != std::string::npos) {
            std::cerr << m_tag << " Security: Invalid characters in path" << std::endl;
            m_state = State::FAILURE;
            return std::nullopt;
        }
        
        // Build tar command with quoted arguments to prevent injection
        std::stringstream cmd;
        cmd << "tar -czf '" << archivePath.string() << "'"
            << " -C '" << parentDir << "'"
            << " '" << dirName << "'";
        
        std::cout << m_tag << " Packing: " << cmd.str() << std::endl;
        
        int result = std::system(cmd.str().c_str());
        if (result != 0) {
            std::cerr << m_tag << " tar command failed with exit code: " << result << std::endl;
            m_state = State::FAILURE;
            return std::nullopt;
        }
        
        // Verify archive was created
        if (!std::filesystem::exists(archivePath)) {
            std::cerr << m_tag << " Archive not found after packing: " << archivePath << std::endl;
            m_state = State::FAILURE;
            return std::nullopt;
        }
        
        std::cout << m_tag << " Successfully packed to: " << archivePath << std::endl;
        std::cout << m_tag << " Archive size: " << std::filesystem::file_size(archivePath) << " bytes" << std::endl;
        
        // Create file info metadata for the packed archive using InfoDataManager (via FileManager callback)
        std::string fileUID = "file-rec-" + m_config.recordUID;
        
        bool fileInfoCreated = FileManager::instance().createFileInfo(
            fileUID,
            archivePath,
            m_config.recordUID,
            m_config.cameraUID
        );
        
        if (fileInfoCreated) {
            std::cout << m_tag << " Created FileInfo entry: " << fileUID << std::endl;
        } else {
            std::cerr << m_tag << " Warning: Failed to create FileInfo entry for: " << fileUID << std::endl;
        }


        m_state = State::PACKED;
        return archivePath;
        
    } catch (const std::exception& e) {
        std::cerr << m_tag << " Exception during packing: " << e.what() << std::endl;
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
