
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
#include <csignal>
#include <atomic>

#include <liblbt/process_step.h>
#include <liblbt/process_step_factory.h>
#include <liblbt/processing_pipeline.h>
#include <liblbt/frame_memory_object.h>
#include <liblbt/pipeline_profiler.h>
#include <liblbt/pipeline_config_loader.h>
#include <liblbt/disk_image_frame_source.h>
#include <liblbt/vimbax_frame_source.h>
#include <liblbt/camera_manager_vimbax.h>

// Signal handler for graceful shutdown
std::atomic<bool> shutdownRequested{false};

void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\n[main] Shutdown signal received (signal: " << signal << "), shutting down gracefully..." << std::endl;
        shutdownRequested = true;
        
#ifdef API_ENABLED
        // Stop the Drogon server
        drogon::app().quit();
        std::cout << "[main] Drogon server shutdown requested" << std::endl;
#endif

#ifdef VIMBAX_ENABLED
        // Shutdown VimbaX system
        auto& vimbaxMgr = lbt::CameraManagerVimbax::getInstance();
        vimbaxMgr.shutdown();
        std::cout << "[main] VimbaX system shutdown complete" << std::endl;
#endif
    }
}


int main(int argc, char* argv[])
{
#ifndef API_ENABLED
    std::cout << "[api] API_ENABLED not defined; rebuild with CONFIG+=api to start server." << std::endl;
    return 0;
#else
    // Set log level to DEBUG to see all detailed logs
    trantor::Logger::setLogLevel(trantor::Logger::kDebug);
    LOG_INFO << "Log level set to DEBUG for detailed diagnostics";
    
    // Register signal handlers for graceful shutdown
    std::signal(SIGINT, signalHandler);   // Ctrl+C
    std::signal(SIGTERM, signalHandler);  // Termination signal
    LOG_INFO << "Signal handlers registered (SIGINT, SIGTERM)";
    
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
    
    // Initialize VimbaX system in background thread to avoid blocking server startup
    #ifdef VIMBAX_ENABLED
    LOG_INFO << "Starting VimbaX camera system initialization in background...";
    std::string vimbaxPath = api::AppConfig::instance().getVimbaxTransportLayerPath();
    
    // Launch initialization in background thread
    std::thread vimbaxInitThread([vimbaxPath]() {
        LOG_INFO << "[VimbaX Thread] Initializing VimbaX camera system...";
        auto& vimbaxMgr = lbt::CameraManagerVimbax::getInstance();
        
        if (vimbaxMgr.startup(vimbaxPath)) {
            LOG_INFO << "[VimbaX Thread] VimbaX startup successful";
            LOG_INFO << "[VimbaX Thread] Waiting for camera discovery (this may take time for GigE cameras)...";
            
            // Give GigE cameras time to be discovered on the network
            // Note: This can block for a while on first GetCameras() call
            auto startTime = std::chrono::steady_clock::now();
            std::this_thread::sleep_for(std::chrono::seconds(2));
            
            LOG_INFO << "[VimbaX Thread] Querying cameras...";
            LOG_INFO << "[VimbaX Thread] Calling updateCameraList() - this will acquire mutex and may block on GetCameras()...";
            vimbaxMgr.updateCameraList();
            LOG_INFO << "[VimbaX Thread] updateCameraList() completed, mutex released";
            
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - startTime).count();
            
            auto status = vimbaxMgr.getStatus();
            auto cameras = vimbaxMgr.getCameras();
            LOG_INFO << "[VimbaX Thread] VimbaX ready after " << elapsed << "s: v" 
                     << status.versionMajor << "." << status.versionMinor 
                     << "." << status.versionPatch << ", found " << cameras.size() << " camera(s)";
            for (const auto& cam : cameras) {
                LOG_INFO << "[VimbaX Thread]   - " << cam.name << " (S/N: " << cam.serial << ", MAC: " << cam.macAddress << ")";
            }
        } else {
            LOG_WARN << "[VimbaX Thread] VimbaX startup failed - camera functionality may be limited";
            LOG_WARN << "[VimbaX Thread] Error: " << vimbaxMgr.getStatus().errorMessage;
        }
    });
    vimbaxInitThread.detach(); // Let it run independently
    LOG_INFO << "VimbaX initialization started in background, continuing with server startup...";
    #else
    LOG_INFO << "VimbaX not enabled in this build";
    #endif
    
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
    LOG_INFO << "Press Ctrl+C to stop the server";
    drogon::app().run();
    
    // Cleanup after server shutdown
    LOG_INFO << "Drogon server stopped";
    
#ifdef VIMBAX_ENABLED
    // Ensure VimbaX is shutdown if not already done by signal handler
    if (!shutdownRequested) {
        LOG_INFO << "Shutting down VimbaX system...";
        auto& vimbaxMgr = lbt::CameraManagerVimbax::getInstance();
        vimbaxMgr.shutdown();
        LOG_INFO << "VimbaX system shutdown complete";
    }
#endif
    
    LOG_INFO << "Application exiting cleanly";
    return 0;
#endif
}


