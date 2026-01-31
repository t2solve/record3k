#include <api/controllers/get_controller.h>
#include <api/runtime/info_data_manager.h>
#include <api/logging.h>
#include <drogon/drogon.h>
#include <fstream>
#include <sstream>

using namespace drogon;
using namespace api;

void GetController::getFileBinary(const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> && cb, const std::string &fileUID) {
    LOG_INFO << "[GetController::getFileBinary] Requested file: " << fileUID;
    
    // Get file info from InfoDataManager
    auto fileInfo = infoDataManager().get("files", fileUID);
    
    if (!fileInfo || !fileInfo->isMember("archivePath")) {
        LOG_ERROR << "[GetController::getFileBinary] File not found or missing archivePath: " << fileUID;
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k404NotFound);
        resp->setBody("File not found: " + fileUID);
        cb(resp);
        return;
    }
    
    std::string archivePath = (*fileInfo)["archivePath"].asString();
    LOG_INFO << "[GetController::getFileBinary] Archive path: " << archivePath;
    
    // Open and read the file
    std::ifstream file(archivePath, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR << "[GetController::getFileBinary] Failed to open file: " << archivePath;
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k500InternalServerError);
        resp->setBody("Failed to read file from disk");
        cb(resp);
        return;
    }
    
    // Read entire file into memory
    std::ostringstream fileStream;
    fileStream << file.rdbuf();
    std::string fileContent = fileStream.str();
    file.close();
    
    LOG_INFO << "[GetController::getFileBinary] Read " << fileContent.size() << " bytes from disk";
    
    // Create response with file content
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    resp->setContentTypeCode(CT_APPLICATION_OCTET_STREAM);
    resp->addHeader("Content-Disposition", "attachment; filename=\"" + fileUID + ".tar.gz\"");
    resp->setBody(fileContent);
    
    LOG_INFO << "[GetController::getFileBinary] Sending response with " << fileContent.size() << " bytes";
    cb(resp);
}
