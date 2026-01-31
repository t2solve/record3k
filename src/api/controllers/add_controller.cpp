#include <api/controllers/add_controller.h>
#include <api/runtime/info_data_manager.h>
#include <api/logging.h>
#include <drogon/drogon.h>
#include <drogon/MultiPart.h>
#include <liblbt/pipeline_config_loader.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <sstream>
#include <iomanip>

using namespace drogon;
using namespace api;

static std::string getCurrentTimeISO8601() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t_now), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

void AddController::addPipeline(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> && cb) {
    LOG_INFO << "[AddController::addPipeline] Received pipeline upload request";
    
    // Parse multipart form data
    MultiPartParser fileUpload;
    if (fileUpload.parse(req) != 0 || fileUpload.getFiles().empty()) {
        LOG_ERROR << "[AddController::addPipeline] No files uploaded or parse error";
        Json::Value err;
        err["error"] = "No pipeline file uploaded";
        auto resp = HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(k400BadRequest);
        cb(resp);
        return;
    }
    
    // Get first file
    const auto& files = fileUpload.getFiles();
    const auto& file = files[0];
    std::string filename = file.getFileName();
    LOG_INFO << "[AddController::addPipeline] Received file: " << filename;
    
    // Check if JSON file
    if (filename.substr(filename.find_last_of(".") + 1) != "json") {
        LOG_ERROR << "[AddController::addPipeline] File is not JSON: " << filename;
        Json::Value err;
        err["error"] = "Pipeline must be a JSON file";
        auto resp = HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(k400BadRequest);
        cb(resp);
        return;
    }
    
    try {
        // Save uploaded file to temp location
        std::string tempPath = "/tmp/pipeline-upload-" + std::to_string(std::time(nullptr)) + ".json";
        file.saveAs(tempPath);
        LOG_INFO << "[AddController::addPipeline] Saved to temp: " << tempPath;
        
        // Load uploaded file and extract metadata
        Json::Value uploadedJson;
        std::ifstream uploadFile(tempPath);
        Json::CharReaderBuilder builder;
        std::string errs;
        if (!Json::parseFromStream(builder, uploadFile, &uploadedJson, &errs)) {
            uploadFile.close();
            LOG_ERROR << "[AddController::addPipeline] Failed to parse uploaded JSON: " << errs;
            throw std::runtime_error("Failed to parse uploaded JSON: " + errs);
        }
        uploadFile.close();
        
        // Debug: Log received JSON structure
        LOG_INFO << "[AddController::addPipeline] Uploaded JSON keys: ";
        for (const auto& key : uploadedJson.getMemberNames()) {
            LOG_INFO << "  - " << key << ": " << uploadedJson[key].toStyledString().substr(0, 100);
        }
        
        // Extract pipelineUID from uploaded file (GUI should have added it)
        if (!uploadedJson.isMember("pipelineUID") || uploadedJson["pipelineUID"].asString().empty()) {
            LOG_ERROR << "[AddController::addPipeline] Uploaded file missing pipelineUID";
            throw std::runtime_error("Uploaded file must contain pipelineUID");
        }
        std::string pipelineUID = uploadedJson["pipelineUID"].asString();
        LOG_INFO << "[AddController::addPipeline] Processing pipeline: " << pipelineUID;
        
        // Validate pipeline using PipelineConfigLoader
        auto config = PipelineConfigLoader::loadFromJSON(tempPath);
        PipelineConfigLoader::validateOrThrow(config);
        LOG_INFO << "[AddController::addPipeline] Pipeline validated successfully with " << config.steps.size() << " steps";
        
        // Debug: Check temp file size before copy
        auto tempSize = std::filesystem::file_size(tempPath);
        LOG_INFO << "[AddController::addPipeline] Temp file size: " << tempSize << " bytes";
        
        // Save to data/pipelines/ directory
        std::filesystem::path pipelinesDir = "data/pipelines";
        std::filesystem::create_directories(pipelinesDir);
        std::string finalPath = (pipelinesDir / (pipelineUID + ".json")).string();
        
        // Simply copy the uploaded file (already has correct format with metadata)
        std::filesystem::copy_file(tempPath, finalPath, std::filesystem::copy_options::overwrite_existing);
        LOG_INFO << "[AddController::addPipeline] Saved pipeline to: " << finalPath;
        
        // Debug: Verify copy
        auto finalSize = std::filesystem::file_size(finalPath);
        LOG_INFO << "[AddController::addPipeline] Final file size: " << finalSize << " bytes";
        if (tempSize != finalSize) {
            LOG_ERROR << "[AddController::addPipeline] FILE SIZE MISMATCH! Temp: " << tempSize << " Final: " << finalSize;
        }
        
        // Extract metadata from uploaded file for response
        std::string description = uploadedJson.isMember("description") ? uploadedJson["description"].asString() : "Uploaded pipeline";
        std::string status = uploadedJson.isMember("status") ? uploadedJson["status"].asString() : "active";
        std::string dateBuild = uploadedJson.isMember("dateBuild") ? uploadedJson["dateBuild"].asString() : getCurrentTimeISO8601();
        
        // NOTE: Do NOT call infoDataManager().put() here as it would overwrite the full pipeline file
        // with just metadata! The file already exists with complete content.
        // InfoDataManager will pick it up on next scan/refresh.
        LOG_INFO << "[AddController::addPipeline] Pipeline saved, InfoDataManager will detect it on next scan";
        
        // Create response
        Json::Value response;
        response["pipelineUID"] = pipelineUID;
        response["description"] = description;
        response["status"] = status;
        response["dateBuild"] = dateBuild;
        response["filePath"] = finalPath;
        response["stepCount"] = (int)config.steps.size();
        
        // Clean up temp file
        std::filesystem::remove(tempPath);
        
        auto resp = HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(k201Created);
        cb(resp);
        
    } catch (const std::exception& e) {
        LOG_ERROR << "[AddController::addPipeline] Error: " << e.what();
        Json::Value err;
        err["error"] = std::string("Pipeline validation or save failed: ") + e.what();
        auto resp = HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(k400BadRequest);
        cb(resp);
    }
}

void AddController::addStudyMetaInfo(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> && cb) {
    auto json = req->getJsonObject();
    StudyMetaInfo info;
    if (json) {
        if (json->isMember("studyInfoUID")) info.studyInfoUID = (*json)["studyInfoUID"].asString(); else info.studyInfoUID = "study-" + std::to_string(std::time(nullptr));
        if (json->isMember("description")) info.description = (*json)["description"].asString();
        if (json->isMember("individualScientificName")) info.individualScientificName = (*json)["individualScientificName"].asString();
        if (json->isMember("weightInMg")) info.weightInMg = (*json)["weightInMg"].asDouble();
    } else {
        // fallback mock
        info.studyInfoUID = "study-" + std::to_string(std::time(nullptr));
        info.individualScientificName = "Unknown species";
        info.weightInMg = 0.0;
    }
    auto resp = HttpResponse::newHttpJsonResponse(info.toJson());
    resp->setStatusCode(k201Created);
    cb(resp);
}
