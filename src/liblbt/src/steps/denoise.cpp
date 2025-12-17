#include "denoise.h"

FrameMemoryObject DenoiseCPUStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    if (input.isEmpty()) {
        return FrameMemoryObject();
    }
    
    cv::Mat inputMat = input.getCpuMat();
    cv::Mat result;
    
    // Parameters for Non-Local Means denoising
    double h = config.getParameter("h", 3.0);
    double hColor = config.getParameter("h_color", 3.0);
    int templateWindowSize = static_cast<int>(config.getParameter("template_window_size", 7));
    int searchWindowSize = static_cast<int>(config.getParameter("search_window_size", 21));
    
    // Choose denoising method based on input type
    if (inputMat.channels() == 1) {
        // Grayscale image
        cv::fastNlMeansDenoising(inputMat, result, h, templateWindowSize, searchWindowSize);
    } else {
        // Color image
        cv::fastNlMeansDenoisingColored(inputMat, result, h, hColor, templateWindowSize, searchWindowSize);
    }
    
    return FrameMemoryObject(result);
}