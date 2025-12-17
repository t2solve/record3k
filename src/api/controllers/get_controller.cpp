#include <api/controllers/get_controller.h>
#include <drogon/drogon.h>

using namespace drogon;
using namespace api;

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
