
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
#include <liblbt/camera_calibrator.h>


using namespace lbt;

int main(int argc, char* argv[]) {
    std::cout << "=== Calibration Example ===" << std::endl;
    
    lbt::CameraCalibrator::Config config;
    config.boardSize = cv::Size(4, 5);
    config.squareSize = 0.025f;
    config.writeExtrinsics = true;
    config.writePoints = true;

    lbt::CameraCalibrator calibrator(config);
    auto result = calibrator.calibrate("/tmp/record3k/file-rec-cal-1769256756/", "camera.yml");

    if (result.success) {
        std::cout << "Calibration succeeded! Error: " << result.avgReprojectionError << std::endl;
    } else {
        std::cerr << "Calibration failed: " << result.errorMessage << std::endl;
    }

    return 0;
    
}
