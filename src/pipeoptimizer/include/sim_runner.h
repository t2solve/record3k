#pragma once
#include "sim_db.h"
#include "metrics.h"
#include <string>
#include <vector>
#include <cstdint>

// Orchestrates a full C-step simulation run:
//   C1  load generation records from synth_root
//   C2  generate pipeline configs via LHS
//   C3  run all (gen × pipe) combinations
//   C4  compute metrics per combination
//   C5  write everything to SQLite
class SimRunner {
public:
    struct Options {
        std::string synth_root;          // dir containing synth-<id>/ sub-dirs (C1 output)
        std::string db_path;             // output .sqlite path
        std::string base_pipeline_path;  // optional: override base pipeline JSON file
        std::string param_ranges_path;   // param_ranges.json — sweep mode
        std::string pipeline_config_path; // single fixed pipeline JSON — no-sweep mode;
                                           // mutually exclusive with param_ranges_path
        std::string description;         // label stored in sim_runs table
        int         n_pipeline_configs = 20;
        uint64_t    seed               = 42;
        bool        save_frame_results = false; // write per-frame rows (can be large)
        double      arena_radius_px    = 400.0; // used for display / arena geometry
        double      miss_penalty_px   = 100.0; // SCORE penalty per missed animal frame
                                                // default arena_radius/4; set to arena_radius
                                                // for strict no-miss preference
        int         n_workers          = 4;     // parallel (gen×pipe) pairs
    };

    explicit SimRunner(const Options& opts);

    // Run the full simulation and print a leaderboard summary.
    // Returns 0 on success, non-zero on error.
    int run();

private:
    Options m_opts;

    struct GenRecord {
        int64_t     db_id = 0;
        std::string record_id;
        std::string frames_dir;      // <content>/frames/
        std::string gt_path;         // <content>/other/ground_truth.json
        std::string config_json;     // synth_config contents (may be empty)
        double      arena_radius_px = 400.0;
    };

    struct PipeConf {
        int64_t     db_id = 0;
        std::string pipeline_id;
        std::string config_json;
        std::string json_file_path;  // written to disk so PipelineConfigLoader can read it
    };

    std::vector<GenRecord> loadGenerationSet(SimDB& db, int64_t sim_run_id);
    std::vector<PipeConf>  buildPipelineSet (SimDB& db, int64_t sim_run_id);

    // Run one (gen, pipe) pair; returns per-frame results + processing time.
    std::vector<FrameResult> runOnePair(const GenRecord& gen,
                                         const PipeConf&  pipe,
                                         double&          out_ms);

    void printLeaderboard(SimDB& db, int64_t sim_run_id) const;
};
