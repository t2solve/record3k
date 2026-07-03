#include "sim_runner.h"
#include "pipe_config_gen.h"

#include <liblbt/pipeline_config_loader.h>
#include <liblbt/processing_pipeline.h>
#include <liblbt/frame_memory_object.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QTemporaryFile>

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

// ── Ground truth loader ───────────────────────────────────────────────────────

struct GtFrame {
    int       idx;
    long long ts_ns;
    bool      is_warmup;
    bool      is_resting;
    float     x_px;
    float     y_px;
};

static std::vector<GtFrame> loadGroundTruth(const std::string& gt_path) {
    QFile f(QString::fromStdString(gt_path));
    if (!f.open(QIODevice::ReadOnly))
        throw std::runtime_error("SimRunner: cannot open " + gt_path);
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (doc.isNull())
        throw std::runtime_error("SimRunner: invalid ground_truth.json at " + gt_path);

    const QJsonArray frames = doc.object()["frames"].toArray();
    std::vector<GtFrame> result;
    result.reserve(frames.size());
    for (const auto& v : frames) {
        QJsonObject o = v.toObject();
        GtFrame g;
        g.idx        = o["idx"].toInt();
        g.ts_ns      = (long long)o["ts_ns"].toDouble();
        g.is_warmup  = o["is_warmup"].toBool();
        g.is_resting = o["is_resting"].toBool();
        g.x_px       = (float)o["x_px"].toDouble();
        g.y_px       = (float)o["y_px"].toDouble();
        result.push_back(g);
    }
    return result;
}

// ── Frame file discovery ──────────────────────────────────────────────────────

static std::vector<std::string> collectFramePaths(const std::string& frames_dir) {
    QDir dir(QString::fromStdString(frames_dir));
    if (!dir.exists())
        throw std::runtime_error("SimRunner: frames dir not found: " + frames_dir);

    QStringList filters;
    filters << "*.jpg" << "*.png";
    QFileInfoList entries = dir.entryInfoList(filters, QDir::Files, QDir::Name);

    std::vector<std::string> paths;
    paths.reserve(entries.size());
    for (const auto& e : entries)
        paths.push_back(e.absoluteFilePath().toStdString());
    return paths;
}

// Extract timestamp from filename "frame_000000_<ts_ns>.jpg"
static long long tsFromFramePath(const std::string& path) {
    // find last underscore before the extension
    size_t dot  = path.rfind('.');
    size_t us   = path.rfind('_', dot);
    if (us == std::string::npos || us + 1 >= dot) return -1;
    try { return std::stoll(path.substr(us + 1, dot - us - 1)); }
    catch (...) { return -1; }
}

// ── SimRunner ─────────────────────────────────────────────────────────────────

SimRunner::SimRunner(const Options& opts) : m_opts(opts) {}

