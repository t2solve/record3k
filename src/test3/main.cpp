
#ifdef VIMBAX_ENABLED
#include <VmbCPP/VmbCPP.h>
#include <VmbCPP/Frame.h>
#include <VmbCPP/Camera.h>
#include <VmbCPP/Feature.h>
#include <VmbCPP/Interface.h>
#include <VmbCPP/IFrameObserver.h>
#endif

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


#ifdef VIMBAX_ENABLED
std::vector<std::tuple<std::string, std::string>> ListCameraSerialAndInterface()
{
    std::vector<std::tuple<std::string, std::string>> result;

    VmbCPP::VmbSystem& sys = VmbCPP::VmbSystem::GetInstance();
    VmbErrorType err = sys.Startup();
    if (err != VmbErrorSuccess)
        return result;

    VmbCPP::CameraPtrVector cameras;
    err = sys.GetCameras(cameras);
    if (err == VmbErrorSuccess)
    {
        for (const auto& camera : cameras)
        {
            std::string serial, interfaceID;
            if (camera->GetSerialNumber(serial) != VmbErrorSuccess)
                serial = "";
            if (camera->GetInterfaceID(interfaceID) != VmbErrorSuccess)
                interfaceID = "";
            result.emplace_back(serial, interfaceID);
        }
    }

    sys.Shutdown();
    return result;
}
#else
std::vector<std::tuple<std::string, std::string>> ListCameraSerialAndInterface()
{
    // VimbaX not enabled; return empty list
    return {};
}
#endif

// Function to load camera calibration from XML file
// Returns FilterConfig with intrinsic and distortion params on success, nullopt on failure
std::optional<FilterConfig> loadCameraCalibration(const std::string& xmlPath, 
                          cv::Mat& cameraMatrix, 
                          cv::Mat& distCoeffs,
                          cv::Mat& extrinsicsCombined) 
{
    FilterConfig lensConfig;
    cv::FileStorage fs(xmlPath, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        std::cerr << "Failed to open calibration file: " << xmlPath << std::endl;
        return std::nullopt;
    }
    
    fs["camera_matrix"] >> cameraMatrix;
    fs["distortion_coefficients"] >> distCoeffs;
    fs["extrinsic_parameters"] >> extrinsicsCombined;
    fs.release();
    
    if (cameraMatrix.empty() || distCoeffs.empty()) {
        std::cerr << "Failed to read camera_matrix or distortion_coefficients" << std::endl;
        return std::nullopt;
    }

    
    // Extract camera matrix parameters
    lensConfig.setParameter("fx", cameraMatrix.at<double>(0, 0));
    lensConfig.setParameter("fy", cameraMatrix.at<double>(1, 1));
    lensConfig.setParameter("cx", cameraMatrix.at<double>(0, 2));
    lensConfig.setParameter("cy", cameraMatrix.at<double>(1, 2));
    
    // Extract distortion coefficients (k1, k2, p1, p2, k3)
    lensConfig.setParameter("k1", distCoeffs.at<double>(0));
    lensConfig.setParameter("k2", distCoeffs.at<double>(1));
    lensConfig.setParameter("p1", distCoeffs.at<double>(2));
    lensConfig.setParameter("p2", distCoeffs.at<double>(3));
    lensConfig.setParameter("k3", distCoeffs.at<double>(4));
    
    return lensConfig;
}

// 

