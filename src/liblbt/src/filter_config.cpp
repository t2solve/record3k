#include "filter_config.h"

void FilterConfig::setParameter(const std::string& key, double value) {
    parameters[key] = value;
}

double FilterConfig::getParameter(const std::string& key, double defaultValue) const {
    auto it = parameters.find(key);
    return (it != parameters.end()) ? it->second : defaultValue;
}

void FilterConfig::setParameter(const std::string& key, const cv::Mat& value) {
    parametersMat[key] = value;
}

bool FilterConfig::getParameter(const std::string& key, cv::Mat& out) const {
    auto it = parametersMat.find(key);
    if (it != parametersMat.end()) {
        out = it->second; // shallow copy is fine; cv::Mat is ref-counted
        return true;
    }
    return false;
}

const std::map<std::string, double>& FilterConfig::getAllParameters() const {
    return parameters;
}

bool FilterConfig::hasParameter(const std::string& key) const {
    if (parameters.find(key) != parameters.end()) return true;
    if (parametersMat.find(key) != parametersMat.end()) return true;
    return false;
}

void FilterConfig::clearParameters() {
    parameters.clear();
    parametersMat.clear();
}

size_t FilterConfig::getParameterCount() const {
    return parameters.size() + parametersMat.size();
}