#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <opencv2/opencv.hpp>
#include <opencv2/imgcodecs.hpp>
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
    
    static cv::Mat generateMovingObjectSequence(int frameNumber, int width = 640, int height = 480) {
        cv::Mat image = cv::Mat::zeros(height, width, CV_8UC3);
        
        // Static background
        image.setTo(cv::Scalar(50, 50, 50));
        
        // Add some static objects
        cv::rectangle(image, cv::Rect(50, 50, 100, 100), cv::Scalar(100, 100, 100), -1);
        cv::rectangle(image, cv::Rect(width-150, height-150, 100, 100), cv::Scalar(80, 80, 80), -1);
        
        // Moving object
        int x = (frameNumber * 3) % (width - 50);
        int y = height/2 + 30 * sin(frameNumber * 0.1);
        cv::circle(image, cv::Point(x, y), 25, cv::Scalar(0, 255, 0), -1);
        
        // Add noise
        cv::Mat noise;
        cv::randn(noise, cv::Scalar::all(0), cv::Scalar::all(5));
        image += noise;
        
        return image;
    }
};