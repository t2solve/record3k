#include "pipeline_config_loader.h"
#include <stdexcept>
#include <iostream>

PipelineConfigLoader::PipelineConfig PipelineConfigLoader::loadFromXML(const std::string& filename) {
    cv::FileStorage fs(filename, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        throw std::runtime_error("Cannot open pipeline config file: " + filename);
    }

    PipelineConfig config;

    try {
        cv::FileNode pipelineNode = fs["pipeline"];
        if (pipelineNode.empty()) {
            std::cerr << "[PipelineConfigLoader] Missing 'pipeline' node in XML: " << filename << "\n";
            fs.release();
            return config;
        }

        // processing_mode is optional
        std::string modeStr;
        cv::FileNode modeNode = pipelineNode["processing_mode"];
        if (!modeNode.empty()) modeNode >> modeStr;
        config.mode = stringToProcessingMode(modeStr);

        // steps is optional
        cv::FileNode stepsNode = pipelineNode["steps"];
        if (stepsNode.empty()) {
            std::cerr << "[PipelineConfigLoader] No 'steps' node; using empty pipeline\n";
            fs.release();
            return config;
        }
        if (stepsNode.type() != cv::FileNode::SEQ) {
            std::cerr << "[PipelineConfigLoader] 'steps' is not a sequence; ignoring\n";
            fs.release();
            return config;
        }

        int idx = 0;
        for (cv::FileNodeIterator it = stepsNode.begin(); it != stepsNode.end(); ++it, ++idx) {
            try {
                cv::FileNode stepNode = *it;
                if (stepNode.empty() || stepNode.type() != cv::FileNode::MAP) {
                    std::cerr << "[PipelineConfigLoader] Step " << idx << " is not a map; skipping\n";
                    continue;
                }

                std::string stepName;
                int enabled = 1;
                cv::FileNode nameNode = stepNode["name"];
                if (nameNode.empty()) {
                    std::cerr << "[PipelineConfigLoader] Step " << idx << " missing 'name'; skipping\n";
                    continue;
                }
                nameNode >> stepName;
                cv::FileNode enabledNode = stepNode["enabled"];
                if (!enabledNode.empty()) enabledNode >> enabled;
                if (!enabled) continue;

                FilterType filterType = ProcessStepFactory::nameToFilterType(stepName);
                if (filterType == FilterType::NONE) {
                    std::cerr << "[PipelineConfigLoader] Unknown step name '" << stepName << "' at index " << idx << "; skipping\n";
                    continue;
                }

                std::shared_ptr<ProcessStep> step = nullptr;
                try {
                    step = ProcessStepFactory::createStep(filterType, config.mode);
                } catch (const std::exception& e) {
                    std::cerr << "[PipelineConfigLoader] Failed to create step '" << stepName << "': " << e.what() << "\n";
                    continue;
                }
                config.steps.push_back(step);

                // parameters optional
                FilterConfig filterConfig;
                cv::FileNode paramsNode = stepNode["parameters"];
                if (!paramsNode.empty() && paramsNode.type() == cv::FileNode::MAP) {
                    for (cv::FileNodeIterator pit = paramsNode.begin(); pit != paramsNode.end(); ++pit) {
                        cv::FileNode paramNode = *pit;
                        std::string paramName = paramNode.name();
                        double paramValue = 0.0;
                        try {
                            paramValue = (double)paramNode;
                        } catch (...) {
                            std::cerr << "[PipelineConfigLoader] Non-numeric parameter '" << paramName << "' in step '" << stepName << "'\n";
                            continue;
                        }
                        filterConfig.setParameter(paramName, paramValue);
                    }
                }
                config.configs.push_back(filterConfig);
            } catch (const std::exception& e) {
                std::cerr << "[PipelineConfigLoader] Error parsing step at index " << idx << ": " << e.what() << "\n";
                // continue to next step
            }
        }

        if (config.steps.size() != config.configs.size()) {
            std::cerr << "[PipelineConfigLoader] Steps/configs size mismatch (" << config.steps.size() << " vs " << config.configs.size() << ") — trimming\n";
            size_t m = std::min(config.steps.size(), config.configs.size());
            config.steps.resize(m);
            config.configs.resize(m);
        }
    } catch (...) {
        fs.release();
        throw;
    }

    fs.release();
    
    // Validate loaded configuration
    std::string validationError;
    if (!validate(config, validationError)) {
        std::cerr << "[PipelineConfigLoader] Warning: " << validationError << std::endl;
        // Don't throw - just warn, allow loading potentially incomplete configs
    }
    
    return config;
}

