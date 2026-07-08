#include "real_metrics.h"

#include <cmath>
#include <numeric>
#include <sstream>

namespace {

bool isDetected(const RealFrameObservation& f) {
    return !std::isnan(f.det_x) && !std::isnan(f.det_y);
}

bool insideRoi(double x, double y, const RoiConfig& roi) {
    if (!roi.roi_defined) return true;
    double dx = x - roi.center_x;
    double dy = y - roi.center_y;
    return std::sqrt(dx * dx + dy * dy) <= roi.radius;
}

} // namespace

RealMetrics computeRealMetrics(const std::vector<RealFrameObservation>& frames,
                                const RealMetricsConfig& config,
                                const RoiConfig& roi,
                                double processing_time_ms) {
    RealMetrics m;
    m.processing_time_ms = processing_time_ms;
    m.n_frames_scored = static_cast<int>(frames.size());

    int n_multi_contour = 0;
    int n_consecutive_pairs = 0;
    bool prev_det = false;

    // "Previous detection" state, independent of frame adjacency -- movement/jump/
    // speed/ROI-violation checks compare against the last frame that actually had a
    // detection, not strictly frame_idx-1, since real detections are sparse (most
    // frames have none at all, per the very first debugging session in this project).
    bool havePrevDetection = false;
    double prevX = 0.0, prevY = 0.0;
    int prevFrameIdx = 0;

    std::vector<double> areas;
    int consecutiveRoiViolations = 0;

    for (const RealFrameObservation& f : frames) {
        const bool det = isDetected(f);
        if (det) {
            ++m.n_frames_detected;
            if (f.contour_area > 0.0) areas.push_back(f.contour_area);
            if (f.area_filter_count > 1) ++n_multi_contour;

            // ROI containment, with tolerance for a few consecutive borderline frames
            // (mirrors TaggingWindow's own tracker-loss tolerance, kRoiViolationTolerance).
            if (!insideRoi(f.det_x, f.det_y, roi)) {
                ++consecutiveRoiViolations;
                if (consecutiveRoiViolations > config.roi_violation_tolerance) {
                    ++m.n_roi_violations;
                }
            } else {
                consecutiveRoiViolations = 0;
            }

            if (havePrevDetection) {
                double dx = f.det_x - prevX;
                double dy = f.det_y - prevY;
                double dist = std::sqrt(dx * dx + dy * dy);

                if (dist >= config.movement_epsilon_px) ++m.n_movement_events;
                if (dist > config.jump_threshold_px) ++m.n_jumps;

                if (config.max_speed_px_per_frame >= 0.0) {
                    int frameGap = std::max(1, f.frame_idx - prevFrameIdx);
                    double speed = dist / static_cast<double>(frameGap);
                    if (speed > config.max_speed_px_per_frame) ++m.n_speed_violations;
                }
            }
            havePrevDetection = true;
            prevX = f.det_x;
            prevY = f.det_y;
            prevFrameIdx = f.frame_idx;
        }

        // Zeta: fraction of consecutive-frame pairs (in the input sequence order) that
        // are both detections -- same definition as computeMetrics()'s zeta.
        if (det && prev_det) ++n_consecutive_pairs;
        prev_det = det;
    }

    m.eta = m.n_frames_detected > 0
        ? static_cast<double>(n_multi_contour) / m.n_frames_detected
        : 0.0;
    m.zeta = (m.n_frames_detected > 1)
        ? static_cast<double>(n_consecutive_pairs) / (m.n_frames_detected - 1)
        : 0.0;

    if (!areas.empty()) {
        m.mean_area = std::accumulate(areas.begin(), areas.end(), 0.0) / areas.size();
        double band = m.mean_area * (config.area_tolerance_pct / 100.0);
        int violations = 0;
        for (double a : areas) {
            if (std::abs(a - m.mean_area) > band) ++violations;
        }
        m.area_tolerance_violation_rate = static_cast<double>(violations) / areas.size();
    }

    if (m.n_movement_events < config.min_movement_events) {
        m.passed = false;
        std::ostringstream ss;
        ss << "only " << m.n_movement_events << " real movement event(s) found (need >= "
           << config.min_movement_events << ") -- likely tracking a static artifact, "
              "not the live animal, or the animal was never reliably detected";
        m.fail_reason = ss.str();
    }

    return m;
}
