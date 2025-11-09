#pragma once
#include <drogon/HttpController.h>
#include <liblbt/api/api_models.h>

class DoController : public drogon::HttpController<DoController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(DoController::cameraCalibrate, "/do/camera/{1}/calibrate", drogon::Post);
    ADD_METHOD_TO(DoController::cameraPipelineTest, "/do/camera/{1}/pipelinetest", drogon::Post);
    ADD_METHOD_TO(DoController::recordStart, "/do/record/start", drogon::Post);
    ADD_METHOD_TO(DoController::cameraRecordStop, "/do/camera/record/{1}/stop", drogon::Post);
    METHOD_LIST_END

    void cameraCalibrate(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb, const std::string &camUID);
    void cameraPipelineTest(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb, const std::string &camUID);
    void recordStart(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb);
    void cameraRecordStop(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb, const std::string &recordUID);
};
