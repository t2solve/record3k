#pragma once
#include <drogon/HttpController.h>
#include <liblbt/api/api_models.h>

class InfoController : public drogon::HttpController<InfoController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(InfoController::listCameras, "/info/cameras/list", drogon::Get);
    ADD_METHOD_TO(InfoController::getCamera, "/info/cameras/get/{1}", drogon::Get);
    ADD_METHOD_TO(InfoController::listPipelines, "/info/pipelines/list", drogon::Get);
    ADD_METHOD_TO(InfoController::getPipeline, "/info/pipelines/get/{1}", drogon::Get);
    ADD_METHOD_TO(InfoController::listCalibrations, "/info/calibrations/list", drogon::Get);
    ADD_METHOD_TO(InfoController::getCalibration, "/info/calibrations/get/{1}", drogon::Get);
    ADD_METHOD_TO(InfoController::listRecords, "/info/records/list", drogon::Get);
    ADD_METHOD_TO(InfoController::getRecord, "/info/records/get/{1}", drogon::Get);
    ADD_METHOD_TO(InfoController::listFiles, "/info/files/list", drogon::Get);
    ADD_METHOD_TO(InfoController::getFileInfo, "/info/files/get/{1}", drogon::Get);
    ADD_METHOD_TO(InfoController::listStudies, "/info/studies/list", drogon::Get);
    ADD_METHOD_TO(InfoController::getStudy, "/info/studies/get/{1}", drogon::Get);
    METHOD_LIST_END

    void listCameras(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb);
    void getCamera(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb, const std::string &camUID);
    void listPipelines(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb);
    void getPipeline(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb, const std::string &pipelineUID);
    void listCalibrations(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb);
    void getCalibration(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb, const std::string &calibrationUID);
    void listRecords(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb);
    void getRecord(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb, const std::string &recordUID);
    void listFiles(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb);
    void getFileInfo(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb, const std::string &fileUID);
    void listStudies(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb);
    void getStudy(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb, const std::string &studyInfoUID);
};
