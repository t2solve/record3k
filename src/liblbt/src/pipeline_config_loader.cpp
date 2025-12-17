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