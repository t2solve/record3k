#include <liblbt/action_manager.h>
#include <liblbt/json_store.h>
#include <liblbt/vimbax_frame_source.h>
#include <liblbt/disk_image_frame_source.h>
#include <liblbt/pipeline_config_loader.h>
#include <liblbt/factory/model_factory.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>

namespace lbt {

ActionManager::ActionManager(std::filesystem::path recordsDir, std::filesystem::path outputDir)
    : recordsDir_(std::move(recordsDir)), outputDir_(std::move(outputDir)) {
    // Ensure directories exist
    std::filesystem::create_directories(recordsDir_);
    std::filesystem::create_directories(outputDir_);
}

ActionManager::~ActionManager() {
    std::lock_guard lock(mutex_);
    // Stop all active recordings
    for (auto& [uid, session] : sessions_) {
        if (session->running) {
            session->stopRequested = true;
            if (session->thread && session->thread->joinable()) {
                session->thread->join();
            }
        }
    }
}

std::string ActionManager::generateRecordUID(RecordType type) const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    
    std::string prefix;
    switch (type) {
        case RecordType::Calibration: prefix = "cal"; break;
        case RecordType::PipelineTest: prefix = "test"; break;
        case RecordType::Recording: prefix = "rec"; break;
    }
    
    ss << prefix << "-" << std::put_time(std::localtime(&time), "%Y%m%d%H%M%S");
    return ss.str();
}

std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

std::unique_ptr<IFrameSource> ActionManager::createFrameSource(const std::string& camUID) {
//     // Load camera info to determine source type
//     JsonStore cameraStore(recordsDir_.parent_path() / "cameras");
//     cameraStore.start();
    
//     auto camJson = cameraStore.get(camUID);
//     if (!camJson) {
//         std::cerr << "[ActionManager] Camera not found: " << camUID << std::endl;
//         return nullptr;
//     }
    
//     auto camResult = ModelFactory::fromJsonCamera(*camJson);
//     if (!camResult.ok()) {
//         std::cerr << "[ActionManager] Invalid camera data: " << camResult.status().message() << std::endl;
//         return nullptr;
//     }
    
//     auto& cam = camResult.value();
    
//     // Create appropriate frame source based on camera type
//     if (cam.cameraType == "TYPE_CAM_VIMBA") {
// #ifdef VIMBAX_ENABLED
//         auto src = std::make_unique<VimbaXFrameSource>();
//         if (!src->configure(cam.macAddress)) {
//             std::cerr << "[ActionManager] Failed to configure VimbaX source for " << camUID << std::endl;
//             return nullptr;
//         }
//         return src;
// #else
//         std::cerr << "[ActionManager] VimbaX not enabled in build" << std::endl;
//         return nullptr;
// #endif
//     } else if (cam.cameraType == "TYPE_DISK_IMAGES") {
//         // Assume disk images stored in a specific location
//         auto imagePath = outputDir_ / "test_images" / camUID;
//         auto src = std::make_unique<DiskImageFrameSource>(imagePath.string());
//         if (!src->isReady()) {
//             std::cerr << "[ActionManager] Failed to configure disk image source for " << camUID << std::endl;
//             return nullptr;
//         }
//         return src;
//     }
    
//     std::cerr << "[ActionManager] Unknown camera type: " << cam.cameraType << std::endl;
    return nullptr;
}

