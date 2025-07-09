#include "frame_observer.h"
#include <QDebug>

// Constructor for the FrameObserver class
FrameObserver::FrameObserver(CameraPtr pCamera) : IFrameObserver(pCamera) {}

// Frame callback notifies about incoming frames
void FrameObserver::FrameReceived(const FramePtr pFrame)
{
    static int frameCount = 0;
    ++frameCount;
    if (frameCount % 10 == 0) {
        qDebug() << "[FrameObserver] Received" << frameCount << "frames";
    }
    m_pCamera->QueueFrame(pFrame);
}