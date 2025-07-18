#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#ifdef CUDA_ENABLED
#include <opencv2/cudaimgproc.hpp>
#endif

#include "processing_pipeline.h"
#include "concrete_process_steps.h"
#include "frame_memory_object.h"

#include <iostream>
#include <vector>
#include <memory>
#include <chrono>

/**
 * @brief Demo showing how to use the new processing pipeline
 * 
 * This demo demonstrates:
 * 1. Creating a processing pipeline with multiple steps
 * 2. Efficient memory management between CPU and GPU
 * 3. Performance profiling of the pipeline
 * 4. Comparison between CPU-only and CUDA-accelerated processing
 */
class ProcessingPipelineDemo {
public:
    static void runDemo() {
        std::cout << "=== Processing Pipeline Demo ===\n\n";
        
        // Create a sample image
        cv::Mat sampleImage = createSampleImage();
        FrameMemoryObject inputFrame(sampleImage);
        
        std::cout << "Created sample image: " << sampleImage.size() << " (" 
                  << sampleImage.channels() << " channels)\n\n";
        
        // Demo 1: CPU-only pipeline
        std::cout << "1. CPU-only processing pipeline\n";
        runCpuPipeline(inputFrame);
        
        // Demo 2: CUDA-accelerated pipeline
        std::cout << "\n2. CUDA-accelerated processing pipeline\n";
        runCudaPipeline(inputFrame);
        
        // Demo 3: Mixed pipeline with profiling
        std::cout << "\n3. Mixed pipeline with performance profiling\n";
        runMixedPipelineWithProfiling(inputFrame);
        
        // Demo 4: Memory type optimization
        std::cout << "\n4. Memory type optimization demo\n";
        runMemoryOptimizationDemo(inputFrame);
        
        std::cout << "\n=== Demo Complete ===\n";
    }
    
private:
    static cv::Mat createSampleImage() {
        // Create a test image with some noise and patterns
        cv::Mat image = cv::Mat::zeros(1024, 1024, CV_8UC3);
        
        // Add some colored rectangles
        cv::rectangle(image, cv::Rect(100, 100, 200, 200), cv::Scalar(255, 0, 0), -1);
        cv::rectangle(image, cv::Rect(400, 200, 150, 300), cv::Scalar(0, 255, 0), -1);
        cv::rectangle(image, cv::Rect(700, 400, 250, 150), cv::Scalar(0, 0, 255), -1);
        
        // Add some circles
        cv::circle(image, cv::Point(300, 600), 80, cv::Scalar(255, 255, 0), -1);
        cv::circle(image, cv::Point(700, 200), 60, cv::Scalar(255, 0, 255), -1);
        
        // Add some noise
        cv::Mat noise = cv::Mat::zeros(image.size(), CV_8UC3);
        cv::randu(noise, cv::Scalar::all(0), cv::Scalar::all(50));
        cv::add(image, noise, image);
        
        return image;
    }
    
    static void runCpuPipeline(const FrameMemoryObject& input) {
        // Create CPU-only processing steps
        std::vector<std::shared_ptr<ProcessStep>> steps;
        steps.push_back(ProcessStepFactory::createGaussianBlur(ProcessingMode::CPU_ONLY));
        steps.push_back(ProcessStepFactory::createBilateralFilter(ProcessingMode::CPU_ONLY));
        steps.push_back(ProcessStepFactory::createMedianFilter(ProcessingMode::CPU_ONLY));
        
        // Configure each step
        std::vector<FilterConfig> configs;
        
        // Gaussian blur config
        FilterConfig gaussianConfig;
        gaussianConfig.setParameter("kernel_size", 7);
        gaussianConfig.setParameter("sigma_x", 2.0);
        configs.push_back(gaussianConfig);
        
        // Bilateral filter config
        FilterConfig bilateralConfig;
        bilateralConfig.setParameter("d", 9);
        bilateralConfig.setParameter("sigma_color", 75.0);
        bilateralConfig.setParameter("sigma_space", 75.0);
        configs.push_back(bilateralConfig);
        
        // Median filter config
        FilterConfig medianConfig;
        medianConfig.setParameter("kernel_size", 5);
        configs.push_back(medianConfig);
        
        // Execute pipeline
        auto start = std::chrono::high_resolution_clock::now();
        FrameMemoryObject result = ProcessingPipeline::processLine(steps, input, configs);
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        std::cout << "CPU pipeline completed in " << duration.count() << " ms\n";
        std::cout << "Result size: " << result.size() << ", Memory type: " 
                  << (result.getMemoryType() == MemoryType::CPU ? "CPU" : "CUDA") << "\n";
    }
    
