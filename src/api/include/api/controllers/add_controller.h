#pragma once
#include <drogon/HttpController.h>
#include <liblbt/api/api_models.h>

class AddController : public drogon::HttpController<AddController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AddController::addPipeline, "/add/pipeline", drogon::Post);
    ADD_METHOD_TO(AddController::addStudyMetaInfo, "/add/studyMetaInfo", drogon::Post);
    METHOD_LIST_END

    void addPipeline(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb);
    void addStudyMetaInfo(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb);
};
