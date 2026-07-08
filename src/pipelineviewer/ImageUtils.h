#pragma once

#include <QImage>
#include <opencv2/imgproc.hpp>

namespace ImageUtils {

inline QImage matToQImage(const cv::Mat& mat) {
    if (mat.channels() == 1) {
        return QImage(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_Grayscale8).copy();
    }
    cv::Mat rgb;
    cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888).copy();
}

} // namespace ImageUtils
