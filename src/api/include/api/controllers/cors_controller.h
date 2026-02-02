#pragma once
#include <drogon/HttpController.h>

class CorsController : public drogon::HttpController<CorsController> {
public:
    METHOD_LIST_BEGIN
    // Info routes
    ADD_METHOD_TO(CorsController::optionsSimple, "/info/cameras/list", drogon::Options);
    ADD_METHOD_TO(CorsController::optionsWithParam, "/info/cameras/get/{1}", drogon::Options);
    ADD_METHOD_TO(CorsController::optionsSimple, "/info/pipelines/list", drogon::Options);
    ADD_METHOD_TO(CorsController::optionsWithParam, "/info/pipelines/get/{1}", drogon::Options);
    ADD_METHOD_TO(CorsController::optionsSimple, "/info/calibrations/list", drogon::Options);
    ADD_METHOD_TO(CorsController::optionsWithParam, "/info/calibrations/get/{1}", drogon::Options);
    ADD_METHOD_TO(CorsController::optionsSimple, "/info/records/list", drogon::Options);
    ADD_METHOD_TO(CorsController::optionsWithParam, "/info/records/get/{1}", drogon::Options);
    ADD_METHOD_TO(CorsController::optionsSimple, "/info/files/list", drogon::Options);
    ADD_METHOD_TO(CorsController::optionsWithParam, "/info/files/get/{1}", drogon::Options);
    ADD_METHOD_TO(CorsController::optionsSimple, "/info/studies/list", drogon::Options);
    ADD_METHOD_TO(CorsController::optionsWithParam, "/info/studies/get/{1}", drogon::Options);
    
    // Add routes
    ADD_METHOD_TO(CorsController::optionsSimple, "/add/study_meta_info", drogon::Options);
    ADD_METHOD_TO(CorsController::optionsSimple, "/add/pipeline", drogon::Options);
    
    // Do routes
    ADD_METHOD_TO(CorsController::optionsSimple, "/do/record/start", drogon::Options);
    ADD_METHOD_TO(CorsController::optionsWithParam, "/do/camera/calibrate/{1}", drogon::Options);
    ADD_METHOD_TO(CorsController::optionsWithParam, "/do/camera/pipeline_test/{1}", drogon::Options);
    ADD_METHOD_TO(CorsController::optionsWithParam, "/do/camera/test/{1}", drogon::Options);
    ADD_METHOD_TO(CorsController::optionsWithParam, "/do/camera/record/stop/{1}", drogon::Options);
    ADD_METHOD_TO(CorsController::optionsWithParam, "/do/camera/record/start/{1}", drogon::Options);
    
    // Get routes
    ADD_METHOD_TO(CorsController::optionsWithParam, "/get/file/binary/{1}", drogon::Options);
    METHOD_LIST_END

    void optionsSimple(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb);
    void optionsWithParam(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb, const std::string &param);
};
