#include <algorithm>
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
#include "steps/contour_area_filter.h"
#include "steps/binary_threshold.h"
#include "steps/mass_center_overlay.h"
#include "steps/morphology_close.h" 
#include "steps/project_point_to_2d_surface.h"

std::shared_ptr<ProcessStep> ProcessStepFactory::createProjectPointTo2DSurface(ProcessingMode mode) {
    switch (mode) {
        case ProcessingMode::CPU_ONLY:
            return std::make_shared<ProjectPointTo2DSurfaceCPUStep>();
        case ProcessingMode::CUDA_PREFERRED:
        case ProcessingMode::CUDA_ONLY:
            // Currently only CPU implementation is available
            return std::make_shared<ProjectPointTo2DSurfaceCPUStep>();
        default:
            return std::make_shared<ProjectPointTo2DSurfaceCPUStep>();
    }
}

std::shared_ptr<ProcessStep> ProcessStepFactory::createMorphologyClose(ProcessingMode mode) {
    switch (mode) {
        case ProcessingMode::CPU_ONLY:
            return std::make_shared<MorphologyCloseCPUStep>();
        case ProcessingMode::CUDA_PREFERRED:
        case ProcessingMode::CUDA_ONLY:
#ifdef CUDA_ENABLED
            if (cv::cuda::getCudaEnabledDeviceCount() > 0) {
                return std::make_shared<MorphologyCloseCUDAStep>();
            } else if (mode == ProcessingMode::CUDA_ONLY) {
                throw std::runtime_error("CUDA not available for morphology close");
            }
#endif
            return std::make_shared<MorphologyCloseCPUStep>();
        default:
            return std::make_shared<MorphologyCloseCPUStep>();
    }
}

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

std::shared_ptr<ProcessStep> ProcessStepFactory::createContourAreaFilter(ProcessingMode mode) {
    switch (mode) {
        case ProcessingMode::CPU_ONLY:
            return std::make_shared<ContourAreaFilterCPUStep>();
        case ProcessingMode::CUDA_PREFERRED:
        case ProcessingMode::CUDA_ONLY:
#ifdef CUDA_ENABLED
            if (cv::cuda::getCudaEnabledDeviceCount() > 0) {
                return std::make_shared<ContourAreaFilterCUDAStep>();
            } else if (mode == ProcessingMode::CUDA_ONLY) {
                throw std::runtime_error("CUDA not available for contour area filter");
            }
#endif
            return std::make_shared<ContourAreaFilterCPUStep>();
        default:
            return std::make_shared<ContourAreaFilterCPUStep>();
    }
}

std::shared_ptr<ProcessStep> ProcessStepFactory::createBinaryThreshold(ProcessingMode mode) {
    switch (mode) {
        case ProcessingMode::CPU_ONLY:
            return std::make_shared<BinaryThresholdCPUStep>();
        case ProcessingMode::CUDA_PREFERRED:
        case ProcessingMode::CUDA_ONLY:
#ifdef CUDA_ENABLED
            if (cv::cuda::getCudaEnabledDeviceCount() > 0) {
                return std::make_shared<BinaryThresholdCUDAStep>();
            } else if (mode == ProcessingMode::CUDA_ONLY) {
                throw std::runtime_error("CUDA not available for binary threshold");
            }
#endif
            return std::make_shared<BinaryThresholdCPUStep>();
        default:
            return std::make_shared<BinaryThresholdCPUStep>();
    }
}

std::shared_ptr<ProcessStep> ProcessStepFactory::createMassCenterOverlay(ProcessingMode mode) {
    // CPU-only step; draws a red dot on the frame
    (void)mode;
    return std::make_shared<MassCenterOverlayCPUStep>();
}

static std::shared_ptr<ProcessStep> createProjectPointTo2DSurface(ProcessingMode mode) {
    (void)mode;
    return std::make_shared<ProjectPointTo2DSurfaceCPUStep>();
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
        case FilterType::CONTOUR_AREA_FILTER:
            return createContourAreaFilter(mode);
        case FilterType::BINARY_THRESHOLD:
            return createBinaryThreshold(mode);
        case FilterType::MASS_CENTER_OVERLAY:
            return createMassCenterOverlay(mode);
        case FilterType::PROJECT_POINT_TO_2D_SURFACE:
            return createProjectPointTo2DSurface(mode);
        case FilterType::MORPHOLOGY_CLOSE:
            return createMorphologyClose(mode);
        case FilterType::NONE:
        case FilterType::CUSTOM:
        default:
            throw std::invalid_argument("Unknown or unsupported filter type");
    }
}

