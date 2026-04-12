
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
#include <json/json.h>
#include <fstream>


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
#include <liblbt/factory/frame_source_factory.h>
#include <liblbt/factory/model_factory.h>
#include <liblbt/record_manager.h>
#include <liblbt/opencv_frame_source.h>

// # test 1
// int main(int argc, char* argv[])
// {
//     // Load camera from JSON file
//     std::string cameraJsonPath = "data/cameras/cam-system-01.json";
//     std::ifstream cameraFile(cameraJsonPath);
//     if (!cameraFile.is_open()) {
//         std::cerr << "[test4] ERROR: Failed to open camera JSON file: " << cameraJsonPath << std::endl;
//         return -1;
//     }
    
//     Json::Value cameraJson;
//     Json::CharReaderBuilder builder;
//     std::string errs;
//     if (!Json::parseFromStream(builder, cameraFile, &cameraJson, &errs)) {
//         std::cerr << "[test4] ERROR: Failed to parse JSON: " << errs << std::endl;
//         return -1;
//     }
//     cameraFile.close();
    
//     auto cameraResult = lbt::ModelFactory::fromJsonCamera(cameraJson);
//     if (!cameraResult.ok()) {
//         std::cerr << "[test4] ERROR: Failed to validate camera JSON: " << cameraResult.error() << std::endl;
//         return -1;
//     }
    
//     auto& camera = cameraResult.value();
//     std::cout << "[test4] Loaded camera: " << camera.camUID << " (Type: " << camera.cameraType << ")" << std::endl;
    
//     // Create appropriate frame source
//     auto frameSource = lbt::FrameSourceFactory::createFromCameraInfo(camera, true);

//     //auto frameSource = std::make_shared<OpenCVFrameSource>(0);


//     //OpenCVFrameSource::listAvailableCameras();
//     //test  1
//     if (frameSource == nullptr) {
//         std::cerr << "Failed to create frame source; exiting.\n";
//         return -1;
//     }

//         // Use it
//     if (frameSource->isReady()) {
//             std::cout << "[test3] Frame source is ready.\n";
//     } else {
//             std::cerr << "[test3] Frame source is NOT ready; exiting.\n";
//             return -1;
//     }

//     // we build the processing pipeline from by hand
//     // Create background subtraction pipeline
//     std::vector<std::shared_ptr<ProcessStep>> pipeline;
//     std::vector<FilterConfig> configs;
//     ProcessingMode mode = ProcessingMode::CUDA_PREFERRED;
    

//     // Step 1: frame write
//     pipeline.push_back(ProcessStepFactory::createFrameWriter(mode));
//     FilterConfig frameWriter;
//     frameWriter.setParameter("output_dir", "/tmp/record3k");
//     frameWriter.setParameter("format", "jpg");
//     configs.push_back(frameWriter);
    

//     //build a config to sum it up
//     PipelineConfigLoader::PipelineConfig pipeConfig;
//     pipeConfig.steps = pipeline;
//     pipeConfig.configs = configs;
//     pipeConfig.mode = mode;

//     //we save the pipeline configuration to XML for future use
//     PipelineConfigLoader::saveToJSON("pipe_default_calibration_01.json", pipeConfig);
//     //PipelineConfigLoader::saveToXML("default_calibration.xml", pipeConfig);
//     //Load pipeline from XML
//     //Configure processing pipeline
//     //auto pipeConfig = PipelineConfigLoader::loadFromXML("config.xml");
//     //ProcessingMode mode = pipeConfig.mode;
//       // Process the image
//     //qDebug() << "Processing pipeline with" << pipeConfig.steps.size() << "steps...";

//     // Set desired frequency (Hz)
//     double frequency = 10.0; // 10 frames per second
//     auto interval = std::chrono::milliseconds(static_cast<int>(1000.0 / frequency));

//     auto start_time = std::chrono::steady_clock::now();
//     auto last_time = start_time;
//     double avg_fps = 0.0;
//     const double alpha = 0.1;
//     int img_counter = 1;
//     bool printedPipelineOnce = false;
//     bool debugPipeline = true;
// #ifdef CUDA_ENABLED
//     if (debugPipeline) {
//         int devs = cv::cuda::getCudaEnabledDeviceCount();
//         qDebug() << "[PipelineDebug] CUDA devices available:" << devs;
//     }
// #endif
//     if (debugPipeline) {
//         qDebug() << "[PipelineDebug] Steps configured:" << pipeline.size();
//         for (size_t i = 0; i < pipeline.size(); ++i) {
//             auto name = QString::fromStdString(pipeline[i]->getName());
//             auto pref = pipeline[i]->getPreferredMemoryLocation() == MemoryLocation::GPU ? "GPU" : "CPU";
//             qDebug() << "  [" << i << "]" << name << "pref=" << pref;
//         }
//     }
//     //start and iterate
//     std::vector<cv::Point2f> mass_center_list;
//     std::vector<cv::Point2d> world_center_list; 
//     while (std::chrono::steady_clock::now() - start_time < std::chrono::minutes(1)) 
//     {
//          auto frame = frameSource->nextFrame();
    
//          auto nmomentFrameCaptured = std::chrono::steady_clock::now();
//          if (frame) {
//              FrameMemoryObject result = ProcessingPipeline::processLine(pipeline, *frame, configs);
//             }
//         double dt = std::chrono::duration<double>(nmomentFrameCaptured - last_time).count(); // seconds
//         last_time = nmomentFrameCaptured;
         
