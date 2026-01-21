#pragma once
#include "iframe_source.h"
#include "frame_converter.h"
#include <opencv2/opencv.hpp>
#include <string>
#include <memory>

/**
 * @brief Frame source using OpenCV's VideoCapture
 * Supports camera devices (by index) or video files
 */
class OpenCVFrameSource : public IFrameSource {
public:
    /**
     * @brief Construct from camera index
     * @param cameraIndex Camera device index (0 = default camera)
     */
    explicit OpenCVFrameSource(int cameraIndex = 0);
    
    /**
     * @brief Construct from video file path
     * @param filePath Path to video file
     */
    explicit OpenCVFrameSource(const std::string& filePath);
    
    /**
     * @brief Construct with camera index and resolution
     * @param cameraIndex Camera device index
     * @param width Desired frame width
     * @param height Desired frame height
     * @param fps Desired frames per second (0 = use default)
     */
    OpenCVFrameSource(int cameraIndex, int width, int height, double fps = 0.0);
    
    virtual ~OpenCVFrameSource() override;
    
    /**
     * @brief Get next frame from the capture source
     * @return Frame as FrameMemoryObject, or nullptr if no frame available
     */
    std::unique_ptr<FrameMemoryObject> nextFrame() override;
    
    /**
     * @brief Check if the capture source is ready
     * @return true if capture is opened and ready
     */
    bool isReady() const override;
    
    /**
     * @brief Get the actual frame width
     */
    int getFrameWidth() const;
    
    /**
     * @brief Get the actual frame height
     */
    int getFrameHeight() const;
    
    /**
     * @brief Get the actual FPS
     */
    double getFPS() const;
    
    /**
     * @brief Get total frame count (for video files, -1 for cameras)
     */
    int getTotalFrames() const;
    
    /**
     * @brief Get current frame position (for video files)
     */
    int getCurrentFramePosition() const;
    
    /**
     * @brief Set frame position (for video files)
     * @param frameNumber Frame number to seek to
     * @return true if successful
     */
    bool setFramePosition(int frameNumber);
    
    /**
     * @brief Check if this is a camera source (vs file)
     */
    bool isCameraSource() const { return isCameraSource_; }

private:
    cv::VideoCapture capture_;
    bool isCameraSource_;
    int frameCounter_;
};
