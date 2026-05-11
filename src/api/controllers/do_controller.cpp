#include <api/controllers/do_controller.h>
#include <drogon/drogon.h>
#include <api/runtime/info_data_manager.h>
#include <api/runtime/app_config.h>
#include <liblbt/camera_manager.h>
#include <liblbt/camera_calibrator.h>
#include <liblbt/factory/model_factory.h>
#include <liblbt/factory/frame_source_factory.h>
#include <liblbt/pipeline_config_loader.h>
#include <liblbt/record_manager.h>
#include <liblbt/calibration_injector.h>
#include <liblbt/file_manager.h>
#include <ctime>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <random>
#include <csignal>
#include <pthread.h>

using namespace drogon;
using namespace api;

static std::string makeUID(const std::string &prefix) {
    // Use milliseconds since epoch + random component for better uniqueness
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    // Add a random component to ensure uniqueness even within the same millisecond
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(1000, 9999);
    
    return prefix + "-" + std::to_string(ms) + "-" + std::to_string(dis(gen));
}

static std::string getCurrentTimeISO8601() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t_now), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

static std::string getThreadIdStr() {
    std::stringstream ss;
    ss << std::this_thread::get_id();
    return ss.str();
}

void DoController::cameraCalibrate(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> && cb, const std::string &camUID) {
    LOG_INFO << "[DoController::cameraCalibrate] Starting calibration for: " << camUID;
    
    auto json = req->getJsonObject();
    std::string desc = (json && json->isMember("description")) ? (*json)["description"].asString() : "Calibration run";
    
    // Run calibration in background thread to avoid blocking API and keep RecordManager alive
    std::thread calibThread([cb = std::move(cb), camUID, desc]() mutable {
        LOG_INFO << "[DoController::cameraCalibrate Thread] Background thread started for: " << camUID;

// #ifdef VIMBAX_ENABLED
//         // Unblock VimbaX signals — FrameSourceFactory opens a VimbaX GigE stream which
//         // internally spawns packet threads; they inherit this thread's mask.
//         {
//             sigset_t vimbaxMask;
//             sigemptyset(&vimbaxMask);
//             sigaddset(&vimbaxMask, SIGUSR1);
//             sigaddset(&vimbaxMask, SIGUSR2);
//             sigaddset(&vimbaxMask, SIGIO);
//             for (int sig = SIGRTMIN; sig <= SIGRTMAX; ++sig)
//                 sigaddset(&vimbaxMask, sig);
//             pthread_sigmask(SIG_UNBLOCK, &vimbaxMask, nullptr);
//         }
// #endif

        try {
            // Load camera info from InfoDataManager (uses cached JsonStore)
            LOG_DEBUG << "[DoController::cameraCalibrate Thread] Loading camera info for: " << camUID;
            auto camJsonOpt = infoDataManager().get("cameras", camUID);
            if (!camJsonOpt) {
                LOG_ERROR << "[DoController::cameraCalibrate Thread] Camera not found: " << camUID;
                Json::Value err;
                err["error"] = "Camera not found: " + camUID;
                auto resp = HttpResponse::newHttpJsonResponse(err);
                resp->setStatusCode(k404NotFound);
                cb(resp);
                return;
            }
            
            // Parse JSON to CameraInfo struct
            auto camInfoResult = lbt::ModelFactory::fromJsonCamera(*camJsonOpt);
            if (!camInfoResult.ok()) {
                LOG_ERROR << "[DoController::cameraCalibrate Thread] Invalid camera data: " << camInfoResult.error();
                Json::Value err;
                err["error"] = "Invalid camera data: " + camInfoResult.error();
                auto resp = HttpResponse::newHttpJsonResponse(err);
                resp->setStatusCode(k500InternalServerError);
                cb(resp);
                return;
            }
            
            api::CameraInfo camInfo = camInfoResult.value();
            LOG_INFO << "[DoController::cameraCalibrate Thread] Camera loaded - Type: " << camInfo.cameraType;

            // Create frame source from camera info
            auto frameSource = lbt::FrameSourceFactory::createFromCameraInfo(camInfo, true);
            if (frameSource == nullptr) {
                LOG_ERROR << "[DoController::cameraCalibrate Thread] Frame source creation failed for camera: " << camUID;
                Json::Value err;
                err["error"] = "Frame source creation failed";
                auto resp = HttpResponse::newHttpJsonResponse(err);
                resp->setStatusCode(k500InternalServerError);
                cb(resp);
                return;
            }

            // Check if frame source is ready
            if (!frameSource->isReady()) {
                LOG_ERROR << "[DoController::cameraCalibrate Thread] Frame source is NOT ready for camera: " << camUID;
                Json::Value err;
                err["error"] = "Camera not ready";
                auto resp = HttpResponse::newHttpJsonResponse(err);
                resp->setStatusCode(k500InternalServerError);
                cb(resp);
                return;
            }

            // // Load default recording pipeline
            // std::string defaultPipelineUID = "pipe_default_calibration_01.json";   
            // auto pipeConfig = PipelineConfigLoader::loadFromJSON(defaultPipelineUID);
            // if (pipeConfig.steps.empty()) {
            //     LOG_ERROR << "[DoController::cameraCalibrate Thread] Failed to load pipeline or pipeline has no steps";
            //     Json::Value err;
            //     err["error"] = "Pipeline load failed or empty";
            //     auto resp = HttpResponse::newHttpJsonResponse(err);
            //     resp->setStatusCode(k500InternalServerError);
            //     cb(resp);
            //     return;
            // }
            
            // Generate unique record UID (without "file-rec-" prefix - RecordManager adds it)
            std::string recordUID = makeUID("cal");
            
            // create a processing pipeline
            std::vector<std::shared_ptr<ProcessStep>> pipeline;
            std::vector<FilterConfig> configs;
            ProcessingMode mode = ProcessingMode::CUDA_PREFERRED;
            
            // Add a frame writer step
            pipeline.push_back(ProcessStepFactory::createFrameWriter(mode));
            FilterConfig frameWriterConfig;
            // Path is managed by FileManager and RecordManager
            // RecordManager will add "file-rec-" prefix: outputDir/file-rec-{recordUID}/frames
            std::string outputDir = lbt::FileManager::instance().getTempOutputPath().string() + "/file-rec-" + recordUID + "/";
            std::string frameOutputDir = outputDir + "frames";
            
                                        frameWriterConfig.setParameter("output_dir", frameOutputDir);
            frameWriterConfig.setParameter("format", "jpg");
            configs.push_back(frameWriterConfig);
            
            // Build pipeline config
            PipelineConfigLoader::PipelineConfig pipeConfig;
            pipeConfig.steps = pipeline;
            pipeConfig.configs = configs;
            pipeConfig.mode = mode;

            // Create RecordManager configuration
            lbt::RecordManager::RecordConfig recordConfig;
            recordConfig.frameSource = std::move(frameSource);
            recordConfig.pipeline = pipeConfig;
            recordConfig.totalTimeSeconds = 30.0;  
            recordConfig.targetFPS = 2;          
            recordConfig.recordUID = recordUID;
            recordConfig.cameraUID = camUID;  // Set camera UID for locking
            recordConfig.outputDir = lbt::FileManager::instance().getTempOutputPath().string();
            
            // Create RecordManager instance (must live as long as recording runs)
            auto recorder = std::make_shared<lbt::RecordManager>(std::move(recordConfig));
            
            // Register with FileManager for automatic packing when complete
            lbt::FileManager::instance().registerRecording(recordUID, recorder, camUID);
            
            LOG_INFO << "[DoController::cameraCalibrate Thread] Initial state: " << recorder->getStateString();
            
            // Start recording
            if (!recorder->start()) {
                LOG_ERROR << "[DoController::cameraCalibrate Thread] Failed to start recording for camera: " << camUID;
                Json::Value err;
                err["error"] = "Recording start failed";
                auto resp = HttpResponse::newHttpJsonResponse(err);
                resp->setStatusCode(k500InternalServerError);
                cb(resp);
                return;
            }
            
            // Wait for recording to start
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            LOG_INFO << "[DoController::cameraCalibrate Thread] Recording started: " << recorder->getStateString();

            // // Send immediate response that calibration started
            std::string now = getCurrentTimeISO8601();
            CalibrationInfo c{recordUID, camUID, desc, "started", now, "file-rec-" + recordUID};
            
            auto resp = HttpResponse::newHttpJsonResponse(c.toJson());
            resp->setStatusCode(k202Accepted);
            cb(resp);
            
            LOG_INFO << "[DoController::cameraCalibrate Thread] Response sent, waiting for recording to complete...";
            
            // Wait for recording to complete (blocking in background thread is OK)
            while (recorder->getState() == lbt::RecordManager::State::RECORDING) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            
            LOG_INFO << "[DoController::cameraCalibrate Thread] Recording completed with state: " << recorder->getStateString();
            
            //do calibration
            //TODO: load calibration config from AppConfig
            lbt::CameraCalibrator::Config config;
            config.boardSize = cv::Size(12, 12);
            config.squareSize = 1.5f;
            config.writeExtrinsics = true;
            config.writePoints = true;

            lbt::CameraCalibrator calibrator(config);

            auto result = calibrator.calibrate(outputDir, "others/camera_calibration_full.xml", "camera_calibration_short.xml");
            //print the whole result
            LOG_INFO << "[DoController::cameraCalibrate Thread] Calibration result: " << result.toString();
            if (result.success) {
                LOG_INFO << "[DoController::cameraCalibrate Thread] Calibration succeeded! Error: " << result.avgReprojectionError;
                //we should package the calibration result file as well
                std::string calUID = makeUID("cal");
                std::string completedAt = getCurrentTimeISO8601();
                //TODO check why created 2 time s ? 
                auto caliInfoResult = lbt::ModelFactory::makeCalibrationInfo(
                    calUID,
                    camUID,
                    desc,
                    result.success ? "completed" : "failed",
                    completedAt,
                    "file-rec-" + recordUID,
                    result.avgReprojectionError,
                    result.validFrames,
                    result.imageSize.width,
                    result.imageSize.height
                );
                
                if (!caliInfoResult.ok()) {
                    LOG_ERROR << "[DoController::cameraCalibrate Thread] Failed to create CalibrationInfo: " << caliInfoResult.error();
                } else {
                    api::CalibrationInfo caliInfo = caliInfoResult.value();
                    
                    // Convert to JSON and store using InfoDataManager
                    Json::Value caliJson = caliInfo.toJson();
                    
                    // Store in InfoDataManager (centralized metadata - single source of truth)
                    if (infoDataManager().put("calibrations", caliInfo.calibrationUID, caliJson)) {
                        LOG_INFO << "[DoController::cameraCalibrate Thread] Stored calibration info for: " << calUID;
                    } else {
                        LOG_ERROR << "[DoController::cameraCalibrate Thread] Failed to store calibration info for: " << calUID;
                    }
                }

            } else {
                LOG_ERROR << "[DoController::cameraCalibrate Thread] Calibration failed: " << result.errorMessage;
                // Persist a failed entry so the UI shows the failure
                std::string calUID = makeUID("cal");
                std::string failedAt = getCurrentTimeISO8601();
                auto failInfoResult = lbt::ModelFactory::makeCalibrationInfo(
                    calUID,
                    camUID,
                    result.errorMessage.empty() ? desc : std::optional<std::string>(result.errorMessage),
                    "failed",
                    failedAt,
                    "file-rec-" + recordUID
                );
                if (failInfoResult.ok()) {
                    Json::Value failJson = failInfoResult.value().toJson();
                    infoDataManager().put("calibrations", failInfoResult.value().calibrationUID, failJson);
                    LOG_INFO << "[DoController::cameraCalibrate Thread] Stored failed calibration info for: " << calUID;
                }
            }

           
            
            // Auto-pack the recording and create FileInfo entry
            lbt::FileManager::instance().checkAndPackCompletedRecordings();
            
            // Explicitly release the RecordManager to free camera resources immediately
            LOG_INFO << "[DoController::cameraCalibrate Thread] Releasing RecordManager to free camera";
            recorder.reset();
            LOG_INFO << "[DoController::cameraCalibrate Thread] RecordManager released";
            
        } catch (const std::exception& e) {
            LOG_ERROR << "[DoController::cameraCalibrate Thread] Exception: " << e.what();
            Json::Value err;
            err["error"] = std::string("Calibration failed: ") + e.what();
            auto resp = HttpResponse::newHttpJsonResponse(err);
            resp->setStatusCode(k500InternalServerError);
            cb(resp);
        }
        
        LOG_INFO << "[DoController::cameraCalibrate Thread] Thread exiting";
    });
    
    calibThread.detach(); // Let it run independently
    LOG_INFO << "[DoController::cameraCalibrate] Request handler returning, calibration running in background";
}

