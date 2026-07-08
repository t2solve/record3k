#include "synth_config.h"
#include "synthetic_generator.h"
#include "sim_runner.h"
#include "sim_db.h"
#include "real_sim_runner.h"

#include <algorithm>
#include <iomanip>
#include <iostream>

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryFile>

// ── Subcommand: generate (Step B) ─────────────────────────────────────────────

static int cmdGenerate(QCommandLineParser& cli) {
    cli.addOption({"config",        "JSON config file (any key overrides defaults).", "file"});
    cli.addOption({"output",        "Output root directory.",                          "dir"});
    cli.addOption({"body-type",     "Animal shape: long | round | both.",             "type"});
    cli.addOption({"n-records",     "Records to generate per body type.",             "N"});
    cli.addOption({"static-records","Extra completely-stationary (dead animal) records (default n-records/5, min 1 when n-records>=5).", "N"});
    cli.addOption({"seed",          "Base random seed.",                              "N"});
    cli.addOption({"warmup",        "Background-only frames (≥ MOG2 history).",       "N"});
    cli.addOption({"animal-frames", "Frames with animal present.",                    "N"});
    cli.addOption({"gaussian-std",  "Camera noise σ (DN).",                           "σ"});
    cli.addOption(QCommandLineOption("speed-variation",
                                     "Draw each record's speed from slow/normal/fast Gaussian "
                                     "classes instead of one fixed speed for every record."));
    cli.addOption({"speed-slow",       "Slow-class mean speed (px/frame).",             "px"});
    cli.addOption({"speed-normal",     "Normal-class mean speed (px/frame).",           "px"});
    cli.addOption({"speed-fast",       "Fast-class mean speed (px/frame).",             "px"});
    cli.addOption({"speed-class-std",  "Gaussian σ around the drawn class mean (px/frame).", "px"});
    cli.addOption(QCommandLineOption("dump-defaults",
                                     "Print a full default config JSON and exit."));
    cli.process(QCoreApplication::instance()->arguments());

    if (cli.isSet("dump-defaults")) {
        SynthConfig defaults;
        qInfo().noquote() << QJsonDocument(defaults.toJson()).toJson();
        return 0;
    }

    SynthConfig cfg = cli.isSet("config")
        ? SynthConfig::fromJsonFile(cli.value("config"))
        : SynthConfig::defaults();

    if (cli.isSet("output"))        cfg.output_dir               = cli.value("output");
    if (cli.isSet("n-records"))     cfg.n_records                 = cli.value("n-records").toInt();
    if (cli.isSet("seed"))          cfg.seed                      = cli.value("seed").toInt();
    if (cli.isSet("warmup"))        cfg.trajectory.n_warmup_frames = cli.value("warmup").toInt();
    if (cli.isSet("animal-frames")) cfg.trajectory.n_animal_frames = cli.value("animal-frames").toInt();
    if (cli.isSet("gaussian-std"))  cfg.noise.gaussian_std        = cli.value("gaussian-std").toDouble();
    if (cli.isSet("speed-variation"))  cfg.trajectory.speed_variation    = true;
    if (cli.isSet("speed-slow"))       cfg.trajectory.speed_slow_px      = cli.value("speed-slow").toDouble();
    if (cli.isSet("speed-normal"))     cfg.trajectory.speed_normal_px    = cli.value("speed-normal").toDouble();
    if (cli.isSet("speed-fast"))       cfg.trajectory.speed_fast_px      = cli.value("speed-fast").toDouble();
    if (cli.isSet("speed-class-std"))  cfg.trajectory.speed_class_std_px = cli.value("speed-class-std").toDouble();

    QStringList body_types;
    if (cli.isSet("body-type")) {
        QString bt = cli.value("body-type");
        if (bt == "both") body_types = {"long", "round"};
        else              body_types = {bt};
    } else {
        body_types = {cfg.animal.body_type};
    }

    // Determine how many static (dead-animal) records to add
    int n_static = 0;
    if (cli.isSet("static-records")) {
        n_static = cli.value("static-records").toInt();
    } else if (cfg.n_records >= 5) {
        n_static = std::max(1, cfg.n_records / 5);
    }

    qDebug().noquote() << "=== pipeoptimizer generate ===";
    qDebug().noquote() << "  output  :" << cfg.output_dir;
    qDebug().noquote() << "  body    :" << body_types.join(", ");
    qDebug().noquote() << "  records :" << cfg.n_records << "per type +"
                       << n_static << "static per type";
    qDebug().noquote() << "  warmup  :" << cfg.trajectory.n_warmup_frames
                       << "  animal  :" << cfg.trajectory.n_animal_frames;

    // Write synth_config.json alongside records so sim_runner can read arena_radius_px
    QDir outDir(cfg.output_dir);
    outDir.mkpath(".");
    QFile cfgOut(outDir.filePath("synth_config.json"));
    if (cfgOut.open(QIODevice::WriteOnly))
        cfgOut.write(QJsonDocument(cfg.toJson()).toJson());

    int total = 0;
    for (const QString& bt : body_types) {
        cfg.animal.body_type = bt;
        for (int i = 0; i < cfg.n_records; ++i) {
            QString record_id = QString("%1-%2")
                .arg(QDateTime::currentMSecsSinceEpoch())
                .arg(cfg.seed + total, 4, 10, QChar('0'));

            qDebug().noquote()
                << QString("\n[record %1/%2]  id=%3")
                       .arg(total + 1)
                       .arg(body_types.size() * cfg.n_records)
                       .arg(record_id);

            SyntheticGenerator gen(cfg);
            QString path = gen.generateRecord(record_id);
            qDebug().noquote() << "  ->" << path;

            ++cfg.seed;
            ++total;
        }
    }

    // Generate static (dead animal) records — no movement, no absorption cap
    if (n_static > 0) {
        SynthConfig static_cfg = cfg;
        static_cfg.trajectory.all_resting = true;
        qDebug().noquote() << "\n-- static (dead-animal) records --";
        for (const QString& bt : body_types) {
            static_cfg.animal.body_type = bt;
            for (int i = 0; i < n_static; ++i) {
                QString record_id = QString("s%1-%2")  // 's' prefix marks static
                    .arg(QDateTime::currentMSecsSinceEpoch())
                    .arg(static_cfg.seed + total, 4, 10, QChar('0'));
                qDebug().noquote()
                    << QString("[static %1/%2]  id=%3")
                           .arg(i + 1).arg(n_static).arg(record_id);
                SyntheticGenerator gen(static_cfg);
                QString path = gen.generateRecord(record_id);
                qDebug().noquote() << "  ->" << path;
                ++static_cfg.seed;
                ++total;
            }
        }
    }

    qDebug().noquote() << QString("\nDone. %1 record(s) written to %2")
                              .arg(total).arg(cfg.output_dir);
    return 0;
}