// Walk synth_root for synth-<id>/synth-<id>/ content dirs.
std::vector<SimRunner::GenRecord> SimRunner::loadGenerationSet(SimDB& db, int64_t sim_run_id) {
    QDir root(QString::fromStdString(m_opts.synth_root));
    if (!root.exists())
        throw std::runtime_error("SimRunner: synth_root not found: " + m_opts.synth_root);

    qInfo() << "[SimRunner] Scanning:" << root.absolutePath();

    // List everything to help diagnose empty-set issues
    const QStringList allEntries = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (allEntries.isEmpty()) {
        qWarning() << "[SimRunner] synth_root is empty — run 'pipeoptimizer generate' first";
    } else {
        qInfo() << "[SimRunner] Dirs found:" << allEntries;
    }

    std::vector<GenRecord> records;

    for (const QString& outer : root.entryList({"synth-*"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        QDir outerDir(root.filePath(outer));
        // inner dir has same name
        QString content_path = outerDir.filePath(outer);
        if (!QDir(content_path).exists()) {
            // fallback: first subdir
            QStringList inner = outerDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            if (inner.isEmpty()) continue;
            content_path = outerDir.filePath(inner.first());
        }

        const std::string frames_dir = QString(content_path + "/frames").toStdString();
        const std::string gt_path    = QString(content_path + "/ground_truth.json").toStdString();

        if (!QDir(QString::fromStdString(frames_dir)).exists()) continue;
        if (!QFile::exists(QString::fromStdString(gt_path)))    continue;

        const std::string record_id = outer.toStdString();

        // Load this record's full synth config — per-record file (written by
        // `generate`, reflects this record's actual seed/body_type/all_resting)
        // preferred, falling back to the batch-level synth_config.json. Both
        // arena_radius_px and the config JSON persisted to the DB come from here.
        double arena_r = m_opts.arena_radius_px;
        std::string config_json;
        const QString per_record_cfg = content_path + "/synth_config.json";
        const QString batch_cfg = QString::fromStdString(m_opts.synth_root) + "/synth_config.json";
        const QString cfg_path = QFile::exists(per_record_cfg) ? per_record_cfg : batch_cfg;
        if (QFile::exists(cfg_path)) {
            QFile cf(cfg_path);
            if (cf.open(QIODevice::ReadOnly)) {
                const QByteArray bytes = cf.readAll();
                config_json = bytes.toStdString();
                QJsonDocument cd = QJsonDocument::fromJson(bytes);
                if (!cd.isNull()) {
                    double r = cd.object()["arena"].toObject()["radius_px"].toDouble();
                    if (r > 0) arena_r = r;
                }
            }
        }

        GenRecord gr;
        gr.record_id      = record_id;
        gr.frames_dir     = frames_dir;
        gr.gt_path        = gt_path;
        gr.config_json    = config_json;
        gr.arena_radius_px = arena_r;
        gr.db_id = db.insertGenerationConfig(sim_run_id, record_id, config_json, content_path.toStdString(), arena_r);
        records.push_back(std::move(gr));
    }

    qInfo() << "[SimRunner] Generation set:" << records.size() << "records";
    return records;
}

// Build pipeline set: either a single fixed pipeline (no-sweep mode) or LHS-generated
// configs; either way, write to <db_dir>/pipe_configs/ and insert to DB.
std::vector<SimRunner::PipeConf> SimRunner::buildPipelineSet(SimDB& db, int64_t sim_run_id) {
    std::vector<PipelineConfigGen::GeneratedConfig> generated;

    if (!m_opts.pipeline_config_path.empty()) {
        // No-sweep mode: run exactly this one pipeline as-is against the generation set.
        QFile pf(QString::fromStdString(m_opts.pipeline_config_path));
        if (!pf.open(QIODevice::ReadOnly))
            throw std::runtime_error("SimRunner: cannot open pipeline config: "
                                     + m_opts.pipeline_config_path);
        const QByteArray raw = pf.readAll();
        QJsonDocument doc = QJsonDocument::fromJson(raw);
        if (doc.isNull())
            throw std::runtime_error("SimRunner: invalid pipeline config JSON: "
                                     + m_opts.pipeline_config_path);

        QString uid = doc.object()["pipelineUID"].toString();
        if (uid.isEmpty())
            uid = QFileInfo(QString::fromStdString(m_opts.pipeline_config_path)).completeBaseName();

        PipelineConfigGen::GeneratedConfig gc;
        gc.pipeline_id = uid.toStdString();
        gc.config_json = std::string(raw.constData(), static_cast<size_t>(raw.size()));
        generated.push_back(std::move(gc));
    } else {
        // Sweep mode: LHS-generate configs from param_ranges.json over a base pipeline.
        std::string base_json;
        if (!m_opts.base_pipeline_path.empty()) {
            QFile bf(QString::fromStdString(m_opts.base_pipeline_path));
            if (!bf.open(QIODevice::ReadOnly))
                throw std::runtime_error("SimRunner: cannot open base pipeline: "
                                         + m_opts.base_pipeline_path);
            base_json = bf.readAll().toStdString();
        } else {
            base_json = PipelineConfigGen::defaultBasePipelineJson();
        }

        generated = PipelineConfigGen::generate(
            base_json, m_opts.param_ranges_path, m_opts.n_pipeline_configs, m_opts.seed);
    }

    // Determine pipe_configs dir (next to db)
    QFileInfo dbInfo(QString::fromStdString(m_opts.db_path));
    QDir pipeCfgDir(dbInfo.dir().filePath("pipe_configs"));
    if (!pipeCfgDir.exists()) pipeCfgDir.mkpath(".");

    std::vector<PipeConf> result;
    result.reserve(generated.size());

    for (auto& gc : generated) {
        // Write config JSON to disk so PipelineConfigLoader can read it
        QString json_path = pipeCfgDir.filePath(QString::fromStdString(gc.pipeline_id) + ".json");
        QFile jf(json_path);
        if (jf.open(QIODevice::WriteOnly))
            jf.write(QByteArray::fromStdString(gc.config_json));

        PipeConf pc;
        pc.pipeline_id   = gc.pipeline_id;
        pc.config_json   = gc.config_json;
        pc.json_file_path = json_path.toStdString();
        pc.db_id = db.insertPipelineConfig(sim_run_id, gc.pipeline_id, gc.config_json);
        result.push_back(std::move(pc));
    }

    qInfo() << "[SimRunner] Pipeline set:" << result.size() << "configs";
    return result;
}

// ── Core pair runner ──────────────────────────────────────────────────────────

std::vector<FrameResult> SimRunner::runOnePair(const GenRecord& gen,
                                                const PipeConf&  pipe,
                                                double&          out_ms) {
    // Load a fresh pipeline (fresh MOG2 state, no cross-record contamination)
    PipelineConfigLoader::PipelineConfig pipelineCfg;
    try {
        pipelineCfg = PipelineConfigLoader::loadFromJSON(pipe.json_file_path);
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("SimRunner: loadFromJSON failed for ") + pipe.pipeline_id
            + ": " + e.what());
    }

    // Load ground truth
    std::vector<GtFrame> gt = loadGroundTruth(gen.gt_path);

    // Collect frame file paths (sorted by filename = frame order)
    std::vector<std::string> frame_paths = collectFramePaths(gen.frames_dir);

    if (frame_paths.size() != gt.size()) {
        qWarning() << "[SimRunner] Warning: frame count" << frame_paths.size()
                   << "!= GT count" << gt.size()
                   << "for record" << QString::fromStdString(gen.record_id);
        // Use the shorter to avoid out-of-bounds
        size_t n = std::min(frame_paths.size(), gt.size());
        frame_paths.resize(n);
        gt.resize(n);
    }

    const size_t n_frames = frame_paths.size();
    std::vector<FrameResult> results;
    results.reserve(n_frames);

    auto t_start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < n_frames; ++i) {
        cv::Mat img = cv::imread(frame_paths[i], cv::IMREAD_COLOR);
        if (img.empty()) {
            qWarning() << "[SimRunner] Skipping unreadable frame:" << QString::fromStdString(frame_paths[i]);
            continue;
        }

        FrameMemoryObject input(img);

        FrameMemoryObject output;
        try {
            output = ProcessingPipeline::processLine(
                pipelineCfg.steps, input, pipelineCfg.configs);
        } catch (const std::exception& e) {
            qWarning() << "[SimRunner] Pipeline error on frame" << (int)i
                       << ":" << e.what();
            // treat as no detection
        }

        FrameResult fr;
        fr.frame_idx  = gt[i].idx;
        fr.ts_ns      = gt[i].ts_ns;
        fr.is_warmup  = gt[i].is_warmup;
        fr.is_resting = gt[i].is_resting;

        // GT position — NaN for warmup frames
        if (!gt[i].is_warmup && gt[i].x_px >= 0.0f) {
            fr.gt_x = gt[i].x_px;
            fr.gt_y = gt[i].y_px;
        }
        // else gt_x/gt_y remain NaN (default)

        // Detection
        if (!output.isEmpty() && output.hasMetadata("mass_center_point")) {
            cv::Point2f center = output.getMetadata<cv::Point2f>("mass_center_point");
            if (center.x >= 0.0f && center.y >= 0.0f) {
                fr.det_x = center.x;
                fr.det_y = center.y;
            }
        }

        // Contour counts (forwarded by MassCenterOverlay)
        if (!output.isEmpty()) {
            if (output.hasMetadata("area_filter_count"))
                fr.area_filter_count = output.getMetadata<int>("area_filter_count");
            if (output.hasMetadata("filtered_contour_count"))
                fr.filtered_count = output.getMetadata<int>("filtered_contour_count");
        }

        results.push_back(fr);
    }

    auto t_end = std::chrono::high_resolution_clock::now();
    out_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    return results;
}

