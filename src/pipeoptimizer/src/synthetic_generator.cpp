#include "synthetic_generator.h"
#include "synth_config.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QDebug>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <random>

// ─────────────────────────────────────────────────────────────────────────────
// SynthConfig JSON helpers
// ─────────────────────────────────────────────────────────────────────────────

static int    jInt   (const QJsonObject& o, const QString& k, int    def) { return o.contains(k) ? o[k].toInt(def)    : def; }
static double jDouble(const QJsonObject& o, const QString& k, double def) { return o.contains(k) ? o[k].toDouble(def) : def; }
static QString jStr  (const QJsonObject& o, const QString& k, const QString& def) {
    return o.contains(k) ? o[k].toString(def) : def;
}

QJsonObject ArenaConfig::toJson() const {
    return QJsonObject{{"width_px",width_px},{"height_px",height_px},{"center_x",center_x},
                       {"center_y",center_y},{"radius_px",radius_px},{"floor_gray",floor_gray},
                       {"floor_texture_std",floor_texture_std},{"wall_gray",wall_gray},
                       {"wall_thickness_px",wall_thickness_px}};
}
ArenaConfig ArenaConfig::fromJson(const QJsonObject& o) {
    ArenaConfig a;
    a.width_px          = jInt(o,"width_px",a.width_px);
    a.height_px         = jInt(o,"height_px",a.height_px);
    a.center_x          = jInt(o,"center_x",a.center_x);
    a.center_y          = jInt(o,"center_y",a.center_y);
    a.radius_px         = jInt(o,"radius_px",a.radius_px);
    a.floor_gray        = jInt(o,"floor_gray",a.floor_gray);
    a.floor_texture_std = jDouble(o,"floor_texture_std",a.floor_texture_std);
    a.wall_gray         = jInt(o,"wall_gray",a.wall_gray);
    a.wall_thickness_px = jInt(o,"wall_thickness_px",a.wall_thickness_px);
    return a;
}

QJsonObject AnimalConfig::toJson() const {
    return QJsonObject{{"body_type",body_type},{"semi_major_px",semi_major_px},
                       {"semi_minor_px",semi_minor_px},{"round_radius_px",round_radius_px},
                       {"gray_value",gray_value}};
}
AnimalConfig AnimalConfig::fromJson(const QJsonObject& o) {
    AnimalConfig a;
    a.body_type      = jStr(o,"body_type",a.body_type);
    a.semi_major_px  = jInt(o,"semi_major_px",a.semi_major_px);
    a.semi_minor_px  = jInt(o,"semi_minor_px",a.semi_minor_px);
    a.round_radius_px = jInt(o,"round_radius_px",a.round_radius_px);
    a.gray_value     = jInt(o,"gray_value",a.gray_value);
    return a;
}

QJsonObject TrajectoryConfig::toJson() const {
    return QJsonObject{{"n_warmup_frames",n_warmup_frames},{"n_animal_frames",n_animal_frames},
                       {"fps",fps},{"speed_px",speed_px},{"turn_std_deg",turn_std_deg},
                       {"wall_buffer_px",wall_buffer_px},
                       {"rest_prob",rest_prob},{"resume_prob",resume_prob},
                       {"rest_jitter_px",rest_jitter_px},
                       {"all_resting", all_resting},
                       {"speed_variation", speed_variation},
                       {"speed_slow_px", speed_slow_px},
                       {"speed_normal_px", speed_normal_px},
                       {"speed_fast_px", speed_fast_px},
                       {"speed_class_std_px", speed_class_std_px}};
}
TrajectoryConfig TrajectoryConfig::fromJson(const QJsonObject& o) {
    TrajectoryConfig t;
    t.n_warmup_frames = jInt   (o,"n_warmup_frames",t.n_warmup_frames);
    t.n_animal_frames = jInt   (o,"n_animal_frames",t.n_animal_frames);
    t.fps             = jDouble(o,"fps",            t.fps);
    t.speed_px        = jDouble(o,"speed_px",       t.speed_px);
    t.turn_std_deg    = jDouble(o,"turn_std_deg",   t.turn_std_deg);
    t.wall_buffer_px  = jDouble(o,"wall_buffer_px", t.wall_buffer_px);
    t.rest_prob       = jDouble(o,"rest_prob",      t.rest_prob);
    t.resume_prob     = jDouble(o,"resume_prob",    t.resume_prob);
    t.rest_jitter_px  = jDouble(o,"rest_jitter_px", t.rest_jitter_px);
    t.all_resting     = o.value("all_resting").toBool(false);
    t.speed_variation    = o.value("speed_variation").toBool(t.speed_variation);
    t.speed_slow_px      = jDouble(o,"speed_slow_px",      t.speed_slow_px);
    t.speed_normal_px    = jDouble(o,"speed_normal_px",    t.speed_normal_px);
    t.speed_fast_px      = jDouble(o,"speed_fast_px",      t.speed_fast_px);
    t.speed_class_std_px = jDouble(o,"speed_class_std_px", t.speed_class_std_px);
    return t;
}

