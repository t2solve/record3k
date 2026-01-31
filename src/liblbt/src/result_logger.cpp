#include <liblbt/result_logger.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace lbt {

ResultLogger::ResultLogger(const Config& config)
    : m_config(config)
{
    initializeDirectories();
    openLogFiles();
}

ResultLogger::~ResultLogger() {
    if (m_metadataLog.is_open()) {
        m_metadataLog.close();
    }
}

void ResultLogger::initializeDirectories() {
    if (m_config.mode == LogMode::NONE) {
        return;
    }

    try {
        // Create debug directory if in DEBUG mode
        if (m_config.mode == LogMode::DEBUG) {
            m_debugDir = m_config.othersDir / "debug";
            std::filesystem::create_directories(m_debugDir);
            std::cout << "[ResultLogger] Created debug directory: " << m_debugDir << std::endl;
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "[ResultLogger] Failed to create directories: " << e.what() << std::endl;
    }
}

void ResultLogger::openLogFiles() {
    if (m_config.mode == LogMode::NONE || !m_config.saveMetadata) {
        return;
    }

    try {
        std::filesystem::path metadataPath = m_config.othersDir / "metadata_log.txt";
        m_metadataLog.open(metadataPath.string(), std::ios::out | std::ios::trunc);
        
        if (m_metadataLog.is_open()) {
            // Write header
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            std::tm tm_buf;
            gmtime_r(&time_t, &tm_buf);
            
            m_metadataLog << "# ResultLogger Metadata Log\n";
            m_metadataLog << "# Started: " << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ") << "\n";
            m_metadataLog << "# Mode: " << (m_config.mode == LogMode::DEBUG ? "DEBUG" : "PRODUCTION") << "\n";
            m_metadataLog << "#\n";
            m_metadataLog << "# Format: [FrameIndex] Timestamp(ns) | Metadata\n";
            m_metadataLog << "#" << std::string(70, '-') << "\n";
            m_metadataLog.flush();
            
            std::cout << "[ResultLogger] Opened metadata log: " << metadataPath << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[ResultLogger] Failed to open log files: " << e.what() << std::endl;
    }
}

void ResultLogger::logFrame(int frameIndex,
                            const FrameMemoryObject& inputFrame,
                            const FrameMemoryObject& resultFrame) {
    if (m_config.mode == LogMode::NONE) {
        return;
    }

    try {
        // Note: Input frame saving is now handled by FrameWriter in the pipeline
        // when DEBUG mode is enabled. This avoids duplication.
        
        // Log metadata from result frame
        if (m_config.saveMetadata) {
            logMetadata(frameIndex, resultFrame);
        }

        // Save debug images based on metadata
        if (m_config.mode == LogMode::DEBUG && m_config.saveDebugImages) {
            // Check for mass center metadata and create debug visualization
            if (resultFrame.hasMetadata("mass_center_point_pixel")) {
                cv::Point2f massCenter = resultFrame.getMetadata<cv::Point2f>("mass_center_point_pixel");
                
                if (massCenter.x >= 0 && massCenter.y >= 0) {
                    // Draw mass center on input image
                    cv::Mat debugImage = inputFrame.getCpuMat().clone();
                    cv::circle(debugImage, massCenter, 10, cv::Scalar(0, 0, 255), -1);
                    
                    // Draw crosshair
                    cv::line(debugImage, 
                            cv::Point(massCenter.x - 20, massCenter.y),
                            cv::Point(massCenter.x + 20, massCenter.y),
                            cv::Scalar(0, 255, 0), 2);
                    cv::line(debugImage,
                            cv::Point(massCenter.x, massCenter.y - 20),
                            cv::Point(massCenter.x, massCenter.y + 20),
                            cv::Scalar(0, 255, 0), 2);
                    
                    // Add text with coordinates
                    std::stringstream text;
                    text << "(" << std::fixed << std::setprecision(1) 
                         << massCenter.x << ", " << massCenter.y << ")";
                    cv::putText(debugImage, text.str(),
                               cv::Point(massCenter.x + 15, massCenter.y - 15),
                               cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 0), 2);
                    
                    // Save debug image
                    std::stringstream debugPath;
                    debugPath << m_debugDir.string() << "/mass_center_" 
                             << std::setfill('0') << std::setw(6) << frameIndex << ".jpg";
                    cv::imwrite(debugPath.str(), debugImage);
                }
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "[ResultLogger] Error logging frame " << frameIndex << ": " << e.what() << std::endl;
    }
}

void ResultLogger::logMetadata(int frameIndex, const FrameMemoryObject& frame) {
    if (!m_metadataLog.is_open()) {
        return;
    }

    try {
        std::stringstream entry;
        entry << "[" << std::setfill('0') << std::setw(6) << frameIndex << "] ";
        
        // Log timestamp if available
        if (frame.hasTimestamp()) {
            entry << "TS:" << frame.timestampNs() << "ns | ";
        } else {
            entry << "TS:none | ";
        }

        // Log known metadata keys
        bool hasMetadata = false;
        
        if (frame.hasMetadata("mass_center_point_pixel")) {
            cv::Point2f massCenter = frame.getMetadata<cv::Point2f>("mass_center_point_pixel");
            entry << "mass_center_pixel=(" << massCenter.x << "," << massCenter.y << ") ";
            hasMetadata = true;
        }
        
        if (frame.hasMetadata("mass_center_point_world")) {
            cv::Point2d worldPt = frame.getMetadata<cv::Point2d>("mass_center_point_world");
            entry << "mass_center_world_mm=(" << std::fixed << std::setprecision(2) 
                  << worldPt.x << "," << worldPt.y << ") ";
            hasMetadata = true;
        }
        
        if (!hasMetadata) {
            entry << "(no metadata)";
        }

        m_metadataLog << entry.str() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "[ResultLogger] Error logging metadata for frame " << frameIndex 
                  << ": " << e.what() << std::endl;
    }
}

void ResultLogger::writeSummary(int totalFrames, double duration) {
    if (m_config.mode == LogMode::NONE) {
        return;
    }

    // Get current time for summary and log closure
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    gmtime_r(&time_t, &tm_buf);

    try {
        std::filesystem::path summaryPath = m_config.othersDir / "result_logger_summary.txt";
        std::ofstream summary(summaryPath.string());
        
        if (summary.is_open()) {
            
            summary << "ResultLogger Summary\n";
            summary << "===================\n\n";
            summary << "Completed: " << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ") << "\n";
            summary << "Mode: " << (m_config.mode == LogMode::DEBUG ? "DEBUG" : "PRODUCTION") << "\n";
            summary << "Total Frames: " << totalFrames << "\n";
            summary << "Duration: " << std::fixed << std::setprecision(3) << duration << "s\n";
            summary << "Average FPS: " << std::setprecision(2) << (totalFrames / duration) << "\n\n";
            
            summary << "Configuration:\n";
            summary << "  - Save Metadata: " << (m_config.saveMetadata ? "yes" : "no") << "\n";
            summary << "  - Save Debug Images: " << (m_config.saveDebugImages ? "yes" : "no") << "\n";
            summary << "  - Record Dir: " << m_config.recordDir << "\n";
            
            summary.close();
            std::cout << "[ResultLogger] Wrote summary: " << summaryPath << std::endl;
        }
        
        // Close metadata log if open
        if (m_metadataLog.is_open()) {
            m_metadataLog << "\n# Log completed at " 
                         << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ") << "\n";
            m_metadataLog << "# Total frames: " << totalFrames << "\n";
            m_metadataLog.flush();
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[ResultLogger] Error writing summary: " << e.what() << std::endl;
    }
}

void ResultLogger::saveFrameImage(const std::filesystem::path& path, const FrameMemoryObject& frame) {
    if (frame.isEmpty()) {
        return;
    }

    try {
        cv::Mat image = frame.getCpuMat();
        cv::imwrite(path.string(), image);
    } catch (const std::exception& e) {
        std::cerr << "[ResultLogger] Failed to save frame image: " << path << " - " << e.what() << std::endl;
    }
}

std::string ResultLogger::metadataToString(const FrameMemoryObject& frame) {
    std::stringstream ss;
    
    // This is a helper for future expansion
    // Currently metadata is logged in logMetadata() method
    
    return ss.str();
}

} // namespace lbt
