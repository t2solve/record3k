#pragma once

#include <QString>
#include <QJsonObject>

// ─── Arena ────────────────────────────────────────────────────────────────────
struct ArenaConfig {
    int    width_px          = 1280;
    int    height_px         = 960;
    int    center_x          = 640;
    int    center_y          = 480;
    int    radius_px         = 400;
    int    floor_gray        = 182;
    double floor_texture_std = 5.0;
    int    wall_gray         = 72;
    int    wall_thickness_px = 8;

    QJsonObject toJson() const;
    static ArenaConfig fromJson(const QJsonObject& o);
};

// ─── Animal ───────────────────────────────────────────────────────────────────
struct AnimalConfig {
    QString body_type      = "long";  // "long" | "round"
    int semi_major_px      = 42;      // long: half-length along heading
    int semi_minor_px      = 11;      // long: half-width
    int round_radius_px    = 22;      // round body radius
    int gray_value         = 58;      // dark blob on light floor (~182)

    QJsonObject toJson() const;
    static AnimalConfig fromJson(const QJsonObject& o);
};

// ─── Trajectory ───────────────────────────────────────────────────────────────
struct TrajectoryConfig {
    int    n_warmup_frames = 500;   // bg-only frames; must cover MOG2 history
    int    n_animal_frames = 600;   // 60 s at 10 fps
    double fps             = 10.0;
    double speed_px        = 7.0;   // pixels per frame (while moving); fixed value
                                     // used as-is unless speed_variation is enabled
    double turn_std_deg    = 12.0;  // heading noise per step (σ)
    double wall_buffer_px  = 60.0;  // inner keep-out margin from wall

    // Per-record mean-speed heterogeneity: instead of every record moving at the
    // same fixed speed_px, draw each record's own constant speed from one of three
    // behavioural classes (slow/normal/fast), each a Gaussian around its class mean.
    // The class is picked uniformly at random per record; speed_px above is ignored
    // (and overwritten with the drawn value, for provenance) when this is enabled.
    bool   speed_variation     = false;
    double speed_slow_px       = 3.0;
    double speed_normal_px     = 7.0;
    double speed_fast_px       = 14.0;
    double speed_class_std_px  = 1.5;   // gaussian σ around the drawn class mean

    // Resting bouts: two-state Markov chain (MOVING ↔ RESTING)
    // mean moving-bout length  = 1 / rest_prob   frames
    // mean resting-bout length = 1 / resume_prob frames
    double rest_prob       = 0.02;  // P(start rest | moving)  → ~50 frames between rests
    double resume_prob     = 0.10;  // P(resume move | resting)→ ~10 frames per rest bout
    double rest_jitter_px  = 0.4;   // micro-movement σ during rest (body tremor)
    bool   all_resting     = false; // dead/completely stationary animal; bypasses MOG2 absorption cap

    QJsonObject toJson() const;
    static TrajectoryConfig fromJson(const QJsonObject& o);
};

// ─── Noise ────────────────────────────────────────────────────────────────────
struct NoiseConfig {
    double gaussian_std         = 7.0;  // per-pixel sensor noise (σ DN)
    int    n_reflections        = 4;    // static bright spots (overhead lamps)
    int    reflection_radius_px = 14;
    int    reflection_brightness = 32;  // DN added on top of floor_gray
    double temporal_flicker_std  = 2.0; // per-frame global brightness shift (σ)

    QJsonObject toJson() const;
    static NoiseConfig fromJson(const QJsonObject& o);
};

// ─── Top-level config ─────────────────────────────────────────────────────────
struct SynthConfig {
    ArenaConfig      arena;
    AnimalConfig     animal;
    TrajectoryConfig trajectory;
    NoiseConfig      noise;
    QString          output_dir = "./data/synthetic";
    int              seed       = 42;
    int              n_records  = 3;

    static SynthConfig defaults();
    static SynthConfig fromJsonFile(const QString& path);
    QJsonObject toJson() const;
};
