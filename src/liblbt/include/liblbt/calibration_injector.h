#pragma once

#include <string>
#include <optional>
#include <filesystem>
#include <opencv2/core.hpp>
#include "filter_config.h"
#include "pipeline_config_loader.h"

namespace lbt {

/**
 * @brief Loads calibration data from archived records and injects into pipeline configs
 * 
 * CalibrationInjector extracts compact calibration files from tar.gz archives
 * and applies camera parameters to lens correction and projection filter steps.
 */
class CalibrationInjector {
public:
    struct CalibrationData {
        cv::Mat cameraMatrix;
        cv::Mat distCoeffs;
        cv::Mat extrinsicParameters;
        double avgReprojectionError = 0.0;
        bool valid = false;
        std::string errorMessage;
    };

    /**
     * @brief Load calibration data from a record archive
     * @param fileUID Unique identifier for the file (e.g., "file-rec-123")
     * @param filesDir Directory containing archived files (default: "data/files/bin")
     * @return CalibrationData structure with loaded parameters
     */
    static CalibrationData loadFromArchive(
        const std::string& fileUID,
        const std::string& filesDir = "data/files/bin");

    /**
     * @brief Load calibration data from a compact XML file
     * @param xmlPath Path to compact calibration XML file
     * @return CalibrationData structure with loaded parameters
     */
    static CalibrationData loadFromCompactXML(const std::string& xmlPath);

    /**
     * @brief Inject calibration data into lens correction filter config
     * @param config FilterConfig to modify
     * @param data CalibrationData to inject
     * @return true if injection succeeded
     */
    static bool injectLensCorrection(FilterConfig& config, const CalibrationData& data);

    /**
     * @brief Inject calibration data into ProjectPointTo2DSurface filter config
     * @param config FilterConfig to modify
     * @param data CalibrationData to inject
     * @return true if injection succeeded
     */
    static bool injectProjection(FilterConfig& config, const CalibrationData& data);

    /**
     * @brief Inject calibration into all matching steps in a pipeline
     * @param pipeline PipelineConfig to modify
     * @param data CalibrationData to inject
     * @return Number of steps modified
     */
    static int injectIntoPipeline(
        PipelineConfigLoader::PipelineConfig& pipeline,
        const CalibrationData& data);

private:
    static std::optional<std::string> extractCalibrationFromArchive(
        const std::filesystem::path& archivePath,
        const std::string& tempDir);
};

} // namespace lbt
