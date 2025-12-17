
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <opencv2/opencv.hpp>
#include <opencv2/imgcodecs.hpp>
#include <chrono>
#include <vector>
#include <memory>

#include "test_video_generator.cpp"


int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    
    // Configuration parameters
    int numFrames = 300;          // Generate 10 seconds at 30 FPS
    double fps = 30.0;            // Standard video frame rate
    std::string outputDir = QDir::currentPath().toStdString() + "/test3/test_frames";
    
    // Print configuration
    qDebug() << "Test Sequence Generator";
    qDebug() << "---------------------";
    qDebug() << "Frames:" << numFrames;
    qDebug() << "FPS:" << fps;
    qDebug() << "Output:" << QString::fromStdString(outputDir);
    qDebug() << "Estimated duration:" << (numFrames / fps) << "seconds";
    qDebug() << "Starting generation...";
    
    // Record start time
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Generate sequence
    bool success = TestImageGenerator::generateTestSequence(numFrames, fps, outputDir);
    
    // Calculate total time
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime);
    
    if (success) {
        qDebug() << "\nSequence generated successfully:";
        qDebug() << "* Total frames:" << numFrames;
        qDebug() << "* Generation time:" << duration.count() << "seconds";
        qDebug() << "* Average generation speed:" 
                 << (static_cast<double>(numFrames) / duration.count()) << "fps";
        qDebug() << "* Output location:" << QString::fromStdString(outputDir);
        
        // List first few files as confirmation
        QDir dir(QString::fromStdString(outputDir));
        QStringList files = dir.entryList(QStringList() << "frame_*.jpg", QDir::Files);
        qDebug() << "\nFirst 5 generated files:";
        int fileSize = (int) files.size();
        for (int i = 0; i < std::min<int>(5, fileSize); ++i) {
            qDebug() << "-" << files[i];
        }
        
        return 0;
    } else {
        qDebug() << "\nFailed to generate sequence!";
        return 1;
    }
}