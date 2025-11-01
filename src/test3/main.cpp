
#include <VmbCPP/VmbCPP.h>
#include <VmbCPP/Frame.h>
#include <VmbCPP/Camera.h>
#include <VmbCPP/Feature.h>
#include <VmbCPP/Interface.h>
#include <VmbCPP/IFrameObserver.h>

#include <QCoreApplication>
#include <QTimer>
#include <QDebug>
#include <QObject>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/opencv.hpp>

#include <thread>

#include <liblbt/process_step_factory.h>
#include <liblbt/processing_pipeline.h>
#include <liblbt/frame_memory_object.h>
#include <liblbt/pipeline_profiler.h>
#include <liblbt/pipeline_config_loader.h>

#include "disk_image_frame_source.h"

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



int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <image_directory>" << std::endl;
        return 1;
    }
   std::string image_dir = argv[1];
   //Configure processing pipeline
   auto pipeConfig = PipelineConfigLoader::loadFromXML("config.xml");
   //ProcessingMode mode = pipeConfig.mode;
      // Process the image
   qDebug() << "Processing pipeline with" << pipeConfig.steps.size() << "steps...";

   std::unique_ptr<IFrameSource> source = std::make_unique<DiskImageFrameSource>(image_dir, "*.jpg");
   // Set desired frequency (Hz)
   double frequency = 10.0; // 10 frames per second
   auto interval = std::chrono::milliseconds(static_cast<int>(1000.0 / frequency));

    auto start_time = std::chrono::steady_clock::now();
    auto last_time = start_time;
    double avg_fps = 0.0;
    const double alpha = 0.1;

    while (std::chrono::steady_clock::now() - start_time < std::chrono::minutes(1)) 
    {
         auto frame = source->nextFrame();
         auto now = std::chrono::steady_clock::now();
         double dt = std::chrono::duration<double>(now - last_time).count(); // seconds
         last_time = now;

         // Calculate instantaneous FPS and update moving average
         double fps = (dt > 0.0) ? (1.0 / dt) : 0.0;
         avg_fps = (1.0 - alpha) * avg_fps + alpha * fps;

         if (frame) {
            FrameMemoryObject result = ProcessingPipeline::processLine(pipeConfig.steps, *frame, pipeConfig.configs);
         }

         // Optionally print or log the moving average FPS
         std::cout << "Moving average FPS: " << avg_fps << std::endl;
         double target_fps = frequency; // 10.0
         double delta_fps = avg_fps - target_fps;

         std::cout << "Moving average FPS: " << avg_fps
          << " | Target FPS: " << target_fps
          << " | Delta: " << delta_fps << std::endl;
         std::this_thread::sleep_for(interval);
   }


   // // #TODO move to new  build env
   // VmbErrorType err; // Every Vimba X function returns an error code that
   //                   // should always be checked for VmbErrorSuccess (not done here for brevity)
   // VmbSystem& system = VmbSystem::GetInstance();
   // err = system.Startup ();

   // auto cameras = ListCameraSerialAndInterface();
   // for (const auto& [serial, iface] : cameras) {
   //    std::cout << "Serial: " << serial << ", InterfaceID: " << iface << std::endl;
   // }
   // // // select the first camera from the list
   // if (cameras.empty()) {
   //     std::cerr << "No cameras found!" << std::endl;
   //     return -1;
   // }
   // we generate a timer to periodically grab frames from the camera
   

   // // Listing available cameras and selecting the one to use
   // CameraPtrVector cameras;
   // err = system.GetCameras( cameras );
   // CameraPtr camera = cameras.at( 0 ); // For this example we just use the first listed camera
   // err = camera->Open( VmbAccessModeFull );

   // FramePtrVector frames( 5 ); // A list of frames for streaming. We chose to queue 5 frames.
   // IFrameObserverPtr observer(
   //    new FrameObserver( camera ) ); // Our implementation of a frame observer
   // FeaturePtr feature;                // Variable to hold features that need to be used
   // VmbUint32_t payloadSize;           // The payload size of one frame

   // err = camera->GetPayloadSize( payloadSize );

   // for( FramePtrVector::iterator iter = frames.begin(); frames.end() != iter; ++iter )
   // {
   //    ( *iter ).reset( new Frame( payloadSize ) );
   //    err = ( *iter )->RegisterObserver( observer );
   //    err = camera->AnnounceFrame( *iter );
   // }

   // err = camera->StartCapture();

   // for( FramePtrVector ::iterator iter = frames.begin(); frames.end() != iter; ++iter )
   // {
   //    err = camera->QueueFrame( *iter );
   // }

   // err = camera->GetFeatureByName( "AcquisitionMode", feature );
   // err = feature->SetValue( "Continuous" );
   // err = camera->GetFeatureByName( "AcquisitionStart", feature );
   // err = feature->RunCommand();

   // // Program runtime ...
   // // When finished , tear down the acquisition chain , close camera and API

   // err = camera->GetFeatureByName( "AcquisitionStop", feature );
   // err = feature->RunCommand();
   // err = camera->EndCapture();
   // err = camera->FlushQueue();
   // err = camera->RevokeAllFrames();
   // err = camera->Close();
   // err = system.Shutdown();
}
