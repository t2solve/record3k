
#include <QCoreApplication>
#include <QTimer>
#include <QDebug>
#include <QObject>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/opencv.hpp>

#include <thread>
#include <optional>
#include <cstdlib>

#include <liblbt/process_step.h>
#include <liblbt/process_step_factory.h>
#include <liblbt/processing_pipeline.h>
#include <liblbt/frame_memory_object.h>
#include <liblbt/pipeline_profiler.h>
#include <liblbt/pipeline_config_loader.h>
#include <liblbt/disk_image_frame_source.h>
#include <liblbt/vimbax_frame_source.h>
#include <liblbt/camera_manager_vimbax.h>
#include <liblbt/opencv_frame_source.h>


int main(int argc, char* argv[])
{
    auto frameSource = std::make_shared<OpenCVFrameSource>(0);
    //test  1
    if (frameSource == nullptr) {
        std::cerr << "Failed to create frame source; exiting.\n";
        return -1;
    }

        // Use it
    if (frameSource->isReady()) {
            std::cout << "[test3] Frame source is ready.\n";
    } else {
            std::cerr << "[test3] Frame source is NOT ready; exiting.\n";
            return -1;
    }

    // we build the processing pipeline from by hand
    // Create background subtraction pipeline
    std::vector<std::shared_ptr<ProcessStep>> pipeline;
    std::vector<FilterConfig> configs;
    ProcessingMode mode = ProcessingMode::CUDA_PREFERRED;
    

    // Step 1: frame write
    pipeline.push_back(ProcessStepFactory::createFrameWriter(mode));
    FilterConfig frameWriter;
    frameWriter.setParameter("output_dir", "/tmp/test");
    frameWriter.setParameter("format", "jpg");
    configs.push_back(frameWriter);
    

    //build a config to sum it up
    PipelineConfigLoader::PipelineConfig pipeConfig;
    pipeConfig.steps = pipeline;
    pipeConfig.configs = configs;
    pipeConfig.mode = mode;

    //we save the pipeline configuration to XML for future use
    PipelineConfigLoader::saveToXML("default_calibration.xml", pipeConfig);
    //Load pipeline from XML
    //Configure processing pipeline
    //auto pipeConfig = PipelineConfigLoader::loadFromXML("config.xml");
    //ProcessingMode mode = pipeConfig.mode;
      // Process the image
    //qDebug() << "Processing pipeline with" << pipeConfig.steps.size() << "steps...";

    // Set desired frequency (Hz)
    double frequency = 10.0; // 10 frames per second
    auto interval = std::chrono::milliseconds(static_cast<int>(1000.0 / frequency));

    auto start_time = std::chrono::steady_clock::now();
    auto last_time = start_time;
    double avg_fps = 0.0;
    const double alpha = 0.1;
    int img_counter = 1;
    bool printedPipelineOnce = false;
    bool debugPipeline = true;
#ifdef CUDA_ENABLED
    if (debugPipeline) {
        int devs = cv::cuda::getCudaEnabledDeviceCount();
        qDebug() << "[PipelineDebug] CUDA devices available:" << devs;
    }
#endif
    if (debugPipeline) {
        qDebug() << "[PipelineDebug] Steps configured:" << pipeline.size();
        for (size_t i = 0; i < pipeline.size(); ++i) {
            auto name = QString::fromStdString(pipeline[i]->getName());
            auto pref = pipeline[i]->getPreferredMemoryLocation() == MemoryLocation::GPU ? "GPU" : "CPU";
            qDebug() << "  [" << i << "]" << name << "pref=" << pref;
        }
    }
    //start and iterate
    std::vector<cv::Point2f> mass_center_list;
    std::vector<cv::Point2d> world_center_list; 
    while (std::chrono::steady_clock::now() - start_time < std::chrono::minutes(1)) 
    {
         auto frame = frameSource->nextFrame();
    
         auto nmomentFrameCaptured = std::chrono::steady_clock::now();
         if (frame) {
             FrameMemoryObject result = ProcessingPipeline::processLine(pipeline, *frame, configs);
            }
        double dt = std::chrono::duration<double>(nmomentFrameCaptured - last_time).count(); // seconds
        last_time = nmomentFrameCaptured;
         
        // Calculate instantaneous FPS and update moving average
        double fps = (dt > 0.0) ? (1.0 / dt) : 0.0;
        avg_fps = (1.0 - alpha) * avg_fps + alpha * fps;
        qDebug() << "Instantaneous FPS:" << fps << ", Moving Average FPS:" << avg_fps;
        auto sleepTimer = interval - std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - nmomentFrameCaptured);
        qDebug() << "sleepTimer ms:" << sleepTimer.count();
        std::this_thread::sleep_for(sleepTimer);
   }//end while
   std::cout << "finished\n";
   

    return 0;
}


/**
//  * Calculate the mass center of the convex hull from a contour selection
//  * @param contourSelection Vector of contours (each contour is a vector of points)
//  * @return Mass center point of the convex hull, or (-1, -1) if input is empty
//  */
// cv::Point2f calculateConvexHullMassCenter(const std::vector<std::vector<cv::Point>>& contourSelection)
// {
//     if (contourSelection.empty()) {
//         return cv::Point2f(-1, -1);
//     }
    
//     // Step 1: Merge all contour points into a single vector
//     std::vector<cv::Point> allContourPoints;
//     for (const auto& contour : contourSelection) {
//         allContourPoints.insert(allContourPoints.end(), contour.begin(), contour.end());
//     }
    
//     if (allContourPoints.empty()) {
//         return cv::Point2f(-1, -1);
//     }
    
//     // Step 2: Calculate convex hull of all merged points
//     std::vector<cv::Point> convexHull;
//     cv::convexHull(allContourPoints, convexHull, false);
    
//     if (convexHull.empty()) {
//         return cv::Point2f(-1, -1);
//     }
    
//     // Step 3: Calculate moments of the convex hull
//     cv::Moments muConvexHull = cv::moments(convexHull, true);
    
//     // Step 4: Calculate mass center from moments
//     if (muConvexHull.m00 == 0) {
//         return cv::Point2f(-1, -1); // Avoid division by zero
//     }
    
//     cv::Point2f massCenter(
//         static_cast<float>(muConvexHull.m10 / muConvexHull.m00),
//         static_cast<float>(muConvexHull.m01 / muConvexHull.m00)
//     );
    
//     return massCenter;
// }