static std::string toLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
    return s;
}

FilterType ProcessStepFactory::nameToFilterType(const std::string& stepName) {
    std::string n = toLowerCopy(stepName);
    // Background variants first to allow GMG/CNT detection
    if (n.find("backgroundsubtraction_gmg") != std::string::npos || n.find("gmg") != std::string::npos)
        return FilterType::BACKGROUND_SUBTRACTION_GMG;
    if (n.find("backgroundsubtraction_cnt") != std::string::npos || n.find("cnt") != std::string::npos)
        return FilterType::BACKGROUND_SUBTRACTION_CNT;
    if (n.find("backgroundsubtraction") != std::string::npos)
        return FilterType::BACKGROUND_SUBTRACTION_MOG2;

    if (n.find("gaussianblur") != std::string::npos) return FilterType::GAUSSIAN_BLUR;
    if (n.find("bilateralfilter") != std::string::npos) return FilterType::BILATERAL_FILTER;
    if (n.find("medianfilter") != std::string::npos) return FilterType::MEDIAN_FILTER;
    if (n.find("sharpen") != std::string::npos) return FilterType::SHARPEN;
    if (n.find("edgedetection") != std::string::npos) return FilterType::EDGE_DETECTION;
    if (n.find("denoise") != std::string::npos) return FilterType::DENOISE;
    if (n.find("contourdetection") != std::string::npos) return FilterType::CONTOUR_DETECTION;
    if (n.find("lenscorrection") != std::string::npos) return FilterType::LENS_CORRECTION;
    if (n.find("roicirclecrop") != std::string::npos || n.find("roi_circle_crop") != std::string::npos) return FilterType::ROI_CIRCLE_CROP;
    if (n.find("binarythreshold") != std::string::npos) return FilterType::BINARY_THRESHOLD;
    if (n.find("contourareafilter") != std::string::npos) return FilterType::CONTOUR_AREA_FILTER;
    if (n.find("masscenteroverlay") != std::string::npos || n.find("mass_center_overlay") != std::string::npos || n.find("drawmasscenter") != std::string::npos)
        return FilterType::MASS_CENTER_OVERLAY;
    if (n.find("projectpointto2dsurface") != std::string::npos || n.find("project_point_to_2d_surface") != std::string::npos || n.find("pixel2world") != std::string::npos)
        return FilterType::PROJECT_POINT_TO_2D_SURFACE;
    if (n.find("morphologyclose") != std::string::npos || n.find("morphclose") != std::string::npos || n.find("closing") != std::string::npos)
        return FilterType::MORPHOLOGY_CLOSE;
    return FilterType::NONE;
}

std::string ProcessStepFactory::filterTypeBaseName(FilterType filterType) {
    switch (filterType) {
        case FilterType::GAUSSIAN_BLUR: return "GaussianBlur";
        case FilterType::BILATERAL_FILTER: return "BilateralFilter";
        case FilterType::MEDIAN_FILTER: return "MedianFilter";
        case FilterType::EDGE_DETECTION: return "EdgeDetection";
        case FilterType::SHARPEN: return "Sharpen";
        case FilterType::DENOISE: return "Denoise";
        case FilterType::BACKGROUND_SUBTRACTION_MOG2: return "BackgroundSubtraction";
        case FilterType::BACKGROUND_SUBTRACTION_GMG: return "BackgroundSubtraction_GMG";
        case FilterType::BACKGROUND_SUBTRACTION_CNT: return "BackgroundSubtraction_CNT";
        case FilterType::CONTOUR_DETECTION: return "ContourDetection";
        case FilterType::LENS_CORRECTION: return "LensCorrection";
        case FilterType::ROI_CIRCLE_CROP: return "ROICircleCrop";
        case FilterType::BINARY_THRESHOLD: return "BinaryThreshold";
        case FilterType::CONTOUR_AREA_FILTER: return "ContourAreaFilter";
        case FilterType::MASS_CENTER_OVERLAY: return "MassCenterOverlay";
    case FilterType::PROJECT_POINT_TO_2D_SURFACE: return "ProjectPointTo2DSurface";
        case FilterType::MORPHOLOGY_CLOSE: return "MorphologyClose";
        case FilterType::NONE:
        case FilterType::CUSTOM:
        default: return "UNKNOWN";
    }
}