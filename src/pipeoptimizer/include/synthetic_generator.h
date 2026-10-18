#pragma once

#include "synth_config.h"
#include <opencv2/core.hpp>
#include <QString>
#include <vector>

struct FramePose {
    int       idx;
    long long ts_ns;
    double    x_px        = -1.0;
    double    y_px        = -1.0;
    double    heading_rad = 0.0;
    bool      is_warmup   = true;
    bool      is_resting  = false;  // true during a rest bout
};

class SyntheticGenerator {
public:
    explicit SyntheticGenerator(const SynthConfig& cfg);

    // Generate one complete record; returns path to the content directory.
    QString generateRecord(const QString& record_id);

private:
    SynthConfig m_cfg;

    cv::Mat               makeBackground();
    std::vector<FramePose> generateTrajectory();
    cv::Mat               renderFrame(const cv::Mat& bg, const FramePose& pose,
                                       double flicker_offset);
    void                  drawAnimal(cv::Mat& frame_f32, const FramePose& pose);

    void writeGroundTruth(const QString& dir, const std::vector<FramePose>& poses,
                          const QString& record_id);
    void writeRecordJson(const QString& dir, const QString& record_id, int n_frames);
    void writeSynthConfigJson(const QString& dir);
    void writePipelineJson(const QString& dir, const QString& record_id);
    void writeMetadataLog(const QString& dir, const QString& record_id);
};
