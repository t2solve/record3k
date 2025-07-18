#include <QCoreApplication>
#include <QTimer>
#include <QDebug>
#include <memory>
#include <opencv2/opencv.hpp>

#include "processing_pipeline.h"
#include "concrete_process_steps.h"
#include "frame_memory_object.h"

// Demo of the new processing pipeline system
int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    
    qDebug() << "=== New Processing Pipeline Demo ===";
    
    // Create a test image
    cv::Mat testImage = cv::Mat::zeros(640, 480, CV_8UC3);
    cv::rectangle(testImage, cv::Rect(100, 100, 200, 200), cv::Scalar(255, 0, 0), -1);
    cv::circle(testImage, cv::Point(300, 300), 50, cv::Scalar(0, 255, 0), -1);
    
    // Add some noise
    cv::Mat noise = cv::Mat::zeros(testImage.size(), CV_8UC3);
    cv::randu(noise, cv::Scalar::all(0), cv::Scalar::all(30));
    cv::add(testImage, noise, testImage);
    
    FrameMemoryObject inputFrame(testImage);
    qDebug() << "Created test image:" << testImage.cols << "x" << testImage.rows;
    
    // Create processing pipeline
    std::vector<std::shared_ptr<ProcessStep>> steps;
    steps.push_back(ProcessStepFactory::createGaussianBlur(ProcessingMode::CUDA_PREFERRED));
    steps.push_back(ProcessStepFactory::createBilateralFilter(ProcessingMode::CUDA_PREFERRED));
    steps.push_back(ProcessStepFactory::createEdgeDetection(ProcessingMode::CUDA_PREFERRED));
    
    // Configure steps
    std::vector<FilterConfig> configs;
    
    // Gaussian blur config
    FilterConfig blurConfig;
    blurConfig.setParameter("kernel_size", 7);
    blurConfig.setParameter("sigma_x", 2.0);
    configs.push_back(blurConfig);
    
    // Bilateral filter config
    FilterConfig bilateralConfig;
    bilateralConfig.setParameter("kernel_size", 5);
    bilateralConfig.setParameter("sigma_color", 75.0);
    bilateralConfig.setParameter("sigma_space", 75.0);
    configs.push_back(bilateralConfig);
    
    // Edge detection config
    FilterConfig edgeConfig;
    edgeConfig.setParameter("threshold1", 100.0);
    edgeConfig.setParameter("threshold2", 200.0);
    configs.push_back(edgeConfig);
    
    qDebug() << "Configured" << steps.size() << "processing steps";
    
    // Execute pipeline with profiling
    qDebug() << "Starting pipeline execution with profiling...";
    auto profile = PipelineProfiler::profileProcessLine(steps, inputFrame, configs);
    
    qDebug() << "Pipeline execution completed";
    
    // Print results
    std::cout << "\n";
    profile.print();
    
    // Execute pipeline normally
    qDebug() << "Executing pipeline normally...";
    auto start = std::chrono::high_resolution_clock::now();
    FrameMemoryObject result = ProcessingPipeline::processLine(steps, inputFrame, configs);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    qDebug() << "Normal execution completed in" << duration.count() << "ms";
    qDebug() << "Result size:" << result.size().width << "x" << result.size().height;
    qDebug() << "Result memory type:" << (result.getMemoryType() == MemoryType::CPU ? "CPU" : "CUDA");
    
    // Save result
    cv::Mat outputImage = result.getCpuMat();
    cv::imwrite("/tmp/demo_output.jpg", outputImage);
    qDebug() << "Saved result to /tmp/demo_output.jpg";
    
    // Test memory optimization
    qDebug() << "\nTesting memory optimization...";
    MemoryType optimalType = ProcessingPipeline::determineOptimalMemoryType(steps);
    qDebug() << "Optimal memory type for this pipeline:" << (optimalType == MemoryType::CPU ? "CPU" : "CUDA");
    
    // Test different pipeline configurations
    qDebug() << "\nTesting CPU-only pipeline...";
    std::vector<std::shared_ptr<ProcessStep>> cpuSteps;
    cpuSteps.push_back(ProcessStepFactory::createGaussianBlur(ProcessingMode::CPU_ONLY));
    cpuSteps.push_back(ProcessStepFactory::createBilateralFilter(ProcessingMode::CPU_ONLY));
    cpuSteps.push_back(ProcessStepFactory::createMedianFilter(ProcessingMode::CPU_ONLY));
    
    std::vector<FilterConfig> cpuConfigs(cpuSteps.size(), blurConfig);
    
    start = std::chrono::high_resolution_clock::now();
    FrameMemoryObject cpuResult = ProcessingPipeline::processLine(cpuSteps, inputFrame, cpuConfigs);
    end = std::chrono::high_resolution_clock::now();
    
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    qDebug() << "CPU-only pipeline completed in" << duration.count() << "ms";
    
    // Schedule application exit
    QTimer::singleShot(100, &app, &QCoreApplication::quit);
    
    return app.exec();
} 
                                  << "Size:" << frame.image.cols << "x" << frame.image.rows;
                     });
    
    QObject::connect(frameProcessor.get(), &FrameProcessor::processingError, 
                     [](const QString& error) {
                         qWarning() << "Processing error:" << error;
                     });
    
    QObject::connect(frameProcessor.get(), &FrameProcessor::statisticsUpdated, 
                     [frameProcessor]() {
                         const auto& stats = frameProcessor->getStatistics();
                         qDebug() << "Stats - Total:" << stats.totalFrames.load()
                                  << "Processed:" << stats.processedFrames.load()
                                  << "Avg time:" << stats.averageProcessingTime.load() << "ms";
                     });
    
    // Start frame processor
    frameProcessor->start();
    
    // Create some test frames
    qDebug() << "Creating test frames...";
    
    for (int i = 0; i < 10; ++i) {
        // Create a simple test image
        cv::Mat testImage = cv::Mat::zeros(480, 640, CV_8UC3);
        
        // Draw some test patterns
        cv::rectangle(testImage, cv::Point(50, 50), cv::Point(200, 200), cv::Scalar(255, 0, 0), -1);
        cv::circle(testImage, cv::Point(400, 300), 50, cv::Scalar(0, 255, 0), -1);
        cv::line(testImage, cv::Point(0, 0), cv::Point(640, 480), cv::Scalar(0, 0, 255), 3);
        
        // Add some text
        cv::putText(testImage, "Test Frame " + std::to_string(i), 
                   cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 255, 255), 2);
        
        // Create processing frame
        ProcessingFrame frame(testImage, i, i * 0.1);
        
        // Enqueue for processing
        if (frameProcessor->enqueueFrame(frame)) {
            qDebug() << "Enqueued test frame" << i;
        } else {
            qWarning() << "Failed to enqueue test frame" << i;
        }
    }
    
    // Set up timer to stop processing
    QTimer* stopTimer = new QTimer(&app);
    stopTimer->setSingleShot(true);
    stopTimer->setInterval(5000); // 5 seconds
    
    QObject::connect(stopTimer, &QTimer::timeout, [&]() {
        qDebug() << "Stopping processing...";
        
        // Stop frame processor
        frameProcessor->stop();
        
        // Print final statistics
        const auto& stats = frameProcessor->getStatistics();
        qDebug() << "=== Final Statistics ===";
        qDebug() << "Total frames:" << stats.totalFrames.load();
        qDebug() << "Processed frames:" << stats.processedFrames.load();
        qDebug() << "Dropped frames:" << stats.droppedFrames.load();
        qDebug() << "Average processing time:" << stats.averageProcessingTime.load() << "ms";
        qDebug() << "CUDA available:" << stats.cudaAvailable.load();
        qDebug() << "Queue size:" << frameProcessor->getQueueSize();
        
        app.quit();
    });
    
    // Start the timer
    stopTimer->start();
    
    qDebug() << "Demo running... Will stop automatically in 5 seconds.";
    
    // Run event loop
    return app.exec();
}