void DoController::recordStart(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> && cb, const std::string &camUID) {
    auto json = req->getJsonObject();
    double durationMs = (json && json->isMember("durationInMS")) ? (*json)["durationInMS"].asDouble() : 1000.0;
    std::string calibrationUID = (json && json->isMember("calibrationUID")) ? (*json)["calibrationUID"].asString() : "cal-latest";
    std::string pipelineUID = (json && json->isMember("pipelineUID")) ? (*json)["pipelineUID"].asString() : "pipe-default";
    std::string studyInfoUID = (json && json->isMember("studyInfoUID")) ? (*json)["studyInfoUID"].asString() : "study-unknown";
    std::string description = (json && json->isMember("description")) ? (*json)["description"].asString() : "record session";
    bool pipeLineTest = (json && json->isMember("pipeLineTest")) ? (*json)["pipeLineTest"].asBool() : false;
    double targetFPS = (json && json->isMember("targetFPS")) ? (*json)["targetFPS"].asDouble() : 5.0;

    //TODO check if all parameters are valid
    //test 1 validate duration
    //TODO test max
    if (durationMs <= 0.0)  {
        Json::Value err;
        err["error"] = "Invalid durationInMS, must be positive";
        auto resp = HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(k400BadRequest);
        cb(resp);
        return;
    } 
    
    // //try to load the pipeline to validate it exists
    std::string staticPipeLineFile = "data/pipelines/pipe-default-42.json";
    // auto pipeConfig = PipelineConfigLoader::loadFromJSON(staticPipeLineFile);
    // if (pipeConfig.steps.empty()) {
    //     Json::Value err;
    //     err["error"] = "Pipeline not found or has no steps: " + pipelineUID;
    //     auto resp = HttpResponse::newHttpJsonResponse(err);
    //     resp->setStatusCode(k400BadRequest);
    //     cb(resp);
    //     return;
    // }

    std::string recordUID = makeUID("rec");
    RecordInfo recordInfoObj{recordUID, calibrationUID, false, "pipe-default-42", studyInfoUID, "started", durationMs/1000.0, "2025-11-07T12:03:00Z", "", "file-rec-placeholder"};
    // auto resp = HttpResponse::newHttpJsonResponse(r.toJson());
    // resp->setStatusCode(k201Created);
    // cb(resp);



    LOG_INFO << "[DoController::recordStart][TID:" << getThreadIdStr() << "] Starting recording for: " << recordUID;
    
     // Run calibration in background thread to avoid blocking API and keep RecordManager alive
    std::thread recordThread([cb = std::move(cb), 
        camUID, 
        description, 
        recordUID, 
        recordInfoObj, 
        durationMs, 
        calibrationUID, 
        pipelineUID, 
        studyInfoUID, 
        staticPipeLineFile,
        pipeLineTest,
        targetFPS]() mutable {
        std::string threadId = getThreadIdStr();
        LOG_INFO << "[DoController::recordStart][TID:" << threadId << "] Background thread started for camera: " << camUID;

// #ifdef VIMBAX_ENABLED
//         // Unblock VimbaX signals — FrameSourceFactory opens a VimbaX GigE stream which
//         // internally spawns packet threads; they inherit this thread's mask.
//         {
//             sigset_t vimbaxMask;
//             sigemptyset(&vimbaxMask);
//             sigaddset(&vimbaxMask, SIGUSR1);
//             sigaddset(&vimbaxMask, SIGUSR2);
//             sigaddset(&vimbaxMask, SIGIO);
//             for (int sig = SIGRTMIN; sig <= SIGRTMAX; ++sig)
//                 sigaddset(&vimbaxMask, sig);
//             pthread_sigmask(SIG_UNBLOCK, &vimbaxMask, nullptr);
//         }
// #endif

        try {
            // Load camera info from InfoDataManager (uses cached JsonStore)
            LOG_DEBUG << "[DoController::recordStart][TID:" << threadId << "] Loading camera info for: " << camUID;
            auto camJsonOpt = infoDataManager().get("cameras", camUID);
            if (!camJsonOpt) {
                LOG_ERROR << "[DoController::recordStart][TID:" << threadId << "] Camera not found: " << camUID;
                Json::Value err;
                err["error"] = "Camera not found: " + camUID;
                auto resp = HttpResponse::newHttpJsonResponse(err);
                resp->setStatusCode(k404NotFound);
                cb(resp);
                return;
            }
            
            // Parse JSON to CameraInfo struct
            auto camInfoResult = lbt::ModelFactory::fromJsonCamera(*camJsonOpt);
            if (!camInfoResult.ok()) {
                LOG_ERROR << "[DoController::recordStart][TID:" << threadId << "] Invalid camera data: " << camInfoResult.error();
                Json::Value err;
                err["error"] = "Invalid camera data: " + camInfoResult.error();
                auto resp = HttpResponse::newHttpJsonResponse(err);
                resp->setStatusCode(k500InternalServerError);
                cb(resp);
                return;
            }
            
            api::CameraInfo camInfo = camInfoResult.value();
            LOG_INFO << "[DoController::recordStart][TID:" << threadId << "] Camera loaded - Type: " << camInfo.cameraType;

            // Create frame source from camera info
            auto frameSource = lbt::FrameSourceFactory::createFromCameraInfo(camInfo, true);
            if (frameSource == nullptr) {
                LOG_ERROR << "[DoController::recordStart][TID:" << threadId << "] Frame source creation failed for camera: " << camUID;
                Json::Value err;
                err["error"] = "Frame source creation failed";
                auto resp = HttpResponse::newHttpJsonResponse(err);
                resp->setStatusCode(k500InternalServerError);
                cb(resp);
                return;
            }

            // Check if frame source is ready
            if (!frameSource->isReady()) {
                LOG_ERROR << "[DoController::recordStart][TID:" << threadId << "] Frame source is NOT ready for camera: " << camUID;
                Json::Value err;
                err["error"] = "Camera not ready";
                auto resp = HttpResponse::newHttpJsonResponse(err);
                resp->setStatusCode(k500InternalServerError);
                cb(resp);
                return;
            }

            // Load pipeline from pipelineUID via InfoDataManager
            LOG_INFO << "[DoController::recordStart][TID:" << threadId << "] Loading pipeline: " << pipelineUID;
            
            PipelineConfigLoader::PipelineConfig pipeConfig;
            std::string pipelineFilePath;
            
            // Try to load from InfoDataManager first
            auto pipelineMetadata = infoDataManager().get("pipelines", pipelineUID);
            if (pipelineMetadata) {
                // Pipeline found in InfoDataManager, construct file path
                pipelineFilePath = "data/pipelines/" + pipelineUID + ".json";
                LOG_INFO << "[DoController::recordStart][TID:" << threadId << "] Found pipeline in InfoDataManager: " << pipelineFilePath;
            } else {
                // Fallback to static pipeline file
                pipelineFilePath = staticPipeLineFile;
                LOG_WARN << "[DoController::recordStart][TID:" << threadId << "] Pipeline not in InfoDataManager, using fallback: " << pipelineFilePath;
            }
            
            // Load pipeline configuration
            pipeConfig = PipelineConfigLoader::loadFromJSON(pipelineFilePath);
            if (pipeConfig.steps.empty()) {
                LOG_ERROR << "[DoController::recordStart][TID:" << threadId << "] Failed to load pipeline or pipeline has no steps: " << pipelineFilePath;
                Json::Value err;
                err["error"] = "Pipeline load failed or empty: " + pipelineFilePath;
                auto resp = HttpResponse::newHttpJsonResponse(err);
                resp->setStatusCode(k500InternalServerError);
                cb(resp);
                return;
            }
            
            LOG_INFO << "[DoController::recordStart][TID:" << threadId << "] Pipeline loaded successfully with " << pipeConfig.steps.size() << " steps";
            
            // Load calibration data
            // First, look up the calibration info from InfoDataManager to get the actual fileUID
            auto calInfoJson = infoDataManager().get("calibrations", calibrationUID);
            if (!calInfoJson) {
                LOG_ERROR << "[DoController::recordStart][TID:" << threadId << "] Calibration not found in InfoDataManager: " << calibrationUID;
                Json::Value err;
                err["error"] = "Calibration not found: " + calibrationUID;
                auto resp = HttpResponse::newHttpJsonResponse(err);
                resp->setStatusCode(k404NotFound);
                cb(resp);
                return;
            }
            
            // Extract fileUID from calibration info
            std::string fileUID = (*calInfoJson).get("fileUID", "").asString();
            if (fileUID.empty()) {
                LOG_ERROR << "[DoController::recordStart][TID:" << threadId << "] Calibration has no fileUID: " << calibrationUID;
                Json::Value err;
                err["error"] = "Calibration missing fileUID: " + calibrationUID;
                auto resp = HttpResponse::newHttpJsonResponse(err);
                resp->setStatusCode(k500InternalServerError);
                cb(resp);
                return;
            }
            
            LOG_INFO << "[DoController::recordStart][TID:" << threadId << "] Loading calibration from fileUID: " << fileUID;
            auto calibData = lbt::CalibrationInjector::loadFromArchive(fileUID);
            
            if (calibData.valid) {
                LOG_INFO << "[DoController::recordStart][TID:" << threadId << "] Calibration loaded successfully!";
                
                // Inject into pipeline
                int modifiedSteps = lbt::CalibrationInjector::injectIntoPipeline(pipeConfig, calibData);
                LOG_INFO << "[DoController::recordStart][TID:" << threadId << "] Injected calibration into " << modifiedSteps << " pipeline steps";
            } else {
                LOG_ERROR << "[DoController::recordStart][TID:" << threadId << "] " << calibData.errorMessage;
                LOG_ERROR << "[DoController::recordStart][TID:" << threadId << "] Failed to load calibration: " << calibData.errorMessage;
                Json::Value err;
                err["error"] = "Calibration injection into pipeline failed: " + calibData.errorMessage;
                auto resp = HttpResponse::newHttpJsonResponse(err);
                resp->setStatusCode(k500InternalServerError);
                cb(resp);
                return;
            }

            // Create RecordManager configuration
            lbt::RecordManager::RecordConfig recordConfig;
            recordConfig.frameSource = std::move(frameSource);
            recordConfig.pipeline = pipeConfig;
            recordConfig.totalTimeSeconds = durationMs / 1000.0;  
            recordConfig.targetFPS = targetFPS;
            recordConfig.recordUID = recordUID;
            recordConfig.cameraUID = camUID;  // Set camera UID for locking
            recordConfig.outputDir = lbt::FileManager::instance().getTempOutputPath().string();
            // Set log mode based on pipeLineTest flag
            recordConfig.logMode = pipeLineTest ? lbt::ResultLogger::LogMode::DEBUG : lbt::ResultLogger::LogMode::PRODUCTION;
            LOG_INFO << "[DoController::recordStart][TID:" << threadId << "] Pipeline test mode: " << (pipeLineTest ? "DEBUG" : "PRODUCTION");
            
            // Set optional calibration and study files for copying
            if (calibrationUID != "cal-latest") {
                // Get calibration file path from InfoDataManager
                auto calInfo = infoDataManager().get("calibrations", calibrationUID);
                if (calInfo && calInfo->isMember("fileUID")) {
                    // Find the actual calibration XML file
                    std::string calFilePath = "data/files/" + (*calInfo)["fileUID"].asString();
                    if (std::filesystem::exists(calFilePath)) {
                        recordConfig.calibrationFile = calFilePath;
                        LOG_INFO << "[DoController::recordStart][TID:" << threadId << "] Will copy calibration: " << calFilePath;
                    }
                }
            }
            
            if (studyInfoUID != "study-unknown") {
                // Verify study exists in InfoDataManager
                auto studyInfo = infoDataManager().get("studies", studyInfoUID);
                if (studyInfo) {
                    // Construct file path based on InfoDataManager structure
                    std::string studyFilePath = "data/studies/" + studyInfoUID + ".json";
                    if (std::filesystem::exists(studyFilePath)) {
                        recordConfig.studyFile = studyFilePath;
                        LOG_INFO << "[DoController::recordStart][TID:" << threadId << "] Will copy study: " << studyFilePath;
                    } else {
                        LOG_WARN << "[DoController::recordStart][TID:" << threadId << "] Study file not found on disk: " << studyFilePath;
                    }
                } else {
                    LOG_WARN << "[DoController::recordStart][TID:" << threadId << "] Study not found in InfoDataManager: " << studyInfoUID;
                }
            }
            
            // Create initial record info in data/records/
            Json::Value recordInfoJson;
            recordInfoJson["recordUID"] = recordUID;
            recordInfoJson["calibrationUID"] = calibrationUID;
            recordInfoJson["pipelineUID"] = pipelineUID;
            recordInfoJson["studyInfoUID"] = studyInfoUID;
            recordInfoJson["cameraUID"] = camUID;
            recordInfoJson["description"] = description;
            recordInfoJson["status"] = "started";
            recordInfoJson["targetDurationSeconds"] = durationMs / 1000.0;
            recordInfoJson["targetFPS"] = targetFPS;
            recordInfoJson["datetimeStart"] = getCurrentTimeISO8601();
            recordInfoJson["fileUID"] = "file-rec-" + recordUID;
            
            if (!infoDataManager().createRecordInfo(recordInfoJson)) {
                LOG_ERROR << "[DoController::recordStart][TID:" << threadId << "] Failed to create record info in data/records/";
            }
            
            // Create RecordManager instance (must live as long as recording runs)
            auto recorder = std::make_shared<lbt::RecordManager>(std::move(recordConfig));
            
            // Register with FileManager for automatic packing when complete
            lbt::FileManager::instance().registerRecording(recordUID, recorder, camUID);
            
            LOG_INFO << "[DoController::recordStart][TID:" << threadId << "] Initial state: " << recorder->getStateString();
            
            // Start recording
            if (!recorder->start()) {
                LOG_ERROR << "[DoController::recordStart][TID:" << threadId << "] Failed to start recording for camera: " << camUID;
                Json::Value err;
                err["error"] = "Recording start failed";
                auto resp = HttpResponse::newHttpJsonResponse(err);
                resp->setStatusCode(k500InternalServerError);
                cb(resp);
                return;
            }
            
            // Wait for recording to start
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            LOG_INFO << "[DoController::recordStart][TID:" << threadId << "] Recording started: " << recorder->getStateString();

  
            //create response
            auto resp = HttpResponse::newHttpJsonResponse(recordInfoObj.toJson());
            resp->setStatusCode(k202Accepted);
            cb(resp);
            
            LOG_INFO << "[DoController::recordStart][TID:" << threadId << "] Response sent, waiting for recording to complete...";
            
            // Wait for recording to complete (blocking in background thread is OK)
            while (recorder->getState() == lbt::RecordManager::State::RECORDING) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            
            LOG_INFO << "[DoController::recordStart][TID:" << threadId << "] Recording completed with state: " << recorder->getStateString();
            
            // Update record info with completion status
            Json::Value updates;
            updates["status"] = (recorder->getState() == lbt::RecordManager::State::STOPPED) ? "completed" : "failed";
            updates["datetimeEnd"] = getCurrentTimeISO8601();
            
            auto stats = recorder->getStatistics();
            updates["framesRecorded"] = stats.framesRecorded;
            updates["actualFPS"] = stats.actualFPS;
            updates["completed"] = stats.completed;
            
            if (!infoDataManager().updateRecordInfo(recordUID, updates)) {
                LOG_ERROR << "[DoController::recordStart][TID:" << threadId << "] Failed to update record info";
            }
            
            // NOTE: Study file copying is handled by RecordManager (lines 489-495 set recordConfig.studyFile)
            // RecordManager copies the file during finalize() before packing the archive.
            // No need to copy it again here - would be duplication.

            // Auto-pack the recording and create FileInfo entry
            lbt::FileManager::instance().checkAndPackCompletedRecordings();
        
            
        } catch (const std::exception& e) {
            LOG_ERROR << "[DoController::recordStart][TID:" << threadId << "] Exception: " << e.what();
            Json::Value err;
            err["error"] = std::string("Recording failed: ") + e.what();
            auto resp = HttpResponse::newHttpJsonResponse(err);
            resp->setStatusCode(k500InternalServerError);
            cb(resp);
        }
        
        LOG_INFO << "[DoController::recordStart][TID:" << threadId << "] Thread exiting";
    });
    
    recordThread.detach(); // Let it run independently
    LOG_INFO << "[DoController::recordStart][TID:" << getThreadIdStr() << "] Request handler returning, recording running in background";

}

