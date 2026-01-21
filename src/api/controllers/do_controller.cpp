#include <api/controllers/do_controller.h>
#include <drogon/drogon.h>
#include <api/runtime/info_data_manager.h>
#include <api/runtime/app_config.h>
#include <liblbt/camera_manager.h>
#include <liblbt/factory/model_factory.h>
#include <liblbt/factory/frame_source_factory.h>
#include <liblbt/pipeline_config_loader.h>
#include <liblbt/record_manager.h>
#include <ctime>
#include <chrono>
#include <iomanip>
#include <sstream>

using namespace drogon;
using namespace api;

static std::string makeUID(const std::string &prefix) {
    return prefix + "-" + std::to_string(std::time(nullptr));
}

static std::string getCurrentTimeISO8601() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t_now), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

void DoController::cameraCalibrate(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> && cb, const std::string &camUID) {
    auto json = req->getJsonObject();
    
    std::string desc = (json && json->isMember("description")) ? (*json)["description"].asString() : "Calibration run";
    
    // Load camera info from InfoDataManager (uses cached JsonStore)
    LOG_DEBUG << "[DoController] cameraCalibrate: Loading camera info for: " << camUID;
    auto camJsonOpt = infoDataManager().get("cameras", camUID);
    if (!camJsonOpt) {
        LOG_ERROR << "[DoController] cameraCalibrate: Camera not found: " << camUID;
        auto resp = HttpResponse::newHttpJsonResponse(Json::Value());
        resp->setStatusCode(k404NotFound);
        cb(resp);
        return;
    }
    
    // Parse JSON to CameraInfo struct
    auto camInfoResult = lbt::ModelFactory::fromJsonCamera(*camJsonOpt);
    if (!camInfoResult.ok()) {
        LOG_ERROR << "[DoController] cameraCalibrate: Invalid camera data: " << camInfoResult.error();
        auto resp = HttpResponse::newHttpJsonResponse(Json::Value());
        resp->setStatusCode(k500InternalServerError);
        cb(resp);
        return;
    }
    
    api::CameraInfo camInfo = camInfoResult.value();
    LOG_INFO << "[DoController] cameraCalibrate: Camera loaded - Type: " << camInfo.cameraType;

    //create frame source from camera info
    auto frameSource = lbt::FrameSourceFactory::createFromCameraInfo(camInfo, true);
    //test  1
    if (frameSource == nullptr) {
        LOG_ERROR << "[DoController] cameraCalibrate: Frame source not found for camera: " << camUID;
        auto resp = HttpResponse::newHttpJsonResponse(Json::Value());
        resp->setStatusCode(k404NotFound);
        cb(resp);
        return;
    }

        // Use it
    if (!frameSource->isReady()) {
        LOG_ERROR << "[DoController] cameraCalibrate: Frame source is NOT ready for camera: " << camUID;
        auto resp = HttpResponse::newHttpJsonResponse(Json::Value());
        resp->setStatusCode(k500InternalServerError);
        cb(resp);
        return;
    }



    //we do load default recording pipeline XXXX
    std::string defaultPipelineUID = "pipe_default_calibration_01.json";   
    auto pipeConfig = PipelineConfigLoader::loadFromJSON(defaultPipelineUID);
    if (pipeConfig.steps.empty()) {
        LOG_ERROR << "[DoController] cameraCalibrate: Failed to load pipeline or pipeline has no steps";
        auto resp = HttpResponse::newHttpJsonResponse(Json::Value());
        resp->setStatusCode(k500InternalServerError);
        cb(resp);
        return;
    }

    //create a record
    // 3. Create RecordManager configuration
    lbt::RecordManager::RecordConfig recordConfig;
    recordConfig.frameSource = std::move(frameSource);
    recordConfig.pipeline = pipeConfig;
    recordConfig.totalTimeSeconds = 20.0;  // 20 seconds
    recordConfig.targetFPS = 10.0;          // 10 FPS
    recordConfig.recordUID = makeUID("cal");
    recordConfig.outputDir = "/tmp/record3k";
    
    // Optional: add calibration and study files (if they exist)
    // recordConfig.calibrationFile = "path/to/calibration.xml";
    // recordConfig.studyFile = "path/to/study.json";
    
    // 4. Create RecordManager instance
    lbt::RecordManager recorder(std::move(recordConfig));
    
    std::cout << "Initial state: " << recorder.getStateString() << std::endl;
    
    // 5. Start recording
    if (!recorder.start()) {
        LOG_ERROR << "[DoController] cameraCalibrate: Failed to start recording for camera: " << camUID;
        auto resp = HttpResponse::newHttpJsonResponse(Json::Value());
        resp->setStatusCode(k500InternalServerError);
        cb(resp);
        return;
    }
     // Wait for record starting
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    //create new calibration info 
    std::string now = getCurrentTimeISO8601();
    CalibrationInfo c{recordConfig.recordUID, camUID, desc, "started", now, "file-cal-placeholder"};
    
    auto resp = HttpResponse::newHttpJsonResponse(c.toJson());
    resp->setStatusCode(k202Accepted);
    cb(resp);
}

void DoController::cameraPipelineTest(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> && cb, const std::string &camUID) {
    auto json = req->getJsonObject();
    std::string pipelineUID = (json && json->isMember("pipelineUID")) ? (*json)["pipelineUID"].asString() : "pipe-default";
    RecordInfo r{makeUID("rec"), "cal-latest", true, pipelineUID, "study-test", "queued", 0.0, "2025-11-07T12:00:00Z", "", "file-rec-placeholder"};
    auto resp = HttpResponse::newHttpJsonResponse(r.toJson());
    resp->setStatusCode(k202Accepted);
    cb(resp);
}

void DoController::recordStart(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> && cb, const std::string &camUID) {
    auto json = req->getJsonObject();
    double durationMs = (json && json->isMember("durationInMS")) ? (*json)["durationInMS"].asDouble() : 1000.0;
    std::string calibrationUID = (json && json->isMember("calibrationUID")) ? (*json)["calibrationUID"].asString() : "cal-latest";
    std::string pipelineUID = (json && json->isMember("pipelineUID")) ? (*json)["pipelineUID"].asString() : "pipe-default";
    std::string description = (json && json->isMember("description")) ? (*json)["description"].asString() : "record session";

    RecordInfo r{makeUID("rec"), calibrationUID, false, pipelineUID, "study-unknown", "started", durationMs/1000.0, "2025-11-07T12:03:00Z", "", "file-rec-placeholder"};
    auto resp = HttpResponse::newHttpJsonResponse(r.toJson());
    resp->setStatusCode(k201Created);
    cb(resp);
}

void DoController::cameraRecordStop(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb, const std::string &recordUID) {
    RecordInfo r{recordUID, "cal-latest", false, "pipe-default", "study-unknown", "stopped", 12.3, "2025-11-07T12:03:00Z", "2025-11-07T12:03:12Z", "file-rec-placeholder"};
    auto resp = HttpResponse::newHttpJsonResponse(r.toJson());
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
                    break;
                case lbt::CameraTestResult::ERROR_NOT_FOUND:
                    resultStr = "offline";
                    errorDetail = "Camera not found or not connected";
                    break;
                case lbt::CameraTestResult::ERROR_NOT_READY:
                    resultStr = "offline";
                    errorDetail = "Camera found but not ready";
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