QJsonObject NoiseConfig::toJson() const {
    return QJsonObject{{"gaussian_std",gaussian_std},{"n_reflections",n_reflections},
                       {"reflection_radius_px",reflection_radius_px},
                       {"reflection_brightness",reflection_brightness},
                       {"temporal_flicker_std",temporal_flicker_std}};
}
NoiseConfig NoiseConfig::fromJson(const QJsonObject& o) {
    NoiseConfig n;
    n.gaussian_std          = jDouble(o,"gaussian_std",n.gaussian_std);
    n.n_reflections         = jInt(o,"n_reflections",n.n_reflections);
    n.reflection_radius_px  = jInt(o,"reflection_radius_px",n.reflection_radius_px);
    n.reflection_brightness = jInt(o,"reflection_brightness",n.reflection_brightness);
    n.temporal_flicker_std  = jDouble(o,"temporal_flicker_std",n.temporal_flicker_std);
    return n;
}

SynthConfig SynthConfig::defaults() { return {}; }

QJsonObject SynthConfig::toJson() const {
    return QJsonObject{{"output_dir",output_dir},{"seed",seed},{"n_records",n_records},
                       {"arena",arena.toJson()},{"animal",animal.toJson()},
                       {"trajectory",trajectory.toJson()},{"noise",noise.toJson()}};
}

SynthConfig SynthConfig::fromJsonFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "[SynthConfig] Cannot open config file:" << path << "— using defaults";
        return defaults();
    }
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonObject o = doc.object();
    SynthConfig cfg;
    cfg.output_dir  = jStr(o,"output_dir",cfg.output_dir);
    cfg.seed        = jInt(o,"seed",cfg.seed);
    cfg.n_records   = jInt(o,"n_records",cfg.n_records);
    if (o.contains("arena"))      cfg.arena      = ArenaConfig::fromJson(o["arena"].toObject());
    if (o.contains("animal"))     cfg.animal     = AnimalConfig::fromJson(o["animal"].toObject());
    if (o.contains("trajectory")) cfg.trajectory = TrajectoryConfig::fromJson(o["trajectory"].toObject());
    if (o.contains("noise"))      cfg.noise      = NoiseConfig::fromJson(o["noise"].toObject());
    return cfg;
}

// ─────────────────────────────────────────────────────────────────────────────
// SyntheticGenerator
// ─────────────────────────────────────────────────────────────────────────────

SyntheticGenerator::SyntheticGenerator(const SynthConfig& cfg) : m_cfg(cfg) {}

// ── Background ────────────────────────────────────────────────────────────────