// ── Leaderboard printer ───────────────────────────────────────────────────────

void SimRunner::printLeaderboard(SimDB& db, int64_t sim_run_id) const {
    auto rows = db.getLeaderboard(sim_run_id, 20);
    if (rows.empty()) { std::cout << "(no results)\n"; return; }

    std::cout << "\n=== LEADERBOARD (top " << rows.size()
              << ", sorted by SCORE asc) ===\n";
    std::cout << std::left
              << std::setw(6)  << "#"
              << std::setw(28) << "pipeline_id"
              << std::setw(22) << "record_id"
              << std::setw(8)  << "SCORE"
              << std::setw(8)  << "α"
              << std::setw(8)  << "α_p95"
              << std::setw(8)  << "γmov"
              << std::setw(8)  << "γrest"
              << std::setw(8)  << "δ"
              << std::setw(8)  << "η"
              << std::setw(8)  << "ζ"
              << "\n"
              << std::string(110, '-') << "\n";
    int rank = 1;
    for (const auto& r : rows) {
        std::cout << std::left
                  << std::setw(6)  << rank++
                  << std::setw(28) << r.pipeline_id.substr(0, 27)
                  << std::setw(22) << r.record_id.substr(0, 21)
                  << std::setw(8)  << std::fixed << std::setprecision(1) << r.score
                  << std::setw(8)  << r.alpha_mean
                  << std::setw(8)  << r.alpha_p95
                  << std::setw(8)  << std::setprecision(3) << r.gamma_moving
                  << std::setw(8)  << r.gamma_resting
                  << std::setw(8)  << r.delta_fpr
                  << std::setw(8)  << r.eta
                  << std::setw(8)  << r.zeta
                  << "\n";
    }
    std::cout << "\n";
}