void PipelineConfigLoader::saveToXML(const std::string& filename, const PipelineConfig& config) {
    // Validate before saving
    validateOrThrow(config);
    
    cv::FileStorage fs(filename, cv::FileStorage::WRITE);
    
    fs << "pipeline" << "{";
    fs << "processing_mode" << processingModeToString(config.mode);
    fs << "steps" << "[";
    
    for (size_t i = 0; i < config.steps.size(); ++i) {
        fs << "{";
        fs << "name" << config.steps[i]->getName();
        fs << "enabled" << 1;
        fs << "parameters" << "{";
        
        // Write all parameter types from FilterConfig
        // 1. Double parameters
        const auto& params = config.configs[i].getAllParameters();
        for (const auto& param : params) {
            fs << param.first << param.second;
        }
        
        // 2. String parameters
        const auto& stringParams = config.configs[i].getAllStringParameters();
        for (const auto& param : stringParams) {
            fs << param.first << param.second;
        }
        
        // 3. Mat parameters (camera matrices, etc.)
        const auto& matParams = config.configs[i].getAllMatParameters();
        for (const auto& param : matParams) {
            fs << param.first << param.second;
        }
        
        fs << "}";
        fs << "}";
    }
    
    fs << "]";
    fs << "}";
    fs.release();
}

bool PipelineConfigLoader::saveToJSON(const std::string& filename, const PipelineConfig& config) {
    // Validate before saving
    try {
        validateOrThrow(config);
    } catch (const std::exception& e) {
        std::cerr << "[PipelineConfigLoader::saveToJSON] Validation failed: " << e.what() << std::endl;
        return false;
    }
    
    try {
        cv::FileStorage fs(filename, cv::FileStorage::WRITE | cv::FileStorage::FORMAT_JSON);
        
        // Write metadata at top level (if present)
        if (!config.pipelineUID.empty()) {
            fs << "pipelineUID" << config.pipelineUID;
        }
        if (!config.description.empty()) {
            fs << "description" << config.description;
        }
        if (!config.status.empty()) {
            fs << "status" << config.status;
        }
        if (!config.datetimeCreated.empty()) {
            fs << "dateBuild" << config.datetimeCreated;
        }
    
        fs << "pipeline" << "{";
        fs << "processing_mode" << processingModeToString(config.mode);
        fs << "steps" << "[";
        
        for (size_t i = 0; i < config.steps.size(); ++i) {
            fs << "{";
            fs << "name" << config.steps[i]->getName();
            fs << "enabled" << 1;
            fs << "parameters" << "{";
            
            // Write all parameter types from FilterConfig
            // 1. Double parameters
            const auto& params = config.configs[i].getAllParameters();
            for (const auto& param : params) {
                fs << param.first << param.second;
            }
            
            // 2. String parameters
            const auto& stringParams = config.configs[i].getAllStringParameters();
            for (const auto& param : stringParams) {
                fs << param.first << param.second;
            }
            
            // 3. Mat parameters (camera matrices, etc.)
            const auto& matParams = config.configs[i].getAllMatParameters();
            for (const auto& param : matParams) {
                fs << param.first << param.second;
            }
            
            fs << "}";
            fs << "}";
        }
        
        fs << "]";
        fs << "}";
        fs.release();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[PipelineConfigLoader::saveToJSON] Error writing file: " << e.what() << std::endl;
        return false;
    }
}