// ── Subcommand: run (Step C) ──────────────────────────────────────────────────

static int cmdRun(QCommandLineParser& cli) {
    cli.addOption({"synth-root",    "Dir containing synth-* record dirs (C1 output).", "dir"});
    cli.addOption({"db",            "Output SQLite database path.",                     "file"});
    cli.addOption({"base-pipeline", "Base pipeline JSON file (default: built-in). Sweep mode only.", "file"});
    cli.addOption({"param-ranges",  "param_ranges.json path. Sweep mode: LHS-generates --n-pipes configs.", "file"});
    cli.addOption({"pipeline-config", "Single fixed pipeline JSON to run as-is, no sweep. "
                                     "Mutually exclusive with --param-ranges.",       "file"});
    cli.addOption({"n-pipes",       "Number of pipeline configs to generate (LHS).",   "N"});
    cli.addOption({"seed",          "Random seed for LHS pipeline generation.",        "N"});
    cli.addOption({"description",   "Label for this simulation run.",                  "text"});
    cli.addOption({"arena-radius",  "Arena radius in pixels (default 400).",           "px"});
    cli.addOption({"miss-penalty",  "SCORE cost per missed animal frame in px (default 100 = arena/4).", "px"});
    cli.addOption({"n-workers",     "Parallel worker threads (default 4).",            "N"});
    cli.addOption(QCommandLineOption("save-frames",
                                     "Write per-frame rows to the database (large!)."));
    cli.process(QCoreApplication::instance()->arguments());

    SimRunner::Options opts;
    opts.synth_root          = cli.value("synth-root").toStdString();
    opts.db_path             = cli.isSet("db")
                               ? cli.value("db").toStdString()
                               : QString(cli.value("synth-root") + "/sim_results.sqlite").toStdString();
    opts.base_pipeline_path  = cli.value("base-pipeline").toStdString();
    opts.param_ranges_path   = cli.value("param-ranges").toStdString();
    opts.pipeline_config_path = cli.value("pipeline-config").toStdString();
    opts.n_pipeline_configs  = cli.isSet("n-pipes")  ? cli.value("n-pipes").toInt() : 20;
    opts.seed                = cli.isSet("seed")      ? (uint64_t)cli.value("seed").toULongLong() : 42;
    opts.description         = cli.isSet("description")
                               ? cli.value("description").toStdString()
                               : "sim-run-" + QDateTime::currentDateTime()
                                     .toString(Qt::ISODate).toStdString();
    opts.arena_radius_px     = cli.isSet("arena-radius")  ? cli.value("arena-radius").toDouble()  : 400.0;
    opts.miss_penalty_px     = cli.isSet("miss-penalty")  ? cli.value("miss-penalty").toDouble()  : 100.0;
    opts.n_workers           = cli.isSet("n-workers") ? cli.value("n-workers").toInt() : 4;
    opts.save_frame_results  = cli.isSet("save-frames");

    if (opts.synth_root.empty()) {
        qCritical() << "run: --synth-root is required";
        return 1;
    }
    if (opts.param_ranges_path.empty() && opts.pipeline_config_path.empty()) {
        qCritical() << "run: either --param-ranges (sweep) or --pipeline-config (single fixed pipeline) is required";
        return 1;
    }
    if (!opts.param_ranges_path.empty() && !opts.pipeline_config_path.empty()) {
        qCritical() << "run: --param-ranges and --pipeline-config are mutually exclusive";
        return 1;
    }

    qInfo().noquote() << "=== pipeoptimizer run ===";
    qInfo().noquote() << "  synth-root   :" << QString::fromStdString(opts.synth_root);
    qInfo().noquote() << "  db           :" << QString::fromStdString(opts.db_path);
    if (!opts.pipeline_config_path.empty()) {
        qInfo().noquote() << "  mode         : single fixed pipeline (no sweep)";
        qInfo().noquote() << "  pipeline     :" << QString::fromStdString(opts.pipeline_config_path);
    } else {
        qInfo().noquote() << "  mode         : LHS sweep";
        qInfo().noquote() << "  n-pipes      :" << opts.n_pipeline_configs;
        qInfo().noquote() << "  seed         :" << opts.seed;
    }
    qInfo().noquote() << "  n-workers    :" << opts.n_workers;
    qInfo().noquote() << "  save-frames  :" << (opts.save_frame_results ? "yes" : "no");

    SimRunner runner(opts);
    return runner.run();
}

