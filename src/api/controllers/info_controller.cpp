#include "info_controller.h"
#include <drogon/drogon.h>

using namespace drogon;
using namespace api;

namespace {
Json::Value mockCamera(const std::string &uid) {
    CameraInfo c{uid, "AA:BB:CC:DD:EE:FF", std::string("Mock camera ")+uid, "online"};
    return c.toJson();
}
}

void InfoController::listCameras(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb) {
    Json::Value arr(Json::arrayValue);
    arr.append(mockCamera("cam-001"));
    arr.append(mockCamera("cam-002"));
    cb(HttpResponse::newHttpJsonResponse(arr));
}

void InfoController::getCamera(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb, const std::string &camUID) {
    cb(HttpResponse::newHttpJsonResponse(mockCamera(camUID)));
}

void InfoController::listPipelines(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb) {
    Json::Value arr(Json::arrayValue);
    PipelineInfo p1{"pipe-abc", "Test pipeline", "active", "2025-11-07T00:00:00Z", "file-pipe-abc"};
    PipelineInfo p2{"pipe-def", "Alt pipeline", "inactive", "2025-11-06T00:00:00Z", "file-pipe-def"};
    arr.append(p1.toJson());
    arr.append(p2.toJson());
    cb(HttpResponse::newHttpJsonResponse(arr));
}

void InfoController::getPipeline(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb, const std::string &pipelineUID) {
    PipelineInfo p{pipelineUID, "Mock pipeline description", "active", "2025-11-07T00:00:00Z", "file-"+pipelineUID};
    cb(HttpResponse::newHttpJsonResponse(p.toJson()));
}

void InfoController::listCalibrations(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb) {
    Json::Value arr(Json::arrayValue);
    CalibrationInfo c1{"cal-001", "cam-001", "First calibration", "valid", "2025-11-05T10:00:00Z", "file-cal-001"};
    CalibrationInfo c2{"cal-002", "cam-002", "Second calibration", "valid", "2025-11-05T11:00:00Z", "file-cal-002"};
    arr.append(c1.toJson());
    arr.append(c2.toJson());
    cb(HttpResponse::newHttpJsonResponse(arr));
}

void InfoController::getCalibration(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb, const std::string &calibrationUID) {
    CalibrationInfo c{calibrationUID, "cam-001", "Mock calibration", "valid", "2025-11-07T12:00:00Z", "file-"+calibrationUID};
    cb(HttpResponse::newHttpJsonResponse(c.toJson()));
}

void InfoController::listRecords(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb) {
    Json::Value arr(Json::arrayValue);
    RecordInfo r1{"rec-001","cal-001",false,"pipe-abc","study-123","finished",12.3,"2025-11-07T12:00:00Z","2025-11-07T12:00:12Z","file-rec-001"};
    RecordInfo r2{"rec-002","cal-001",true,"pipe-def","study-456","running",30.0,"2025-11-07T12:01:00Z","","file-rec-002"};
    arr.append(r1.toJson());
    arr.append(r2.toJson());
    cb(HttpResponse::newHttpJsonResponse(arr));
}

void InfoController::getRecord(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb, const std::string &recordUID) {
    RecordInfo r{recordUID,"cal-001",false,"pipe-abc","study-123","finished",12.3,"2025-11-07T12:00:00Z","2025-11-07T12:00:12Z","file-"+recordUID};
    cb(HttpResponse::newHttpJsonResponse(r.toJson()));
}
