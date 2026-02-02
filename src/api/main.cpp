
// Only build/run API server when API_ENABLED is defined (CONFIG+=api)
#ifdef API_ENABLED
#include <drogon/drogon.h>
#include <trantor/utils/Logger.h>
#include <api/runtime/app_config.h>
#include <api/runtime/info_data_manager.h>
#include <liblbt/file_manager.h>
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
    // Set log level based on build configuration (from LOG_LEVEL define)
#if defined(LOG_LEVEL_TRACE)
    trantor::Logger::setLogLevel(trantor::Logger::kTrace);
    LOG_INFO << "Log level: TRACE";
#elif defined(LOG_LEVEL_DEBUG)
    trantor::Logger::setLogLevel(trantor::Logger::kDebug);
    LOG_INFO << "Log level: DEBUG";
#elif defined(LOG_LEVEL_INFO)
    trantor::Logger::setLogLevel(trantor::Logger::kInfo);
    LOG_INFO << "Log level: INFO";
#elif defined(LOG_LEVEL_WARN)
    trantor::Logger::setLogLevel(trantor::Logger::kWarn);
    LOG_WARN << "Log level: WARN";
#elif defined(LOG_LEVEL_ERROR)
    trantor::Logger::setLogLevel(trantor::Logger::kError);
    LOG_ERROR << "Log level: ERROR";
#elif defined(LOG_LEVEL_FATAL)
    trantor::Logger::setLogLevel(trantor::Logger::kFatal);
#else
    // Fallback to INFO if not specified
    trantor::Logger::setLogLevel(trantor::Logger::kInfo);
    LOG_INFO << "Log level: INFO (default)";
#endif
    
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
        
        // Load data_root_path: Priority: ENV > config > default
        std::string dataPath;
        if (const char* envPath = std::getenv("DATA_ROOT_PATH")) {
            dataPath = envPath;
            LOG_INFO << "Data root path from environment variable: " << dataPath;
        } else if (!customConfig.empty() && customConfig.isMember("data_root_path")) {
            dataPath = customConfig["data_root_path"].asString();
            LOG_INFO << "Data root path from config file: " << dataPath;
        } else {
            dataPath = "data";
            LOG_INFO << "Using default data root path: " << dataPath;
        }
        api::AppConfig::instance().setDataRootPath(dataPath);
        
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
    
    // Create required directory structure
    std::error_code ec;
    std::string tmpDir = "/tmp/record3k";
    std::filesystem::create_directories(tmpDir, ec);
    //std::filesystem::create_directories(api::AppConfig::instance().getDataRootPath() / "files" / "bin", ec);
    
    // Initialize FileManager with centralized path configuration
    std::string tempOutputPath = tmpDir;  // Could be made configurable
    lbt::FileManager::instance().initialize(
        api::AppConfig::instance().getDataRootPath(),
        tempOutputPath
    );
    LOG_INFO << "FileManager initialized";
    
    // Initialize InfoDataManager (this also starts the stores)
    infoDataManager().start();
    LOG_INFO << "InfoDataManager initialized";
    
    // Setup FileManager callback to use InfoDataManager
    lbt::FileManager::instance().setFileInfoCreator(
        [](const std::string& fileUID, const std::filesystem::path& archivePath, 
           const std::string& recordUID, const Json::Value& metadata) {
            return infoDataManager().createFileInfo(fileUID, archivePath, recordUID, metadata);
        }
    );
    LOG_INFO << "FileManager callback configured";
    
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
    
    // Load API token from environment variable
    const char* envToken = std::getenv("API_TOKEN");
    std::string API_TOKEN = envToken ? envToken : "foobartest42";
    
    if (envToken) {
        LOG_INFO << "API_TOKEN loaded from environment (length: " << API_TOKEN.length() << " chars)";
    } else {
        LOG_WARN << "API_TOKEN not set in environment - using default development token";
        LOG_WARN << "Set API_TOKEN environment variable for production!";
        LOG_WARN << "Using default token is insecure and should only be done in development!";
    }
    
    // Enable CORS for browser access from different origins
    drogon::app().registerPreHandlingAdvice([API_TOKEN](const drogon::HttpRequestPtr &req, 
                                               drogon::AdviceCallback &&acb, 
                                               drogon::AdviceChainCallback &&accb) {
        // Handle OPTIONS preflight requests (no auth required)
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
        
        // Check API token for all other requests
        std::string authHeader = req->getHeader("Authorization");
        std::string expectedAuth = "Bearer " + API_TOKEN;
        
        if (authHeader != expectedAuth) {
            LOG_WARN << "Unauthorized request to " << req->path() << " from " << req->peerAddr().toIp();
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k401Unauthorized);
            resp->addHeader("Access-Control-Allow-Origin", "*");
            resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
            
            Json::Value error;
            error["error"] = "Unauthorized - Invalid or missing API token";
            error["message"] = "Please provide a valid Bearer token in the Authorization header";
            
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            resp->setBody(Json::writeString(builder, error));
            
            acb(resp);
            return;
        }
        
        // Token valid - continue with normal request processing
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


