#include <liblbt/camera_calibrator.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/calib3d.hpp>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <sstream>
#include <ctime>

namespace lbt {

std::string CameraCalibrator::CalibrationResult::toString() const {
    std::ostringstream oss;
    oss << "CalibrationResult{";
    oss << "success=" << (success ? "true" : "false");
    if (success) {
        oss << ", avgReprojectionError=" << avgReprojectionError;
        oss << ", validFrames=" << validFrames;
        oss << ", imageSize=" << imageSize.width << "x" << imageSize.height;
    } else {
        oss << ", errorMessage=\"" << errorMessage << "\"";
    }
    oss << "}";
    return oss.str();
}

CameraCalibrator::CameraCalibrator()
    : CameraCalibrator(Config{})
{
}

CameraCalibrator::CameraCalibrator(Config config)
    : m_config(std::move(config))
{
    // Initialize ChArUco board if needed
    if (m_config.pattern == Pattern::CHARUCO_BOARD) {
        cv::aruco::Dictionary dictionary;
        
        if (m_config.arucoDictFile) {
            std::cout << "[CameraCalibrator] Loading custom ArUco dictionary from: " 
                      << *m_config.arucoDictFile << std::endl;
            cv::FileStorage dictFile(*m_config.arucoDictFile, cv::FileStorage::READ);
            cv::FileNode fn(dictFile.root());
            dictionary.readDictionary(fn);
        } else {
            std::cout << "[CameraCalibrator] Using predefined ArUco dictionary: " 
                      << m_config.arucoDict << std::endl;
            dictionary = cv::aruco::getPredefinedDictionary(m_config.arucoDict);
        }
        
        m_charucoBoard = std::make_shared<cv::aruco::CharucoBoard>(
            m_config.boardSize,
            m_config.squareSize,
            m_config.markerSize,
            dictionary
        );
        
        m_charucoDetector = std::make_shared<cv::aruco::CharucoDetector>(*m_charucoBoard);
    }
}

std::vector<std::string> CameraCalibrator::loadImageList(const std::filesystem::path& framesDir) {
    std::vector<std::string> imageList;
    
    if (!std::filesystem::exists(framesDir) || !std::filesystem::is_directory(framesDir)) {
        std::cerr << "[CameraCalibrator] Frames directory does not exist: " << framesDir << std::endl;
        return imageList;
    }
    
    // Collect all image files
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(framesDir)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || 
                ext == ".bmp" || ext == ".tiff" || ext == ".tif") {
                paths.push_back(entry.path());
            }
        }
    }
    
    // Sort by filename for consistent ordering
    std::sort(paths.begin(), paths.end());
    
    for (const auto& p : paths) {
        imageList.push_back(p.string());
    }
    
    std::cout << "[CameraCalibrator] Found " << imageList.size() << " images in " << framesDir << std::endl;
    return imageList;
}