//         // Calculate instantaneous FPS and update moving average
//         double fps = (dt > 0.0) ? (1.0 / dt) : 0.0;
//         avg_fps = (1.0 - alpha) * avg_fps + alpha * fps;
//         qDebug() << "Instantaneous FPS:" << fps << ", Moving Average FPS:" << avg_fps;
//         auto sleepTimer = interval - std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - nmomentFrameCaptured);
//         qDebug() << "sleepTimer ms:" << sleepTimer.count();
//         std::this_thread::sleep_for(sleepTimer);
//    }//end while
//    std::cout << "finished\n";
   

//     return 0;
// }



using namespace lbt;

int main(int argc, char* argv[]) {
    std::cout << "=== RecordManager Example (VimbaX) ===" << std::endl;

    // --- Opencv Camera Source Commented out 
    //auto frameSource = std::make_shared<OpenCVFrameSource>(0);

    // --- VimbaX camera parameters ---
    /// Adjust these to match your camera; leave serial empty to match by MAC only.
    const std::string ctiPath   = "../../vimbax/cti/VimbaGigETL.cti";
    const std::string serial    = "";                  // e.g. "02-2611A-07380"
    const std::string macOrIface = "00:0F:31:03:38:D4"; // Camera MAC → maps to DEV_000F310338D4
    const std::string cameraIp  = "192.168.178.99";

    // 0. Start VimbaX system (manages VmbSystem lifetime)
    auto& vmbMgr = CameraManagerVimbax::getInstance();
    std::cout << "Starting CameraManagerVimbax..." << std::endl;
    if (!vmbMgr.startup(ctiPath)) {
        std::cerr << "Failed to start VimbaX camera system" << std::endl;
        return -1;
    }
    // Wait for GigE broadcast discovery
    std::cout << "Waiting 5s for camera discovery..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // 1. Create VimbaXFrameSource
    auto frameSource = std::make_shared<VimbaXFrameSource>(serial, macOrIface, /*debug=*/true, cameraIp);

    if (!frameSource->isReady()) {
        std::cerr << "VimbaXFrameSource not ready — camera not found or could not be opened" << std::endl;
        vmbMgr.shutdown();
        return -1;
    }

    std::cout << "VimbaX camera opened successfully" << std::endl;
    
    // 2. Create a processing pipeline
    std::vector<std::shared_ptr<ProcessStep>> pipeline;
    std::vector<FilterConfig> configs;
    ProcessingMode mode = ProcessingMode::CUDA_PREFERRED;

    
    // Add a frame writer step
    pipeline.push_back(ProcessStepFactory::createFrameWriter(mode));
    FilterConfig frameWriterConfig;
    frameWriterConfig.setParameter("output_dir", "/tmp/record3k/file-rec-test-vimba-001/frames");
    frameWriterConfig.setParameter("format", "jpg");
    configs.push_back(frameWriterConfig);
    
    // Build pipeline config
    PipelineConfigLoader::PipelineConfig pipeConfig;
    pipeConfig.steps = pipeline;
    pipeConfig.configs = configs;
    pipeConfig.mode = mode;
    
    // 3. Create RecordManager configuration
    RecordManager::RecordConfig recordConfig;
    recordConfig.frameSource = frameSource;
    recordConfig.pipeline = pipeConfig;
    recordConfig.totalTimeSeconds = 20.0;  // 20 seconds
    recordConfig.targetFPS = 10.0;          // 10 FPS
    recordConfig.recordUID = "test-vimba-001";
    recordConfig.outputDir = "/tmp/record3k";
    
    //set record log mode to DEBUG to save raw frames, PRODUCTION to skip frame saving
    recordConfig.logMode = ResultLogger::LogMode::DEBUG; // Set to DEBUG to save raw frames, PRODUCTION to skip frame saving
    
    // Optional: add calibration and study files (if they exist)
    // recordConfig.calibrationFile = "path/to/calibration.xml";
    // recordConfig.studyFile = "path/to/study.json";
    
    // 4. Create RecordManager instance
    RecordManager recorder(std::move(recordConfig));
    
    std::cout << "Initial state: " << recorder.getStateString() << std::endl;
    
    // 5. Start recording
    if (!recorder.start()) {
        std::cerr << "Failed to start recording" << std::endl;
        return -1;
    }
    
    std::cout << "Recording state: " << recorder.getStateString() << std::endl;
    
    // 6. Monitor progress
    while (recorder.getState() == RecordManager::State::RECORDING) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        auto stats = recorder.getStatistics();
        std::cout << "Progress: " << stats.framesRecorded << " frames, "
                  << "FPS: " << stats.actualFPS << std::endl;
    }
    
    // Wait for completion
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    std::cout << "Final state: " << recorder.getStateString() << std::endl;
    
    // 7. Get final statistics
    auto finalStats = recorder.getStatistics();
    std::cout << "\n=== Recording Statistics ===" << std::endl;
    std::cout << "Frames recorded: " << finalStats.framesRecorded << std::endl;
    std::cout << "Average FPS: " << finalStats.actualFPS << std::endl;
    std::cout << "Completed: " << (finalStats.completed ? "Yes" : "No") << std::endl;
    std::cout << "Output directory: " << recorder.getOutputPath() << std::endl;
    
    // 8. Pack the recording into an archive
    if (recorder.getState() == RecordManager::State::STOPPED) {
        std::cout << "\n=== Packing Recording ===" << std::endl;
        auto archivePath = recorder.pack();
        
        if (archivePath) {
            std::cout << "Successfully packed to: " << *archivePath << std::endl;
            std::cout << "Final state: " << recorder.getStateString() << std::endl;
        } else {
            std::cerr << "Failed to pack recording" << std::endl;
        }
    }
    
    std::cout << "\n=== Example Complete ===" << std::endl;

    // Shut down VimbaX cleanly
    vmbMgr.shutdown();
    return 0;
}
