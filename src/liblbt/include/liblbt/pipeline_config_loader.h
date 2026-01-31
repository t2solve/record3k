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
        std::string pipelineUID; //aka its own file name 
        std::string status;
        std::string description;
        std::string datetimeCreated;  // ISO-8601 format
    };
    
    static PipelineConfig loadFromXML(const std::string& filename);
    static void saveToXML(const std::string& filename, const PipelineConfig& config);
    
    static PipelineConfig loadFromJSON(const std::string& filename);
    static bool saveToJSON(const std::string& filename, const PipelineConfig& config);
    
    /**
     * Validate a PipelineConfig structure
     * @param config The pipeline configuration to validate
     * @param errorMsg Output parameter for error message if validation fails
     * @return true if valid, false otherwise
     */
    static bool validate(const PipelineConfig& config, std::string& errorMsg);
    
    /**
     * Validate and throw exception if invalid
     * @param config The pipeline configuration to validate
     * @throws std::runtime_error if validation fails
     */
    static void validateOrThrow(const PipelineConfig& config);
    
    static ProcessingMode stringToProcessingMode(const std::string& modeStr);
    static std::string processingModeToString(ProcessingMode mode);
    
private:
    static FilterType stringToFilterType(const std::string& filterName);
    static std::string filterTypeToString(FilterType filterType);
};