void DoController::cameraRecordStop(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb, const std::string &recordUID) {
    LOG_INFO << "[DoController::cameraRecordStop] Stopping recording: " << recordUID;
    
    // Check if recording exists in FileManager
    auto stateOpt = lbt::FileManager::instance().getRecordingState(recordUID);
    if (!stateOpt) {
        LOG_WARN << "[DoController::cameraRecordStop] Recording not found: " << recordUID;
        Json::Value err;
        err["error"] = "Recording not found: " + recordUID;
        auto resp = HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(k404NotFound);
        cb(resp);
        return;
    }
    
    std::string currentState = *stateOpt;
    LOG_INFO << "[DoController::cameraRecordStop] Current state: " << currentState;
    
    // Check if recording is still active
    if (currentState != "RECORDING") {
        LOG_WARN << "[DoController::cameraRecordStop] Recording not in RECORDING state: " << currentState;
        Json::Value response;
        response["recordUID"] = recordUID;
        response["status"] = currentState;
        response["message"] = "Recording already stopped or in state: " + currentState;
        auto resp = HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(k200OK);
        cb(resp);
        return;
    }
    
    // Stop the recording
    if (!lbt::FileManager::instance().stopRecording(recordUID)) {
        LOG_ERROR << "[DoController::cameraRecordStop] Failed to stop recording: " << recordUID;
        Json::Value err;
        err["error"] = "Failed to stop recording";
        auto resp = HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(k500InternalServerError);
        cb(resp);
        return;
    }
    
    // Get record info from InfoDataManager
    auto recordInfo = infoDataManager().get("records", recordUID);
    Json::Value response;
    if (recordInfo) {
        response = *recordInfo;
        response["status"] = "stopped";
        response["datetimeEnd"] = getCurrentTimeISO8601();
        
        // Update record info in InfoDataManager
        Json::Value updates;
        updates["status"] = "stopped";
        updates["datetimeEnd"] = getCurrentTimeISO8601();
        infoDataManager().updateRecordInfo(recordUID, updates);
    } else {
        // Fallback if record info not found
        response["recordUID"] = recordUID;
        response["status"] = "stopped";
        response["datetimeEnd"] = getCurrentTimeISO8601();
    }
    
    LOG_INFO << "[DoController::cameraRecordStop] Recording stopped successfully: " << recordUID;
    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);
    cb(resp);
}

