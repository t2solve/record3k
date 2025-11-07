#include "get_controller.h"
#include <drogon/drogon.h>

using namespace drogon;
using namespace api;

void GetController::listFiles(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb) {
    Json::Value arr(Json::arrayValue);
    FileInfo f1{"file-001","2025-11-07T10:00:00Z","available","mock file 1"};
    FileInfo f2{"file-002","2025-11-07T11:00:00Z","available","mock file 2"};
    arr.append(f1.toJson());
    arr.append(f2.toJson());
    cb(HttpResponse::newHttpJsonResponse(arr));
}

void GetController::getFileInfo(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb, const std::string &fileUID) {
    FileInfo f{fileUID,"2025-11-07T12:00:00Z","available","sample"};
    cb(HttpResponse::newHttpJsonResponse(f.toJson()));
}

void GetController::getFileBinary(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb, const std::string &fileUID) {
    // Mock: return a tiny gzip header-like payload; replace with actual file retrieval.
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    resp->setContentTypeCode(CT_APPLICATION_OCTET_STREAM);
    resp->addHeader("Content-Disposition", "attachment; filename=\"" + fileUID + ".tar.gz\"");
    std::string fakeGz = "\x1F\x8B\x08\x00\x00\x00\x00\x00\x00\x03"; // not a real archive
    resp->setBody(fakeGz);
    cb(resp);
}
