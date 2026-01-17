#include <api/runtime/app_config.h>
#include <iostream>

namespace api {

AppConfig::AppConfig() : dataRootPath_("data") {}

AppConfig& AppConfig::instance() {
    static AppConfig inst;
    return inst;
}

void AppConfig::setDataRootPath(const std::string& path) {
    dataRootPath_ = path;
}

std::filesystem::path AppConfig::getDataRootPath() const {
    return dataRootPath_;
}

std::filesystem::path AppConfig::getInfoPath(const std::string& category) const {
    return dataRootPath_ /  category;
}

void AppConfig::setVimbaxTransportLayerPath(const std::string& path) {
    vimbaxTransportLayerPath_ = path;
}

std::string AppConfig::getVimbaxTransportLayerPath() const {
    if (vimbaxTransportLayerPath_.empty()) {
        std::cout << "[AppConfig] getVimbaxTransportLayerPath() no path set, returning default value VimbaGigETL.cti" << std::endl;
        return "VimbaGigETL.cti";
    }
    else    
    {
        std::cout << "[AppConfig] getVimbaxTransportLayerPath() returning: '" << vimbaxTransportLayerPath_ << "'" << std::endl;
    }
    return vimbaxTransportLayerPath_;
}

bool AppConfig::validateDataPath(std::vector<std::string>& errors) const {
    bool allValid = true;
    
    // Check if data root path exists
    if (!std::filesystem::exists(dataRootPath_)) {
        errors.push_back("Data root path does not exist: " + dataRootPath_.string());
        return false;
    }
    
    if (!std::filesystem::is_directory(dataRootPath_)) {
        errors.push_back("Data root path is not a directory: " + dataRootPath_.string());
        return false;
    }
    
    // Check required subdirectories
    std::vector<std::string> requiredDirs = {
        "cameras",
        "pipelines",
        "calibrations",
        "records",
        "files",
        "studies"
    };
    
    for (const auto& dir : requiredDirs) {
        auto fullPath = dataRootPath_ / dir;
        if (!std::filesystem::exists(fullPath)) {
            errors.push_back("Missing directory: " + fullPath.string());
            allValid = false;
        } else if (!std::filesystem::is_directory(fullPath)) {
            errors.push_back("Path is not a directory: " + fullPath.string());
            allValid = false;
        }
    }
    
    // Check required config files
    std::vector<std::string> requiredFiles = {
        "pipelines/default_recording.json",
        "pipelines/default_calibration.json"
    };
    
    for (const auto& file : requiredFiles) {
        auto fullPath = dataRootPath_ / file;
        if (!std::filesystem::exists(fullPath)) {
            errors.push_back("Missing required config file: " + fullPath.string());
            allValid = false;
        } else if (!std::filesystem::is_regular_file(fullPath)) {
            errors.push_back("Path is not a regular file: " + fullPath.string());
            allValid = false;
        }
    }
    
    // Check cameras folder has at least one JSON file
    auto camerasPath = dataRootPath_ / "cameras";
    if (std::filesystem::exists(camerasPath) && std::filesystem::is_directory(camerasPath)) {
        bool hasJsonFile = false;
        for (const auto& entry : std::filesystem::directory_iterator(camerasPath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                hasJsonFile = true;
                break;
            }
        }
        if (!hasJsonFile) {
            errors.push_back("Cameras directory must contain at least one .json file: " + camerasPath.string());
            allValid = false;
        }
    }
    
    return allValid;
}

} // namespace api