CameraCalibrator::CalibrationResult CameraCalibrator::calibrate(
    const std::filesystem::path& recordDir,
    const std::string& outputFilenameFull,
    const std::string& outputFilenameCompact)
{
    CalibrationResult result;
    
    // Load image list from frames directory
    std::filesystem::path framesDir = recordDir / "frames";
    auto imageList = loadImageList(framesDir);
    
    if (imageList.empty()) {
        result.errorMessage = "No images found in frames directory";
        std::cerr << "[CameraCalibrator] " << result.errorMessage << std::endl;
        return result;
    }
    
    // Detect patterns in all images
    std::vector<std::vector<cv::Point2f>> imagePoints;
    cv::Size imageSize;
    
    std::cout << "[CameraCalibrator] Detecting calibration pattern..." << std::endl;
    int detectionCount = 0;
    
    for (size_t i = 0; i < imageList.size(); i++) {
        cv::Mat image = cv::imread(imageList[i], cv::IMREAD_COLOR);
        if (image.empty()) {
            std::cerr << "[CameraCalibrator] Failed to load image: " << imageList[i] << std::endl;
            continue;
        }
        
        if (imageSize.width == 0) {
            imageSize = image.size();
        } else if (image.size() != imageSize) {
            std::cerr << "[CameraCalibrator] Image size mismatch: " << imageList[i] << std::endl;
            continue;
        }
        
        std::vector<cv::Point2f> corners;
        bool found = detectPattern(image, corners);
        
        if (found) {
            imagePoints.push_back(corners);
            detectionCount++;
            
            if (detectionCount % 10 == 0) {
                std::cout << "[CameraCalibrator] Detected pattern in " << detectionCount << " images" << std::endl;
            }
        }
    }
    
    std::cout << "[CameraCalibrator] Pattern detected in " << detectionCount << "/" << imageList.size() << " images" << std::endl;
    
    if (imagePoints.size() < 3) {
        result.errorMessage = "Not enough valid images for calibration (need at least 3)";
        std::cerr << "[CameraCalibrator] " << result.errorMessage << std::endl;
        return result;
    }
    
    result.validFrames = static_cast<int>(imagePoints.size());
    result.imageSize = imageSize;
    
    // Initialize camera matrix
    result.cameraMatrix = cv::Mat::eye(3, 3, CV_64F);
    if (m_config.initialCameraMatrix) {
        result.cameraMatrix = m_config.initialCameraMatrix->clone();
    }
    
    // Run calibration
    std::cout << "[CameraCalibrator] Running calibration..." << std::endl;
    result.success = runCalibration(
        imagePoints,
        imageSize,
        result.cameraMatrix,
        result.distCoeffs,
        result.rvecs,
        result.tvecs,
        result.perViewErrors,
        result.objectPoints,
        result.avgReprojectionError
    );
    
    if (result.success) {
        std::cout << "[CameraCalibrator] Calibration succeeded!" << std::endl;
        std::cout << "[CameraCalibrator] Average reprojection error: " << result.avgReprojectionError << std::endl;
        
        // Save full calibration parameters
        std::filesystem::path outputPathFull = recordDir / outputFilenameFull;
        saveCameraParams(
            outputPathFull.string(),
            imageSize,
            result.cameraMatrix,
            result.distCoeffs,
            m_config.writeExtrinsics ? result.rvecs : std::vector<cv::Mat>(),
            m_config.writeExtrinsics ? result.tvecs : std::vector<cv::Mat>(),
            m_config.writeExtrinsics ? result.perViewErrors : std::vector<float>(),
            m_config.writePoints ? imagePoints : std::vector<std::vector<cv::Point2f>>(),
            m_config.writeGrid ? result.objectPoints : std::vector<cv::Point3f>(),
            result.avgReprojectionError
        );
        
        std::cout << "[CameraCalibrator] Saved full calibration to: " << outputPathFull << std::endl;
        
        // Prepare extrinsic parameters matrix (combine rvecs and tvecs)
        cv::Mat extrinsicParameters;
        if (!result.rvecs.empty() && !result.tvecs.empty()) {
            extrinsicParameters = cv::Mat(static_cast<int>(result.rvecs.size()), 6, result.rvecs[0].type());
            for (size_t i = 0; i < result.rvecs.size(); i++) {
                cv::Mat r = extrinsicParameters(cv::Range(i, i + 1), cv::Range(0, 3));
                cv::Mat t = extrinsicParameters(cv::Range(i, i + 1), cv::Range(3, 6));
                result.rvecs[i].reshape(1, 1).copyTo(r);
                result.tvecs[i].reshape(1, 1).copyTo(t);
            }
        }
        
        // Save compact calibration parameters (only essentials for processing)
        std::filesystem::path outputPathCompact = recordDir / outputFilenameCompact;
        saveCompactCameraParams(
            outputPathCompact.string(),
            result.cameraMatrix,
            result.distCoeffs,
            extrinsicParameters,
            result.avgReprojectionError
        );
        
        std::cout << "[CameraCalibrator] Saved compact calibration to: " << outputPathCompact << std::endl;

     
    } else {
        result.errorMessage = "Calibration failed";
        std::cerr << "[CameraCalibrator] " << result.errorMessage << std::endl;
    }
    
    return result;
}

bool CameraCalibrator::detectPattern(const cv::Mat& image, std::vector<cv::Point2f>& corners) {
    switch (m_config.pattern) {
        case Pattern::CHESSBOARD:
            return detectChessboard(image, corners);
        case Pattern::CIRCLES_GRID:
            return detectCircles(image, corners);
        case Pattern::ASYMMETRIC_CIRCLES_GRID:
            return detectCircles(image, corners);
        case Pattern::CHARUCO_BOARD:
            return detectCharuco(image, corners);
        default:
            return false;
    }
}

