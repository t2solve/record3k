#pragma once

#include <opencv2/opencv.hpp>
#include "process_step_factory.h"
#include "filter_config.h"

class PipelineConfigLoader {
public:
    struct PipelineConfig {
        ProcessingMode mode;
        std::vector<std::shared_ptr<ProcessStep>> steps;
        std::vector<FilterConfig> configs;
    };
    
    static PipelineConfig loadFromXML(const std::string& filename);
    static void saveToXML(const std::string& filename, const PipelineConfig& config);
    static ProcessingMode stringToProcessingMode(const std::string& modeStr);
    static std::string processingModeToString(ProcessingMode mode);
    
private:
    static FilterType stringToFilterType(const std::string& filterName);
    static std::string filterTypeToString(FilterType filterType);
};