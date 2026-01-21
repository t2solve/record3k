#include <liblbt/opencv_frame_source.h>
#include <liblbt/frame_converter.h>
#include <iostream>

void OpenCVFrameSource::listAvailableCameras() {
    std::cout << "[OpenCVFrameSource] === Listing Available Cameras ===" << std::endl;
    
    int maxCamerasToCheck = 10;  // Check indices 0-9
    int foundCount = 0;
    
    for (int i = 0; i < maxCamerasToCheck; i++) {
        cv::VideoCapture testCapture;
        std::cout << "[OpenCVFrameSource] Testing camera index " << i << "..." << std::endl;
        
        // Try to open the camera
        bool opened = testCapture.open(i);
        
        if (opened && testCapture.isOpened()) {
            foundCount++;
            std::cout << "[OpenCVFrameSource] ✓ Camera " << i << " found:" << std::endl;
            
            // Get properties
            int width = static_cast<int>(testCapture.get(cv::CAP_PROP_FRAME_WIDTH));
            int height = static_cast<int>(testCapture.get(cv::CAP_PROP_FRAME_HEIGHT));
            double fps = testCapture.get(cv::CAP_PROP_FPS);
            int backend = static_cast<int>(testCapture.get(cv::CAP_PROP_BACKEND));
            int fourcc = static_cast<int>(testCapture.get(cv::CAP_PROP_FOURCC));
            
            std::cout << "[OpenCVFrameSource]   Resolution: " << width << "x" << height << std::endl;
            std::cout << "[OpenCVFrameSource]   FPS: " << fps << std::endl;
            std::cout << "[OpenCVFrameSource]   Backend: " << backend << std::endl;
            
            // Try to get a frame to verify it really works
            cv::Mat testFrame;
            bool canRead = testCapture.read(testFrame);
            if (canRead && !testFrame.empty()) {
                std::cout << "[OpenCVFrameSource]   Status: Working (captured test frame)" << std::endl;
            } else {
                std::cout << "[OpenCVFrameSource]   Status: Opened but failed to capture frame" << std::endl;
            }
            
            testCapture.release();
        } else {
            std::cout << "[OpenCVFrameSource]   (not available)" << std::endl;
        }
    }
    
    std::cout << "[OpenCVFrameSource] === Found " << foundCount << " camera(s) ===" << std::endl;
    
}

OpenCVFrameSource::OpenCVFrameSource(int cameraIndex)
    : isCameraSource_(true), frameCounter_(0)
{
    std::cout << "[OpenCVFrameSource] Opening camera index: " << cameraIndex << std::endl;
    capture_.open(cameraIndex);
    
    if (!capture_.isOpened()) {
        std::cerr << "[OpenCVFrameSource] ERROR: Failed to open camera " << cameraIndex << std::endl;
        std::cerr << "[OpenCVFrameSource] Listing all available cameras for debugging:" << std::endl;
        listAvailableCameras();
    } else {
        std::cout << "[OpenCVFrameSource] Camera opened successfully" << std::endl;
        std::cout << "[OpenCVFrameSource]   Resolution: " << getFrameWidth() << "x" << getFrameHeight() << std::endl;
        std::cout << "[OpenCVFrameSource]   FPS: " << getFPS() << std::endl;
    }
}

OpenCVFrameSource::OpenCVFrameSource(const std::string& filePath)
    : isCameraSource_(false), frameCounter_(0)
{
    // Check if this is a device path (like /dev/video0) - treat as camera
    bool isDevicePath = (filePath.find("/dev/video") == 0);
    
    if (isDevicePath) {
        isCameraSource_ = true;
        std::cout << "[OpenCVFrameSource] Opening camera device: " << filePath << std::endl;
    } else {
        std::cout << "[OpenCVFrameSource] Opening video file: " << filePath << std::endl;
    }
    
    capture_.open(filePath);
    
    if (!capture_.isOpened()) {
        if (isDevicePath) {
            std::cerr << "[OpenCVFrameSource] ERROR: Failed to open camera device: " << filePath << std::endl;
            std::cerr << "[OpenCVFrameSource] Listing all available cameras for debugging:" << std::endl;
            listAvailableCameras();
        } else {
            std::cerr << "[OpenCVFrameSource] ERROR: Failed to open video file: " << filePath << std::endl;
        }
    } else {
        if (isDevicePath) {
            std::cout << "[OpenCVFrameSource] Camera device opened successfully" << std::endl;
        } else {
            std::cout << "[OpenCVFrameSource] Video file opened successfully" << std::endl;
        }
        std::cout << "[OpenCVFrameSource]   Resolution: " << getFrameWidth() << "x" << getFrameHeight() << std::endl;
        std::cout << "[OpenCVFrameSource]   FPS: " << getFPS() << std::endl;
        std::cout << "[OpenCVFrameSource]   Total frames: " << getTotalFrames() << std::endl;
    }
}

