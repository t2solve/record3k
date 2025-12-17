#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <opencv2/opencv.hpp>
#include <opencv2/imgcodecs.hpp>
#include <chrono>
#include <vector>
#include <memory>

#include <liblbt/process_step_factory.h>
#include <liblbt/processing_pipeline.h>
#include <liblbt/frame_memory_object.h>
#include <liblbt/pipeline_profiler.h>
#include <liblbt/pipeline_config_loader.h>

#include "test_image_generator.cpp"

#define ENABLE_PROFILING

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    
    qDebug() << "=== OpenCV Processing Pipeline Demo ===";
    qDebug() << "OpenCV Version:" << CV_VERSION;
    
    // Check CUDA availability
    bool cudaAvailable = false;
#ifdef CUDA_ENABLED
    cudaAvailable = cv::cuda::getCudaEnabledDeviceCount() > 0;
    qDebug() << "CUDA Devices Available:" << cv::cuda::getCudaEnabledDeviceCount();
#else
    qDebug() << "CUDA Support: Not compiled";
#endif
    
    // Create output directory
    QDir outputDir("demo_output");
    if (!outputDir.exists()) {
        outputDir.mkpath(".");
    }
    
    // === Test 1: Single Image Processing ===
    qDebug() << "\n=== Test 1: Single Image Processing ===";
    
    // Generate test image
    cv::Mat testImage = TestImageGenerator::generateTestImage();
    cv::imwrite("demo_output/00_original.jpg", testImage);
    qDebug() << "Generated test image:" << testImage.cols << "x" << testImage.rows;
    
    //ProcessingMode mode = cudaAvailable ? ProcessingMode::CUDA_PREFERRED : ProcessingMode::CPU_ONLY;
    // // Create processing pipeline
    // std::vector<std::shared_ptr<ProcessStep>> pipeline;
    // std::vector<FilterConfig> configs;
    
    
    // // Step 1: Gaussian Blur (noise reduction)
    // pipeline.push_back(ProcessStepFactory::createGaussianBlur(mode));
    // FilterConfig blurConfig;
    // blurConfig.setParameter("kernel_size", 5);
    // blurConfig.setParameter("sigma_x", 1.0);
    // blurConfig.setParameter("sigma_y", 1.0);
    // configs.push_back(blurConfig);
    
    // // Step 2: Bilateral Filter (edge-preserving smoothing)
    // pipeline.push_back(ProcessStepFactory::createBilateralFilter(mode));
    // FilterConfig bilateralConfig;
    // bilateralConfig.setParameter("d", 9);
    // bilateralConfig.setParameter("sigma_color", 75.0);
    // bilateralConfig.setParameter("sigma_space", 75.0);
    // configs.push_back(bilateralConfig);
    
    // // Step 3: Sharpen
    // pipeline.push_back(ProcessStepFactory::createSharpen(mode));
    // FilterConfig sharpenConfig;
    // sharpenConfig.setParameter("strength", 1.5);
    // configs.push_back(sharpenConfig);
    
    // // Step 4: Edge Detection
    // pipeline.push_back(ProcessStepFactory::createEdgeDetection(mode));
    // FilterConfig edgeConfig;
    // edgeConfig.setParameter("threshold1", 100.0);
    // edgeConfig.setParameter("threshold2", 200.0);
    // edgeConfig.setParameter("aperture_size", 3);
    // configs.push_back(edgeConfig);
    // Load pipeline from XML
    auto pipeConfig = PipelineConfigLoader::loadFromXML("config.xml");
    ProcessingMode mode = pipeConfig.mode;
      // Process the image
    qDebug() << "Processing pipeline with" << pipeConfig.steps.size() << "steps...";

    auto startTime = std::chrono::high_resolution_clock::now();
    
    FrameMemoryObject inputFrame(testImage);
    //FrameMemoryObject result = ProcessingPipeline::processLine(pipeline, inputFrame, configs);
    FrameMemoryObject result = ProcessingPipeline::processLine(pipeConfig.steps, inputFrame, pipeConfig.configs);

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    
    qDebug() << "Processing completed in" << duration.count() << "microseconds";
    
    // Save result
    cv::Mat resultImage = result.getCpuMat();
    cv::imwrite("demo_output/01_processed.jpg", resultImage);
    qDebug() << "Saved processed image";

     // === Check for contour metadata ===
    qDebug() << "\n=== Contour Detection Results ===";
    if (result.hasMetadata("contour_count")) {
        int contourCount = result.getMetadata<int>("contour_count");
        qDebug() << "Found contours:" << contourCount;
        
        if (contourCount > 0) {
            auto contours = result.getMetadata<std::vector<std::vector<cv::Point>>>("contours");
            auto areas = result.getMetadata<std::vector<double>>("contour_areas");
            auto perimeters = result.getMetadata<std::vector<double>>("contour_perimeters");
            
            qDebug() << "Contour statistics:";
            for (int i = 0; i < std::min(contourCount, 10); ++i) { // Show first 10
                qDebug() << "  Contour" << i << ": Area =" << areas[i] << ", Perimeter =" << perimeters[i];
            }
            
            if (result.hasMetadata("total_contour_area")) {
                double totalArea = result.getMetadata<double>("total_contour_area");
                double meanArea = result.getMetadata<double>("mean_contour_area");
                double maxArea = result.getMetadata<double>("max_contour_area");
                double minArea = result.getMetadata<double>("min_contour_area");
                
                qDebug() << "Area statistics:";
                qDebug() << "  Total area:" << totalArea;
                qDebug() << "  Mean area:" << meanArea;
                qDebug() << "  Max area:" << maxArea;
                qDebug() << "  Min area:" << minArea;
            }
        }
    } else {
        qDebug() << "No contour metadata found";
    }
    
    // // Check for edge detection metadata
    // if (result.hasMetadata("edge_pixel_count")) {
    //     int edgePixels = result.getMetadata<int>("edge_pixel_count");
    //     double edgeDensity = result.getMetadata<double>("edge_density");
    //     qDebug() << "Edge detection results:";
    //     qDebug() << "  Edge pixels:" << edgePixels;
    //     qDebug() << "  Edge density:" << edgeDensity;
    // }
    
    // === Test 2: Individual Step Processing ===
    qDebug() << "\n=== Test 2: Individual Step Processing ===";
    
    // Test each step individually
    std::vector<std::pair<std::string, std::shared_ptr<ProcessStep>>> individualSteps = {
        {"gaussian_blur", ProcessStepFactory::createGaussianBlur(mode)},
        {"bilateral_filter", ProcessStepFactory::createBilateralFilter(mode)},
        {"median_filter", ProcessStepFactory::createMedianFilter(mode)},
        {"sharpen", ProcessStepFactory::createSharpen(mode)},
        {"edge_detection", ProcessStepFactory::createEdgeDetection(mode)},
        {"denoise", ProcessStepFactory::createDenoise(mode)}
    };
    
    for (size_t i = 0; i < individualSteps.size(); ++i) {
        const auto& stepPair = individualSteps[i];
        const std::string& stepName = stepPair.first;
        const auto& step = stepPair.second;
        
        qDebug() << "Processing step:" << QString::fromStdString(stepName);
        
        // Use appropriate config for each step
        FilterConfig config = pipeConfig.configs[std::min(i, pipeConfig.configs.size()-1)];
        // Process
        auto stepStart = std::chrono::high_resolution_clock::now();
        FrameMemoryObject stepResult = step->process(inputFrame, config);
        auto stepEnd = std::chrono::high_resolution_clock::now();
        auto stepDuration = std::chrono::duration_cast<std::chrono::microseconds>(stepEnd - stepStart);
        
        qDebug() << "  Time:" << stepDuration.count() << "μs";
        qDebug() << "  Memory:" << QString::fromStdString(stepResult.getInfo());
        
        // Save result
        cv::Mat stepImage = stepResult.getCpuMat();
        std::string filename = "demo_output/step_" + std::to_string(i+2) + "_" + stepName + ".jpg";
        cv::imwrite(filename, stepImage);
    }
    
    // === Test 3: Background Subtraction Pipeline ===
    qDebug() << "\n=== Test 3: Background Subtraction Pipeline ===";
    
    // Create background subtraction pipeline
    std::vector<std::shared_ptr<ProcessStep>> bgPipeline;
    std::vector<FilterConfig> bgConfigs;
    
    // Preprocessing: Gaussian blur
    bgPipeline.push_back(ProcessStepFactory::createGaussianBlur(mode));
    FilterConfig bgBlurConfig;
    bgBlurConfig.setParameter("kernel_size", 3);
    bgBlurConfig.setParameter("sigma_x", 1.0);
    bgConfigs.push_back(bgBlurConfig);
    
    // Background subtraction
    bgPipeline.push_back(ProcessStepFactory::createBackgroundSubtractionMOG2(mode));
    FilterConfig mog2Config;
    mog2Config.setParameter("history", 100);
    mog2Config.setParameter("var_threshold", 16.0);
    mog2Config.setParameter("detect_shadows", 1.0);
    mog2Config.setParameter("learning_rate", 0.01);
    mog2Config.setParameter("apply_morphology", 1.0);
    mog2Config.setParameter("morph_kernel_size", 3);
    bgConfigs.push_back(mog2Config);
    
    // Post-processing: Median filter
    bgPipeline.push_back(ProcessStepFactory::createMedianFilter(mode));
    FilterConfig bgMedianConfig;
    bgMedianConfig.setParameter("kernel_size", 3);
    bgConfigs.push_back(bgMedianConfig);
    
    // Process sequence of frames
    qDebug() << "Processing background subtraction sequence...";
    
    std::chrono::microseconds totalBgTime(0);
    int frameCount = 60;

    for (int frame = 0; frame < frameCount; ++frame) {
        cv::Mat frameImage = TestImageGenerator::generateMovingObjectSequence(frame);
    FrameMemoryObject frameInput(frameImage);

    auto bgStart = std::chrono::high_resolution_clock::now();
    FrameMemoryObject bgResult = ProcessingPipeline::processLine(bgPipeline, frameInput, bgConfigs);
    auto bgEnd = std::chrono::high_resolution_clock::now();
    auto bgDuration = std::chrono::duration_cast<std::chrono::microseconds>(bgEnd - bgStart);
    totalBgTime += bgDuration;

    if (frame % 5 == 0) {
        cv::Mat bgMask = bgResult.getCpuMat();
        std::string filename = "demo_output/bg_frame_" + std::to_string(frame) + ".jpg";
        cv::imwrite(filename, bgMask);

        std::string origFilename = "demo_output/bg_orig_" + std::to_string(frame) + ".jpg";
        cv::imwrite(origFilename, frameImage);
    }

    qDebug() << "  Frame" << frame << "processing time:" << bgDuration.count() << "μs";

    if (frame % 10 == 0) {
        qDebug() << "  Processed frame" << frame;
    }
    }

    double avgBgTime = totalBgTime.count() / static_cast<double>(frameCount);
    qDebug() << "Total BGS processing time for" << frameCount << "frames:" << totalBgTime.count() << "μs";
    qDebug() << "Average BGS processing time per frame:" << avgBgTime << "μs";

    
    // === Test 4: Performance Profiling ===
    qDebug() << "\n=== Test 4: Performance Profiling ===";
    
