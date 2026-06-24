#include <api/controllers/get_controller.h>
#include <api/runtime/info_data_manager.h>
#include <api/logging.h>
#include <drogon/drogon.h>
#include <filesystem>

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
    
    // Verify the file is accessible before handing it to Drogon
    if (!std::filesystem::exists(archivePath)) {
        LOG_ERROR << "[GetController::getFileBinary] File does not exist on disk: " << archivePath;
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k500InternalServerError);
        resp->setBody("File not accessible on disk");
        cb(resp);
        return;
    }

    auto fileSize = std::filesystem::file_size(archivePath);
    LOG_INFO << "[GetController::getFileBinary] Streaming " << fileSize << " bytes from disk";

    // newFileResponse streams directly from disk — no full-file RAM copy.
    // Drogon sets Content-Length and serves in chunks.
    auto resp = HttpResponse::newFileResponse(
        archivePath,
        fileUID + ".tar.gz",
        CT_APPLICATION_OCTET_STREAM
    );
    cb(resp);
}
