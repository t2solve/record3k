#pragma once
#include <drogon/HttpController.h>
#include <liblbt/api/api_models.h>

class GetController : public drogon::HttpController<GetController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(GetController::getFileBinary, "/get/file/binary/{1}", drogon::Get);
    METHOD_LIST_END

    void getFileBinary(const drogon::HttpRequestPtr&, std::function<void (const drogon::HttpResponsePtr &)> && cb, const std::string &fileUID);
};
