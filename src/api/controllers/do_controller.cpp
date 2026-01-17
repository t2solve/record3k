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
void DoController::cameraTest(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb, const std::string &camUID) {
    try {
        auto cameraPath = api::AppConfig::instance().getInfoPath("cameras");
        lbt::CameraManager mgr(cameraPath);
        mgr.load();
        
        // Get VimbaX configuration
        std::string vimbaxCtiPath = api::AppConfig::instance().getVimbaxTransportLayerPath();
        LOG_DEBUG << "[DoController] Retrieved VimbaX CTI path: '" << vimbaxCtiPath << "'";
        
        // Get camera info by UID
        auto camInfo = mgr.get(camUID);
        if (!camInfo) {
            Json::Value err; 
            err["error"] = "Camera not found: " + camUID;
            auto resp = HttpResponse::newHttpJsonResponse(err);
            resp->setStatusCode(k404NotFound);
            cb(resp);
            return;
        }
        
        // Test the camera
        lbt::CameraTestResult testResult = mgr.testCamera(*camInfo, vimbaxCtiPath);
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
        
        // Prepare response
        Json::Value result;
        result["camUID"] = camUID;
        result["testResult"] = resultStr;
        result["success"] = success;
        if (!errorDetail.empty()) {
            result["errorDetail"] = errorDetail;
        }
        
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(k200OK);
        cb(resp);
    } catch (const std::exception& e) {
        Json::Value err; 
        err["error"] = std::string("Camera test failed: ") + e.what();
        auto resp = HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(k500InternalServerError);
        cb(resp);
    }
}
