
// Only build/run API server when API_ENABLED is defined (CONFIG+=api)
#ifdef API_ENABLED
#include <drogon/drogon.h>
#include <trantor/utils/Logger.h>
#include <api/runtime/app_config.h>
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
            LOG_WARN << "Config loaded but no listeners defined; using 127.0.0.1:8080";
        } else {
            LOG_INFO << "Loaded drogon.config.json";
        }
    } catch (const std::exception &e) {
        LOG_WARN << "No usable drogon.config.json (" << e.what() << "), using fallback 127.0.0.1:8080";
        drogon::app().addListener("127.0.0.1", 8080);
    }
    if (drogon::app().getThreadNum() == 0) {
        drogon::app().setThreadNum(std::thread::hardware_concurrency());
    }
    
    // Load custom data_root_path and vimbax config
    try {
        auto customConfig = drogon::app().getCustomConfig();
        LOG_INFO << "Custom config loaded, has " << customConfig.size() << " root elements";
        
        // Debug: print all root keys
        for (const auto& key : customConfig.getMemberNames()) {
            LOG_DEBUG << "Config key found: " << key;
        }
        
        // data_root_path is nested under "app" section
        if (!customConfig.empty() && customConfig.isMember("data_root_path")) {
            std::string dataPath = customConfig["data_root_path"].asString();
            api::AppConfig::instance().setDataRootPath(dataPath);
            LOG_INFO << "Data root path set to: " << dataPath;
        } else {
            LOG_INFO << "Using default data root path: data";
        }
        
        // Load VimbaX configuration
        if (!customConfig.empty() && customConfig.isMember("vimbax_transport_layer_file")) {
            std::string tlFile = customConfig["vimbax_transport_layer_file"].asString();
            LOG_INFO << "Read vimbax_transport_layer_file from config: " << tlFile;
            
            // If it's a relative path or just a filename, try to resolve it
            std::filesystem::path tlPath(tlFile);
            if (!tlPath.is_absolute()) {
                // Try relative to current directory first
                if (std::filesystem::exists(tlFile)) {
                    tlFile = std::filesystem::absolute(tlFile).string();
                    LOG_INFO << "Resolved to absolute path: " << tlFile;
                }
                // Try relative to common vimbax/cti directory
                else {
                    std::filesystem::path vimbaxPath = "vimbax/cti/" + std::string(tlFile);
                    if (std::filesystem::exists(vimbaxPath)) {
                        tlFile = std::filesystem::absolute(vimbaxPath).string();
                        LOG_INFO << "Resolved to vimbax/cti path: " << tlFile;
                    } else {
                        LOG_WARN << "Could not find .cti file, will use as-is: " << tlFile;
                    }
                }
            }
            
            api::AppConfig::instance().setVimbaxTransportLayerPath(tlFile);
            LOG_INFO << "VimbaX transport layer file set to: " << tlFile;
        } else {
            LOG_INFO << "No vimbax_transport_layer_file in drogon.config.json";
        }
    } catch (const std::exception &e) {
        LOG_WARN << "Could not load custom config, using default data path: " << e.what();
    }
    
    // Validate data path structure and required files
    std::vector<std::string> validationErrors;
    if (!api::AppConfig::instance().validateDataPath(validationErrors)) {
        LOG_ERROR << "Data path validation failed:";
        for (const auto& error : validationErrors) {
            LOG_ERROR << "  - " << error;
        }
        LOG_FATAL << "Server cannot start with invalid data path structure";
        return 1;
    }
    LOG_INFO << "Data path validation successful";
    
    // Enable CORS for browser access from different origins
    drogon::app().registerPreHandlingAdvice([](const drogon::HttpRequestPtr &req, 
                                               drogon::AdviceCallback &&acb, 
                                               drogon::AdviceChainCallback &&accb) {
        // Handle OPTIONS preflight requests
        if (req->method() == drogon::HttpMethod::Options) {
            LOG_DEBUG << "Handling OPTIONS preflight request for: " << req->path();
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
    
    LOG_INFO << "Starting Drogon server; swagger (if enabled) at /swagger and spec at /openapi.json";
    LOG_INFO << "CORS enabled for browser access (with preflight support)";
    drogon::app().run();
    return 0;
#endif
}


