#include "morphology_close.h"

namespace {
int toElementShape(int shapeParam) {
    switch (shapeParam) {
        case 0: return cv::MORPH_RECT;
        case 1: return cv::MORPH_CROSS;
        case 2:
        default: return cv::MORPH_ELLIPSE;
    }
}
}

FrameMemoryObject MorphologyCloseCPUStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty()) {
        return FrameMemoryObject();
    }

    int ksize = static_cast<int>(config.getParameter("ksize", 15.0));
    if (ksize < 1) ksize = 1;
    if ((ksize % 2) == 0) ksize++; // ensure odd
    int shapeParam = static_cast<int>(config.getParameter("shape", 2.0)); // 0=rect,1=cross,2=ellipse
    int iterations = static_cast<int>(config.getParameter("iterations", 1.0));
    if (iterations < 1) iterations = 1;

    cv::Mat src = input.getCpuMat();
    if (src.empty()) return FrameMemoryObject();

    // Operate on grayscale
    cv::Mat gray;
    if (src.channels() == 1) {
        gray = src;
    } else {
        cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    }

    cv::Mat element = cv::getStructuringElement(toElementShape(shapeParam), cv::Size(ksize, ksize));
    cv::Mat dst;
    cv::morphologyEx(gray, dst, cv::MORPH_CLOSE, element, cv::Point(-1, -1), iterations);

    return FrameMemoryObject(dst);
}

#ifdef CUDA_ENABLED
FrameMemoryObject MorphologyCloseCUDAStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty() || !isAvailable()) {
        return FrameMemoryObject();
    }

    int ksize = static_cast<int>(config.getParameter("ksize", 15.0));
    if (ksize < 1) ksize = 1;
    if ((ksize % 2) == 0) ksize++; // ensure odd
    int shapeParam = static_cast<int>(config.getParameter("shape", 2.0)); // 0=rect,1=cross,2=ellipse
    int iterations = static_cast<int>(config.getParameter("iterations", 1.0));
    if (iterations < 1) iterations = 1;

    cv::cuda::GpuMat gpuSrc = input.getGpuMat();
    if (gpuSrc.empty()) return FrameMemoryObject();

    // Ensure we work on 1-channel
    cv::cuda::GpuMat gpuGray;
    if (gpuSrc.channels() == 1) {
        gpuGray = gpuSrc;
    } else {
        cv::cuda::cvtColor(gpuSrc, gpuGray, cv::COLOR_BGR2GRAY);
    }

    // Kernel is created on CPU side (as cv::Mat)
    cv::Mat element = cv::getStructuringElement(toElementShape(shapeParam), cv::Size(ksize, ksize));

    cv::Ptr<cv::cuda::Filter> filt = cv::cuda::createMorphologyFilter(
        cv::MORPH_CLOSE, gpuGray.type(), element, cv::Point(-1, -1), iterations);

    cv::cuda::GpuMat gpuDst;
    filt->apply(gpuGray, gpuDst);

    return FrameMemoryObject(gpuDst);
}
#endif