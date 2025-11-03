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
    
};