int main(int argc, char* argv[])
{
      // Defaults: image directory and calibration file
    std::string image_dir = "build/bin/data-frames";
    std::string calib_xml = "build/bin/camera_calibration.xml";
    // Positional overrides if provided
    if (argc >= 2) image_dir = argv[1];
    if (argc >= 3) calib_xml = argv[2];
    if (argc == 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
        std::cerr << "Usage: " << argv[0] << " [image_directory] [calibration_xml]" << std::endl;
        std::cerr << "Defaults: image_directory='" << image_dir << "', calibration_xml='" << calib_xml << "'\n";
        return 0;
    }

    //we list all connected cameras
    auto cams = ListCameraSerialAndInterface();
    std::cout << "Connected cameras:\n";
    for (const auto& [serial, interfaceID] : cams) {
        std::cout << " - Serial: " << serial << ", Interface: " << interfaceID << "\n";
    }
    //we select the first camera if any
    std::shared_ptr<IFrameSource> frameSource;
    if (!cams.empty()) {
        const auto& [serial, interfaceID] = cams.front();
        frameSource = std::make_shared<VimbaXFrameSource>(serial, interfaceID, true);
    }
    else {
        std::cout << "No cameras found; exiting. Using disk image frame source instead.\n";
        frameSource = std::make_shared<DiskImageFrameSource>(image_dir, "*.jpg");
    }

    //test 
    if (frameSource == nullptr) {
        std::cerr << "Failed to create frame source; exiting.\n";
        return -1;
    }
    // we build the processing pipeline from by hand
    // Create background subtraction pipeline
    std::vector<std::shared_ptr<ProcessStep>> pipeline;
    std::vector<FilterConfig> configs;
    ProcessingMode mode = ProcessingMode::CUDA_PREFERRED;
    
    // Step 1: Camera Calibration / Lens Correction
    pipeline.push_back(ProcessStepFactory::createLensCorrection(mode));
    cv::Mat cameraMatrix, distCoeffs, extrinsicsCombined;
    std::optional<FilterConfig> lensConfig = loadCameraCalibration(calib_xml, cameraMatrix, distCoeffs, extrinsicsCombined);
    if (lensConfig == std::nullopt) {
        // No value returned
        std::cerr << "No lens config available!" << std::endl;
        return -1;
    } else {
        configs.push_back(*lensConfig);
    }

    // Step 2: cut roi (circular crop)
    pipeline.push_back(ProcessStepFactory::createROICircleCrop(mode));
    FilterConfig roiConfig;
    roiConfig.setParameter("center_x", 985);
    roiConfig.setParameter("center_y", 725);
    roiConfig.setParameter("radius", 700);
    configs.push_back(roiConfig);
    
    // Step 3: background subtraction
    pipeline.push_back(ProcessStepFactory::createBackgroundSubtractionMOG2(mode));
    FilterConfig bgsConfig;
    bgsConfig.setParameter("history", 500);
    bgsConfig.setParameter("varThreshold", 16.0);
    bgsConfig.setParameter("detectShadows", false);
    configs.push_back(bgsConfig);

    // Step 4: Binary Thresholding
    pipeline.push_back(ProcessStepFactory::createBinaryThreshold(mode));
    FilterConfig binaryConfig;
    binaryConfig.setParameter("threshold", 80);
    binaryConfig.setParameter("max_value", 255);
    configs.push_back(binaryConfig);

    // Step 5: Morphology Close
    pipeline.push_back(ProcessStepFactory::createMorphologyClose(mode));
    FilterConfig morphConfig;
    morphConfig.setParameter("ksize", int(15)); // kernel size
    morphConfig.setParameter("shape", int(2));  // 0=RECT,1=CROSS,2=ELLIPSE
    morphConfig.setParameter("iterations", int(2)); // number of iterations
    configs.push_back(morphConfig);

    // // Step 5: GaussianBlur (to reduce noise before contour detection)
    // pipeline.push_back(ProcessStepFactory::createGaussianBlur(mode));
    // FilterConfig blurConfig;
    // // blurConfig.setParameter("kernel_size", int(3));
    // // blurConfig.setParameter("sigma_x", double(1.0));
    // blurConfig.setParameter("kernel_size", int(11));
    // blurConfig.setParameter("sigma_x", double(3.0));
    // configs.push_back(blurConfig);

    // Step 6: Edge Detection
    pipeline.push_back(ProcessStepFactory::createEdgeDetection(mode));
    FilterConfig edgeConfig;
    edgeConfig.setParameter("threshold1", double(150.0)); //min
    edgeConfig.setParameter("threshold2", double(200.0)); //max
    edgeConfig.setParameter("aperture_size", int(3));
    configs.push_back(edgeConfig);

    // Step 7: Contour Area Filtering
    pipeline.push_back(ProcessStepFactory::createContourAreaFilter(mode));
    FilterConfig contourConfig;
    contourConfig.setParameter("min_area", 7);
    contourConfig.setParameter("max_area", 300000);
    contourConfig.setParameter("debug", 1.0); // enable debug overlays
    configs.push_back(contourConfig);

    // Step 8: Mass Center Overlay
    pipeline.push_back(ProcessStepFactory::createMassCenterOverlay(ProcessingMode::CPU_ONLY));
    FilterConfig massCenterConfig; // no parameters needed
    configs.push_back(massCenterConfig);

    // Step 9: Project Point to 2D Surface
    pipeline.push_back(ProcessStepFactory::createProjectPointTo2DSurface(ProcessingMode::CPU_ONLY));
    FilterConfig projectConfig; // no parameters needed
    projectConfig.setParameter("camera_matrix", cameraMatrix);
    projectConfig.setParameter("distortion_coefficients", distCoeffs);
    projectConfig.setParameter("extrinsics_combined", extrinsicsCombined);
    configs.push_back(projectConfig);

    //build a config to sum it up
    PipelineConfigLoader::PipelineConfig pipeConfig;
    pipeConfig.steps = pipeline;
    pipeConfig.configs = configs;
    pipeConfig.mode = mode;

    // we save the pipeline configuration to XML for future use
    //PipelineConfigLoader::saveToXML("build/bin/pipeline_test3_config.xml", pipeConfig);
    // Load pipeline from XML
//    //Configure processing pipeline
//    auto pipeConfig = PipelineConfigLoader::loadFromXML("config.xml");
//    //ProcessingMode mode = pipeConfig.mode;
//       // Process the image
//    qDebug() << "Processing pipeline with" << pipeConfig.steps.size() << "steps...";

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
         auto now = std::chrono::steady_clock::now();
         double dt = std::chrono::duration<double>(now - last_time).count(); // seconds
         last_time = now;

         // Calculate instantaneous FPS and update moving average
         double fps = (dt > 0.0) ? (1.0 / dt) : 0.0;
         avg_fps = (1.0 - alpha) * avg_fps + alpha * fps;

         if (frame) {
            FrameMemoryObject result;
            if (debugPipeline && !printedPipelineOnce) {
                // Walk the pipeline step by step once and log memory locations
                FrameMemoryObject cur = *frame;
                for (size_t i = 0; i < pipeline.size(); ++i) {
                    auto beforeLoc = cur.getMemoryLocation();
                    cur = pipeline[i]->process(cur, configs[i]);
                    auto afterLoc = cur.getMemoryLocation();
                    auto name = QString::fromStdString(pipeline[i]->getName());
                    qDebug() << "[PipelineDebug] step" << i << name
                             << "loc:" << (beforeLoc == MemoryLocation::GPU ? "GPU" : "CPU")
                             << "->" << (afterLoc == MemoryLocation::GPU ? "GPU" : "CPU");
                }
                result = cur;
                printedPipelineOnce = true;
            } else {
                result = ProcessingPipeline::processLine(pipeline, *frame, configs);
            }
            //we check for mass center metadata produced by MassCenterOverlay
            if (result.hasMetadata("mass_center_point_pixel")) {
                cv::Point2f massCenter = result.getMetadata<cv::Point2f>("mass_center_point_pixel");
                cv::Point2d worldPt = result.getMetadata<cv::Point2d>("mass_center_point_world");
                if (massCenter.x >= 0 && massCenter.y >= 0) {
                    std::cout << "Mass Center Pixel: (" << massCenter.x << ", " << massCenter.y << ")\n";
                    mass_center_list.push_back(massCenter);
                    std::cout << "Projected World in mm: (" << worldPt.x << ", " << worldPt.y << ")\n";
                    world_center_list.push_back(worldPt);
                    // //we draw the mass center on the original image and save it
                    // cv::Mat outputImage = frame->getCpuMat().clone();
                    // cv::circle(outputImage, massCenter, 10, cv::Scalar(0, 0, 255), -1);

                    // cv::imwrite("data/mass_center_" + std::to_string(img_counter++) + ".jpg", outputImage);
                } 
                else {
                    std::cout << "Mass Center could not be calculated.\n";
                }
            }

            // Optionally print or log the moving average FPS
            std::cout << "Moving average FPS: " << avg_fps << std::endl;
            double target_fps = frequency; // 10.0
            double delta_fps = avg_fps - target_fps;

            std::cout << "Moving average FPS: " << avg_fps
             << " | Target FPS: " << target_fps
             << " | Delta: " << delta_fps << std::endl;
         }
         std::this_thread::sleep_for(interval);
   }//end while
   std::cout << "XPixel;YPixel;XRealInMM;YRealInMM\n";
   for (size_t i = 0; i < mass_center_list.size(); ++i) {
       const auto& p = mass_center_list[i];
       const auto& p2 = world_center_list[i];
       std::cout << p.x << ";" << p.y << ";" << p2.x << ";" << p2.y << "\n";
   }

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
