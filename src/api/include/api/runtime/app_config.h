#pragma once
#include <string>
#include <filesystem>
#include <vector>

namespace api {

class AppConfig {
public:
    static AppConfig& instance();

    void setDataRootPath(const std::string& path);
    std::filesystem::path getDataRootPath() const;
    std::filesystem::path getInfoPath(const std::string& category) const;
    
    // VimbaX configuration
    void setVimbaxTransportLayerPath(const std::string& path);
    std::string getVimbaxTransportLayerPath() const;
    
    // Validates data path structure and required files
    bool validateDataPath(std::vector<std::string>& errors) const;

private:
    AppConfig();
    AppConfig(const AppConfig&) = delete;
    AppConfig& operator=(const AppConfig&) = delete;

    std::filesystem::path dataRootPath_;
    std::string vimbaxTransportLayerPath_;
};

} // namespace api
