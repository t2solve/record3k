#include <api/controllers/do_controller.h>
#include <drogon/drogon.h>
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

void DoController::recordStart(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> && cb) {
    auto json = req->getJsonObject();
    std::string camUID = (json && json->isMember("camUID")) ? (*json)["camUID"].asString() : "cam-unknown";
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
