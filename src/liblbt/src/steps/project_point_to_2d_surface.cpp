#include "project_point_to_2d_surface.h"
#include <opencv2/calib3d.hpp>
#include <iostream>
#include <filesystem>



bool ProjectPointTo2DSurfaceCPUStep::initDuringFirstStart(cv::Mat cameraMatrix, cv::Mat distCoeffs, cv::Mat extrinsic_parameters) {

    if (m_initialized) {
        return true;
    }
    if (cameraMatrix.empty()) {
        std::cerr << "[ProjectPointTo2DSurface] Missing cameraMatrix during initialization.\n";
        return false;
    }
    if (distCoeffs.empty() ) {
        std::cerr << "[ProjectPointTo2DSurface] Missing distCoeffs during initialization.\n";
        return false;
    }
    if (extrinsic_parameters.empty()) {
        std::cerr << "[ProjectPointTo2DSurface] Missing extrinsic_parameters during initialization.\n";
        return false;
    }
    // Extract the first pose (matches foo.cpp example semantics)
    int i = 0;
    if (extrinsic_parameters.rows < 1 || extrinsic_parameters.cols < 6) {
        std::cerr << "[ProjectPointTo2DSurface] extrinsic_parameters has unexpected shape: "
                  << extrinsic_parameters.size() << "\n";
        return false;
    }
    //get the parts from extrinsic_parameters
    cv::Mat r = extrinsic_parameters(cv::Range(i, i + 1), cv::Range(0, 3));
    cv::Mat t = extrinsic_parameters(cv::Range(i, i + 1), cv::Range(3, 6));
    cv::Mat rvec = r.t();
    cv::Mat tvec = t.t();

    if (!buildInverseHomography(cameraMatrix, rvec, tvec)) {
        return false;
    }
    else {
        m_initialized = true;
        return true;
    }
}

bool ProjectPointTo2DSurfaceCPUStep::buildInverseHomography(const cv::Mat& cameraMatrix,
                                                            const cv::Mat& rvec,
                                                            const cv::Mat& tvec) {
    if (cameraMatrix.empty() || rvec.empty() || tvec.empty()) return false;
    CV_Assert(cameraMatrix.type() == CV_64F || cameraMatrix.type() == CV_32F);

    cv::Mat rotVec64, translatVec64, camMat64;
    cameraMatrix.convertTo(camMat64, CV_64F);
    rvec.convertTo(rotVec64, CV_64F);
    tvec.convertTo(translatVec64, CV_64F);

    cv::Mat rotateMat;
    cv::Rodrigues(rotVec64, rotateMat); // 3x3

    // Form [rotateMat|translatVec64]
    cv::Mat extrinsicMatrix;
    cv::hconcat(rotateMat, translatVec64, extrinsicMatrix); // 3x4

    // Projection P = cameraMatrix  x [rotateMat|translatVec64]
    cv::Mat P = camMat64 * extrinsicMatrix; // 3x4

    // Build homography assuming planar Z=0: take columns 0,1,3 of P
    double p11 = P.at<double>(0, 0), p12 = P.at<double>(0, 1), p14 = P.at<double>(0, 3);
    double p21 = P.at<double>(1, 0), p22 = P.at<double>(1, 1), p24 = P.at<double>(1, 3);
    double p31 = P.at<double>(2, 0), p32 = P.at<double>(2, 1), p34 = P.at<double>(2, 3);

    cv::Mat homographyMatrix = (cv::Mat_<double>(3, 3) << p11, p12, p14,
                                           p21, p22, p24,
                                           p31, p32, p34);
    cv::Mat homographyMatrixInv = homographyMatrix.inv();
    if (homographyMatrixInv.empty()) return false;
    {    
        m_inverseH = homographyMatrixInv;
    }
    return true;
}

cv::Point2d ProjectPointTo2DSurfaceCPUStep::projectPixelToPlane(const cv::Point2d& p, const cv::Mat& invH) {
    cv::Mat a = (cv::Mat_<double>(3, 1) << p.x, p.y, 1.0);
    cv::Mat wpt = invH * a; // 3x1
    double w = wpt.at<double>(2, 0);
    if (std::abs(w) < 1e-12) {
        return cv::Point2d(std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::quiet_NaN());
    }
    wpt /= w; // normalize
    return cv::Point2d(wpt.at<double>(0, 0), wpt.at<double>(1, 0));
}

FrameMemoryObject ProjectPointTo2DSurfaceCPUStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    try {
        if (input.isEmpty()) {
            return FrameMemoryObject();
        }

        cv::Mat img = input.getCpuMat().clone();

        if (!m_initialized) {
            cv::Mat cameraMatrix, distCoeffs, extrinsicsCombined;
            config.getParameter("camera_matrix", cameraMatrix);
            config.getParameter("distortion_coefficients", distCoeffs);
            config.getParameter("extrinsics_combined", extrinsicsCombined);
            if (!initDuringFirstStart(cameraMatrix, distCoeffs, extrinsicsCombined)) {
                std::cerr << "[ProjectPointTo2DSurface] Failed to initialize with calibration data; skipping projection.\n";
                return FrameMemoryObject(img);
            }
        }

        if (m_inverseH.empty()) {
            std::cerr << "[ProjectPointTo2DSurface] Inverse homography matrix is empty; skipping projection.\n";
            return FrameMemoryObject(img);
        }

        if (!input.hasMetadata("mass_center_point")) {
            std::cerr << "[ProjectPointTo2DSurface] Missing metadata 'mass_center_point'; ensure MassCenterOverlay runs before this step.\n";
            return FrameMemoryObject(img);
        }

        cv::Point2f center;
        try {
            center = input.getMetadata<cv::Point2f>("mass_center_point");
        } catch (const std::exception& e) {
            std::cerr << "[ProjectPointTo2DSurface] Metadata type mismatch for 'mass_center_point': " << e.what() << "\n";
            return FrameMemoryObject(img);
        }


        // Perform the inverse homogeneous projection onto the plane
        cv::Point2d worldPt = projectPixelToPlane(cv::Point2d(center.x, center.y), m_inverseH);

        // Return image unchanged, but attach the projected point
        FrameMemoryObject out(img);
        out.setMetadata<cv::Point2f>("mass_center_point_pixel", center); // keep original for downstream
        out.setMetadata<cv::Point2d>("mass_center_point_world", worldPt);

        return out;
    } catch (const std::exception& e) {
        std::cerr << "[ProjectPointTo2DSurface] Exception: " << e.what() << "\n";
        return FrameMemoryObject();
    }
}
