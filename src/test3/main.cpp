
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
#include <liblbt/camera_manager_vimbax.h>
#include <liblbt/opencv_frame_source.h>
#include <liblbt/calibration_injector.h>

#ifdef VIMBAX_ENABLED
// Minimal test function that directly uses VmbSystem like SDK example
void TestVimbaXDirectly(const std::string& ctiPath = "")
{
    std::cout << "\n=== [DirectTest] Testing VimbaX SDK Directly (like ListCameras.cpp) ===" << std::endl;
    
    VmbCPP::VmbSystem& sys = VmbCPP::VmbSystem::GetInstance();
    
    // Query version
    VmbVersionInfo_t versionInfo;
    VmbErrorType err = sys.QueryVersion(versionInfo);
    if (err == VmbErrorSuccess) {
        std::cout << "[DirectTest] Vmb Version Major: " << versionInfo.major 
                  << " Minor: " << versionInfo.minor 
                  << " Patch: " << versionInfo.patch << std::endl;
    } else {
        std::cerr << "[DirectTest] ERROR: QueryVersion failed with error: " << err << std::endl;
    }
    
    // Startup
    std::cout << "[DirectTest] Calling VmbSystem::Startup()..." << std::endl;
    if (ctiPath.empty()) {
        err = sys.Startup();
    } else {
        std::cout << "[DirectTest] Using CTI path: " << ctiPath << std::endl;
        err = sys.Startup(ctiPath.c_str());
    }
    
    if (err != VmbErrorSuccess) {
        std::cerr << "[DirectTest] ERROR: Could not start system. Error code: " << err << std::endl;
        return;
    }
    
    std::cout << "[DirectTest] *** Startup SUCCESS ***" << std::endl;
    
    // Get transport layers
    VmbCPP::TransportLayerPtrVector transportlayers;
    err = sys.GetTransportLayers(transportlayers);
    if (err == VmbErrorSuccess) {
        std::cout << "[DirectTest] TransportLayers found: " << transportlayers.size() << std::endl;
    } else {
        std::cerr << "[DirectTest] ERROR: GetTransportLayers failed with error: " << err << std::endl;
    }
    
    // Get interfaces
    VmbCPP::InterfacePtrVector interfaces;
    err = sys.GetInterfaces(interfaces);
    if (err == VmbErrorSuccess) {
        std::cout << "[DirectTest] Interfaces found: " << interfaces.size() << std::endl;
    } else {
        std::cerr << "[DirectTest] ERROR: GetInterfaces failed with error: " << err << std::endl;
    }
    
    // Get cameras
    std::cout << "[DirectTest] Calling GetCameras()..." << std::endl;
    VmbCPP::CameraPtrVector cameras;
    err = sys.GetCameras(cameras);
    if (err == VmbErrorSuccess) {
        std::cout << "[DirectTest] *** Cameras found: " << cameras.size() << " ***" << std::endl;
        
        // Print camera details
        for (size_t i = 0; i < cameras.size(); i++) {
            const auto& camera = cameras[i];
            std::string strID, strName, strModel, strSerial, strInterfaceID;
            
            camera->GetID(strID);
            camera->GetName(strName);
            camera->GetModel(strModel);
            camera->GetSerialNumber(strSerial);
            camera->GetInterfaceID(strInterfaceID);
            
            std::cout << "[DirectTest] Camera " << (i+1) << ":" << std::endl;
            std::cout << "[DirectTest]   Name          : " << strName << std::endl;
            std::cout << "[DirectTest]   Model         : " << strModel << std::endl;
            std::cout << "[DirectTest]   ID            : " << strID << std::endl;
            std::cout << "[DirectTest]   Serial        : " << strSerial << std::endl;
            std::cout << "[DirectTest]   Interface ID  : " << strInterfaceID << std::endl;
            
            VmbCPP::TransportLayerPtr pTL;
            if (camera->GetTransportLayer(pTL) == VmbErrorSuccess) {
                std::string tlID, tlPath;
                pTL->GetID(tlID);
                pTL->GetPath(tlPath);
                std::cout << "[DirectTest]   TL ID         : " << tlID << std::endl;
                std::cout << "[DirectTest]   TL Path       : " << tlPath << std::endl;
            }
        }
    } else {
        std::cerr << "[DirectTest] ERROR: Could not list cameras. Error code: " << err << std::endl;
    }
    
    // Shutdown
    std::cout << "[DirectTest] Calling Shutdown()..." << std::endl;
    sys.Shutdown();
    std::cout << "[DirectTest] === Test Complete ===" << std::endl << std::endl;
}

