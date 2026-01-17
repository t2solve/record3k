#include <api/app_config.h>

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
    return dataRootPath_ / "info" / category;
}

} // namespace api