std::vector<std::shared_ptr<ProcessStep>> ActionManager::loadPipeline(const std::string& pipelineUID) {
    // // Load pipeline configuration
    // JsonStore pipelineStore(recordsDir_.parent_path() / "pipelines");
    // pipelineStore.start();
    
    // auto pipeJson = pipelineStore.get(pipelineUID);
    // if (!pipeJson) {
    //     std::cerr << "[ActionManager] Pipeline not found: " << pipelineUID << std::endl;
    //     return {};
    // }
    
    // auto pipeResult = ModelFactory::fromJsonPipeline(*pipeJson);
    // if (!pipeResult.ok()) {
    //     std::cerr << "[ActionManager] Invalid pipeline data: " << std::endl;
    //     return {};
    // }
    
    // auto& pipeInfo = pipeResult.value();
    
    // // Load the actual pipeline XML/config file
    // auto configPath = recordsDir_.parent_path().parent_path() / "files" / "bin" / (pipeInfo.fileUID + ".xml");
    // if (!std::filesystem::exists(configPath)) {
    //     std::cerr << "[ActionManager] Pipeline config file not found: " << configPath << std::endl;
    //     return {};
    // }
    
    // PipelineConfigLoader loader;
    // auto steps = loader.load(configPath.string());
    // if (steps.empty()) {
    //     std::cerr << "[ActionManager] Failed to load pipeline steps from: " << configPath << std::endl;
    // }
    
    // return steps;
    return {};
}

