#include <api/controllers/cors_controller.h>

using namespace drogon;

void CorsController::optionsSimple(const HttpRequestPtr&, 
                                   std::function<void (const HttpResponsePtr &)> && cb) {
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    resp->addHeader("Access-Control-Max-Age", "86400");
    cb(resp);
}

void CorsController::optionsWithParam(const HttpRequestPtr&, 
                                      std::function<void (const HttpResponsePtr &)> && cb,
                                      const std::string &param) {
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    resp->addHeader("Access-Control-Max-Age", "86400");
    cb(resp);
}
