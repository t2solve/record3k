#pragma once

#include <map>
#include <string>

class FilterConfig {
public:
    void setParameter(const std::string& key, double value);
    double getParameter(const std::string& key, double defaultValue = 0.0) const;
    
    // Method to get all parameters (useful for XML serialization)
    const std::map<std::string, double>& getAllParameters() const;
    
    // Method to check if parameter exists
    bool hasParameter(const std::string& key) const;
    
    // Method to clear all parameters
    void clearParameters();
    
    // Method to get parameter count
    size_t getParameterCount() const;

private:
    std::map<std::string, double> parameters;
};