bool CameraCalibrator::detectChessboard(const cv::Mat& image, std::vector<cv::Point2f>& corners) {
    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    
    bool found = cv::findChessboardCorners(
        image,
        m_config.boardSize,
        corners,
        cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_FAST_CHECK | cv::CALIB_CB_NORMALIZE_IMAGE
    );
    
    if (found) {
        // Refine corner locations
        cv::cornerSubPix(
            gray,
            corners,
            cv::Size(m_config.cornerSubPixWinSize, m_config.cornerSubPixWinSize),
            cv::Size(-1, -1),
            cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.0001)
        );
    }
    
    return found;
}

bool CameraCalibrator::detectCircles(const cv::Mat& image, std::vector<cv::Point2f>& corners) {
    int flags = (m_config.pattern == Pattern::ASYMMETRIC_CIRCLES_GRID) 
                ? cv::CALIB_CB_ASYMMETRIC_GRID 
                : 0;
    
    return cv::findCirclesGrid(image, m_config.boardSize, corners, flags);
}

bool CameraCalibrator::detectCharuco(const cv::Mat& image, std::vector<cv::Point2f>& corners) {
    if (!m_charucoDetector) {
        std::cerr << "[CameraCalibrator] ChArUco detector not initialized" << std::endl;
        return false;
    }
    
    std::vector<int> markerIds;
    m_charucoDetector->detectBoard(image, corners, markerIds);
    
    // ChArUco needs all inner corners detected
    size_t expectedCorners = (m_config.boardSize.width - 1) * (m_config.boardSize.height - 1);
    return corners.size() == expectedCorners;
}

void CameraCalibrator::calcBoardCorners(std::vector<cv::Point3f>& corners) {
    corners.clear();
    
    switch (m_config.pattern) {
        case Pattern::CHESSBOARD:
        case Pattern::CIRCLES_GRID:
            for (int i = 0; i < m_config.boardSize.height; i++) {
                for (int j = 0; j < m_config.boardSize.width; j++) {
                    corners.push_back(cv::Point3f(
                        float(j * m_config.squareSize),
                        float(i * m_config.squareSize),
                        0.0f
                    ));
                }
            }
            break;
            
        case Pattern::ASYMMETRIC_CIRCLES_GRID:
            for (int i = 0; i < m_config.boardSize.height; i++) {
                for (int j = 0; j < m_config.boardSize.width; j++) {
                    corners.push_back(cv::Point3f(
                        float((2 * j + i % 2) * m_config.squareSize),
                        float(i * m_config.squareSize),
                        0.0f
                    ));
                }
            }
            break;
            
        case Pattern::CHARUCO_BOARD:
            for (int i = 0; i < m_config.boardSize.height - 1; i++) {
                for (int j = 0; j < m_config.boardSize.width - 1; j++) {
                    corners.push_back(cv::Point3f(
                        float(j * m_config.squareSize),
                        float(i * m_config.squareSize),
                        0.0f
                    ));
                }
            }
            break;
    }
}

double CameraCalibrator::computeReprojectionErrors(
    const std::vector<std::vector<cv::Point3f>>& objectPoints,
    const std::vector<std::vector<cv::Point2f>>& imagePoints,
    const std::vector<cv::Mat>& rvecs,
    const std::vector<cv::Mat>& tvecs,
    const cv::Mat& cameraMatrix,
    const cv::Mat& distCoeffs,
    std::vector<float>& perViewErrors)
{
    perViewErrors.resize(objectPoints.size());
    double totalErr = 0.0;
    int totalPoints = 0;
    
    for (size_t i = 0; i < objectPoints.size(); i++) {
        std::vector<cv::Point2f> imagePoints2;
        cv::projectPoints(
            objectPoints[i],
            rvecs[i],
            tvecs[i],
            cameraMatrix,
            distCoeffs,
            imagePoints2
        );
        
        double err = cv::norm(imagePoints[i], imagePoints2, cv::NORM_L2);
        int n = static_cast<int>(objectPoints[i].size());
        perViewErrors[i] = static_cast<float>(std::sqrt(err * err / n));
        totalErr += err * err;
        totalPoints += n;
    }
    
    return std::sqrt(totalErr / totalPoints);
}