// ── Subcommand: run-real (Step 2, src/pipeoptimizer/plan.md) ──────────────────
// Sweeps pipeline configs against ONE real record's frames (decoded once, replayed
// once per config -- see RealSimRunner), scored with a no-ground-truth proxy metric
// instead of the synthetic sweep's GT-based one. Writes one row per config to a
// per-record sqlite file.

static int cmdRunReal(QCommandLineParser& cli) {
    cli.addOption({"record-archive", "Record .tar.gz to extract, score, then clean up. "
                                     "Mutually exclusive with --record-dir.",           "file"});
    cli.addOption({"record-dir",    "Already-unpacked record directory, used as-is (never deleted). "
                                     "Mutually exclusive with --record-archive.",       "dir"});
    cli.addOption({"base-pipeline", "Base pipeline JSON file (default: built-in). Sweep mode only.", "file"});
    cli.addOption({"param-ranges",  "param_ranges.json path. Sweep mode: LHS-generates --n-pipes configs.", "file"});
    cli.addOption({"pipeline-config", "Single fixed pipeline JSON to run as-is, no sweep. "
                                     "Mutually exclusive with --param-ranges.",       "file"});
    cli.addOption({"n-pipes",       "Number of pipeline configs to generate (LHS).",   "N"});
    cli.addOption({"seed",          "Random seed for LHS pipeline generation.",        "N"});
    cli.addOption({"description",   "Label for this run.",                             "text"});
    cli.addOption({"db",            "Output sqlite path (default: run_test_param_<record_uid>.sqlite3 "
                                     "next to the record dir).",                        "file"});
    cli.addOption({"tags-db",       "Tag data store to consult for ROI/valid-range/complete-failure "
                                     "(default: data/simulation/supervised_records_data.sqlite3).", "file"});
    cli.addOption({"n-workers",     "Parallel worker threads (default 4).",            "N"});
    cli.addOption({"movement-epsilon-px", "Min displacement between detections to count as real movement "
                                          "(default 5.0). Provisional -- see plan.",   "px"});
    cli.addOption({"min-movement-events", "Hard-fail if fewer real movement events occur (default 3). "
                                          "Provisional -- see plan.",                  "N"});
    cli.addOption({"area-tolerance-pct", "+/- %% from mean detected area before a frame is flagged "
                                         "(default 30). Provisional -- see plan.",     "pct"});
    cli.addOption({"jump-threshold-px", "Displacement between detections above this counts as a jump "
                                        "(default 32.0). Provisional -- see plan.",    "px"});
    cli.addOption({"max-speed-px-per-frame", "Speed cap between detections (default: unbounded, no basis "
                                             "yet for a real value).",                 "px"});
    cli.addOption({"roi-violation-tolerance", "Consecutive out-of-ROI frames tolerated before counting a "
                                              "violation (default 5).",                "N"});
    cli.process(QCoreApplication::instance()->arguments());

    RealSimRunner::Options opts;
    opts.record_archive_path = cli.value("record-archive").toStdString();
    opts.record_dir_path = cli.value("record-dir").toStdString();
    opts.base_pipeline_path = cli.value("base-pipeline").toStdString();
    opts.param_ranges_path = cli.value("param-ranges").toStdString();
    opts.pipeline_config_path = cli.value("pipeline-config").toStdString();
    opts.n_pipeline_configs = cli.isSet("n-pipes") ? cli.value("n-pipes").toInt() : 20;
    opts.seed = cli.isSet("seed") ? (uint64_t)cli.value("seed").toULongLong() : 42;
    opts.description = cli.value("description").toStdString();
    opts.db_path = cli.value("db").toStdString();
    opts.tags_db_path = cli.value("tags-db").toStdString();
    opts.n_workers = cli.isSet("n-workers") ? cli.value("n-workers").toInt() : 4;

    if (cli.isSet("movement-epsilon-px"))
        opts.metrics_config.movement_epsilon_px = cli.value("movement-epsilon-px").toDouble();
    if (cli.isSet("min-movement-events"))
        opts.metrics_config.min_movement_events = cli.value("min-movement-events").toInt();
    if (cli.isSet("area-tolerance-pct"))
        opts.metrics_config.area_tolerance_pct = cli.value("area-tolerance-pct").toDouble();
    if (cli.isSet("jump-threshold-px"))
        opts.metrics_config.jump_threshold_px = cli.value("jump-threshold-px").toDouble();
    if (cli.isSet("max-speed-px-per-frame"))
        opts.metrics_config.max_speed_px_per_frame = cli.value("max-speed-px-per-frame").toDouble();
    if (cli.isSet("roi-violation-tolerance"))
        opts.metrics_config.roi_violation_tolerance = cli.value("roi-violation-tolerance").toInt();

    if (opts.record_archive_path.empty() && opts.record_dir_path.empty()) {
        qCritical() << "run-real: either --record-archive or --record-dir is required";
        return 1;
    }
    if (!opts.record_archive_path.empty() && !opts.record_dir_path.empty()) {
        qCritical() << "run-real: --record-archive and --record-dir are mutually exclusive";
        return 1;
    }
    if (opts.param_ranges_path.empty() && opts.pipeline_config_path.empty()) {
        qCritical() << "run-real: either --param-ranges (sweep) or --pipeline-config (single fixed pipeline) is required";
        return 1;
    }
    if (!opts.param_ranges_path.empty() && !opts.pipeline_config_path.empty()) {
        qCritical() << "run-real: --param-ranges and --pipeline-config are mutually exclusive";
        return 1;
    }

    qInfo().noquote() << "=== pipeoptimizer run-real ===";
    if (!opts.record_archive_path.empty())
        qInfo().noquote() << "  record-archive:" << QString::fromStdString(opts.record_archive_path);
    else
        qInfo().noquote() << "  record-dir    :" << QString::fromStdString(opts.record_dir_path);
    qInfo().noquote() << "  n-workers     :" << opts.n_workers;

    RealSimRunner runner(std::move(opts));
    return runner.run();
}

