#pragma once
#include <drogon/HttpController.h>

class DocsController : public drogon::HttpController<DocsController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(DocsController::openapi, "/openapi.json", drogon::Get);
    ADD_METHOD_TO(DocsController::swagger, "/swagger", drogon::Get);
    METHOD_LIST_END

    void openapi(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb);
    void swagger(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb);
};
