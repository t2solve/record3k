#include "filter_config.h"

void FilterConfig::setParameter(const std::string& key, double value) {
    parameters[key] = value;
}

double FilterConfig::getParameter(const std::string& key, double defaultValue) const {
    auto it = parameters.find(key);
    return (it != parameters.end()) ? it->second : defaultValue;
}

const std::map<std::string, double>& FilterConfig::getAllParameters() const {
    return parameters;
}

bool FilterConfig::hasParameter(const std::string& key) const {
    return parameters.find(key) != parameters.end();
}

void FilterConfig::clearParameters() {
    parameters.clear();
}

size_t FilterConfig::getParameterCount() const {
    return parameters.size();
}