    static void runCudaPipeline(const FrameMemoryObject& input) {
#ifdef CUDA_ENABLED
        if (cv::cuda::getCudaEnabledDeviceCount() == 0) {
            std::cout << "CUDA not available, skipping CUDA pipeline demo\n";
            return;
        }
        
        // Create CUDA-accelerated processing steps
        std::vector<std::shared_ptr<ProcessStep>> steps;
        steps.push_back(ProcessStepFactory::createGaussianBlur(ProcessingMode::CUDA_PREFERRED));
        steps.push_back(ProcessStepFactory::createBilateralFilter(ProcessingMode::CUDA_PREFERRED));
        steps.push_back(ProcessStepFactory::createEdgeDetection(ProcessingMode::CUDA_PREFERRED));
        
        // Configure each step
        std::vector<FilterConfig> configs;
        
        // Gaussian blur config
        FilterConfig gaussianConfig;
        gaussianConfig.setParameter("kernel_size", 7);
        gaussianConfig.setParameter("sigma_x", 2.0);
        configs.push_back(gaussianConfig);
        
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
        
        // Execute pipeline
        auto start = std::chrono::high_resolution_clock::now();
        FrameMemoryObject result = ProcessingPipeline::processLine(steps, input, configs);
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        std::cout << "CUDA pipeline completed in " << duration.count() << " ms\n";
        std::cout << "Result size: " << result.size() << ", Memory type: " 
                  << (result.getMemoryType() == MemoryType::CPU ? "CPU" : "CUDA") << "\n";
#else
        std::cout << "CUDA not compiled in, skipping CUDA pipeline demo\n";
#endif
    }
    
    static void runMixedPipelineWithProfiling(const FrameMemoryObject& input) {
        // Create a mixed pipeline
        std::vector<std::shared_ptr<ProcessStep>> steps;
        steps.push_back(ProcessStepFactory::createGaussianBlur(ProcessingMode::CUDA_PREFERRED));
        steps.push_back(ProcessStepFactory::createMedianFilter(ProcessingMode::CPU_ONLY)); // Forces CPU
        steps.push_back(ProcessStepFactory::createBilateralFilter(ProcessingMode::CUDA_PREFERRED));
        steps.push_back(ProcessStepFactory::createEdgeDetection(ProcessingMode::CUDA_PREFERRED));
        
        // Configure each step
        std::vector<FilterConfig> configs;
        
        FilterConfig gaussianConfig;
        gaussianConfig.setParameter("kernel_size", 5);
        gaussianConfig.setParameter("sigma_x", 1.5);
        configs.push_back(gaussianConfig);
        
        FilterConfig medianConfig;
        medianConfig.setParameter("kernel_size", 3);
        configs.push_back(medianConfig);
        
        FilterConfig bilateralConfig;
        bilateralConfig.setParameter("kernel_size", 5);
        bilateralConfig.setParameter("sigma_color", 50.0);
        bilateralConfig.setParameter("sigma_space", 50.0);
        configs.push_back(bilateralConfig);
        
        FilterConfig edgeConfig;
        edgeConfig.setParameter("threshold1", 50.0);
        edgeConfig.setParameter("threshold2", 150.0);
        configs.push_back(edgeConfig);
        
        // Execute with profiling
        auto profile = PipelineProfiler::profileProcessLine(steps, input, configs);
        profile.print();
    }
    