// ── Subcommand: leaderboard (C5 global view) ──────────────────────────────────

// Writes the leader's (rows[0]) pipeline config_json to a randomly-named file
// under export_dir (default: system tmp dir) and prints the path. Returns
// false (with a warning) if the leader's config_json can't be found in the DB.
static bool exportLeaderConfig(SimDB& db, const AggregateLeaderboardRow& leader,
                                const QString& export_dir) {
    const std::string config_json = db.getPipelineConfigJson(leader.pipeline_id);
    if (config_json.empty()) {
        qWarning() << "leaderboard --export-leader: no pipeline_configs row found for"
                   << QString::fromStdString(leader.pipeline_id);
        return false;
    }

    const QString dir = export_dir.isEmpty() ? QDir::tempPath() : export_dir;
    QDir().mkpath(dir);
    QTemporaryFile tmp(dir + "/pipeoptimizer-leader-XXXXXX.json");
    tmp.setAutoRemove(false);
    if (!tmp.open()) {
        qWarning() << "leaderboard --export-leader: cannot create temp file in" << dir;
        return false;
    }
    tmp.write(config_json.data(), static_cast<qint64>(config_json.size()));
    tmp.close();

    std::cout << "Leader pipeline (" << leader.pipeline_id << ", avg SCORE="
              << std::fixed << std::setprecision(1) << leader.avg_score
              << ") config written to: " << tmp.fileName().toStdString() << "\n";
    return true;
}