// Test a specific camera by UID
void DoController::cameraTest(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> && cb, const std::string &camUID) {
    LOG_INFO << "[DoController::cameraTest] Starting camera test for: " << camUID;
    
    // Run camera test in a background thread to avoid blocking the API
    std::thread testThread([cb = std::move(cb), camUID]() mutable {
        LOG_INFO << "[DoController::cameraTest Thread] Background thread started for: " << camUID;
        try {
            // Load camera info from InfoDataManager (uses cached JsonStore)
            LOG_DEBUG << "[DoController::cameraTest Thread] Looking up camera in InfoDataManager: " << camUID;
            auto camJsonOpt = infoDataManager().get("cameras", camUID);
            if (!camJsonOpt) {
                LOG_WARN << "[DoController::cameraTest Thread] Camera not found: " << camUID;
                Json::Value err; 
                err["error"] = "Camera not found: " + camUID;
                auto resp = HttpResponse::newHttpJsonResponse(err);
                resp->setStatusCode(k404NotFound);
                LOG_DEBUG << "[DoController::cameraTest Thread] Sending 404 response";
                cb(resp);
                LOG_DEBUG << "[DoController::cameraTest Thread] Response sent, thread exiting";
                return;
            }
            
            // Parse JSON to CameraInfo struct
            auto camInfoResult = lbt::ModelFactory::fromJsonCamera(*camJsonOpt);
            if (!camInfoResult.ok()) {
                LOG_ERROR << "[DoController::cameraTest Thread] Invalid camera data: " << camInfoResult.error();
                Json::Value err; 
                err["error"] = "Invalid camera data: " + camInfoResult.error();
                auto resp = HttpResponse::newHttpJsonResponse(err);
                resp->setStatusCode(k500InternalServerError);
                cb(resp);
                return;
            }
            
            api::CameraInfo camInfo = camInfoResult.value();
            LOG_INFO << "[DoController::cameraTest Thread] Camera found, type: " << camInfo.cameraType;
            
            // // Get VimbaX configuration
            // LOG_DEBUG << "[DoController::cameraTest Thread] Getting VimbaX CTI path...";
            // std::string vimbaxCtiPath = api::AppConfig::instance().getVimbaxTransportLayerPath();
            // LOG_DEBUG << "[DoController::cameraTest Thread] VimbaX CTI path: '" << vimbaxCtiPath << "'";
            
            // Test the camera (this may block but it's in a background thread now)
            LOG_INFO << "[DoController::cameraTest Thread] Calling testCamera()...";
            
            // Create temporary CameraManager for testing
            auto cameraPath = api::AppConfig::instance().getInfoPath("cameras");
            lbt::CameraManager testMgr(cameraPath);
            lbt::CameraTestResult testResult = testMgr.testCamera(camInfo);
            LOG_INFO << "[DoController::cameraTest Thread] testCamera() returned with result: " << static_cast<int>(testResult);
            
            bool success = (testResult == lbt::CameraTestResult::SUCCESS);
            
            // Map enum to string for response
            std::string resultStr;
            std::string errorDetail;
            switch (testResult) {
                case lbt::CameraTestResult::SUCCESS:
                    resultStr = "online";
                    {
                        // Update datetimeLastSeen in the camera JSON and persist to disk
                        camInfo.datetimeLastSeen = getCurrentTimeISO8601();
                        Json::Value updatedCamJson = camInfo.toJson();
                        if (infoDataManager().put("cameras", camUID, updatedCamJson)) {
                            LOG_INFO << "[DoController::cameraTest Thread] Updated datetimeLastSeen for: " << camUID;
                        } else {
                            LOG_ERROR << "[DoController::cameraTest Thread] Failed to persist datetimeLastSeen for: " << camUID;
                        }
                    }
                    break;
                case lbt::CameraTestResult::ERROR_NOT_FOUND:
                    resultStr = "offline";
                    errorDetail = "Camera not found or not connected";
                    break;
                case lbt::CameraTestResult::ERROR_NOT_READY:
                    resultStr = "offline";
                    errorDetail = "Camera found but not ready; Might close VimbaX Viewer if it's open or check camera connection";
                    break;
                case lbt::CameraTestResult::ERROR_NO_FRAME:
                    resultStr = "offline";
                    errorDetail = "Camera ready but cannot get frame";
                    break;
                case lbt::CameraTestResult::ERROR_PATH_NOT_EXIST:
                    resultStr = "offline";
                    errorDetail = "File path does not exist";
                    break;
                case lbt::CameraTestResult::ERROR_EXCEPTION:
                    resultStr = "offline";
                    errorDetail = "Exception occurred during test";
                    break;
                case lbt::CameraTestResult::ERROR_NOT_BUILT:
                    resultStr = "offline";
                    errorDetail = "Required feature not built (e.g., VimbaX disabled)";
                    break;
                case lbt::CameraTestResult::ERROR_UNKNOWN_TYPE:
                    resultStr = "offline";
                    errorDetail = "Unknown camera type";
                    break;
                default:
                    resultStr = "offline";
                    errorDetail = "Unknown error";
                    break;
            }
            
            LOG_INFO << "[DoController::cameraTest Thread] Result: " << resultStr << ", Success: " << success;
            
            // Prepare response
            Json::Value result;
            result["camUID"] = camUID;
            result["testResult"] = resultStr;
            result["success"] = success;
            if (!errorDetail.empty()) {
                result["errorDetail"] = errorDetail;
            }
            
            LOG_DEBUG << "[DoController::cameraTest Thread] Preparing HTTP response...";
            auto resp = HttpResponse::newHttpJsonResponse(result);
            resp->setStatusCode(k200OK);
            LOG_INFO << "[DoController::cameraTest Thread] Sending response back...";
            cb(resp);
            LOG_INFO << "[DoController::cameraTest Thread] Response sent successfully, thread completing";
        } catch (const std::exception& e) {
            LOG_ERROR << "[DoController::cameraTest Thread] Exception caught: " << e.what();
            Json::Value err; 
            err["error"] = std::string("Camera test failed: ") + e.what();
            auto resp = HttpResponse::newHttpJsonResponse(err);
            resp->setStatusCode(k500InternalServerError);
            LOG_DEBUG << "[DoController::cameraTest Thread] Sending error response";
            cb(resp);
            LOG_DEBUG << "[DoController::cameraTest Thread] Error response sent";
        } catch (...) {
            LOG_ERROR << "[DoController::cameraTest Thread] Unknown exception caught";
            Json::Value err; 
            err["error"] = "Camera test failed: unknown exception";
            auto resp = HttpResponse::newHttpJsonResponse(err);
            resp->setStatusCode(k500InternalServerError);
            cb(resp);
        }
        LOG_INFO << "[DoController::cameraTest Thread] Thread exiting";
    });
    
    testThread.detach(); // Let it run independently
    LOG_INFO << "[DoController::cameraTest] Request handler returning, test running in background";
}
