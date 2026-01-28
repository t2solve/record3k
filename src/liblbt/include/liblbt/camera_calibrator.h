#pragma once

#include <opencv2/core.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/objdetect/charuco_detector.hpp>
#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <sstream>

namespace lbt {

/**
 * CameraCalibrator - Performs camera calibration using recorded frame sequences
 * 
 * Based on OpenCV's calibration sample, this class:
 * - Loads images from a recording's frames directory
 * - Detects calibration pattern (chessboard, circles, or ChArUco)
 * - Computes camera intrinsics and distortion coefficients
 * - Saves calibration results to YAML/XML file
 */
class CameraCalibrator {
public:
    enum class Pattern {
        CHESSBOARD,
        CIRCLES_GRID,
        ASYMMETRIC_CIRCLES_GRID,
        CHARUCO_BOARD
    };
    
    struct Config {
        // Board configuration
        cv::Size boardSize{4, 5};              // Inner corners for chessboard (width x height)
        float squareSize = 0.025f;              // Size of squares in meters (or any unit)
        float markerSize = 0.5f;                // Marker size for ChArUco (relative to square)
        Pattern pattern = Pattern::CHESSBOARD;
        
        // ChArUco specific
        int arucoDict = cv::aruco::DICT_4X4_50;
        std::optional<std::string> arucoDictFile;  // Custom dictionary file
        
        // Calibration flags
        bool fixAspectRatio = false;
        float aspectRatio = 1.0f;
        bool fixPrincipalPoint = false;
        bool zeroTangentDist = false;
        bool useIntrinsicGuess = false;
        bool fixK3 = true;  // Most cameras don't need K3
        
        // Optional initial intrinsics
        std::optional<cv::Mat> initialCameraMatrix;
        
        // Output options
        bool writeExtrinsics = true;
        bool writePoints = true;
        bool writeGrid = false;
        
        // Processing
        int cornerSubPixWinSize = 11;
        float gridWidth = -1.0f;  // For board imperfection correction
        bool releaseObject = false;
    };
    
    struct CalibrationResult {
        bool success = false;
        double avgReprojectionError = 0.0;
        cv::Mat cameraMatrix;
        cv::Mat distCoeffs;
        std::vector<cv::Mat> rvecs;
        std::vector<cv::Mat> tvecs;
        std::vector<float> perViewErrors;
        std::vector<cv::Point3f> objectPoints;
        cv::Size imageSize;
        int validFrames = 0;
        std::string errorMessage;
        
        std::string toString() const;
    };
    
    /**
     * Default constructor with default configuration
     */
    CameraCalibrator();
    
    /**
     * Constructor
     * @param config Calibration configuration
     */
    explicit CameraCalibrator(Config config);
    
    /**
     * Run calibration on a recorded session
     * @param recordDir Path to recording directory (e.g., /tmp/record3k/file-rec-cal-123/)
     * @param outputFilenameFull Full output YAML/XML filename with all calibration data (e.g., "camera.yml")
     * @param outputFilenameCompact Compact output YAML/XML filename with only essential parameters (e.g., "camera_compact.yml")
     * @return Calibration result with success status and parameters
     */
    CalibrationResult calibrate(const std::filesystem::path& recordDir,
                               const std::string& outputFilenameFull = "camera.yml",
                               const std::string& outputFilenameCompact = "camera_compact.yml");
    
    /**
     * Load images from frames directory
     * @param framesDir Path to frames directory
     * @return List of image file paths
     */
    static std::vector<std::string> loadImageList(const std::filesystem::path& framesDir);
    
private:
    Config m_config;
    
    // Pattern detection
    bool detectPattern(const cv::Mat& image, std::vector<cv::Point2f>& corners);
    bool detectChessboard(const cv::Mat& image, std::vector<cv::Point2f>& corners);
    bool detectCircles(const cv::Mat& image, std::vector<cv::Point2f>& corners);
    bool detectCharuco(const cv::Mat& image, std::vector<cv::Point2f>& corners);
    
    // Calibration computation
    void calcBoardCorners(std::vector<cv::Point3f>& corners);
    
    // Save methods
    void saveCompactCameraParams(
        const std::string& filename,
        const cv::Mat& cameraMatrix,
        const cv::Mat& distCoeffs,
        const cv::Mat& extrinsicParameters,
        double avgReprojectionError);
    double computeReprojectionErrors(
        const std::vector<std::vector<cv::Point3f>>& objectPoints,
        const std::vector<std::vector<cv::Point2f>>& imagePoints,
        const std::vector<cv::Mat>& rvecs,
        const std::vector<cv::Mat>& tvecs,
        const cv::Mat& cameraMatrix,
        const cv::Mat& distCoeffs,
        std::vector<float>& perViewErrors);
    
    bool runCalibration(
        const std::vector<std::vector<cv::Point2f>>& imagePoints,
        cv::Size imageSize,
        cv::Mat& cameraMatrix,
        cv::Mat& distCoeffs,
        std::vector<cv::Mat>& rvecs,
        std::vector<cv::Mat>& tvecs,
        std::vector<float>& reprojErrs,
        std::vector<cv::Point3f>& newObjPoints,
        double& totalAvgErr);
    
    // Output
    void saveCameraParams(
        const std::string& filename,
        cv::Size imageSize,
        const cv::Mat& cameraMatrix,
        const cv::Mat& distCoeffs,
        const std::vector<cv::Mat>& rvecs,
        const std::vector<cv::Mat>& tvecs,
        const std::vector<float>& reprojErrs,
        const std::vector<std::vector<cv::Point2f>>& imagePoints,
        const std::vector<cv::Point3f>& newObjPoints,
        double totalAvgErr);
    
    // ChArUco board and detector
    std::shared_ptr<cv::aruco::CharucoBoard> m_charucoBoard;
    std::shared_ptr<cv::aruco::CharucoDetector> m_charucoDetector;
};

} // namespace lbt
