#pragma once

#ifdef CUDA_ENABLED
#include <opencv2/cudaimgproc.hpp>

inline cv::cuda::GpuMat ensureCudaFilterCompatible(const cv::cuda::GpuMat& input) {
    int scn = input.channels();
    if (scn == 1 || scn == 4) {
        return input;
    }
    cv::cuda::GpuMat out;
    // Convert BGR (3 channels) to GRAY (1 channel)
    cv::cuda::cvtColor(input, out, cv::COLOR_BGR2GRAY);
    return out;
}
#endif