cv::Mat SyntheticGenerator::makeBackground() {
    const auto& a = m_cfg.arena;
    const auto& n = m_cfg.noise;
    const int h = a.height_px, w = a.width_px;

    std::mt19937_64 rng(static_cast<uint64_t>(m_cfg.seed));
    std::normal_distribution<float>  normal_d(0.f, static_cast<float>(a.floor_texture_std));
    std::uniform_real_distribution<double> uniform_d(0.0, 1.0);

    // Start with out-of-arena darkness
    cv::Mat bg(h, w, CV_32F, cv::Scalar(38.f));

    // Circular masks
    cv::Mat inside_mask(h, w, CV_8U, cv::Scalar(0));
    cv::circle(inside_mask, cv::Point(a.center_x, a.center_y), a.radius_px, 255, -1);

    cv::Mat floor(h, w, CV_32F, cv::Scalar(static_cast<float>(a.floor_gray)));
    // Per-pixel static texture noise
    cv::Mat texture(h, w, CV_32F);
    cv::randn(texture, 0.f, static_cast<float>(a.floor_texture_std));
    floor += texture;

    floor.copyTo(bg, inside_mask);

    // Wall ring
    cv::Mat wall_mask(h, w, CV_8U, cv::Scalar(0));
    cv::circle(wall_mask, cv::Point(a.center_x, a.center_y), a.radius_px, 255, -1);
    cv::circle(wall_mask, cv::Point(a.center_x, a.center_y),
               a.radius_px - a.wall_thickness_px, 0, -1);
    bg.setTo(static_cast<float>(a.wall_gray), wall_mask);

    // Static reflections (bright spots from overhead lamps)
    for (int r = 0; r < n.n_reflections; ++r) {
        double ang = uniform_d(rng) * 2.0 * M_PI;
        double ri  = uniform_d(rng) * a.radius_px * 0.72;
        int rx = static_cast<int>(a.center_x + ri * std::cos(ang));
        int ry = static_cast<int>(a.center_y + ri * std::sin(ang));

        cv::Mat spot(h, w, CV_32F, cv::Scalar(0.f));
        cv::circle(spot, cv::Point(rx, ry), n.reflection_radius_px,
                   static_cast<float>(n.reflection_brightness), -1);
        double sigma = n.reflection_radius_px * 0.55;
        cv::GaussianBlur(spot, spot, cv::Size(0, 0), sigma);
        bg += spot;
    }

    cv::Mat result;
    bg.convertTo(result, CV_8U);  // saturating conversion
    return result;
}

// ── Trajectory ────────────────────────────────────────────────────────────────

