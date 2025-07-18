#include "pipeline_config_loader.h"
#include <stdexcept>

PipelineConfigLoader::PipelineConfig PipelineConfigLoader::loadFromXML(const std::string& filename) {
    cv::FileStorage fs(filename, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        throw std::runtime_error("Cannot open pipeline config file: " + filename);
    }
    
    PipelineConfig config;
    
    // Read processing mode
    std::string modeStr;
    fs["pipeline"]["processing_mode"] >> modeStr;
    config.mode = stringToProcessingMode(modeStr);
    
    // Read steps
    cv::FileNode stepsNode = fs["pipeline"]["steps"];
    for (cv::FileNodeIterator it = stepsNode.begin(); it != stepsNode.end(); ++it) {
        cv::FileNode stepNode = *it;
        
        std::string stepName;
        int enabled;
        stepNode["name"] >> stepName;
        stepNode["enabled"] >> enabled;
        
        if (enabled) {
            // Create step
            FilterType filterType = stringToFilterType(stepName);
            auto step = ProcessStepFactory::createStep(filterType, config.mode);
            config.steps.push_back(step);
            
            // Create config
            FilterConfig filterConfig;
            cv::FileNode paramsNode = stepNode["parameters"];
            for (cv::FileNodeIterator paramIt = paramsNode.begin(); paramIt != paramsNode.end(); ++paramIt) {
                cv::FileNode paramNode = *paramIt;
                std::string paramName = paramNode.name();
                double paramValue = (double)paramNode;
                filterConfig.setParameter(paramName, paramValue);
            }

            config.configs.push_back(filterConfig);
        }
    }
    
    fs.release();
    return config;
}

void PipelineConfigLoader::saveToXML(const std::string& filename, const PipelineConfig& config) {
    cv::FileStorage fs(filename, cv::FileStorage::WRITE);
    
    fs << "pipeline" << "{";
    fs << "processing_mode" << processingModeToString(config.mode);
    fs << "steps" << "[";
    
    for (size_t i = 0; i < config.steps.size(); ++i) {
        fs << "{";
        fs << "name" << config.steps[i]->getName();
        fs << "enabled" << 1;
        fs << "parameters" << "{";
        
        // Write parameters from FilterConfig
         // Use the new getAllParameters method
        const auto& params = config.configs[i].getAllParameters();
        for (const auto& param : params) {
            fs << param.first << param.second;
        }
        fs << "}";
        fs << "}";
    }
    
    fs << "]";
    fs << "}";
    fs.release();
}

ProcessingMode PipelineConfigLoader::stringToProcessingMode(const std::string& modeStr) {
    if (modeStr == "CPU_ONLY") return ProcessingMode::CPU_ONLY;
    if (modeStr == "CUDA_ONLY") return ProcessingMode::CUDA_ONLY;
    if (modeStr == "CUDA_PREFERRED") return ProcessingMode::CUDA_PREFERRED;
    return ProcessingMode::CPU_ONLY; // default
}

std::string PipelineConfigLoader::processingModeToString(ProcessingMode mode) {
    switch (mode) {
        case ProcessingMode::CPU_ONLY: return "CPU_ONLY";
        case ProcessingMode::CUDA_ONLY: return "CUDA_ONLY";
        case ProcessingMode::CUDA_PREFERRED: return "CUDA_PREFERRED";
        default: return "CPU_ONLY";
    }
}

FilterType PipelineConfigLoader::stringToFilterType(const std::string& filterName) {
    if (filterName == "GaussianBlur_CPU" || filterName == "GaussianBlur_CUDA") return FilterType::GAUSSIAN_BLUR;
    if (filterName == "BilateralFilter_CPU" || filterName == "BilateralFilter_CUDA") return FilterType::BILATERAL_FILTER;
    if (filterName == "MedianFilter_CPU") return FilterType::MEDIAN_FILTER;
    if (filterName == "Sharpen_CPU" || filterName == "Sharpen_CUDA") return FilterType::SHARPEN;
    if (filterName == "EdgeDetection_CPU" || filterName == "EdgeDetection_CUDA") return FilterType::EDGE_DETECTION;
    if (filterName == "Denoise_CPU") return FilterType::DENOISE;
    if (filterName == "BackgroundSubtraction_CPU" || filterName == "BackgroundSubtraction_CUDA") return FilterType::BACKGROUND_SUBTRACTION_MOG2;
    return FilterType::NONE;
}

std::string PipelineConfigLoader::filterTypeToString(FilterType filterType) {
    switch (filterType) {
        case FilterType::GAUSSIAN_BLUR: return "GAUSSIAN_BLUR";
        case FilterType::BILATERAL_FILTER: return "BILATERAL_FILTER";
        case FilterType::MEDIAN_FILTER: return "MEDIAN_FILTER";
        case FilterType::SHARPEN: return "SHARPEN";
        case FilterType::EDGE_DETECTION: return "EDGE_DETECTION";
        case FilterType::DENOISE: return "DENOISE";
        case FilterType::BACKGROUND_SUBTRACTION_MOG2: return "BACKGROUND_SUBTRACTION_MOG2";
        default: return "NONE";
    }
}