bool CameraCalibrator::runCalibration(
    const std::vector<std::vector<cv::Point2f>>& imagePoints,
    cv::Size imageSize,
    cv::Mat& cameraMatrix,
    cv::Mat& distCoeffs,
    std::vector<cv::Mat>& rvecs,
    std::vector<cv::Mat>& tvecs,
    std::vector<float>& reprojErrs,
    std::vector<cv::Point3f>& newObjPoints,
    double& totalAvgErr)
{
    // Build calibration flags
    int flags = 0;
    if (m_config.fixAspectRatio) {
        flags |= cv::CALIB_FIX_ASPECT_RATIO;
        cameraMatrix.at<double>(0, 0) = m_config.aspectRatio;
    }
    if (m_config.fixPrincipalPoint) {
        flags |= cv::CALIB_FIX_PRINCIPAL_POINT;
    }
    if (m_config.zeroTangentDist) {
        flags |= cv::CALIB_ZERO_TANGENT_DIST;
    }
    if (m_config.useIntrinsicGuess) {
        flags |= cv::CALIB_USE_INTRINSIC_GUESS;
    }
    if (m_config.fixK3) {
        flags |= cv::CALIB_FIX_K3;
    }
    
    // Initialize distortion coefficients
    distCoeffs = cv::Mat::zeros(8, 1, CV_64F);
    
    // Calculate object points
    std::vector<cv::Point3f> objectPointsTemplate;
    calcBoardCorners(objectPointsTemplate);
    
    // Apply board imperfection correction if specified
    if (m_config.gridWidth > 0.0f) {
        if (m_config.pattern == Pattern::CHESSBOARD) {
            int offset = m_config.boardSize.width - 1;
            objectPointsTemplate[offset].x = objectPointsTemplate[0].x + m_config.gridWidth;
        } else if (m_config.pattern == Pattern::CHARUCO_BOARD) {
            int offset = m_config.boardSize.width - 2;
            objectPointsTemplate[offset].x = objectPointsTemplate[0].x + m_config.gridWidth;
        }
    }
    
    newObjPoints = objectPointsTemplate;
    
    // Create object points for all views
    std::vector<std::vector<cv::Point3f>> objectPoints(imagePoints.size(), objectPointsTemplate);
    
    // Run calibration
    double rms;
    if (m_config.releaseObject) {
        int iFixedPoint = m_config.boardSize.width - 1;
        rms = cv::calibrateCameraRO(
            objectPoints,
            imagePoints,
            imageSize,
            iFixedPoint,
            cameraMatrix,
            distCoeffs,
            rvecs,
            tvecs,
            newObjPoints,
            flags | cv::CALIB_USE_LU
        );
    } else {
        rms = cv::calibrateCamera(
            objectPoints,
            imagePoints,
            imageSize,
            cameraMatrix,
            distCoeffs,
            rvecs,
            tvecs,
            flags
        );
    }
    
    std::cout << "[CameraCalibrator] RMS error reported by calibrateCamera: " << rms << std::endl;
    
    bool ok = cv::checkRange(cameraMatrix) && cv::checkRange(distCoeffs);
    
    if (ok) {
        // Update object points with refined values
        objectPoints.clear();
        objectPoints.resize(imagePoints.size(), newObjPoints);
        
        // Compute per-view errors
        totalAvgErr = computeReprojectionErrors(
            objectPoints,
            imagePoints,
            rvecs,
            tvecs,
            cameraMatrix,
            distCoeffs,
            reprojErrs
        );
    }
    
    return ok;
}