// ── Main entry point ──────────────────────────────────────────────────────────

int SimRunner::run() {
    SimDB db(m_opts.db_path);

    // Insert this run record (n_pipe updated after generation)
    const int64_t sim_run_id = db.insertSimRun(
        m_opts.description, 0, 0);

    // C1: load generation set
    std::vector<GenRecord> gen_records = loadGenerationSet(db, sim_run_id);
    if (gen_records.empty()) {
        qCritical() << "[SimRunner] No generation records found in"
                    << QString::fromStdString(m_opts.synth_root);
        return 1;
    }

    // C2: build pipeline set
    std::vector<PipeConf> pipe_confs = buildPipelineSet(db, sim_run_id);
    if (pipe_confs.empty()) {
        qCritical() << "[SimRunner] Pipeline set is empty";
        return 1;
    }

    const int total = static_cast<int>(gen_records.size() * pipe_confs.size());
    const int n_workers = std::max(1, std::min(m_opts.n_workers, total));

    qInfo() << "[SimRunner] Running" << total << "combinations ("
            << gen_records.size() << "records ×"
            << pipe_confs.size()  << "pipelines)"
            << "with" << n_workers << "workers";

    // Flat work list — each entry is an (gen_idx, pipe_idx) pair
    struct WorkItem { int gi, pi; };
    std::vector<WorkItem> work;
    work.reserve(total);
    for (int gi = 0; gi < (int)gen_records.size(); ++gi)
        for (int pi = 0; pi < (int)pipe_confs.size(); ++pi)
            work.push_back({gi, pi});

    // Shared state accessed from worker threads
    std::atomic<int> work_cursor{0};
    std::atomic<int> done_count{0};
    std::mutex       db_mutex;
    std::mutex       print_mutex;

    // Initialise CUDA on main thread before workers start
    try { cv::cuda::setDevice(0); } catch (...) {}

    // C3 + C4 + C5  (parallel)
    auto worker_fn = [&]() {
        // Bind worker thread to the same CUDA device
        try { cv::cuda::setDevice(0); } catch (...) {}

        while (true) {
            const int idx = work_cursor.fetch_add(1);
            if (idx >= total) break;

            const GenRecord& gen  = gen_records[static_cast<size_t>(work[idx].gi)];
            const PipeConf&  pipe = pipe_confs [static_cast<size_t>(work[idx].pi)];

            double ms = 0.0;
            std::vector<FrameResult> frames;
            try {
                frames = runOnePair(gen, pipe, ms);
            } catch (const std::exception& e) {
                std::lock_guard<std::mutex> lk(print_mutex);
                qWarning() << "[SimRunner] Pair failed:"
                           << QString::fromStdString(gen.record_id)
                           << "×" << QString::fromStdString(pipe.pipeline_id)
                           << "—" << e.what();
                done_count.fetch_add(1);
                continue;
            }

            Metrics m = computeMetrics(frames, m_opts.miss_penalty_px, ms);

            {
                std::lock_guard<std::mutex> lk(db_mutex);
                int64_t res_id = db.insertSimResult(
                    sim_run_id, gen.db_id, pipe.db_id,
                    gen.record_id, pipe.pipeline_id, m);
                if (m_opts.save_frame_results)
                    db.insertFrameResults(res_id, frames, gen.record_id, pipe.pipeline_id);
            }

            const int done = done_count.fetch_add(1) + 1;
            {
                std::lock_guard<std::mutex> lk(print_mutex);
                qInfo().noquote()
                    << QString("[%1/%2] %3 × %4  SCORE=%5 α=%6 γmov=%7 γrest=%8 δ=%9 η=%10 ζ=%11 (%12ms)")
                           .arg(done).arg(total)
                           .arg(QString::fromStdString(gen.record_id).right(18))
                           .arg(QString::fromStdString(pipe.pipeline_id).right(18))
                           .arg(m.score,         0, 'f', 1)
                           .arg(m.alpha_mean,    0, 'f', 1)
                           .arg(m.gamma_moving,  0, 'f', 3)
                           .arg(m.gamma_resting, 0, 'f', 3)
                           .arg(m.delta_fpr,     0, 'f', 3)
                           .arg(m.eta,           0, 'f', 3)
                           .arg(m.zeta,          0, 'f', 3)
                           .arg(ms,              0, 'f', 0);
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(n_workers);
    for (int t = 0; t < n_workers; ++t)
        workers.emplace_back(worker_fn);
    for (auto& w : workers) w.join();

    printLeaderboard(db, sim_run_id);
    qInfo() << "[SimRunner] Results stored in" << QString::fromStdString(m_opts.db_path);
    return 0;
}
