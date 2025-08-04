#include "process_step_factory.h"
#include "steps/gaussian_blur.h"
#include "steps/bilateral_filter.h"
#include "steps/median_filter.h"
#include "steps/edge_detection.h"
#include "steps/sharpen.h"
#include "steps/denoise.h"
#include "steps/background_subtraction.h"
#include "steps/contour_detection.h"
#include "steps/lens_correction.h"
#include "steps/roi_circle_crop.h"

std::shared_ptr<ProcessStep> ProcessStepFactory::createGaussianBlur(ProcessingMode mode) {
    switch (mode) {
        case ProcessingMode::CPU_ONLY:
            return std::make_shared<GaussianBlurCPUStep>();
        case ProcessingMode::CUDA_PREFERRED:
        case ProcessingMode::CUDA_ONLY:
#ifdef CUDA_ENABLED
            if (cv::cuda::getCudaEnabledDeviceCount() > 0) {
                return std::make_shared<GaussianBlurCUDAStep>();
            } else if (mode == ProcessingMode::CUDA_ONLY) {
                throw std::runtime_error("CUDA not available for Gaussian blur");
            }
#endif
            return std::make_shared<GaussianBlurCPUStep>();
        default:
            return std::make_shared<GaussianBlurCPUStep>();
    }
}

std::shared_ptr<ProcessStep> ProcessStepFactory::createBilateralFilter(ProcessingMode mode) {
    switch (mode) {
        case ProcessingMode::CPU_ONLY:
            return std::make_shared<BilateralFilterCPUStep>();
        case ProcessingMode::CUDA_PREFERRED:
        case ProcessingMode::CUDA_ONLY:
#ifdef CUDA_ENABLED
            if (cv::cuda::getCudaEnabledDeviceCount() > 0) {
                return std::make_shared<BilateralFilterCUDAStep>();
            } else if (mode == ProcessingMode::CUDA_ONLY) {
                throw std::runtime_error("CUDA not available for bilateral filter");
            }
#endif
            return std::make_shared<BilateralFilterCPUStep>();
        default:
            return std::make_shared<BilateralFilterCPUStep>();
    }
}

std::shared_ptr<ProcessStep> ProcessStepFactory::createMedianFilter(ProcessingMode mode) {
    // Median filter is typically CPU-only in OpenCV
    return std::make_shared<MedianFilterCPUStep>();
}

std::shared_ptr<ProcessStep> ProcessStepFactory::createEdgeDetection(ProcessingMode mode) {
    switch (mode) {
        case ProcessingMode::CPU_ONLY:
            return std::make_shared<EdgeDetectionCPUStep>();
        case ProcessingMode::CUDA_PREFERRED:
        case ProcessingMode::CUDA_ONLY:
#ifdef CUDA_ENABLED
            if (cv::cuda::getCudaEnabledDeviceCount() > 0) {
                return std::make_shared<EdgeDetectionCUDAStep>();
            } else if (mode == ProcessingMode::CUDA_ONLY) {
                throw std::runtime_error("CUDA not available for edge detection");
            }
#endif
            return std::make_shared<EdgeDetectionCPUStep>();
        default:
            return std::make_shared<EdgeDetectionCPUStep>();
    }
}

std::shared_ptr<ProcessStep> ProcessStepFactory::createSharpen(ProcessingMode mode) {
    switch (mode) {
        case ProcessingMode::CPU_ONLY:
            return std::make_shared<SharpenCPUStep>();
        case ProcessingMode::CUDA_PREFERRED:
        case ProcessingMode::CUDA_ONLY:
#ifdef CUDA_ENABLED
            if (cv::cuda::getCudaEnabledDeviceCount() > 0) {
                return std::make_shared<SharpenCUDAStep>();
            } else if (mode == ProcessingMode::CUDA_ONLY) {
                throw std::runtime_error("CUDA not available for sharpen");
            }
#endif
            return std::make_shared<SharpenCPUStep>();
        default:
            return std::make_shared<SharpenCPUStep>();
    }
}

std::shared_ptr<ProcessStep> ProcessStepFactory::createContourDetection(ProcessingMode mode) {
    switch (mode) {
        case ProcessingMode::CPU_ONLY:
            return std::make_shared<ContourDetectionCPUStep>();
        case ProcessingMode::CUDA_ONLY:
#ifdef CUDA_ENABLED
            return std::make_shared<ContourDetectionCUDAStep>();
#else
            return std::make_shared<ContourDetectionCPUStep>();
#endif
        case ProcessingMode::CUDA_PREFERRED:
#ifdef CUDA_ENABLED
            if (ContourDetectionCUDAStep().isAvailable()) {
                return std::make_shared<ContourDetectionCUDAStep>();
            }
#endif
            return std::make_shared<ContourDetectionCPUStep>();
        default:
            return std::make_shared<ContourDetectionCPUStep>();
    }
}

