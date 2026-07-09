#pragma once
#include <limits>
#include <string>
#include <vector>

// Per-frame observation collected while sweeping one pipeline config against one real
// record's buffered frames. Unlike metrics.h's FrameResult, there is no ground truth
// (gt_x/gt_y) -- see src/pipeoptimizer/plan.md step 2.
struct RealFrameObservation {
    int frame_idx = 0;
    long long ts_ns = 0;
    double det_x = std::numeric_limits<double>::quiet_NaN(); // NaN = no detection this frame
    double det_y = std::numeric_limits<double>::quiet_NaN();
    double contour_area = 0.0;    // cv::contourArea() of the accepted contour, 0 if none
    int area_filter_count = 0;    // contours passing the area gate (for eta)
    int filtered_count = 0;       // contours remaining after max_contours selection
};

// Round ROI arena, if the record has been tagged via pipelineviewer's Mode B
// (lbt::TagDataStore). roi_defined=false disables the containment check entirely
// (always passes) -- an untagged record isn't penalized for a check we have no data
// for yet.
struct RoiConfig {
    bool roi_defined = false;
    double center_x = 0.0;
    double center_y = 0.0;
    double radius = 0.0;
};

// Tunable thresholds implementing src/pipeoptimizer/plan.md's "basic assumptions" on
// the moving object. All defaults below are provisional placeholders, not measured --
// plan.md's own "open items" section flags exactly this as still needing a real sanity
// pass over a known-decent run. Exposed as CLI flags (see main.cpp) rather than
// hardcoded so they can be tuned without a rebuild.
struct RealMetricsConfig {
    double movement_epsilon_px = 5.0;    // min displacement between two detections to count as "real movement"
    int min_movement_events = 3;         // hard-fail the whole run if fewer than this many movement events occur
    double area_tolerance_pct = 30.0;    // +/- % from the record's mean detected area before a frame is flagged
    double jump_threshold_px = 32.0;     // displacement between consecutive detections above this counts as a jump
                                          // (reuses TaggingWindow's own jump-highlight constant, for consistency --
                                          // that value was picked for visual highlighting, not validated as a
                                          // scoring cutoff)
    double max_speed_px_per_frame = -1.0; // -1 = unbounded; no basis yet for a real value
    int roi_violation_tolerance = 5;      // consecutive out-of-ROI frames tolerated before counting a violation
                                           // (mirrors TaggingWindow's own tracker-loss tolerance)

    // Hard-fail gates promoted from bgsub_sweep1's leaderboard, not just guessed cold:
    // that campaign's "winner" (perfect pass_rate via min_movement_events alone) turned
    // out to be exactly the degenerate optimum this file's own header comment warns
    // about -- it passed while spending ~11% of its detected frames outside the tagged
    // ROI and having an area_tolerance_violation_rate of 0.43 vs 0.09-0.24 everywhere
    // else. Scoring these low was not enough; a bad-enough run must be excluded from
    // the passed pool the same way min_movement_events already excludes a static-
    // artifact lock. -1 = disabled (unbounded), matching max_speed_px_per_frame's
    // convention -- still provisional, calibrated only from bgsub_sweep1's 18 records,
    // same caveat as every other threshold in this struct.
    double max_area_violation_rate = -1.0; // hard-fail if area_tolerance_violation_rate exceeds this
    double max_roi_violation_rate = -1.0;  // hard-fail if (n_roi_violations / n_frames_detected) exceeds this
};

struct RealMetrics {
    // Ported directly from metrics.cpp's computeMetrics(): eta/zeta need no ground
    // truth and are computed identically here, as an apples-to-apples reference point
    // against the synthetic sweep's own numbers for the same pipeline config.
    // delta_fpr is NOT ported: the synthetic version measures false positives during
    // known-animal-absent "warmup" frames, and real records have no equivalent
    // known-empty reference period -- forcing an analog would be misleading rather
    // than useful, so it's simply omitted here.
    double eta = 0.0;  // multi-contour rate among detected frames
    double zeta = 0.0; // trajectory continuity: consecutive-frame detection rate

    // New, no-ground-truth checks implementing plan.md's assumptions.
    double mean_area = 0.0;
    double area_tolerance_violation_rate = 0.0; // fraction of detected frames outside the tolerance band
    int n_movement_events = 0;   // consecutive-detection pairs exceeding movement_epsilon_px
    int n_jumps = 0;             // consecutive-detection pairs exceeding jump_threshold_px
    int n_roi_violations = 0;    // detections outside the ROI, past roi_violation_tolerance
    int n_speed_violations = 0;  // detections exceeding max_speed_px_per_frame (always 0 if unbounded)

    int n_frames_scored = 0;
    int n_frames_detected = 0;

    bool passed = true;   // hard-fail flag -- e.g. min_movement_events not met
    std::string fail_reason; // empty when passed == true

    double processing_time_ms = 0.0;
};

RealMetrics computeRealMetrics(const std::vector<RealFrameObservation>& frames,
                                const RealMetricsConfig& config,
                                const RoiConfig& roi,
                                double processing_time_ms = 0.0);
