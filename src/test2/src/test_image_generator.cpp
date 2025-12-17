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
        // Create base image
        cv::Mat image = cv::Mat::zeros(height, width, CV_8UC3);
        
        // Create a simple background
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int intensity = static_cast<int>(50 + 30 * sin(x * 0.01) * cos(y * 0.01));
                image.at<cv::Vec3b>(y, x) = cv::Vec3b(intensity, intensity, intensity);
            }
        }
        
        // Add moving objects
        int objectX = (frameNumber * 10) % width;
        int objectY = height / 2 + static_cast<int>(50 * sin(frameNumber * 0.1));
        
        // Moving circle
        cv::circle(image, cv::Point(objectX, objectY), 30, cv::Scalar(0, 255, 0), -1);
        
        // Moving rectangle
        int rectX = ((frameNumber * 8) + 200) % width;
        int rectY = height / 3;
        cv::rectangle(image, cv::Rect(rectX, rectY, 40, 60), cv::Scalar(255, 0, 0), -1);
        
        // Add some noise - FIX: Initialize noise matrix first
        cv::Mat noise(image.size(), image.type());
        cv::randu(noise, cv::Scalar::all(0), cv::Scalar::all(10));
        image += noise;
        
        return image;
    }
};