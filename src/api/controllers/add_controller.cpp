#include "add_controller.h"
#include <drogon/drogon.h>

using namespace drogon;
using namespace api;

void AddController::addPipeline(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> && cb) {
    // Expected: multipart or JSON with description + binary pipeline file (tar.gz)
    // Mock implementation: just generate a UID
    std::string newUID = "pipe-" + std::to_string(std::time(nullptr));
    PipelineInfo p{newUID, "mock description", "active", "2025-11-07T12:00:00Z", "file-"+newUID};
    auto resp = HttpResponse::newHttpJsonResponse(p.toJson());
    resp->setStatusCode(k201Created);
    cb(resp);
}

void AddController::addStudyMetaInfo(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> && cb) {
    auto json = req->getJsonObject();
    StudyMetaInfo info;
    if (json) {
        if (json->isMember("studyInfoUID")) info.studyInfoUID = (*json)["studyInfoUID"].asString(); else info.studyInfoUID = "study-" + std::to_string(std::time(nullptr));
        if (json->isMember("description")) info.description = (*json)["description"].asString();
        if (json->isMember("individualScientificName")) info.individualScientificName = (*json)["individualScientificName"].asString();
        if (json->isMember("weightInMg")) info.weightInMg = (*json)["weightInMg"].asDouble();
    } else {
        // fallback mock
        info.studyInfoUID = "study-" + std::to_string(std::time(nullptr));
        info.individualScientificName = "Unknown species";
        info.weightInMg = 0.0;
    }
    auto resp = HttpResponse::newHttpJsonResponse(info.toJson());
    resp->setStatusCode(k201Created);
    cb(resp);
}
