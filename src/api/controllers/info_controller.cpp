#include <api/controllers/info_controller.h>
#include <drogon/drogon.h>
#ifdef DEBUG_FLAG_MOCK
#include <string>
#else
#include <api/runtime/info_data_manager.h>
#endif

using namespace drogon;
using namespace api;

#ifdef DEBUG_FLAG_MOCK
namespace {
Json::Value mockCamera(const std::string &uid) {
    CameraInfo c{uid, "AA:BB:CC:DD:EE:FF", std::string("Mock camera ")+uid, "online"};
    return c.toJson();
}
}
#endif

void InfoController::listCameras(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb) {
#ifdef DEBUG_FLAG_MOCK
    Json::Value arr(Json::arrayValue);
    arr.append(mockCamera("cam-001"));
    arr.append(mockCamera("cam-002"));
    cb(HttpResponse::newHttpJsonResponse(arr));
#else
    cb(HttpResponse::newHttpJsonResponse(infoDataManager().list("cameras")));
#endif
}

void InfoController::getCamera(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb, const std::string &camUID) {
#ifdef DEBUG_FLAG_MOCK
    cb(HttpResponse::newHttpJsonResponse(mockCamera(camUID)));
#else
    auto v = infoDataManager().get("cameras", camUID);
    cb(HttpResponse::newHttpJsonResponse(v.value_or(Json::Value(Json::objectValue))));
#endif
}

void InfoController::listPipelines(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb) {
#ifdef DEBUG_FLAG_MOCK
    Json::Value arr(Json::arrayValue);
    PipelineInfo p1{"pipe-abc", "Test pipeline", "active", "2025-11-07T00:00:00Z", "file-pipe-abc"};
    PipelineInfo p2{"pipe-def", "Alt pipeline", "inactive", "2025-11-06T00:00:00Z", "file-pipe-def"};
    arr.append(p1.toJson());
    arr.append(p2.toJson());
    cb(HttpResponse::newHttpJsonResponse(arr));
#else
    cb(HttpResponse::newHttpJsonResponse(infoDataManager().list("pipelines")));
#endif
}

void InfoController::getPipeline(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb, const std::string &pipelineUID) {
#ifdef DEBUG_FLAG_MOCK
    PipelineInfo p{pipelineUID, "Mock pipeline description", "active", "2025-11-07T00:00:00Z", "file-"+pipelineUID};
    cb(HttpResponse::newHttpJsonResponse(p.toJson()));
#else
    auto v = infoDataManager().get("pipelines", pipelineUID);
    cb(HttpResponse::newHttpJsonResponse(v.value_or(Json::Value(Json::objectValue))));
#endif
}

void InfoController::listCalibrations(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb) {
#ifdef DEBUG_FLAG_MOCK
    Json::Value arr(Json::arrayValue);
    CalibrationInfo c1{"cal-001", "cam-001", "First calibration", "valid", "2025-11-05T10:00:00Z", "file-cal-001"};
    CalibrationInfo c2{"cal-002", "cam-002", "Second calibration", "valid", "2025-11-05T11:00:00Z", "file-cal-002"};
    arr.append(c1.toJson());
    arr.append(c2.toJson());
    cb(HttpResponse::newHttpJsonResponse(arr));
#else
    cb(HttpResponse::newHttpJsonResponse(infoDataManager().list("calibrations")));
#endif
}

void InfoController::getCalibration(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb, const std::string &calibrationUID) {
#ifdef DEBUG_FLAG_MOCK
    CalibrationInfo c{calibrationUID, "cam-001", "Mock calibration", "valid", "2025-11-07T12:00:00Z", "file-"+calibrationUID};
    cb(HttpResponse::newHttpJsonResponse(c.toJson()));
#else
    auto v = infoDataManager().get("calibrations", calibrationUID);
    cb(HttpResponse::newHttpJsonResponse(v.value_or(Json::Value(Json::objectValue))));
#endif
}

void InfoController::listRecords(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb) {
#ifdef DEBUG_FLAG_MOCK
    Json::Value arr(Json::arrayValue);
    RecordInfo r1{"rec-001","cal-001",false,"pipe-abc","study-123","finished",12.3,"2025-11-07T12:00:00Z","2025-11-07T12:00:12Z","file-rec-001"};
    RecordInfo r2{"rec-002","cal-001",true,"pipe-def","study-456","running",30.0,"2025-11-07T12:01:00Z","","file-rec-002"};
    arr.append(r1.toJson());
    arr.append(r2.toJson());
    cb(HttpResponse::newHttpJsonResponse(arr));
#else
    cb(HttpResponse::newHttpJsonResponse(infoDataManager().list("records")));
#endif
}

void InfoController::getRecord(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb, const std::string &recordUID) {
#ifdef DEBUG_FLAG_MOCK
    RecordInfo r{recordUID,"cal-001",false,"pipe-abc","study-123","finished",12.3,"2025-11-07T12:00:00Z","2025-11-07T12:00:12Z","file-"+recordUID};
    cb(HttpResponse::newHttpJsonResponse(r.toJson()));
#else
    auto v = infoDataManager().get("records", recordUID);
    cb(HttpResponse::newHttpJsonResponse(v.value_or(Json::Value(Json::objectValue))));
#endif
}

void InfoController::listFiles(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb) {
#ifdef DEBUG_FLAG_MOCK
    Json::Value arr(Json::arrayValue);
    FileInfo f1{"file-001","2025-11-07T10:00:00Z","available","mock file 1"};
    FileInfo f2{"file-002","2025-11-07T11:00:00Z","available","mock file 2"};
    arr.append(f1.toJson());
    arr.append(f2.toJson());
    cb(HttpResponse::newHttpJsonResponse(arr));
#else
    cb(HttpResponse::newHttpJsonResponse(infoDataManager().list("files")));
#endif
}

void InfoController::getFileInfo(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb, const std::string &fileUID) {
#ifdef DEBUG_FLAG_MOCK
    FileInfo f{fileUID,"2025-11-07T12:00:00Z","available","sample"};
    cb(HttpResponse::newHttpJsonResponse(f.toJson()));
#else
    auto v = infoDataManager().get("files", fileUID);
    cb(HttpResponse::newHttpJsonResponse(v.value_or(Json::Value(Json::objectValue))));
#endif
}

void InfoController::listStudies(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb) {
#ifdef DEBUG_FLAG_MOCK
    Json::Value arr(Json::arrayValue);
    StudyMetaInfo s1{"study-001", std::string("Baseline colony"), "Apis mellifera", 120.5};
    StudyMetaInfo s2{"study-002", std::string("Control group"), "Bombus terrestris", 98.2};
    arr.append(s1.toJson());
    arr.append(s2.toJson());
    cb(HttpResponse::newHttpJsonResponse(arr));
#else
    cb(HttpResponse::newHttpJsonResponse(infoDataManager().list("studies")));
#endif
}

void InfoController::getStudy(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb, const std::string &studyInfoUID) {
#ifdef DEBUG_FLAG_MOCK
    StudyMetaInfo s{studyInfoUID, std::string("Mock study description"), "Apis mellifera", 123.4};
    cb(HttpResponse::newHttpJsonResponse(s.toJson()));
#else
    auto v = infoDataManager().get("studies", studyInfoUID);
    cb(HttpResponse::newHttpJsonResponse(v.value_or(Json::Value(Json::objectValue))));
#endif
}
