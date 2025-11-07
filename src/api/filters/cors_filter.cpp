#include "cors_filter.h"

using namespace drogon;

void CorsFilter::doFilter(const HttpRequestPtr &req,
                          FilterCallback &&fcb,
                          FilterChainCallback &&fccb)
{
    // Handle OPTIONS preflight
    if (req->method() == drogon::Options)
    {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(k200OK);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
        resp->addHeader("Access-Control-Max-Age", "86400");
        fcb(resp);
        return;
    }
    
    // Continue to next handler
    fccb();
}
