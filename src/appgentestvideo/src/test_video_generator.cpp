#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <opencv2/opencv.hpp>
#include <opencv2/imgcodecs.hpp>
#include <thread>
#include <chrono>
#include <vector>
#include <memory>

class TestImageGenerator {
public:
    static cv::Mat generateTestImage(int width = 640, int height = 480) {
        cv::Mat image = cv::Mat::zeros(height, width, CV_8UC3);
        
        // Create a colorful test pattern
        
        // Background gradient
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int blue = static_cast<int>(255.0 * x / width);
                int green = static_cast<int>(255.0 * y / height);
                int red = static_cast<int>(255.0 * (x + y) / (width + height));
                
                image.at<cv::Vec3b>(y, x) = cv::Vec3b(blue, green, red);
            }
        }
        
        // Add some geometric shapes
        
        // Circle
        cv::circle(image, cv::Point(width/4, height/4), 50, cv::Scalar(255, 255, 255), -1);
        cv::circle(image, cv::Point(width/4, height/4), 30, cv::Scalar(0, 0, 0), -1);
        
        // Rectangle
        cv::rectangle(image, cv::Rect(width/2, height/4, 100, 80), cv::Scalar(255, 0, 0), -1);
        
        // Triangle
        std::vector<cv::Point> triangle = {
            cv::Point(3*width/4, height/4),
            cv::Point(3*width/4 - 50, height/4 + 80),
            cv::Point(3*width/4 + 50, height/4 + 80)
        };
        cv::fillPoly(image, std::vector<std::vector<cv::Point>>{triangle}, cv::Scalar(0, 255, 0));
        
        // Add some text
        cv::putText(image, "OpenCV Pipeline Test", cv::Point(width/4, 3*height/4), 
                   cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(255, 255, 255), 2);
        
        // Add some noise
        cv::Mat noise(image.size(), image.type());
        cv::randn(noise, cv::Scalar::all(0), cv::Scalar::all(10));
        image += noise;

        return image;
    }

    static bool generateTestSequence(int numFrames, double fps, const std::string& outputDir = "/tmp/test_frames") {
        // Create output directory if it doesn't exist
        QDir dir(QString::fromStdString(outputDir));
        if (!dir.exists()) {
            if (!dir.mkpath(".")) {
                qDebug() << "Failed to create output directory:" << QString::fromStdString(outputDir);
                return false;
            }
        }

        // Calculate frame duration in milliseconds
        double frameDurationMs = 1000.0 / fps;
        
        // Get start time
        auto startTime = std::chrono::high_resolution_clock::now();
        
        // Generate frames
        for (int i = 0; i < numFrames; ++i) {
            // Calculate current timestamp in milliseconds since start
            auto currentTime = std::chrono::high_resolution_clock::now();
            auto timeSpan = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime);
            int64_t timestampMs = timeSpan.count();
            
            // Generate frame
            cv::Mat frame = generateMovingObjectSequence(i);
            
            // Create filename with frame number and timestamp
            std::string filename = dir.filePath(
                QString("frame_%05d_%lld.jpg").arg(i).arg(timestampMs)
            ).toStdString();
            
            // Save frame
            if (!cv::imwrite(filename, frame)) {
                qDebug() << "Failed to write frame:" << QString::fromStdString(filename);
                return false;
            }
            
            // Optional: Simulate real-time frame generation
            std::this_thread::sleep_for(std::chrono::milliseconds(
                static_cast<int>(frameDurationMs)
            ));
            
            // Log progress
            if (i % 10 == 0) {
                qDebug() << "Generated frame" << i << "at timestamp" << timestampMs << "ms";
            }
        }
        
        qDebug() << "Generated" << numFrames << "frames at" << fps << "FPS";
        return true;
    }

    static cv::Mat generateMovingObjectSequence(int frameNumber, int width = 640, int height = 480) {
        // Create base image with more dynamic background
        cv::Mat image = cv::Mat::zeros(height, width, CV_8UC3);
        
        // Create a more complex background pattern
        double time = frameNumber * 0.05;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                double dx = x - width/2;
                double dy = y - height/2;
                double distance = sqrt(dx*dx + dy*dy);
                int blue = static_cast<int>(128 + 127 * sin(distance * 0.05 + time));
                int green = static_cast<int>(128 + 127 * cos(x * 0.02 + time));
                int red = static_cast<int>(128 + 127 * sin(y * 0.02 - time));
                image.at<cv::Vec3b>(y, x) = cv::Vec3b(blue, green, red);
            }
        }
        
        // Add multiple moving objects
        
        // Moving circle with oscillating size
        int circleSize = 20 + static_cast<int>(10 * sin(frameNumber * 0.1));
        int objectX = (frameNumber * 5) % width;
        int objectY = height/2 + static_cast<int>(100 * sin(frameNumber * 0.05));
        cv::circle(image, cv::Point(objectX, objectY), circleSize, 
                  cv::Scalar(0, 255, 0), -1);
        
        // Moving rectangle with rotation
        int rectX = ((frameNumber * 7) + width/2) % width;
        int rectY = height/3 + static_cast<int>(50 * cos(frameNumber * 0.08));
        cv::Point2f center(rectX + 20, rectY + 30);
        cv::Mat rot = cv::getRotationMatrix2D(center, frameNumber * 5.0, 1.0);
        cv::Mat rotatedRect;
        cv::Mat rectMask = cv::Mat::zeros(height, width, CV_8UC3);
        cv::rectangle(rectMask, cv::Rect(rectX, rectY, 40, 60), 
                     cv::Scalar(255, 0, 0), -1);
        cv::warpAffine(rectMask, rotatedRect, rot, image.size());
        image = image + rotatedRect;
        
        // Add bouncing triangle
        int triangleY = height/2 + static_cast<int>(150 * abs(sin(frameNumber * 0.03)));
        std::vector<cv::Point> triangle = {
            cv::Point(3*width/4, triangleY),
            cv::Point(3*width/4 - 30, triangleY + 50),
            cv::Point(3*width/4 + 30, triangleY + 50)
        };
        cv::fillPoly(image, std::vector<std::vector<cv::Point>>{triangle}, 
                    cv::Scalar(0, 255, 255));
        
        // Add frame counter and timestamp
        cv::putText(image, "Frame: " + std::to_string(frameNumber), 
                   cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1.0, 
                   cv::Scalar(255, 255, 255), 2);
        
        // Add some noise
        cv::Mat noise(image.size(), image.type());
        cv::randn(noise, cv::Scalar::all(0), cv::Scalar::all(5));
        image += noise;
        
        return image;
    }
};