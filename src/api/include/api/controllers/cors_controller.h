#pragma once
#include <drogon/HttpController.h>

class CorsController : public drogon::HttpController<CorsController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(CorsController::optionsSimple, "/add/studyMetaInfo", drogon::Options);
    ADD_METHOD_TO(CorsController::optionsSimple, "/add/pipeline", drogon::Options);
    ADD_METHOD_TO(CorsController::optionsSimple, "/do/record/start", drogon::Options);
    ADD_METHOD_TO(CorsController::optionsWithParam, "/do/camera/{1}/calibrate", drogon::Options);
    ADD_METHOD_TO(CorsController::optionsWithParam, "/do/camera/{1}/pipeline/test", drogon::Options);
    ADD_METHOD_TO(CorsController::optionsWithParam, "/do/camera/record/{1}/stop", drogon::Options);
    METHOD_LIST_END

    void optionsSimple(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb);
    void optionsWithParam(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb, const std::string &param);
};
