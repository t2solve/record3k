
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


int main()
{
  // #TODO move to new  build env
   VmbErrorType err; // Every Vimba X function returns an error code that
                     // should always be checked for VmbErrorSuccess (not done here for brevity)
   VmbSystem& system = VmbSystem::GetInstance();
   err = system.Startup ();

   // Listing available cameras and selecting the one to use
   CameraPtrVector cameras;
   err = system.GetCameras( cameras );
   CameraPtr camera = cameras.at( 0 ); // For this example we just use the first listed camera
   err = camera->Open( VmbAccessModeFull );

   FramePtrVector frames( 5 ); // A list of frames for streaming. We chose to queue 5 frames.
   IFrameObserverPtr observer(
      new FrameObserver( camera ) ); // Our implementation of a frame observer
   FeaturePtr feature;                // Variable to hold features that need to be used
   VmbUint32_t payloadSize;           // The payload size of one frame

   err = camera->GetPayloadSize( payloadSize );

   for( FramePtrVector::iterator iter = frames.begin(); frames.end() != iter; ++iter )
   {
      ( *iter ).reset( new Frame( payloadSize ) );
      err = ( *iter )->RegisterObserver( observer );
      err = camera->AnnounceFrame( *iter );
   }

   err = camera->StartCapture();

   for( FramePtrVector ::iterator iter = frames.begin(); frames.end() != iter; ++iter )
   {
      err = camera->QueueFrame( *iter );
   }

   err = camera->GetFeatureByName( "AcquisitionMode", feature );
   err = feature->SetValue( "Continuous" );
   err = camera->GetFeatureByName( "AcquisitionStart", feature );
   err = feature->RunCommand();

   // Program runtime ...
   // When finished , tear down the acquisition chain , close camera and API

   err = camera->GetFeatureByName( "AcquisitionStop", feature );
   err = feature->RunCommand();
   err = camera->EndCapture();
   err = camera->FlushQueue();
   err = camera->RevokeAllFrames();
   err = camera->Close();
   err = system.Shutdown();
}