std::vector<lbt::VimbaXCameraInfo> ListCamerasUsingManager(const std::string& ctiPath = "")
{
    std::cout << "[test3] Initializing VimbaX camera system..." << std::endl;
    
    // Get singleton instance and initialize
    auto& vimbaxMgr = lbt::CameraManagerVimbax::getInstance();
    
    if (!vimbaxMgr.startup(ctiPath)) {
        std::cerr << "[test3] Failed to initialize VimbaX system" << std::endl;
        auto status = vimbaxMgr.getStatus();
        std::cerr << "[test3] Error: " << status.errorMessage << std::endl;
        return {};
    }
    
    std::cout << "[test3] VimbaX system initialized successfully" << std::endl;
    
    // Get camera list
    std::cout << "[test3] Getting camera list..." << std::endl;
    auto cameras = vimbaxMgr.getCameras();
    
    std::cout << "[test3] Found " << cameras.size() << " camera(s)" << std::endl;
    
    return cameras;
}
#else
std::vector<lbt::VimbaXCameraInfo> ListCamerasUsingManager(const std::string& ctiPath = "")
{
    std::cerr << "[test3] VimbaX not enabled in build" << std::endl;
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
    std::string image_dir = "testset/frames";
    std::string calib_xml = "testset/camera_calibration.xml";
    // Positional overrides if provided
    if (argc >= 2) image_dir = argv[1];
    if (argc >= 3) calib_xml = argv[2];
    if (argc == 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
        std::cerr << "Usage: " << argv[0] << " [image_directory] [calibration_xml]" << std::endl;
        std::cerr << "Defaults: image_directory='" << image_dir << "', calibration_xml='" << calib_xml << "'\n";
        return 0;
    }
    std::cout << "[test3] Using image directory: " << image_dir << std::endl;
    std::cout << "[test3] Using calibration XML: " << calib_xml << std::endl;

    // //we list all connected cameras using new CameraManagerVimbax
    // std::string ctiPath = "VimbaGigETL.cti"; // or provide full path
    
    // // First test VimbaX directly like the SDK example
    // TestVimbaXDirectly();
    
    // // Then try our wrapper
    // auto cams = ListCamerasUsingManager(ctiPath);
    // std::cout << "Connected cameras:\n";
    // for (const auto& cam : cams) {
    //     std::cout << " - Name: " << cam.name 
    //               << ", Serial: " << cam.serial 
    //               << ", Model: " << cam.model
    //               << ", MAC: " << cam.macAddress
    //               << ", Interface: " << cam.interfaceId << "\n";
    // }
    
    // //we select the first camera if any
    // std::shared_ptr<IFrameSource> frameSource;
    // if (!cams.empty()) {
    //     // Use the first camera found - try by serial number and interface
    //     const auto& firstCam = cams.front();
    //     std::cout << "[test3] Using camera: " << firstCam.name << " (S/N: " << firstCam.serial << ")" << std::endl;
    //     frameSource = std::make_shared<VimbaXFrameSource>(firstCam.serial, firstCam.interfaceId, true);
    // }
    
    //std::cout << "No cameras found; using disk image frame source instead.\n";
    auto frameSource = std::make_shared<DiskImageFrameSource>(image_dir, "*.jpg");
    
    // Default camera
    //auto frameSource = std::make_shared<OpenCVFrameSource>(0);



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

    // // we build the processing pipeline from by hand
    // // Create background subtraction pipeline
    // std::vector<std::shared_ptr<ProcessStep>> pipeline;
    // std::vector<FilterConfig> configs;
    // ProcessingMode mode = ProcessingMode::CUDA_PREFERRED;
    
    // // Step 1: Camera Calibration / Lens Correction
    // pipeline.push_back(ProcessStepFactory::createLensCorrection(mode));
    // cv::Mat cameraMatrix, distCoeffs, extrinsicsCombined;
    // std::optional<FilterConfig> lensConfig = loadCameraCalibration(calib_xml, cameraMatrix, distCoeffs, extrinsicsCombined);
    // if (lensConfig == std::nullopt) {
    //     // No value returned
    //     std::cerr << "No lens config available!" << std::endl;
    //     return -1;
    // } else {
    //     configs.push_back(*lensConfig);
    // }

    // // Step 2: cut roi (circular crop)
    // pipeline.push_back(ProcessStepFactory::createROICircleCrop(mode));
    // FilterConfig roiConfig;
    // roiConfig.setParameter("center_x", 985);
    // roiConfig.setParameter("center_y", 725);
    // roiConfig.setParameter("radius", 700);
    // configs.push_back(roiConfig);
    
    // // Step 3: background subtraction
    // pipeline.push_back(ProcessStepFactory::createBackgroundSubtractionMOG2(mode));
    // FilterConfig bgsConfig;
    // bgsConfig.setParameter("history", 500);
    // bgsConfig.setParameter("varThreshold", 16.0);
    // bgsConfig.setParameter("detectShadows", false);
    // configs.push_back(bgsConfig);

    // // Step 4: Binary Thresholding
    // pipeline.push_back(ProcessStepFactory::createBinaryThreshold(mode));
    // FilterConfig binaryConfig;
    // binaryConfig.setParameter("threshold", 80);
    // binaryConfig.setParameter("max_value", 255);
    // configs.push_back(binaryConfig);

    // // Step 5: Morphology Close
    // pipeline.push_back(ProcessStepFactory::createMorphologyClose(mode));
    // FilterConfig morphConfig;
    // morphConfig.setParameter("ksize", int(15)); // kernel size
    // morphConfig.setParameter("shape", int(2));  // 0=RECT,1=CROSS,2=ELLIPSE
    // morphConfig.setParameter("iterations", int(2)); // number of iterations
    // configs.push_back(morphConfig);

    // // // Step 5: GaussianBlur (to reduce noise before contour detection)
    // // pipeline.push_back(ProcessStepFactory::createGaussianBlur(mode));
    // // FilterConfig blurConfig;
    // // // blurConfig.setParameter("kernel_size", int(3));
    // // // blurConfig.setParameter("sigma_x", double(1.0));
    // // blurConfig.setParameter("kernel_size", int(11));
    // // blurConfig.setParameter("sigma_x", double(3.0));
    // // configs.push_back(blurConfig);

    // // Step 6: Edge Detection
    // pipeline.push_back(ProcessStepFactory::createEdgeDetection(mode));
    // FilterConfig edgeConfig;
    // edgeConfig.setParameter("threshold1", double(150.0)); //min
    // edgeConfig.setParameter("threshold2", double(200.0)); //max
    // edgeConfig.setParameter("aperture_size", int(3));
    // configs.push_back(edgeConfig);

    // // Step 7: Contour Area Filtering
    // pipeline.push_back(ProcessStepFactory::createContourAreaFilter(mode));
    // FilterConfig contourConfig;
    // contourConfig.setParameter("min_area", 7);
    // contourConfig.setParameter("max_area", 300000);
    // contourConfig.setParameter("debug", 1.0); // enable debug overlays
    // configs.push_back(contourConfig);

    // // Step 8: Mass Center Overlay
    // pipeline.push_back(ProcessStepFactory::createMassCenterOverlay(ProcessingMode::CPU_ONLY));
    // FilterConfig massCenterConfig; // no parameters needed
    // configs.push_back(massCenterConfig);

    // // Step 9: Project Point to 2D Surface
    // pipeline.push_back(ProcessStepFactory::createProjectPointTo2DSurface(ProcessingMode::CPU_ONLY));
    // FilterConfig projectConfig; // no parameters needed
    // projectConfig.setParameter("camera_matrix", cameraMatrix);
    // projectConfig.setParameter("distortion_coefficients", distCoeffs);
    // projectConfig.setParameter("extrinsics_combined", extrinsicsCombined);
    // configs.push_back(projectConfig);

    // //build a config to sum it up
    // PipelineConfigLoader::PipelineConfig pipeConfig;
    // pipeConfig.steps = pipeline;
    // pipeConfig.configs = configs;
    // pipeConfig.mode = mode;

    // //we save the pipeline configuration to XML for future use
    // PipelineConfigLoader::saveToXML("pipeline_test3_config.xml", pipeConfig);
    // PipelineConfigLoader::saveToJSON("pipeline_test3_config.json", pipeConfig);
    //Load pipeline from XML
    //Configure processing pipeline
    auto pipeConfig = PipelineConfigLoader::loadFromJSON("testset/pipe-1769792531203-9269.json");
    ProcessingMode mode = pipeConfig.mode;
    
    // ===== TEST CalibrationInjector =====
    std::cout << "\n===== Testing CalibrationInjector =====" << std::endl;
    
    // Print pipeline steps and identify calibration-related ones
    std::cout << "\n[BEFORE INJECTION] Pipeline configuration:" << std::endl;
    for (size_t i = 0; i < pipeConfig.steps.size(); ++i) {
        std::cout << "  Step " << i << ": " << pipeConfig.steps[i]->getName();
        FilterType ft = pipeConfig.steps[i]->getFilterType();
        
        if (ft == FilterType::LENS_CORRECTION) {
            std::cout << " [LENS_CORRECTION]" << std::endl;
            std::cout << "    fx: " << pipeConfig.configs[i].getParameter("fx", -1.0) << std::endl;
            std::cout << "    fy: " << pipeConfig.configs[i].getParameter("fy", -1.0) << std::endl;
            std::cout << "    cx: " << pipeConfig.configs[i].getParameter("cx", -1.0) << std::endl;
            std::cout << "    cy: " << pipeConfig.configs[i].getParameter("cy", -1.0) << std::endl;
            std::cout << "    k1: " << pipeConfig.configs[i].getParameter("k1", -999.0) << std::endl;
            std::cout << "    k2: " << pipeConfig.configs[i].getParameter("k2", -999.0) << std::endl;
            std::cout << "    k3: " << pipeConfig.configs[i].getParameter("k3", -999.0) << std::endl;
        } else if (ft == FilterType::PROJECT_POINT_TO_2D_SURFACE) {
            std::cout << " [PROJECT_POINT_TO_2D_SURFACE]" << std::endl;
            // Check if matrices exist
            bool hasCameraMat = pipeConfig.configs[i].hasParameter("camera_matrix");
            bool hasDistCoeffs = pipeConfig.configs[i].hasParameter("distortion_coefficients");
            bool hasExtrinsics = pipeConfig.configs[i].hasParameter("extrinsics_combined");
            std::cout << "    camera_matrix: " << (hasCameraMat ? "present" : "MISSING") << std::endl;
            std::cout << "    distortion_coefficients: " << (hasDistCoeffs ? "present" : "MISSING") << std::endl;
            std::cout << "    extrinsics_combined: " << (hasExtrinsics ? "present" : "MISSING") << std::endl;
            if (hasCameraMat) {
                cv::Mat cm;
                pipeConfig.configs[i].getParameter("camera_matrix", cm);
                std::cout << "    camera_matrix size: " << cm.size() << std::endl;
            }
        } else {
            std::cout << std::endl;
        }
    }
    
    // Option 1: Load from archive (replace with your actual fileUID)
    std::string testFileUID = "file-rec-cal-1769775932988-9680"; // example: "file-rec-123456789"
    auto calibData = lbt::CalibrationInjector::loadFromArchive(testFileUID, "testset");
    
    // Option 2: Load directly from compact XML file (if you have one)
    // auto calibData = lbt::CalibrationInjector::loadFromCompactXML("path/to/calibration-compact-rec-123.xml");
    
    if (calibData.valid) {
        std::cout << "\n[test3] Calibration loaded successfully!" << std::endl;
        
        // Inject into pipeline
        int modifiedSteps = lbt::CalibrationInjector::injectIntoPipeline(pipeConfig, calibData);
        std::cout << "[test3] Injected calibration into " << modifiedSteps << " pipeline steps" << std::endl;
        
        // Print AFTER injection
        std::cout << "\n[AFTER INJECTION] Pipeline configuration:" << std::endl;
        for (size_t i = 0; i < pipeConfig.steps.size(); ++i) {
            FilterType ft = pipeConfig.steps[i]->getFilterType();
            
            if (ft == FilterType::LENS_CORRECTION) {
                std::cout << "  Step " << i << ": " << pipeConfig.steps[i]->getName() << " [LENS_CORRECTION]" << std::endl;
                std::cout << "    fx: " << pipeConfig.configs[i].getParameter("fx", -1.0) << std::endl;
                std::cout << "    fy: " << pipeConfig.configs[i].getParameter("fy", -1.0) << std::endl;
                std::cout << "    cx: " << pipeConfig.configs[i].getParameter("cx", -1.0) << std::endl;
                std::cout << "    cy: " << pipeConfig.configs[i].getParameter("cy", -1.0) << std::endl;
                std::cout << "    k1: " << pipeConfig.configs[i].getParameter("k1", -999.0) << std::endl;
                std::cout << "    k2: " << pipeConfig.configs[i].getParameter("k2", -999.0) << std::endl;
                std::cout << "    k3: " << pipeConfig.configs[i].getParameter("k3", -999.0) << std::endl;
            } else if (ft == FilterType::PROJECT_POINT_TO_2D_SURFACE) {
                std::cout << "  Step " << i << ": " << pipeConfig.steps[i]->getName() << " [PROJECT_POINT_TO_2D_SURFACE]" << std::endl;
                bool hasCameraMat = pipeConfig.configs[i].hasParameter("camera_matrix");
                bool hasDistCoeffs = pipeConfig.configs[i].hasParameter("distortion_coefficients");
                bool hasExtrinsics = pipeConfig.configs[i].hasParameter("extrinsics_combined");
                std::cout << "    camera_matrix: " << (hasCameraMat ? "present" : "MISSING") << std::endl;
                std::cout << "    distortion_coefficients: " << (hasDistCoeffs ? "present" : "MISSING") << std::endl;
                std::cout << "    extrinsics_combined: " << (hasExtrinsics ? "present" : "MISSING") << std::endl;
                if (hasCameraMat) {
                    cv::Mat cm;
                    pipeConfig.configs[i].getParameter("camera_matrix", cm);
                    std::cout << "    camera_matrix size: " << cm.size() << std::endl;
                }
            }
        }
        
        // Alternatively, inject into specific configs manually:
        // if (pipeConfig.steps.size() > 0) {
        //     lbt::CalibrationInjector::injectLensCorrection(pipeConfig.configs[0], calibData);
        //     lbt::CalibrationInjector::injectProjection(pipeConfig.configs[8], calibData);
        // }
    } else {
        std::cout << "[test3] Warning: Could not load calibration: " << calibData.errorMessage << std::endl;
        std::cout << "[test3] Continuing with existing pipeline configuration..." << std::endl;
    }
    
    std::cout << "\n===== CalibrationInjector Test Complete =====\n" << std::endl;
    
    
    // ===== END TEST =====
    
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
        qDebug() << "[PipelineDebug] Steps configured:" << pipeConfig.steps.size();
        for (size_t i = 0; i < pipeConfig.steps.size(); ++i) {
            auto name = QString::fromStdString(pipeConfig.steps[i]->getName());
            auto pref = pipeConfig.steps[i]->getPreferredMemoryLocation() == MemoryLocation::GPU ? "GPU" : "CPU";
            qDebug() << "  [" << i << "]" << name << "pref=" << pref;
        }
    }
