#include "metrics.h"
#include <algorithm>
#include <numeric>

static inline bool isDetected(const FrameResult& f) {
    return !std::isnan(f.det_x) && f.det_x >= 0.0f;
}

static inline bool hasGt(const FrameResult& f) {
    return !std::isnan(f.gt_x);
}

Metrics computeMetrics(const std::vector<FrameResult>& frames,
                       double arena_radius_px,
                       double processing_time_ms)
{
    Metrics m;
    m.processing_time_ms = processing_time_ms;

    std::vector<double> alphas;
    std::vector<double> imputed;
    std::vector<double> betas;

    int n_moving = 0, n_moving_det = 0;
    int n_resting = 0, n_resting_det = 0;
    int n_multi_contour = 0;
    int n_consecutive_pairs = 0;

    bool  prev_det   = false;
    float prev_det_x = std::numeric_limits<float>::quiet_NaN();
    float prev_det_y = std::numeric_limits<float>::quiet_NaN();
    float prev_gt_x  = std::numeric_limits<float>::quiet_NaN();
    float prev_gt_y  = std::numeric_limits<float>::quiet_NaN();

    for (const auto& f : frames) {
        if (f.is_warmup) {
            ++m.n_warmup_frames;
            if (isDetected(f)) ++m.n_warmup_detections;
            prev_det = false;
            prev_det_x = prev_det_y = std::numeric_limits<float>::quiet_NaN();
            continue;
        }

        ++m.n_animal_frames;
        const bool det = isDetected(f);
        if (det) ++m.n_detected_frames;

        if (!f.is_resting) { ++n_moving;  if (det) ++n_moving_det; }
        else                { ++n_resting; if (det) ++n_resting_det; }

        // Alpha and imputed-alpha
        if (det && hasGt(f)) {
            double dx = f.det_x - f.gt_x;
            double dy = f.det_y - f.gt_y;
            double err = std::sqrt(dx*dx + dy*dy);
            alphas.push_back(err);
            imputed.push_back(err);
        } else {
            imputed.push_back(arena_radius_px);
        }

        // Beta: consecutive frames where both det+gt are available
        if (det && hasGt(f) && !std::isnan(prev_det_x) && !std::isnan(prev_gt_x)) {
            double det_speed = std::hypot(f.det_x - prev_det_x, f.det_y - prev_det_y);
            double gt_speed  = std::hypot(f.gt_x  - prev_gt_x,  f.gt_y  - prev_gt_y);
            betas.push_back(std::abs(det_speed - gt_speed));
        }

        // Eta
        if (det && f.area_filter_count > 1) ++n_multi_contour;

        // Zeta
        if (det && prev_det) ++n_consecutive_pairs;
        prev_det = det;

        if (det) { prev_det_x = f.det_x; prev_det_y = f.det_y; }
        else      { prev_det_x = prev_det_y = std::numeric_limits<float>::quiet_NaN(); }
        if (hasGt(f)) { prev_gt_x = f.gt_x; prev_gt_y = f.gt_y; }
    }

    auto mean_of = [](const std::vector<double>& v) {
        return v.empty() ? 0.0
            : std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
    };

    m.alpha_mean  = mean_of(alphas);
    m.beta_mean   = mean_of(betas);
    m.score       = mean_of(imputed);

    if (!alphas.empty()) {
        std::vector<double> sorted = alphas;
        std::sort(sorted.begin(), sorted.end());
        m.alpha_p95 = sorted[static_cast<size_t>(0.95 * (sorted.size() - 1))];
    }

    const double NaN = std::numeric_limits<double>::quiet_NaN();
    // NaN when the category has no frames (e.g. all_resting records have no moving frames)
    m.gamma_moving  = n_moving  > 0 ? (double)n_moving_det  / n_moving  : NaN;
    m.gamma_resting = n_resting > 0 ? (double)n_resting_det / n_resting : NaN;
    m.delta_fpr     = m.n_warmup_frames > 0
                      ? (double)m.n_warmup_detections / m.n_warmup_frames : 0.0;
    m.eta           = m.n_detected_frames > 0
                      ? (double)n_multi_contour / m.n_detected_frames : 0.0;
    m.zeta          = (m.n_detected_frames > 1)
                      ? (double)n_consecutive_pairs / (m.n_detected_frames - 1) : 0.0;

    return m;
}