static int cmdLeaderboardAggregate(SimDB& db, int limit, int64_t run_id,
                                    bool export_leader, const QString& export_dir) {
    const auto rows = db.getAggregateLeaderboard(limit, run_id);
    if (rows.empty()) {
        std::cout << "(no results)\n";
        return 0;
    }

    std::cout << "\n=== AGGREGATE LEADERBOARD (top " << rows.size()
              << ", one row per pipeline_id averaged over every record it was tested "
                 "against, sorted by avg SCORE asc"
              << (run_id >= 0 ? ", scoped to sim_run_id=" + std::to_string(run_id) : ", across all sim_runs")
              << ") ===\n";
    std::cout << std::left
              << std::setw(28) << "pipeline_id"
              << std::setw(7)  << "n_rec"
              << std::setw(7)  << "n_run"
              << std::setw(9)  << "avgSCORE"
              << std::setw(8)  << "a"
              << std::setw(8)  << "a_p95"
              << std::setw(8)  << "gmov"
              << std::setw(8)  << "grest"
              << std::setw(8)  << "d"
              << std::setw(8)  << "eta"
              << std::setw(8)  << "zeta"
              << "\n"
              << std::string(115, '-') << "\n";

    for (const auto& r : rows) {
        std::cout << std::left
                  << std::setw(28) << r.pipeline_id.substr(0, 27)
                  << std::setw(7)  << r.n_records
                  << std::setw(7)  << r.n_sim_runs
                  << std::setw(9)  << std::fixed << std::setprecision(1) << r.avg_score
                  << std::setw(8)  << r.avg_alpha_mean
                  << std::setw(8)  << r.avg_alpha_p95
                  << std::setw(8)  << std::setprecision(3) << r.avg_gamma_moving
                  << std::setw(8)  << r.avg_gamma_resting
                  << std::setw(8)  << r.avg_delta_fpr
                  << std::setw(8)  << r.avg_eta
                  << std::setw(8)  << r.avg_zeta
                  << "\n";
    }
    std::cout << "\n";

    if (export_leader && !exportLeaderConfig(db, rows.front(), export_dir))
        return 1;
    return 0;
}