//     //start and iterate
//     std::vector<cv::Point2f> mass_center_list;
//     std::vector<cv::Point2d> world_center_list; 
//     while (std::chrono::steady_clock::now() - start_time < std::chrono::minutes(1)) 
//     {
//          auto frame = frameSource->nextFrame();
//          auto now = std::chrono::steady_clock::now();
//          double dt = std::chrono::duration<double>(now - last_time).count(); // seconds
//          last_time = now;

//          // Calculate instantaneous FPS and update moving average
//          double fps = (dt > 0.0) ? (1.0 / dt) : 0.0;
//          avg_fps = (1.0 - alpha) * avg_fps + alpha * fps;

//          if (frame) {
//             FrameMemoryObject result;
//             if (debugPipeline && !printedPipelineOnce) {
//                 // Walk the pipeline step by step once and log memory locations
//                 FrameMemoryObject cur = *frame;
//                 for (size_t i = 0; i < pipeConfig.steps.size(); ++i) {
//                     auto beforeLoc = cur.getMemoryLocation();
//                     cur = pipeConfig.steps[i]->process(cur, pipeConfig.configs[i]);
//                     auto afterLoc = cur.getMemoryLocation();
//                     auto name = QString::fromStdString(pipeConfig.steps[i]->getName());
//                     qDebug() << "[PipelineDebug] step" << i << name
//                              << "loc:" << (beforeLoc == MemoryLocation::GPU ? "GPU" : "CPU")
//                              << "->" << (afterLoc == MemoryLocation::GPU ? "GPU" : "CPU");
//                 }
//                 result = cur;
//                 printedPipelineOnce = true;
//             } else {
//                 result = ProcessingPipeline::processLine(pipeConfig.steps, *frame, pipeConfig.configs);
//             }
//             //we check for mass center metadata produced by MassCenterOverlay
//             if (result.hasMetadata("mass_center_point_pixel")) {
//                 cv::Point2f massCenter = result.getMetadata<cv::Point2f>("mass_center_point_pixel");
//                 cv::Point2d worldPt = result.getMetadata<cv::Point2d>("mass_center_point_world");
//                 if (massCenter.x >= 0 && massCenter.y >= 0) {
//                     std::cout << "Mass Center Pixel: (" << massCenter.x << ", " << massCenter.y << ")\n";
//                     mass_center_list.push_back(massCenter);
//                     std::cout << "Projected World in mm: (" << worldPt.x << ", " << worldPt.y << ")\n";
//                     world_center_list.push_back(worldPt);
//                     // //we draw the mass center on the original image and save it
//                     // cv::Mat outputImage = frame->getCpuMat().clone();
//                     // cv::circle(outputImage, massCenter, 10, cv::Scalar(0, 0, 255), -1);

//                     // cv::imwrite("data/mass_center_" + std::to_string(img_counter++) + ".jpg", outputImage);
//                 } 
//                 else {
//                     std::cout << "Mass Center could not be calculated.\n";
//                 }
//             }

//             // Optionally print or log the moving average FPS
//             std::cout << "Moving average FPS: " << avg_fps << std::endl;
//             double target_fps = frequency; // 10.0
//             double delta_fps = avg_fps - target_fps;

//             std::cout << "Moving average FPS: " << avg_fps
//              << " | Target FPS: " << target_fps
//              << " | Delta: " << delta_fps << std::endl;
//          }
//          std::this_thread::sleep_for(interval);
//    }//end while
//    std::cout << "XPixel;YPixel;XRealInMM;YRealInMM\n";
//    for (size_t i = 0; i < mass_center_list.size(); ++i) {
//        const auto& p = mass_center_list[i];
//        const auto& p2 = world_center_list[i];
//        std::cout << p.x << ";" << p.y << ";" << p2.x << ";" << p2.y << "\n";
//    }

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