std::shared_ptr<ProcessStep> ProcessStepFactory::createLensCorrection(ProcessingMode mode) {
    switch (mode) {
        case ProcessingMode::CPU_ONLY:
            return std::make_shared<LensCorrectionCPUStep>();
        case ProcessingMode::CUDA_PREFERRED:
        case ProcessingMode::CUDA_ONLY:
#ifdef CUDA_ENABLED
            if (cv::cuda::getCudaEnabledDeviceCount() > 0) {
                return std::make_shared<LensCorrectionCUDAStep>();
            } else if (mode == ProcessingMode::CUDA_ONLY) {
                throw std::runtime_error("CUDA not available for lens correction");
            }
#endif
            return std::make_shared<LensCorrectionCPUStep>();
        default:
            return std::make_shared<LensCorrectionCPUStep>();
    }
}

std::shared_ptr<ProcessStep> ProcessStepFactory::createROICircleCrop(ProcessingMode mode) {
    switch (mode) {
        case ProcessingMode::CPU_ONLY:
            return std::make_shared<ROICircleCropCPUStep>();
        case ProcessingMode::CUDA_PREFERRED:
        case ProcessingMode::CUDA_ONLY:
#ifdef CUDA_ENABLED
            if (cv::cuda::getCudaEnabledDeviceCount() > 0) {
                return std::make_shared<ROICircleCropCUDAStep>();
            } else if (mode == ProcessingMode::CUDA_ONLY) {
                throw std::runtime_error("CUDA not available for ROI circle crop");
            }
#endif
            return std::make_shared<ROICircleCropCPUStep>();
        default:
            return std::make_shared<ROICircleCropCPUStep>();
    }
}

std::shared_ptr<ProcessStep> ProcessStepFactory::createDenoise(ProcessingMode mode) {
    // Denoise is typically CPU-only
    return std::make_shared<DenoiseCPUStep>();
}

std::shared_ptr<ProcessStep> ProcessStepFactory::createBackgroundSubtractionMOG2(ProcessingMode mode) {
    switch (mode) {
        case ProcessingMode::CPU_ONLY:
            return std::make_shared<BackgroundSubtractionCPUStep>();
        case ProcessingMode::CUDA_PREFERRED:
        case ProcessingMode::CUDA_ONLY:
#ifdef CUDA_ENABLED
            if (cv::cuda::getCudaEnabledDeviceCount() > 0) {
                return std::make_shared<BackgroundSubtractionCUDAStep>();
            } else if (mode == ProcessingMode::CUDA_ONLY) {
                throw std::runtime_error("CUDA not available for background subtraction MOG2");
            }
#endif
            return std::make_shared<BackgroundSubtractionCPUStep>();
        default:
            return std::make_shared<BackgroundSubtractionCPUStep>();
    }
}

std::shared_ptr<ProcessStep> ProcessStepFactory::createBackgroundSubtractionGMG(ProcessingMode mode) {
    return std::make_shared<BackgroundSubtractionCPUStep>();
}

std::shared_ptr<ProcessStep> ProcessStepFactory::createBackgroundSubtractionCNT(ProcessingMode mode) {
    return std::make_shared<BackgroundSubtractionCPUStep>();
}

std::shared_ptr<ProcessStep> ProcessStepFactory::createStep(FilterType filterType, ProcessingMode mode) {
    switch (filterType) {
        case FilterType::GAUSSIAN_BLUR:
            return createGaussianBlur(mode);
        case FilterType::BILATERAL_FILTER:
            return createBilateralFilter(mode);
        case FilterType::MEDIAN_FILTER:
            return createMedianFilter(mode);
        case FilterType::EDGE_DETECTION:
            return createEdgeDetection(mode);
        case FilterType::SHARPEN:
            return createSharpen(mode);
        case FilterType::DENOISE:
            return createDenoise(mode);
        case FilterType::BACKGROUND_SUBTRACTION_MOG2:
            return createBackgroundSubtractionMOG2(mode);
        case FilterType::BACKGROUND_SUBTRACTION_GMG:
            return createBackgroundSubtractionGMG(mode);
        case FilterType::BACKGROUND_SUBTRACTION_CNT:
            return createBackgroundSubtractionCNT(mode);
        case FilterType::CONTOUR_DETECTION:
            return createContourDetection(mode);
        case FilterType::LENS_CORRECTION:
            return createLensCorrection(mode);
        case FilterType::ROI_CIRCLE_CROP:
            return createROICircleCrop(mode);
        case FilterType::NONE:
        case FilterType::CUSTOM:
        default:
            throw std::invalid_argument("Unknown or unsupported filter type");
    }
}