std::vector<FramePose> SyntheticGenerator::generateTrajectory() {
    // Per-record mean-speed heterogeneity: draw this record's constant speed from
    // one of three behavioural classes so a generation batch has a realistic spread
    // of individual movement speeds instead of every record moving identically.
    // Overwrites trajectory.speed_px so the drawn value is what gets persisted to
    // this record's synth_config.json (see writeSynthConfigJson).
    if (m_cfg.trajectory.speed_variation) {
        std::mt19937_64 speed_rng(static_cast<uint64_t>(m_cfg.seed) + 7);
        std::uniform_int_distribution<int> class_d(0, 2);
        std::normal_distribution<double> jitter_d(0.0, m_cfg.trajectory.speed_class_std_px);
        const double class_means[3] = {
            m_cfg.trajectory.speed_slow_px,
            m_cfg.trajectory.speed_normal_px,
            m_cfg.trajectory.speed_fast_px,
        };
        const double mean = class_means[class_d(speed_rng)];
        m_cfg.trajectory.speed_px = std::max(0.5, mean + jitter_d(speed_rng));
    }

    const auto& a = m_cfg.arena;
    const auto& t = m_cfg.trajectory;
    const auto& an = m_cfg.animal;

    const double cx = a.center_x, cy = a.center_y;
    const double safe_r = a.radius_px - t.wall_buffer_px - an.semi_major_px;

    std::mt19937_64 rng(static_cast<uint64_t>(m_cfg.seed + 1));
    std::normal_distribution<double>        normal_d(0.0, 1.0);
    std::uniform_real_distribution<double>  uniform_d(0.0, 1.0);

    const double turn_std = t.turn_std_deg * M_PI / 180.0;
    const long long ns_per_frame = static_cast<long long>(1e9 / t.fps);

    // Start near centre
    double heading = uniform_d(rng) * 2.0 * M_PI;
    double r0 = uniform_d(rng) * safe_r * 0.4;
    double x = cx + r0 * std::cos(heading + M_PI / 2);
    double y = cy + r0 * std::sin(heading + M_PI / 2);

    // Two-state Markov: MOVING ↔ RESTING
    // Safety cap: MOG2 absorbs a perfectly still object after ~history frames.
    // We force a resume if the animal has been stationary for too long.
    const int mog2_absorption_limit = static_cast<int>(t.n_warmup_frames * 0.8);
    bool is_resting        = false;
    int  consecutive_rest  = 0;

    const int total = t.n_warmup_frames + t.n_animal_frames;
    std::vector<FramePose> poses;
    poses.reserve(total);

    for (int i = 0; i < total; ++i) {
        FramePose p;
        p.idx       = i;
        p.ts_ns     = static_cast<long long>(i) * ns_per_frame;
        p.is_warmup = (i < t.n_warmup_frames);

        if (p.is_warmup) {
            poses.push_back(p);
            continue;
        }

        // Dead / completely stationary animal: skip Markov chain and absorption cap
        if (t.all_resting) {
            x += normal_d(rng) * t.rest_jitter_px;
            y += normal_d(rng) * t.rest_jitter_px;
            p.x_px       = x;
            p.y_px       = y;
            p.heading_rad = heading;
            p.is_resting  = true;
            poses.push_back(p);
            continue;
        }

        // ── State transition ────────────────────────────────────────────────
        if (is_resting) {
            ++consecutive_rest;
            // Force resume before MOG2 absorbs the animal
            if (consecutive_rest >= mog2_absorption_limit ||
                uniform_d(rng) < t.resume_prob) {
                is_resting       = false;
                consecutive_rest = 0;
            }
        } else {
            if (uniform_d(rng) < t.rest_prob) {
                is_resting       = true;
                consecutive_rest = 0;
            }
        }

        // ── Position update ─────────────────────────────────────────────────
        if (is_resting) {
            // Micro-jitter only (body tremor / vibration)
            x += normal_d(rng) * t.rest_jitter_px;
            y += normal_d(rng) * t.rest_jitter_px;
        } else {
            // Wall repulsion: steer toward centre when near boundary
            double dist_c = std::hypot(x - cx, y - cy);
            if (dist_c > safe_r * 0.85) {
                double to_center = std::atan2(cy - y, cx - x);
                double delta = to_center - heading;
                delta = std::fmod(delta + M_PI, 2.0 * M_PI) - M_PI;
                heading += 0.25 * delta;
            }

            heading += normal_d(rng) * turn_std;
            heading  = std::fmod(heading, 2.0 * M_PI);
            if (heading < 0.0) heading += 2.0 * M_PI;

            double nx = x + t.speed_px * std::cos(heading);
            double ny = y + t.speed_px * std::sin(heading);

            // Hard boundary: bounce
            if (std::hypot(nx - cx, ny - cy) > safe_r) {
                double n_angle = std::atan2(ny - cy, nx - cx);
                heading = n_angle + M_PI + (uniform_d(rng) * 0.6 - 0.3);
                nx = cx + safe_r * 0.92 * std::cos(n_angle + M_PI);
                ny = cy + safe_r * 0.92 * std::sin(n_angle + M_PI);
            }
            x = nx; y = ny;
        }

        p.x_px        = x;
        p.y_px        = y;
        p.heading_rad = heading;
        p.is_resting  = is_resting;
        poses.push_back(p);
    }
    return poses;
}

// ── Rendering ─────────────────────────────────────────────────────────────────

void SyntheticGenerator::drawAnimal(cv::Mat& frame_f32, const FramePose& pose) {
    const auto& an = m_cfg.animal;
    const int cx = static_cast<int>(std::round(pose.x_px));
    const int cy = static_cast<int>(std::round(pose.y_px));

    cv::Mat mask(frame_f32.size(), CV_8U, cv::Scalar(0));
    if (an.body_type == "long") {
        double deg = pose.heading_rad * 180.0 / M_PI;
        cv::ellipse(mask, cv::Point(cx, cy),
                    cv::Size(an.semi_major_px, an.semi_minor_px),
                    deg, 0.0, 360.0, 255, -1);
    } else {
        cv::circle(mask, cv::Point(cx, cy), an.round_radius_px, 255, -1);
    }
    frame_f32.setTo(static_cast<float>(an.gray_value), mask);
}