#ifdef ENABLE_PROFILING
    // Profile the original pipeline
    auto profile = PipelineProfiler::profileProcessLine(pipeConfig.steps, inputFrame, pipeConfig.configs);    
    qDebug() << "=== Performance Profile ===";
    profile.print();
#else
    qDebug() << "Profiling not enabled in this build";
#endif
    
    // === Test 5: Memory Location Optimization ===
    qDebug() << "\n=== Test 5: Memory Location Optimization ===";
    
    MemoryLocation optimalLocation = ProcessingPipeline::determineOptimalMemoryLocation(pipeConfig.steps);
    qDebug() << "Optimal memory location:" << (optimalLocation == MemoryLocation::CPU ? "CPU" : "GPU");
    
    // Test different memory strategies
    std::vector<MemoryLocation> testLocations = {MemoryLocation::CPU};
    if (cudaAvailable) {
        testLocations.push_back(MemoryLocation::GPU);
    }
    
    for (MemoryLocation location : testLocations) {
        qDebug() << "Testing with memory location:" << (location == MemoryLocation::CPU ? "CPU" : "GPU");
        
        FrameMemoryObject testFrame(testImage);
        testFrame.moveToMemoryLocation(location);
        
        auto locationStart = std::chrono::high_resolution_clock::now();
        FrameMemoryObject locationResult = ProcessingPipeline::processLine(pipeConfig.steps, inputFrame, pipeConfig.configs);
        auto locationEnd = std::chrono::high_resolution_clock::now();
        auto locationDuration = std::chrono::duration_cast<std::chrono::microseconds>(locationEnd - locationStart);
        
        qDebug() << "  Processing time:" << locationDuration.count() << "μs";
        qDebug() << "  Memory usage:" << QString::fromStdString(locationResult.getInfo());
    } 
    
    qDebug() << "\n=== Demo Complete ===";
    qDebug() << "Check the 'demo_output' directory for result images";
    
    return 0;
}   