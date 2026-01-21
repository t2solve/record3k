#include <api/controllers/do_controller.h>
#include <drogon/drogon.h>
#include <api/runtime/info_data_manager.h>
#include <api/runtime/app_config.h>
#include <liblbt/camera_manager.h>
#include <ctime>

using namespace drogon;
using namespace api;

static std::string makeUID(const std::string &prefix) {
    return prefix + "-" + std::to_string(std::time(nullptr));
}

void DoController::cameraCalibrate(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> && cb, const std::string &camUID) {
    auto json = req->getJsonObject();
    std::string desc = (json && json->isMember("description")) ? (*json)["description"].asString() : "Calibration run";
    CalibrationInfo c{makeUID("cal"), camUID, desc, "started", "2025-11-07T12:00:00Z", "file-cal-placeholder"};
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
            LOG_DEBUG << "[DoController::cameraTest Thread] Getting camera info path...";
            auto cameraPath = api::AppConfig::instance().getInfoPath("cameras");
            LOG_DEBUG << "[DoController::cameraTest Thread] Camera path: " << cameraPath;
            
            LOG_DEBUG << "[DoController::cameraTest Thread] Creating CameraManager...";
            lbt::CameraManager mgr(cameraPath);
            LOG_DEBUG << "[DoController::cameraTest Thread] Loading cameras...";
            mgr.load();
            LOG_DEBUG << "[DoController::cameraTest Thread] Cameras loaded";
            
            // Get VimbaX configuration
            LOG_DEBUG << "[DoController::cameraTest Thread] Getting VimbaX CTI path...";
            std::string vimbaxCtiPath = api::AppConfig::instance().getVimbaxTransportLayerPath();
            LOG_DEBUG << "[DoController::cameraTest Thread] VimbaX CTI path: '" << vimbaxCtiPath << "'";
            
            // Get camera info by UID
            LOG_DEBUG << "[DoController::cameraTest Thread] Looking up camera: " << camUID;
            auto camInfo = mgr.get(camUID);
            if (!camInfo) {
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
            LOG_INFO << "[DoController::cameraTest Thread] Camera found, type: " << camInfo->cameraType;
            
            // Test the camera (this may block but it's in a background thread now)
            LOG_INFO << "[DoController::cameraTest Thread] Calling testCamera()...";
            lbt::CameraTestResult testResult = mgr.testCamera(*camInfo, vimbaxCtiPath);
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
