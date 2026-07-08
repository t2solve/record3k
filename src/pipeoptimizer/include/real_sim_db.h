#pragma once
#include "real_metrics.h"
#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;

// SQLite-backed store for one real-record pipeline sweep. Deliberately one file PER
// RECORD (src/pipeoptimizer/plan.md step 2), not one shared multi-run DB like SimDB --
// avoids concurrent-writer lock contention when many records are swept in parallel
// (one podman container per record). Mirrors SimDB's boilerplate style (raw sqlite3.h,
// checkRc-style error throwing, prepared statements, WAL mode), new schema (this one
// has no ground truth, so none of SimDB's columns apply).
// Thread-unsafe -- callers running a multi-threaded sweep must guard access with their
// own mutex (see SimRunner::run()'s db_mutex for the established pattern).
class RealSimDB {
public:
    explicit RealSimDB(const std::string& path);
    ~RealSimDB();

    // Returns the new run_info row id.
    int64_t insertRunInfo(const std::string& record_uid, const std::string& description,
                           int n_frames, int n_pipe_configs);

    int64_t insertPipelineConfig(int64_t run_info_id, const std::string& pipeline_id,
                                  const std::string& config_json);

    int64_t insertResult(int64_t run_info_id, int64_t pipeline_config_id,
                          const std::string& pipeline_id, const RealMetrics& m);

    struct ResultRow {
        std::string pipeline_id;
        RealMetrics metrics;
    };
    // run_info_id < 0 (default) returns every result ever stored in this file.
    std::vector<ResultRow> getResults(int64_t run_info_id = -1, int limit = 100);

    std::string getPipelineConfigJson(const std::string& pipeline_id);

private:
    sqlite3* m_db = nullptr;
    void createSchema();
    void exec(const std::string& sql);
};
