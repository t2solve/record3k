
// Only build/run API server when API_ENABLED is defined (CONFIG+=api)
#ifdef API_ENABLED
#include <drogon/drogon.h>
#endif

#include <QCoreApplication>
#include <QTimer>
#include <QDebug>
#include <QObject>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/opencv.hpp>

#include <thread>
#include <optional>
#include <cstdlib>

#include <liblbt/process_step.h>
#include <liblbt/process_step_factory.h>
#include <liblbt/processing_pipeline.h>
#include <liblbt/frame_memory_object.h>
#include <liblbt/pipeline_profiler.h>
#include <liblbt/pipeline_config_loader.h>
#include <liblbt/disk_image_frame_source.h>
#include <liblbt/vimbax_frame_source.h>


int main(int argc, char* argv[])
{
#ifndef API_ENABLED
    std::cout << "[api] API_ENABLED not defined; rebuild with CONFIG+=api to start server." << std::endl;
    return 0;
#else
    // Try to load external config (includes swagger settings). If missing, fallback.
    try {
        drogon::app().loadConfigFile("./drogon.config.json");
        if (drogon::app().getListeners().empty()) {
            drogon::app().addListener("127.0.0.1", 8080);
            std::cout << "[api] Config loaded but no listeners defined; using 127.0.0.1:8080" << std::endl;
        } else {
            std::cout << "[api] Loaded drogon.config.json" << std::endl;
        }
    } catch (const std::exception &e) {
        std::cout << "[api] No usable drogon.config.json (" << e.what() << "), using fallback 127.0.0.1:8080" << std::endl;
        drogon::app().addListener("127.0.0.1", 8080);
    }
    if (drogon::app().getThreadNum() == 0) {
        drogon::app().setThreadNum(std::thread::hardware_concurrency());
    }
    
    // Enable CORS for browser access from different origins
    drogon::app().registerPreHandlingAdvice([](const drogon::HttpRequestPtr &req, 
                                               drogon::AdviceCallback &&acb, 
                                               drogon::AdviceChainCallback &&accb) {
        // Handle OPTIONS preflight requests
        if (req->method() == drogon::HttpMethod::Options) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k200OK);
            resp->addHeader("Access-Control-Allow-Origin", "*");
            resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
            resp->addHeader("Access-Control-Max-Age", "86400");
            acb(resp);
            return;
        }
        // Continue with normal request processing
        accb();
    });
    
    // Add CORS headers to all responses
    drogon::app().registerPostHandlingAdvice([](const drogon::HttpRequestPtr &req, 
                                                 const drogon::HttpResponsePtr &resp) {
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    });
    
    std::cout << "[api] Starting Drogon server; swagger (if enabled) at /swagger and spec at /openapi.json" << std::endl;
    std::cout << "[api] CORS enabled for browser access (with preflight support)" << std::endl;
    drogon::app().run();
    return 0;
#endif
}