PipelineConfigLoader::PipelineConfig PipelineConfigLoader::loadFromJSON(const std::string& filename) {
    cv::FileStorage fs(filename, cv::FileStorage::READ | cv::FileStorage::FORMAT_JSON);
    PipelineConfig result;
    
    if (!fs.isOpened()) {
        throw std::runtime_error("Could not open JSON file: " + filename);
    }
    
    cv::FileNode pipelineNode = fs["pipeline"];
    if (pipelineNode.empty()) {
        throw std::runtime_error("No 'pipeline' node found in JSON");
    }
    
    // Read processing mode
    std::string modeStr = (std::string)pipelineNode["processing_mode"];
    result.mode = stringToProcessingMode(modeStr);
    
    // Read steps
    cv::FileNode stepsNode = pipelineNode["steps"];
    for (cv::FileNodeIterator it = stepsNode.begin(); it != stepsNode.end(); ++it) {
        cv::FileNode stepNode = *it;
        std::string stepName = (std::string)stepNode["name"];
        int enabled = (int)stepNode["enabled"];
        
        if (!enabled) continue;
        
        FilterType filterType = stringToFilterType(stepName);
        auto step = ProcessStepFactory::createStep(filterType, result.mode);
        result.steps.push_back(step);
        
        // Read parameters
        FilterConfig config;
        cv::FileNode paramsNode = stepNode["parameters"];
        
        for (cv::FileNodeIterator paramIt = paramsNode.begin(); paramIt != paramsNode.end(); ++paramIt) {
            std::string key = (*paramIt).name();
            cv::FileNode valueNode = *paramIt;
            
            // Determine type and read accordingly
            if (valueNode.isReal() || valueNode.isInt()) {
                double value = (double)valueNode;
                config.setParameter(key, value);
            }
            else if (valueNode.isString()) {
                std::string value = (std::string)valueNode;
                config.setParameter(key, value);
            }
            else if (valueNode.isMap() || valueNode.isSeq()) {
                // This is likely a Mat - try to read it
                try {
                    cv::Mat value;
                    valueNode >> value;
                    config.setParameter(key, value);
                } catch (...) {
                    std::cerr << "[PipelineConfigLoader] Failed to read Mat parameter: " << key << std::endl;
                }
            }
        }
        
        result.configs.push_back(config);
    }
    
    fs.release();
    
    // Validate loaded configuration
    validateOrThrow(result);
    
    return result;
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
    // Delegate to the factory helper to avoid duplication and keep the mapping in one place
    return ProcessStepFactory::nameToFilterType(filterName);
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
        case FilterType::BACKGROUND_SUBTRACTION_GMG: return "BACKGROUND_SUBTRACTION_GMG";
        case FilterType::BACKGROUND_SUBTRACTION_CNT: return "BACKGROUND_SUBTRACTION_CNT";
        case FilterType::CONTOUR_DETECTION: return "CONTOUR_DETECTION";
        case FilterType::LENS_CORRECTION: return "LENS_CORRECTION";
        case FilterType::ROI_CIRCLE_CROP: return "ROI_CIRCLE_CROP";
        case FilterType::MASS_CENTER_OVERLAY: return "MASS_CENTER_OVERLAY";
        case FilterType::BINARY_THRESHOLD: return "BINARY_THRESHOLD";
        case FilterType::CONTOUR_AREA_FILTER: return "CONTOUR_AREA_FILTER";
        case FilterType::MORPHOLOGY_CLOSE: return "MORPHOLOGY_CLOSE";
        default: return "NONE";
    }
}

bool PipelineConfigLoader::validate(const PipelineConfig& config, std::string& errorMsg) {
    // Check if steps and configs arrays have matching sizes
    if (config.steps.size() != config.configs.size()) {
        errorMsg = "Pipeline validation failed: steps.size() (" + 
                   std::to_string(config.steps.size()) + 
                   ") != configs.size() (" + 
                   std::to_string(config.configs.size()) + ")";
        return false;
    }
    
    // Check if pipeline has at least one step
    if (config.steps.empty()) {
        errorMsg = "Pipeline validation failed: pipeline must have at least one processing step";
        return false;
    }
    
    // Validate each step
    for (size_t i = 0; i < config.steps.size(); ++i) {
        const auto& step = config.steps[i];
        
        // Check step is not null
        if (!step) {
            errorMsg = "Pipeline validation failed: step " + std::to_string(i) + " is null";
            return false;
        }
        
        // Check step name is not empty
        std::string stepName = step->getName();
        if (stepName.empty()) {
            errorMsg = "Pipeline validation failed: step " + std::to_string(i) + " has empty name";
            return false;
        }
        
        // Check step is available (e.g., CUDA steps on non-CUDA system)
        if (!step->isAvailable()) {
            errorMsg = "Pipeline validation failed: step " + std::to_string(i) + 
                      " (" + stepName + ") is not available on this system";
            return false;
        }
        
        // Check filter type is valid
        FilterType ft = step->getFilterType();
        if (ft == FilterType::NONE) {
            errorMsg = "Pipeline validation failed: step " + std::to_string(i) + 
                      " (" + stepName + ") has invalid filter type";
            return false;
        }
    }
    
    // Validate processing mode
    if (config.mode != ProcessingMode::CPU_ONLY && 
        config.mode != ProcessingMode::CUDA_PREFERRED && 
        config.mode != ProcessingMode::CUDA_ONLY) {
        errorMsg = "Pipeline validation failed: invalid processing mode";
        return false;
    }
    
    // All validations passed
    return true;
}

void PipelineConfigLoader::validateOrThrow(const PipelineConfig& config) {
    std::string errorMsg;
    if (!validate(config, errorMsg)) {
        throw std::runtime_error(errorMsg);
    }
}