void CameraCalibrator::saveCameraParams(
    const std::string& filename,
    cv::Size imageSize,
    const cv::Mat& cameraMatrix,
    const cv::Mat& distCoeffs,
    const std::vector<cv::Mat>& rvecs,
    const std::vector<cv::Mat>& tvecs,
    const std::vector<float>& reprojErrs,
    const std::vector<std::vector<cv::Point2f>>& imagePoints,
    const std::vector<cv::Point3f>& newObjPoints,
    double totalAvgErr)
{
    cv::FileStorage fs(filename, cv::FileStorage::WRITE);
    
    // Timestamp
    std::time_t now = std::time(nullptr);
    std::tm* timeinfo = std::localtime(&now);
    char buffer[80];
    std::strftime(buffer, sizeof(buffer), "%c", timeinfo);
    
    fs << "calibration_time" << buffer;
    
    // Frame count
    if (!rvecs.empty() || !reprojErrs.empty()) {
        fs << "nframes" << static_cast<int>(std::max(rvecs.size(), reprojErrs.size()));
    }
    
    // Image and board dimensions
    fs << "image_width" << imageSize.width;
    fs << "image_height" << imageSize.height;
    fs << "board_width" << m_config.boardSize.width;
    fs << "board_height" << m_config.boardSize.height;
    fs << "square_size" << m_config.squareSize;
    
    // Pattern type
    std::string patternType;
    switch (m_config.pattern) {
        case Pattern::CHESSBOARD: patternType = "chessboard"; break;
        case Pattern::CIRCLES_GRID: patternType = "circles"; break;
        case Pattern::ASYMMETRIC_CIRCLES_GRID: patternType = "acircles"; break;
        case Pattern::CHARUCO_BOARD: patternType = "charuco"; break;
    }
    fs << "pattern_type" << patternType;
    
    // Calibration flags
    if (m_config.fixAspectRatio) {
        fs << "aspect_ratio" << m_config.aspectRatio;
    }
    
    // Camera parameters
    fs << "camera_matrix" << cameraMatrix;
    fs << "distortion_coefficients" << distCoeffs;
    
    // Errors
    fs << "avg_reprojection_error" << totalAvgErr;
    if (!reprojErrs.empty()) {
        fs << "per_view_reprojection_errors" << cv::Mat(reprojErrs);
    }
    
    // Extrinsic parameters
    if (!rvecs.empty() && !tvecs.empty()) {
        cv::Mat bigmat(static_cast<int>(rvecs.size()), 6, rvecs[0].type());
        for (size_t i = 0; i < rvecs.size(); i++) {
            cv::Mat r = bigmat(cv::Range(i, i + 1), cv::Range(0, 3));
            cv::Mat t = bigmat(cv::Range(i, i + 1), cv::Range(3, 6));
            rvecs[i].reshape(1, 1).copyTo(r);
            tvecs[i].reshape(1, 1).copyTo(t);
        }
        fs << "extrinsic_parameters" << bigmat;
    }
    
    // Image points
    if (!imagePoints.empty()) {
        cv::Mat imagePtMat(static_cast<int>(imagePoints.size()), 
                          static_cast<int>(imagePoints[0].size()), 
                          CV_32FC2);
        for (size_t i = 0; i < imagePoints.size(); i++) {
            cv::Mat r = imagePtMat.row(i).reshape(2, imagePtMat.cols);
            cv::Mat(imagePoints[i]).copyTo(r);
        }
        fs << "image_points" << imagePtMat;
    }
    
    // Object points (if refined)
    if (!newObjPoints.empty()) {
        fs << "grid_points" << newObjPoints;
    }
    
    fs.release();
}

void CameraCalibrator::saveCompactCameraParams(
    const std::string& filename,
    const cv::Mat& cameraMatrix,
    const cv::Mat& distCoeffs,
    const cv::Mat& extrinsicParameters,
    double avgReprojectionError)
{
    cv::FileStorage fs(filename, cv::FileStorage::WRITE);
    
    // Essential parameters for lens correction and plane projection
    fs << "camera_matrix" << cameraMatrix;
    fs << "distortion_coefficients" << distCoeffs;
    
    // Store extrinsic parameters if available
    if (!extrinsicParameters.empty()) {
        fs << "extrinsic_parameters" << extrinsicParameters;
    }
    
    // Quality metric
    fs << "avg_reprojection_error" << avgReprojectionError;
    
    // Extract individual parameters for easy access
    if (!cameraMatrix.empty() && cameraMatrix.rows == 3 && cameraMatrix.cols == 3) {
        fs << "fx" << cameraMatrix.at<double>(0, 0);
        fs << "fy" << cameraMatrix.at<double>(1, 1);
        fs << "cx" << cameraMatrix.at<double>(0, 2);
        fs << "cy" << cameraMatrix.at<double>(1, 2);
    }
    
    if (!distCoeffs.empty() && distCoeffs.total() >= 5) {
        fs << "k1" << distCoeffs.at<double>(0);
        fs << "k2" << distCoeffs.at<double>(1);
        fs << "p1" << distCoeffs.at<double>(2);
        fs << "p2" << distCoeffs.at<double>(3);
        fs << "k3" << distCoeffs.at<double>(4);
    }
    
    fs.release();
}

} // namespace lbt