static int cmdLeaderboardRaw(SimDB& db, int limit, int64_t run_id) {
    const auto rows = run_id >= 0 ? db.getLeaderboard(run_id, limit)
                                   : db.getGlobalLeaderboard(limit);
    if (rows.empty()) {
        std::cout << "(no results)\n";
        return 0;
    }

    std::cout << "\n=== LEADERBOARD (top " << rows.size()
              << ", sorted by SCORE asc, "
              << (run_id >= 0 ? "scoped to sim_run_id=" + std::to_string(run_id)
                              : "across all simulation runs in this DB")
              << ") ===\n";
    std::cout << std::left
              << std::setw(5)  << "#"
              << std::setw(18) << "run"
              << std::setw(28) << "pipeline_id"
              << std::setw(22) << "record_id"
              << std::setw(8)  << "SCORE"
              << std::setw(8)  << "a"
              << std::setw(8)  << "a_p95"
              << std::setw(8)  << "gmov"
              << std::setw(8)  << "grest"
              << std::setw(8)  << "d"
              << std::setw(8)  << "eta"
              << std::setw(8)  << "zeta"
              << "\n"
              << std::string(140, '-') << "\n";

    int rank = 1;
    for (const auto& r : rows) {
        const std::string run_label = "#" + std::to_string(r.sim_run_id)
            + (r.run_description.empty() ? "" : (" " + r.run_description));
        std::cout << std::left
                  << std::setw(5)  << rank++
                  << std::setw(18) << run_label.substr(0, 17)
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
    return 0;
}

static int cmdLeaderboard(QCommandLineParser& cli) {
    cli.addOption({"db",    "SQLite database to read (accumulates across all simulation runs).", "file"});
    cli.addOption({"limit", "Number of rows to show (default 30).",                                "N"});
    cli.addOption({"run-id", "Scope to one sim_run_id instead of the whole DB.",                   "N"});
    cli.addOption(QCommandLineOption("aggregate",
        "Group by pipeline_id and average every metric across all records it was "
        "tested against (best-on-average configuration), instead of raw per-"
        "(pipeline,record) rows."));
    cli.addOption(QCommandLineOption("export-leader",
        "Also write the #1 ranked pipeline's full config JSON to a random-named "
        "file (see --export-dir). Aggregate mode only."));
    cli.addOption({"export-dir", "Directory for --export-leader output (default: system tmp dir).", "dir"});
    cli.process(QCoreApplication::instance()->arguments());

    if (!cli.isSet("db")) {
        qCritical() << "leaderboard: --db is required";
        return 1;
    }
    if (cli.isSet("export-leader") && !cli.isSet("aggregate")) {
        qCritical() << "leaderboard: --export-leader requires --aggregate";
        return 1;
    }
    const int limit  = cli.isSet("limit")  ? cli.value("limit").toInt() : 30;
    const int64_t run_id = cli.isSet("run-id") ? cli.value("run-id").toLongLong() : -1;

    SimDB db(cli.value("db").toStdString());
    return cli.isSet("aggregate")
        ? cmdLeaderboardAggregate(db, limit, run_id, cli.isSet("export-leader"),
                                   cli.value("export-dir"))
        : cmdLeaderboardRaw(db, limit, run_id);
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("pipeoptimizer");
    app.setApplicationVersion("0.2");

    QCommandLineParser cli;
    cli.setApplicationDescription(
        "pipeoptimizer — synthetic record generation and pipeline sweep.\n"
        "\n"
        "Commands:\n"
        "  generate    Generate synthetic tracking records (Step B)\n"
        "  run         Run pipeline optimisation sweep or single pipeline (Step C)\n"
        "  run-real    Sweep pipeline configs against ONE real record, no ground truth "
                       "(plan.md step 2)\n"
        "  leaderboard Show global leaderboard across all runs stored in a DB\n"
        "\n"
        "Usage:\n"
        "  pipeoptimizer generate    [options]\n"
        "  pipeoptimizer run         [options]\n"
        "  pipeoptimizer run-real    [options]\n"
        "  pipeoptimizer leaderboard [options]");
    cli.addHelpOption();
    cli.addVersionOption();
    cli.addPositionalArgument("command", "generate | run | run-real | leaderboard");

    // Parse only the first argument to determine command
    cli.parse(app.arguments());
    const QStringList pos = cli.positionalArguments();

    if (pos.isEmpty() || pos.first() == "generate")
        return cmdGenerate(cli);
    if (pos.first() == "run")
        return cmdRun(cli);
    if (pos.first() == "run-real")
        return cmdRunReal(cli);
    if (pos.first() == "leaderboard")
        return cmdLeaderboard(cli);

    qCritical() << "Unknown command:" << pos.first()
                << "\nRun with --help for usage.";
    return 1;
}