void ActionManager::recordingWorker(const std::string& recordUID) {
    std::unique_ptr<RecordSession> session;
    
    {
        std::lock_guard lock(mutex_);
        auto it = sessions_.find(recordUID);
        if (it == sessions_.end()) {
            std::cerr << "[ActionManager] Recording session not found: " << recordUID << std::endl;
            return;
        }
        // Move session out to avoid holding lock during processing
        session = std::move(it->second);
        sessions_.erase(it);
    }
    
    session->running = true;
    updateRecordStatus(recordUID, "running");
    
    std::cout << "[ActionManager] Starting recording: " << recordUID << std::endl;
    
    auto startTime = std::chrono::steady_clock::now();
    auto endTime = startTime + std::chrono::milliseconds(static_cast<int64_t>(session->info.durationInSeconds * 1000));
    
    int frameCount = 0;
    
    while (!session->stopRequested && std::chrono::steady_clock::now() < endTime) {
        if (!session->frameSource->isReady()) {
            std::cerr << "[ActionManager] Frame source not ready for: " << recordUID << std::endl;
            break;
        }
        
        auto frame = session->frameSource->nextFrame();
        if (!frame) {
            std::cerr << "[ActionManager] No more frames available for: " << recordUID << std::endl;
            break;
        }
        
        // Process frame through pipeline
        if (!session->pipeline.empty()) {
            FilterConfig config; // Could be loaded per-record
            *frame = ProcessingPipeline::processLine(session->pipeline, *frame, config);
        }
        
        // TODO: Save processed frame to output file
        // For now, just count frames
        frameCount++;
        
        // Small sleep to avoid busy waiting (adjust based on camera frame rate)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    session->running = false;
    session->info.datetimeEnd = getCurrentTimestamp();
    session->info.status = session->stopRequested ? "stopped" : "completed";
    
    std::cout << "[ActionManager] Finished recording: " << recordUID 
              << " (frames: " << frameCount << ")" << std::endl;
    
    persistRecordInfo(session->info);
    
    // Put session back for cleanup
    {
        std::lock_guard lock(mutex_);
        sessions_[recordUID] = std::move(session);
    }
}

void ActionManager::persistRecordInfo(const api::RecordInfo& info) {
    JsonStore store(recordsDir_);
    store.start();
    store.put(info.recordUID, info.toJson());
}

void ActionManager::updateRecordStatus(const std::string& recordUID, const std::string& status) {
    std::lock_guard lock(mutex_);
    auto it = sessions_.find(recordUID);
    if (it != sessions_.end()) {
        it->second->info.status = status;
        persistRecordInfo(it->second->info);
    }
}

std::optional<std::string> ActionManager::startCalibration(
    const std::string& camUID,
    const std::string& calibrationUID,
    const std::string& pipelineUID,
    double durationInSeconds,
    const std::string& description) {

    (void)description; // Mark as intentionally unused for now
    //TODO check bound for durationInSeconds
    
    std::lock_guard lock(mutex_);
    
    auto recordUID = generateRecordUID(RecordType::Calibration);
    auto session = std::make_unique<RecordSession>();
    
    session->recordUID = recordUID;
    session->type = RecordType::Calibration;
    session->info.recordUID = recordUID;
    session->info.calibrationUID = calibrationUID;
    session->info.flagIsPipelineTest = false;
    session->info.pipelineUID = pipelineUID;
    session->info.studyUID = "";
    session->info.status = "queued";
    session->info.durationInSeconds = durationInSeconds;
    session->info.datetimeStart = getCurrentTimestamp();
    session->info.datetimeEnd = "";
    session->info.fileUID = "file-" + recordUID;
    session->outputPath = outputDir_ / (recordUID + ".data");
    

    CameraManager camMgr(recordsDir_.parent_path() / "cameras"); //TODO check path
    camMgr.load();
    auto cam = camMgr.get(camUID);
    if (!cam) {
        std::cerr << "[ActionManager] Camera not found: " << camUID << std::endl;
        return std::nullopt;
    }

    // Create appropriate frame source based on camera type
    if (cam->cameraType == "TYPE_CAM_VIMBA") {
#ifdef VIMBAX_ENABLED
        // VimbaXFrameSource constructor: VimbaXFrameSource(serial, mac, flag)
        session->frameSource = std::make_unique<VimbaXFrameSource>(camUID, cam->macAddress, false);
        if (!session->frameSource->isReady()) {
            std::cerr << "[ActionManager] Failed to initialize VimbaX source for " << camUID << std::endl;
            return std::nullopt;
        }
#else
        std::cerr << "[ActionManager] VimbaX not enabled in build" << std::endl;
        return std::nullopt;
#endif
    } else if (cam->cameraType == "TYPE_FILE") {
        std::filesystem::path imgDir = recordsDir_.parent_path().parent_path() / "files" / "bin" / camUID;
        if (!std::filesystem::exists(imgDir)) {
            std::cerr << "[ActionManager] Image directory not found: " << imgDir << std::endl;
            return std::nullopt;
        }
        session->frameSource = std::make_unique<DiskImageFrameSource>(imgDir.string(), "*.jpg");
        if (!session->frameSource->isReady()) {
            std::cerr << "[ActionManager] Failed to initialize disk image source for " << camUID << std::endl;
            return std::nullopt;
        }
    } else {
        std::cerr << "[ActionManager] Unknown camera type: " << cam->cameraType << std::endl;
        return std::nullopt;
    }

    // // Create frame source and pipeline
    // session->frameSource = createFrameSource(camUID);
    // if (!session->frameSource) {
    //     return std::nullopt;
    // }
    
    session->pipeline = createDefaultPipeline();
    if (session->pipeline.empty()) {
        std::cerr << "[ActionManager] Warning: empty pipeline for " << recordUID << std::endl;
    }
    
    // Start recording thread
    session->thread = std::make_unique<std::thread>(&ActionManager::recordingWorker, this, recordUID);
    
    persistRecordInfo(session->info);
    sessions_[recordUID] = std::move(session);
    
    return recordUID;
}

std::vector<std::shared_ptr<ProcessStep>> ActionManager::createDefaultPipeline() {
    std::vector<std::shared_ptr<ProcessStep>> pipeline;
    // Example: simple lens correction step
    pipeline.push_back(ProcessStepFactory::createLensCorrection(ProcessingMode::CPU_ONLY));
    // Add more default steps as needed
    return pipeline;
}


std::optional<std::string> ActionManager::startPipelineTest(
    const std::string& camUID,
    const std::string& pipelineUID,
    double durationInSeconds,
    const std::string& description) {
    
    std::lock_guard lock(mutex_);
    
    auto recordUID = generateRecordUID(RecordType::PipelineTest);
    auto session = std::make_unique<RecordSession>();
    
    session->recordUID = recordUID;
    session->type = RecordType::PipelineTest;
    session->info.recordUID = recordUID;
    session->info.calibrationUID = "";
    session->info.flagIsPipelineTest = true;
    session->info.pipelineUID = pipelineUID;
    session->info.studyUID = "";
    session->info.status = "queued";
    session->info.durationInSeconds = durationInSeconds;
    session->info.datetimeStart = getCurrentTimestamp();
    session->info.datetimeEnd = "";
    session->info.fileUID = "file-" + recordUID;
    session->outputPath = outputDir_ / (recordUID + ".data");
    
    session->frameSource = createFrameSource(camUID);
    if (!session->frameSource) {
        return std::nullopt;
    }
    
    session->pipeline = loadPipeline(pipelineUID);
    
    session->thread = std::make_unique<std::thread>(&ActionManager::recordingWorker, this, recordUID);
    
    persistRecordInfo(session->info);
    sessions_[recordUID] = std::move(session);
    
    return recordUID;
}

std::optional<std::string> ActionManager::startRecording(
    const std::string& camUID,
    const std::string& calibrationUID,
    const std::string& pipelineUID,
    const std::string& studyUID,
    double durationInSeconds,
    const std::string& description) {
    
    std::lock_guard lock(mutex_);
    
    auto recordUID = generateRecordUID(RecordType::Recording);
    auto session = std::make_unique<RecordSession>();
    
    session->recordUID = recordUID;
    session->type = RecordType::Recording;
    session->info.recordUID = recordUID;
    session->info.calibrationUID = calibrationUID;
    session->info.flagIsPipelineTest = false;
    session->info.pipelineUID = pipelineUID;
    session->info.studyUID = studyUID;
    session->info.status = "queued";
    session->info.durationInSeconds = durationInSeconds;
    session->info.datetimeStart = getCurrentTimestamp();
    session->info.datetimeEnd = "";
    session->info.fileUID = "file-" + recordUID;
    session->outputPath = outputDir_ / (recordUID + ".data");
    
    session->frameSource = createFrameSource(camUID);
    if (!session->frameSource) {
        return std::nullopt;
    }
    
    session->pipeline = loadPipeline(pipelineUID);
    
    session->thread = std::make_unique<std::thread>(&ActionManager::recordingWorker, this, recordUID);
    
    persistRecordInfo(session->info);
    sessions_[recordUID] = std::move(session);
    
    return recordUID;
}

bool ActionManager::stopRecording(const std::string& recordUID) {
    std::lock_guard lock(mutex_);
    
    auto it = sessions_.find(recordUID);
    if (it == sessions_.end()) {
        return false;
    }
    
    it->second->stopRequested = true;
    return true;
}

std::optional<api::RecordInfo> ActionManager::getRecordInfo(const std::string& recordUID) const {
    std::lock_guard lock(mutex_);
    
    auto it = sessions_.find(recordUID);
    if (it != sessions_.end()) {
        return it->second->info;
    }
    
    // Try loading from disk if not in active sessions
    JsonStore store(recordsDir_);
    store.start();
    auto json = store.get(recordUID);
    if (json) {
        return api::RecordInfo::fromJson(*json);
    }
    
    return std::nullopt;
}

std::vector<api::RecordInfo> ActionManager::getActiveRecordings() const {
    std::lock_guard lock(mutex_);
    
    std::vector<api::RecordInfo> result;
    for (const auto& [uid, session] : sessions_) {
        if (session->running) {
            result.push_back(session->info);
        }
    }
    return result;
}

void ActionManager::cleanupFinished() {
    std::lock_guard lock(mutex_);
    
    std::vector<std::string> toRemove;
    for (auto& [uid, session] : sessions_) {
        if (!session->running && session->thread && session->thread->joinable()) {
            session->thread->join();
            toRemove.push_back(uid);
        }
    }
    
    for (const auto& uid : toRemove) {
        sessions_.erase(uid);
    }
}

} // namespace lbt
