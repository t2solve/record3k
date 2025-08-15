#pragma once
#include <VmbCPP/IFrameObserver.h>
#include <VmbCPP/Camera.h>
#include <VmbCPP/Frame.h>
#include <VmbCPP/SharedPointerDefines.h>

using namespace VmbCPP;

class FrameObserver : public IFrameObserver
{
public:
    FrameObserver(CameraPtr pCamera);
    void FrameReceived(const FramePtr pFrame) override;
};