cv::Mat SyntheticGenerator::renderFrame(const cv::Mat& bg, const FramePose& pose,
                                         double flicker_offset) {
    cv::Mat frame;
    bg.convertTo(frame, CV_32F);

    frame += static_cast<float>(flicker_offset);

    if (!pose.is_warmup)
        drawAnimal(frame, pose);

    cv::Mat camera_noise(frame.size(), CV_32F);
    cv::randn(camera_noise, 0.f, static_cast<float>(m_cfg.noise.gaussian_std));
    frame += camera_noise;

    cv::Mat result;
    frame.convertTo(result, CV_8U);  // saturating
    return result;
}

// ── JSON writers ──────────────────────────────────────────────────────────────

void SyntheticGenerator::writeGroundTruth(const QString& dir,
                                           const std::vector<FramePose>& poses,
                                           const QString& record_id) {
    QJsonArray frames_arr;
    for (const auto& p : poses) {
        QJsonObject f;
        f["idx"]       = p.idx;
        f["ts_ns"]     = static_cast<qint64>(p.ts_ns);
        f["is_warmup"] = p.is_warmup;
        if (!p.is_warmup) {
            f["x_px"]        = p.x_px;
            f["y_px"]        = p.y_px;
            f["heading_deg"] = p.heading_rad * 180.0 / M_PI;
            f["is_resting"]  = p.is_resting;
        }
        frames_arr.append(f);
    }

    QJsonObject root;
    root["generator"]        = "pipeoptimizer";
    root["record_id"]        = record_id;
    root["body_type"]        = m_cfg.animal.body_type;
    root["fps"]              = m_cfg.trajectory.fps;
    root["n_warmup_frames"]  = m_cfg.trajectory.n_warmup_frames;
    root["arena"]            = m_cfg.arena.toJson();
    root["animal"]           = m_cfg.animal.toJson();
    root["trajectory"]       = m_cfg.trajectory.toJson();
    root["noise"]            = m_cfg.noise.toJson();
    root["frames"]           = frames_arr;

    QFile f(dir + "/ground_truth.json");
    f.open(QIODevice::WriteOnly);
    f.write(QJsonDocument(root).toJson());
}

void SyntheticGenerator::writeRecordJson(const QString& dir,
                                          const QString& record_id, int n_frames) {
    QJsonObject o;
    o["recordUID"]      = QStringLiteral("synth-%1").arg(record_id);
    o["fileUID"]        = QStringLiteral("synth-%1").arg(record_id);
    o["pipelineUID"]    = QStringLiteral("pipe-synth-%1").arg(record_id);
    o["cameraUID"]      = "synthetic";
    o["status"]         = "completed";
    o["framesRecorded"] = n_frames;
    o["actualFPS"]      = m_cfg.trajectory.fps;
    o["targetFPS"]      = m_cfg.trajectory.fps;
    o["isSynthetic"]    = true;
    o["bodyType"]       = m_cfg.animal.body_type;

    QFile f(dir + "/record-synth-" + record_id + ".json");
    f.open(QIODevice::WriteOnly);
    f.write(QJsonDocument(o).toJson());
}

// Full effective SynthConfig for this specific record (seed, body_type, all_resting,
// etc. all reflect the per-record state, unlike the batch-level synth_config.json
// written once by `pipeoptimizer generate`). Read back by SimRunner and persisted into
// sim_results.sqlite so every simulation result carries its exact generation config.
void SyntheticGenerator::writeSynthConfigJson(const QString& dir) {
    QFile f(dir + "/synth_config.json");
    f.open(QIODevice::WriteOnly);
    f.write(QJsonDocument(m_cfg.toJson()).toJson());
}