OpenCVFrameSource::OpenCVFrameSource(int cameraIndex, int width, int height, double fps)
    : isCameraSource_(true), frameCounter_(0)
{
    std::cout << "[OpenCVFrameSource] Opening camera " << cameraIndex 
              << " with resolution " << width << "x" << height;
    if (fps > 0) {
        std::cout << " at " << fps << " FPS";
    }
    std::cout << std::endl;
    
    capture_.open(cameraIndex);
    
    if (!capture_.isOpened()) {
        std::cerr << "[OpenCVFrameSource] ERROR: Failed to open camera " << cameraIndex << std::endl;
        return;
    }
    
    // Set resolution
    capture_.set(cv::CAP_PROP_FRAME_WIDTH, width);
    capture_.set(cv::CAP_PROP_FRAME_HEIGHT, height);
    
    // Set FPS if specified
    if (fps > 0) {
        capture_.set(cv::CAP_PROP_FPS, fps);
    }
    
    std::cout << "[OpenCVFrameSource] Camera opened successfully" << std::endl;
    std::cout << "[OpenCVFrameSource]   Requested: " << width << "x" << height;
    if (fps > 0) std::cout << " @ " << fps << " FPS";
    std::cout << std::endl;
    std::cout << "[OpenCVFrameSource]   Actual: " << getFrameWidth() << "x" << getFrameHeight() 
              << " @ " << getFPS() << " FPS" << std::endl;
}

OpenCVFrameSource::~OpenCVFrameSource()
{
    if (capture_.isOpened()) {
        std::cout << "[OpenCVFrameSource] Closing capture (captured " << frameCounter_ << " frames)" << std::endl;
        capture_.release();
    }
}

std::unique_ptr<FrameMemoryObject> OpenCVFrameSource::nextFrame()
{
    if (!capture_.isOpened()) {
        std::cerr << "[OpenCVFrameSource] ERROR: Capture not opened" << std::endl;
        return nullptr;
    }
    
    cv::Mat frame;
    bool success = capture_.read(frame);
    
    if (!success || frame.empty()) {
        if (isCameraSource_) {
            std::cerr << "[OpenCVFrameSource] ERROR: Failed to capture frame from camera" << std::endl;
        } else {
            std::cout << "[OpenCVFrameSource] End of video file reached" << std::endl;
        }
        return nullptr;
    }
    
    frameCounter_++;
    
    // Create FrameMemoryObject from the captured frame
    FrameMemoryObject frameObj = FrameConverter::convertMatToMemoryObject(frame);
    frameObj.touchTimestampNow();//set timetstamp to now
    
    // Add metadata
    frameObj.setMetadata("frame_number", frameCounter_);
    frameObj.setMetadata("source_type", isCameraSource_ ? "camera" : "file");
    
    if (!isCameraSource_) {
        frameObj.setMetadata("position_ms", static_cast<int>(capture_.get(cv::CAP_PROP_POS_MSEC)));
    }

    
    return std::make_unique<FrameMemoryObject>(std::move(frameObj));
}

bool OpenCVFrameSource::isReady() const
{
    return capture_.isOpened();
}

int OpenCVFrameSource::getFrameWidth() const
{
    if (!capture_.isOpened()) return -1;
    return static_cast<int>(capture_.get(cv::CAP_PROP_FRAME_WIDTH));
}

int OpenCVFrameSource::getFrameHeight() const
{
    if (!capture_.isOpened()) return -1;
    return static_cast<int>(capture_.get(cv::CAP_PROP_FRAME_HEIGHT));
}

double OpenCVFrameSource::getFPS() const
{
    if (!capture_.isOpened()) return -1.0;
    return capture_.get(cv::CAP_PROP_FPS);
}

int OpenCVFrameSource::getTotalFrames() const
{
    if (!capture_.isOpened() || isCameraSource_) return -1;
    return static_cast<int>(capture_.get(cv::CAP_PROP_FRAME_COUNT));
}

int OpenCVFrameSource::getCurrentFramePosition() const
{
    if (!capture_.isOpened()) return -1;
    return static_cast<int>(capture_.get(cv::CAP_PROP_POS_FRAMES));
}

bool OpenCVFrameSource::setFramePosition(int frameNumber)
{
    if (!capture_.isOpened() || isCameraSource_) {
        std::cerr << "[OpenCVFrameSource] ERROR: Cannot seek in camera source" << std::endl;
        return false;
    }
    
    bool success = capture_.set(cv::CAP_PROP_POS_FRAMES, frameNumber);
    if (success) {
        std::cout << "[OpenCVFrameSource] Seeked to frame " << frameNumber << std::endl;
    } else {
        std::cerr << "[OpenCVFrameSource] ERROR: Failed to seek to frame " << frameNumber << std::endl;
    }
    return success;
}
