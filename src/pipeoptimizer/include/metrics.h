#pragma once
#include <vector>
#include <limits>
#include <cmath>

// Per-frame result collected during C3 simulation run.
// Use NaN to signal "data not available" for gt_x/gt_y and det_x/det_y.
struct FrameResult {
    int       frame_idx          = 0;
    long long ts_ns              = 0;
    bool      is_warmup          = false;
    bool      is_resting         = false;
    float     gt_x               = std::numeric_limits<float>::quiet_NaN();
    float     gt_y               = std::numeric_limits<float>::quiet_NaN();
    float     det_x              = std::numeric_limits<float>::quiet_NaN(); // NaN = no detection
    float     det_y              = std::numeric_limits<float>::quiet_NaN();
    int       area_filter_count  = 0;  // contours passing area gate (for η)
    int       filtered_count     = 0;  // contours after max_contours selection
};

// Aggregated C4 metrics for one (generation, pipeline) pair.
struct Metrics {
    double alpha_mean   = 0.0;  // mean positional error on detected animal frames [px]
    double alpha_p95    = 0.0;  // 95th-percentile positional error [px]
    double beta_mean    = 0.0;  // mean |det_speed - gt_speed| per consecutive pair [px/f]
    double gamma_moving  = 0.0; // detection rate during movement bouts [0-1]; NaN if no moving frames
    double gamma_resting = 0.0; // detection rate during rest bouts [0-1];   NaN if no resting frames
    double delta_fpr    = 0.0;  // false positive rate in warmup frames [0-1]
    double eta          = 0.0;  // multi-contour rate (centroid-smear diagnostic) [0-1]
    double zeta         = 0.0;  // trajectory continuity [0-1]
    double score        = 0.0;  // composite imputed-alpha [px], lower is better

    int    n_warmup_frames     = 0;
    int    n_animal_frames     = 0;
    int    n_detected_frames   = 0;
    int    n_warmup_detections = 0;
    double processing_time_ms  = 0.0;
};

// miss_penalty_px: cost added per missed animal frame in SCORE.
//   Default = arena_radius / 4 (~100 px for a 400 px arena).
//   arena_radius / 4 means "a miss is as bad as being 100 px wrong" — moderate.
//   Set to arena_radius for the strictest "no-miss" preference.
//   Warmup frames are excluded from all scoring; only animal frames count.
Metrics computeMetrics(const std::vector<FrameResult>& frames,
                       double miss_penalty_px,
                       double processing_time_ms = 0.0);
