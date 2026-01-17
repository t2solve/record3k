#pragma once
#include <string>
#include <filesystem>

namespace api {

class AppConfig {
public:
    static AppConfig& instance();

    void setDataRootPath(const std::string& path);
    std::filesystem::path getDataRootPath() const;
    std::filesystem::path getInfoPath(const std::string& category) const;

private:
    AppConfig();
    AppConfig(const AppConfig&) = delete;
    AppConfig& operator=(const AppConfig&) = delete;

    std::filesystem::path dataRootPath_;
};

} // namespace api
