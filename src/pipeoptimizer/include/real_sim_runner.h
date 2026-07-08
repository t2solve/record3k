#pragma once
#include "real_metrics.h"
#include <cstdint>
#include <string>

// Runs a pipeline-config sweep (or a single fixed pipeline) against ONE real record's
// frames, decoded once into RAM (liblbt's BufferedFrameSource) and replayed once per
// config -- src/pipeoptimizer/plan.md step 2. Writes one row per config to a per-record
// sqlite file (RealSimDB). Optionally consults lbt::TagDataStore for the record's ROI/
// valid-range/complete-failure state.
class RealSimRunner {
public:
    struct Options {
        // Exactly one of these two must be set.
        std::string record_archive_path; // a .tar.gz -- extracted to a temp dir, cleaned up after
        std::string record_dir_path;     // an already-unpacked record dir, used as-is, never deleted

        std::string base_pipeline_path;   // optional; falls back to PipelineConfigGen::defaultBasePipelineJson()
        std::string param_ranges_path;    // sweep mode
        std::string pipeline_config_path; // single fixed pipeline, no sweep -- mutually exclusive with param_ranges_path
        int n_pipeline_configs = 20;
        uint64_t seed = 42;
        std::string description;

        std::string db_path;      // default: run_test_param_<record_uid>.sqlite3 next to the record dir
        std::string tags_db_path; // default: data/simulation/supervised_records_data.sqlite3
        int n_workers = 4;

        RealMetricsConfig metrics_config;
    };

    explicit RealSimRunner(Options opts);

    // Returns a process exit code (0 = success).
    int run();

private:
    Options m_opts;

    std::string resolveRecordDir(bool& didExtract, std::string& extractedTempDir) const;
    std::string deriveRecordUID(const std::string& recordDir) const;
};
