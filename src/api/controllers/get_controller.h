#pragma once
#include <drogon/HttpController.h>
#include <liblbt/api/api_models.h>

// Endpoints under /get/* for file listings and file download
class GetController : public drogon::HttpController<GetController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(GetController::listFiles, "/get/file/list", drogon::Get);
    ADD_METHOD_TO(GetController::getFileInfo, "/get/file/info/{1}", drogon::Get);
    ADD_METHOD_TO(GetController::getFileBinary, "/get/file/binary/{1}", drogon::Get);
    METHOD_LIST_END

    void listFiles(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb);
    void getFileInfo(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb, const std::string &fileUID);
    void getFileBinary(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb, const std::string &fileUID);
};