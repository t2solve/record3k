#pragma once

#include <map>
#include <string>
#include <opencv2/core.hpp>

class FilterConfig {
public:
    void setParameter(const std::string& key, double value);
    double getParameter(const std::string& key, double defaultValue = 0.0) const;
    
    // Overloads for matrix parameters (e.g., camera calibration Mats)
    void setParameter(const std::string& key, const cv::Mat& value);
    // Returns true and fills 'out' if present; false otherwise
    bool getParameter(const std::string& key, cv::Mat& out) const;
    
    // Overloads for string parameters (e.g., file paths, output directories)
    void setParameter(const std::string& key, const std::string& value);
    std::string getParameter(const std::string& key, const std::string& defaultValue) const;
    
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
    std::map<std::string, cv::Mat> parametersMat;
    std::map<std::string, std::string> parametersString;
    
};