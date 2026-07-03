#pragma once
#include "metrics.h"
#include <string>
#include <vector>
#include <cstdint>

struct sqlite3;

// One row in the leaderboard query.
struct LeaderboardRow {
    int64_t     sim_result_id;
    int64_t     sim_run_id = 0;       // set only by getGlobalLeaderboard()
    std::string run_description;      // set only by getGlobalLeaderboard()
    std::string run_created_at;       // set only by getGlobalLeaderboard()
    std::string pipeline_id;
    std::string record_id;
    double      score;
    double      alpha_mean;
    double      alpha_p95;
    double      gamma_moving;
    double      gamma_resting;
    double      delta_fpr;
    double      eta;
    double      zeta;
    double      beta_mean;
    double      processing_time_ms;
};

// One row in the aggregate leaderboard: one pipeline_id, averaged over every
// record (and every sim_run, if unscoped) it was ever tested against.
struct AggregateLeaderboardRow {
    std::string pipeline_id;
    int64_t     n_results   = 0;  // total (record, sim_run) evaluations averaged in
    int64_t     n_records   = 0;  // distinct records this pipeline was tested against
    int64_t     n_sim_runs  = 0;  // distinct sim_runs this pipeline appeared in
    double      avg_score;
    double      avg_alpha_mean;
    double      avg_alpha_p95;
    double      avg_gamma_moving;
    double      avg_gamma_resting;
    double      avg_delta_fpr;
    double      avg_eta;
    double      avg_zeta;
    double      avg_beta_mean;
    double      avg_processing_time_ms;
};

// SQLite-backed store for simulation runs (C5).
// Thread-unsafe — use from a single thread.
class SimDB {
public:
    explicit SimDB(const std::string& path);
    ~SimDB();

    // ── Insert helpers (return new rowid) ────────────────────────────────
    int64_t insertSimRun(const std::string& description, int n_gen, int n_pipe);

    int64_t insertGenerationConfig(int64_t sim_run_id,
                                    const std::string& record_id,
                                    const std::string& config_json,
                                    const std::string& record_path,
                                    double arena_radius_px);

    int64_t insertPipelineConfig(int64_t sim_run_id,
                                  const std::string& pipeline_id,
                                  const std::string& config_json);

    int64_t insertSimResult(int64_t sim_run_id,
                             int64_t gen_id,
                             int64_t pipe_id,
                             const std::string& record_id,
                             const std::string& pipeline_id,
                             const Metrics& m);

    // Batch-inserts per-frame rows inside a single transaction.
    void insertFrameResults(int64_t sim_result_id,
                             const std::vector<FrameResult>& frames,
                             const std::string& record_id,
                             const std::string& pipeline_id);

    // ── Query ────────────────────────────────────────────────────────────
    // Returns top-N results for a run, ranked by score ascending.
    std::vector<LeaderboardRow> getLeaderboard(int64_t sim_run_id, int limit = 30);

    // Returns top-N results across ALL simulation runs ever stored in this DB,
    // ranked by score ascending. Lets you compare pipeline/generation combos from
    // separate sweeps (different param_ranges.json, different --pipeline-config
    // runs, etc.) on one global leaderboard.
    std::vector<LeaderboardRow> getGlobalLeaderboard(int limit = 30);

    // Groups sim_results by pipeline_id and averages every metric across all
    // records it was tested against, ranked by avg_score ascending — this is
    // "which configuration is best on average", as opposed to getLeaderboard()/
    // getGlobalLeaderboard() which return raw per-(pipeline,record) rows.
    //
    // pipeline_id is only a stable identity across sim_runs in fixed-pipeline mode
    // (--pipeline-config, where the id comes from the JSON's own pipelineUID);
    // LHS-sweep pipeline_ids are freshly generated per sweep, so they naturally
    // won't merge across runs — n_sim_runs tells you which case you're looking at.
    //
    // sim_run_id < 0 (default) aggregates across the whole DB; pass a specific
    // sim_run_id to scope the aggregate to one sweep/run.
    std::vector<AggregateLeaderboardRow> getAggregateLeaderboard(int limit = 30,
                                                                  int64_t sim_run_id = -1);

private:
    sqlite3* m_db = nullptr;
    void createSchema();
    void exec(const std::string& sql);
};