    static void runMemoryOptimizationDemo(const FrameMemoryObject& input) {
        std::cout << "Memory optimization analysis:\n";
        
        // Test different pipeline configurations
        std::vector<std::shared_ptr<ProcessStep>> cpuPipeline;
        cpuPipeline.push_back(ProcessStepFactory::createGaussianBlur(ProcessingMode::CPU_ONLY));
        cpuPipeline.push_back(ProcessStepFactory::createBilateralFilter(ProcessingMode::CPU_ONLY));
        cpuPipeline.push_back(ProcessStepFactory::createMedianFilter(ProcessingMode::CPU_ONLY));
        
        MemoryType optimalCpu = ProcessingPipeline::determineOptimalMemoryType(cpuPipeline);
        std::cout << "CPU-only pipeline optimal memory type: " 
                  << (optimalCpu == MemoryType::CPU ? "CPU" : "CUDA") << "\n";
        
#ifdef CUDA_ENABLED
        if (cv::cuda::getCudaEnabledDeviceCount() > 0) {
            std::vector<std::shared_ptr<ProcessStep>> cudaPipeline;
            cudaPipeline.push_back(ProcessStepFactory::createGaussianBlur(ProcessingMode::CUDA_PREFERRED));
            cudaPipeline.push_back(ProcessStepFactory::createBilateralFilter(ProcessingMode::CUDA_PREFERRED));
            cudaPipeline.push_back(ProcessStepFactory::createEdgeDetection(ProcessingMode::CUDA_PREFERRED));
            
            MemoryType optimalCuda = ProcessingPipeline::determineOptimalMemoryType(cudaPipeline);
            std::cout << "CUDA pipeline optimal memory type: " 
                      << (optimalCuda == MemoryType::CPU ? "CPU" : "CUDA") << "\n";
        }
#endif
        
        // Test memory conversions
        std::cout << "\nMemory conversion test:\n";
        FrameMemoryObject testFrame = input.clone();
        
        std::cout << "Original frame memory type: " 
                  << (testFrame.getMemoryType() == MemoryType::CPU ? "CPU" : "CUDA") << "\n";
        
#ifdef CUDA_ENABLED
        if (cv::cuda::getCudaEnabledDeviceCount() > 0) {
            auto start = std::chrono::high_resolution_clock::now();
            testFrame.toGpu();
            auto end = std::chrono::high_resolution_clock::now();
            
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            std::cout << "CPU->GPU conversion time: " << duration.count() << " μs\n";
            
            start = std::chrono::high_resolution_clock::now();
            testFrame.toCpu();
            end = std::chrono::high_resolution_clock::now();
            
            duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            std::cout << "GPU->CPU conversion time: " << duration.count() << " μs\n";
        }
#endif
    }
};

// Example usage function
void demonstrateProcessingPipeline() {
    try {
        ProcessingPipelineDemo::runDemo();
    } catch (const std::exception& e) {
        std::cerr << "Error in processing pipeline demo: " << e.what() << std::endl;
    }
}

// Example of efficient CUDA processing like in the original example
void efficientCudaProcessingPipelineExample() {
    std::cout << "\n=== Efficient CUDA Processing Pipeline Example ===\n";
    
    // Create input
    cv::Mat inputImage = cv::imread("input.jpg"); // Replace with actual image
    if (inputImage.empty()) {
        // Create a test image if no input file
        inputImage = cv::Mat::zeros(1024, 1024, CV_8UC3);
        cv::rectangle(inputImage, cv::Rect(100, 100, 800, 800), cv::Scalar(128, 128, 128), -1);
    }
    
    FrameMemoryObject inputFrame(inputImage);
    
    // Create a pipeline that keeps data on GPU
    std::vector<std::shared_ptr<ProcessStep>> steps;
    steps.push_back(ProcessStepFactory::createGaussianBlur(ProcessingMode::CUDA_PREFERRED));
    steps.push_back(ProcessStepFactory::createBilateralFilter(ProcessingMode::CUDA_PREFERRED));
    steps.push_back(ProcessStepFactory::createEdgeDetection(ProcessingMode::CUDA_PREFERRED));
    
    // Configure steps
    FilterConfig config;
    config.setParameter("kernel_size", 7);
    config.setParameter("sigma_x", 2.0);
    config.setParameter("sigma_color", 75.0);
    config.setParameter("sigma_space", 75.0);
    config.setParameter("threshold1", 100.0);
    config.setParameter("threshold2", 200.0);
    
    // Execute with profiling
    auto profile = PipelineProfiler::profileProcessLine(steps, inputFrame, 
                                                      std::vector<FilterConfig>(steps.size(), config));
    
    std::cout << "Efficient CUDA pipeline results:\n";
    profile.print();
    
    // The result stays on GPU until explicitly converted
    FrameMemoryObject result = ProcessingPipeline::processLine(steps, inputFrame, config);
    std::cout << "Final result memory type: " 
              << (result.getMemoryType() == MemoryType::CPU ? "CPU" : "CUDA") << "\n";
}

int main() {
    std::cout << "Starting Processing Pipeline Demo\n";
    
    demonstrateProcessingPipeline();
    efficientCudaProcessingPipelineExample();
    
    return 0;
}
