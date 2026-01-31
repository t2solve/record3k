#include <liblbt/calibration_injector.h>
#include <liblbt/process_step_factory.h>
#include <opencv2/core.hpp>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <sstream>

namespace lbt {

CalibrationInjector::CalibrationData CalibrationInjector::loadFromArchive(
    const std::string& fileUID,
    const std::string& filesDir)
{
    CalibrationData result;
    
    // Construct archive path
    std::filesystem::path archivePath = std::filesystem::path(filesDir) / (fileUID + ".tar.gz");
    
    if (!std::filesystem::exists(archivePath)) {
        result.errorMessage = "Archive not found: " + archivePath.string();
        std::cerr << "[CalibrationInjector] " << result.errorMessage << std::endl;
        return result;
    }
    
    std::cout << "[CalibrationInjector] Loading calibration from: " << archivePath << std::endl;
    
    // Create temporary extraction directory
    std::string tempDir = "/tmp/calib_extract_" + fileUID;
    std::filesystem::create_directories(tempDir);
    
    // Extract calibration file
    auto calibFile = extractCalibrationFromArchive(archivePath, tempDir);
    
    if (!calibFile) {
        result.errorMessage = "Failed to extract calibration from archive";
        std::cerr << "[CalibrationInjector] " << result.errorMessage << std::endl;
        std::filesystem::remove_all(tempDir);
        return result;
    }
    
    // Load from extracted file
    result = loadFromCompactXML(*calibFile);
    
    // Cleanup
    std::filesystem::remove_all(tempDir);
    
    return result;
}

CalibrationInjector::CalibrationData CalibrationInjector::loadFromCompactXML(
    const std::string& xmlPath)
{
    CalibrationData result;
    
    cv::FileStorage fs(xmlPath, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        result.errorMessage = "Failed to open calibration file: " + xmlPath;
        std::cerr << "[CalibrationInjector] " << result.errorMessage << std::endl;
        return result;
    }
    
    // Read camera matrix
    fs["camera_matrix"] >> result.cameraMatrix;
    if (result.cameraMatrix.empty()) {
        result.errorMessage = "Missing camera_matrix in calibration file";
        std::cerr << "[CalibrationInjector] " << result.errorMessage << std::endl;
        fs.release();
        return result;
    }
    
    // Read distortion coefficients
    fs["distortion_coefficients"] >> result.distCoeffs;
    if (result.distCoeffs.empty()) {
        result.errorMessage = "Missing distortion_coefficients in calibration file";
        std::cerr << "[CalibrationInjector] " << result.errorMessage << std::endl;
        fs.release();
        return result;
    }
    
    // Read extrinsic parameters (optional)
    fs["extrinsic_parameters"] >> result.extrinsicParameters;
    
    // Read reprojection error (optional)
    fs["avg_reprojection_error"] >> result.avgReprojectionError;
    
    fs.release();
    
    result.valid = true;
    std::cout << "[CalibrationInjector] Loaded calibration successfully" << std::endl;
    std::cout << "[CalibrationInjector]   Camera matrix: " << result.cameraMatrix.size() << std::endl;
    std::cout << "[CalibrationInjector]   Distortion coeffs: " << result.distCoeffs.total() << " values" << std::endl;
    std::cout << "[CalibrationInjector]   Extrinsics: " << result.extrinsicParameters.size() << std::endl;
    std::cout << "[CalibrationInjector]   Avg reprojection error: " << result.avgReprojectionError << std::endl;
    
    return result;
}

bool CalibrationInjector::injectLensCorrection(
    FilterConfig& config,
    const CalibrationData& data)
{
    if (!data.valid || data.cameraMatrix.empty() || data.distCoeffs.empty()) {
        std::cerr << "[CalibrationInjector] Invalid calibration data for lens correction" << std::endl;
        return false;
    }
    
    // Extract camera matrix parameters
    config.setParameter("fx", data.cameraMatrix.at<double>(0, 0));
    config.setParameter("fy", data.cameraMatrix.at<double>(1, 1));
    config.setParameter("cx", data.cameraMatrix.at<double>(0, 2));
    config.setParameter("cy", data.cameraMatrix.at<double>(1, 2));
    
    // Extract distortion coefficients (k1, k2, p1, p2, k3)
    if (data.distCoeffs.total() >= 5) {
        config.setParameter("k1", data.distCoeffs.at<double>(0));
        config.setParameter("k2", data.distCoeffs.at<double>(1));
        config.setParameter("p1", data.distCoeffs.at<double>(2));
        config.setParameter("p2", data.distCoeffs.at<double>(3));
        config.setParameter("k3", data.distCoeffs.at<double>(4));
    }
    
    std::cout << "[CalibrationInjector] Injected lens correction parameters" << std::endl;
    return true;
}

bool CalibrationInjector::injectProjection(
    FilterConfig& config,
    const CalibrationData& data)
{
    if (!data.valid || data.cameraMatrix.empty() || data.distCoeffs.empty()) {
        std::cerr << "[CalibrationInjector] Invalid calibration data for projection" << std::endl;
        return false;
    }
    
    // Inject full matrices for projection
    config.setParameter("camera_matrix", data.cameraMatrix);
    config.setParameter("distortion_coefficients", data.distCoeffs);
    
    if (!data.extrinsicParameters.empty()) {
        config.setParameter("extrinsics_combined", data.extrinsicParameters);
        std::cout << "[CalibrationInjector] Injected projection parameters with extrinsics" << std::endl;
    } else {
        std::cout << "[CalibrationInjector] Injected projection parameters (no extrinsics)" << std::endl;
    }
    
    return true;
}

int CalibrationInjector::injectIntoPipeline(
    PipelineConfigLoader::PipelineConfig& pipeline,
    const CalibrationData& data)
{
    if (!data.valid) {
        std::cerr << "[CalibrationInjector] Cannot inject invalid calibration data" << std::endl;
        return 0;
    }
    
    int modifiedCount = 0;
    
    for (size_t i = 0; i < pipeline.steps.size(); ++i) {
        if (!pipeline.steps[i]) continue;
        
        FilterType type = pipeline.steps[i]->getFilterType();
        
        // Inject into lens correction steps
        if (type == FilterType::LENS_CORRECTION) {
            if (i < pipeline.configs.size()) {
                if (injectLensCorrection(pipeline.configs[i], data)) {
                    modifiedCount++;
                    std::cout << "[CalibrationInjector] Updated LensCorrection at step " << i << std::endl;
                }
            }
        }
        
        // Inject into projection steps
        if (type == FilterType::PROJECT_POINT_TO_2D_SURFACE) {
            if (i < pipeline.configs.size()) {
                if (injectProjection(pipeline.configs[i], data)) {
                    modifiedCount++;
                    std::cout << "[CalibrationInjector] Updated ProjectPointTo2DSurface at step " << i << std::endl;
                }
            }
        }
    }
    
    std::cout << "[CalibrationInjector] Modified " << modifiedCount << " pipeline steps" << std::endl;
    return modifiedCount;
}

std::optional<std::string> CalibrationInjector::extractCalibrationFromArchive(
    const std::filesystem::path& archivePath,
    const std::string& tempDir)
{
    // Security validation
    std::string archiveStr = archivePath.string();
    if (archiveStr.find('\'') != std::string::npos ||
        archiveStr.find('"') != std::string::npos ||
        archiveStr.find('$') != std::string::npos ||
        archiveStr.find('`') != std::string::npos ||
        archiveStr.find(';') != std::string::npos ||
        archiveStr.find('|') != std::string::npos ||
        archiveStr.find('&') != std::string::npos) {
        std::cerr << "[CalibrationInjector] Security: Invalid characters in archive path" << std::endl;
        return std::nullopt;
    }
    
    if (tempDir.find('\'') != std::string::npos ||
        tempDir.find('"') != std::string::npos) {
        std::cerr << "[CalibrationInjector] Security: Invalid characters in temp dir" << std::endl;
        return std::nullopt;
    }
    
    // Extract archive
    std::stringstream cmd;
    cmd << "tar -xzf '" << archiveStr << "' -C '" << tempDir << "' --wildcards '*/camera_calibration_short.xml' 2>&1";
    
    std::cout << "[CalibrationInjector] Extracting: " << cmd.str() << std::endl;
    
    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
        std::cerr << "[CalibrationInjector] Failed to execute tar command" << std::endl;
        return std::nullopt;
    }
    
    char buffer[256];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    
    int status = pclose(pipe);
    if (status != 0) {
        std::cerr << "[CalibrationInjector] Tar extraction failed: " << output << std::endl;
        return std::nullopt;
    }
    
    // Find extracted calibration file
    for (const auto& entry : std::filesystem::recursive_directory_iterator(tempDir)) {
        if (entry.is_regular_file()) {
            std::string filename = entry.path().filename().string();
            if (filename == "camera_calibration_short.xml") {
                std::cout << "[CalibrationInjector] Found calibration file: " << entry.path() << std::endl;
                return entry.path().string();
            }
        }
    }
    
    std::cerr << "[CalibrationInjector] No calibration file found in archive" << std::endl;
    return std::nullopt;
}

} // namespace lbt