// Pipeline JSON: pipe-default-42 core detection steps only — no lens correction,
// no 3D projection (synthetic frames need neither).
void SyntheticGenerator::writePipelineJson(const QString& dir, const QString& record_id) {
    auto step = [](const QString& name, const QJsonObject& params, bool enabled = true) {
        return QJsonObject{{"name",name},{"enabled",enabled ? 1 : 0},{"parameters",params}};
    };

    QJsonArray steps;
    steps.append(step("BackgroundSubtraction_CUDA",
        QJsonObject{{"detectShadows",0.0},{"history",500.0},{"varThreshold",16.0}}));
    steps.append(step("BinaryThreshold_CUDA",
        QJsonObject{{"max_value",255.0},{"threshold",80.0}}));
    steps.append(step("MorphologyClose_CUDA",
        QJsonObject{{"iterations",2.0},{"ksize",15.0},{"shape",2.0}}));
    steps.append(step("EdgeDetection_CUDA",
        QJsonObject{{"aperture_size",3.0},{"threshold1",150.0},{"threshold2",200.0}}));
    steps.append(step("ContourAreaFilter_CUDA",
        QJsonObject{{"debug",1.0},{"max_area",300000.0},{"min_area",7.0}}));
    steps.append(step("MassCenterOverlay_CPU", QJsonObject{}));

    QJsonObject pipe;
    pipe["processing_mode"] = "CUDA_PREFERRED";
    pipe["steps"] = steps;

    QJsonObject root;
    root["pipelineUID"]  = QStringLiteral("pipe-synth-%1").arg(record_id);
    root["status"]       = "active";
    root["description"]  = "synthetic eval pipeline — no lens-correction, no 3D projection";
    root["pipeline"]     = pipe;

    QFile f(dir + "/pipeline-synth-" + record_id + ".json");
    f.open(QIODevice::WriteOnly);
    f.write(QJsonDocument(root).toJson());
}

void SyntheticGenerator::writeMetadataLog(const QString& dir, const QString& record_id) {
    QFile f(dir + "/other/metadata_log.txt");
    f.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream s(&f);
    s << "# synthetic record " << record_id << "\n";
    s << "# body_type: "       << m_cfg.animal.body_type << "\n";
    s << "# Format: [FrameIndex] Timestamp(ns) | Metadata\n";
    s << "# " << QString(70, '-') << "\n";
}

// ── Main entry ────────────────────────────────────────────────────────────────

QString SyntheticGenerator::generateRecord(const QString& record_id) {
    const QString outer   = m_cfg.output_dir + "/synth-" + record_id;
    const QString content = outer + "/synth-" + record_id;
    const QString frames_dir = content + "/frames";
    const QString other_dir  = content + "/other";

    QDir().mkpath(frames_dir);
    QDir().mkpath(other_dir);

    std::mt19937_64 flicker_rng(static_cast<uint64_t>(m_cfg.seed + 2));
    std::normal_distribution<double> flicker_d(0.0, m_cfg.noise.temporal_flicker_std);

    const cv::Mat bg = makeBackground();
    const auto poses  = generateTrajectory();

    int n_animal = 0;
    for (const auto& p : poses)
        if (!p.is_warmup) ++n_animal;

    qDebug().noquote()
        << QString("  body=%1  warmup=%2  animal=%3  seed=%4")
               .arg(m_cfg.animal.body_type)
               .arg(m_cfg.trajectory.n_warmup_frames)
               .arg(n_animal)
               .arg(m_cfg.seed);

    static const std::vector<int> jpg_params{cv::IMWRITE_JPEG_QUALITY, 92};

    for (const auto& pose : poses) {
        double flicker = flicker_d(flicker_rng);
        cv::Mat frame  = renderFrame(bg, pose, flicker);

        QString fname = QString("frame_%1_%2.jpg")
                            .arg(pose.idx, 6, 10, QChar('0'))
                            .arg(pose.ts_ns);
        cv::imwrite(QString(frames_dir + "/" + fname).toStdString(), frame, jpg_params);

        if (pose.idx % 200 == 0)
            qDebug().noquote() << QString("    frame %1/%2").arg(pose.idx).arg(poses.size());
    }

    writeGroundTruth(content, poses, record_id);
    writeRecordJson(content, record_id, static_cast<int>(poses.size()));
    writeSynthConfigJson(content);
    writePipelineJson(content, record_id);
    writeMetadataLog(content, record_id);

    return content;
}
