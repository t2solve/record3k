#include "real_sim_db.h"

#include <sqlite3.h>
#include <stdexcept>

namespace {
void checkRc(int rc, const std::string& ctx, sqlite3* db = nullptr) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
        std::string msg = ctx + ": " + (db ? sqlite3_errmsg(db) : sqlite3_errstr(rc));
        throw std::runtime_error(msg);
    }
}
} // namespace

RealSimDB::RealSimDB(const std::string& path) {
    checkRc(sqlite3_open(path.c_str(), &m_db), "sqlite3_open");
    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA synchronous=NORMAL;");
    createSchema();
}

RealSimDB::~RealSimDB() {
    if (m_db) sqlite3_close(m_db);
}

void RealSimDB::exec(const std::string& sql) {
    char* err = nullptr;
    int rc = sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = "SQL error: " + std::string(err ? err : "unknown");
        sqlite3_free(err);
        throw std::runtime_error(msg);
    }
}

void RealSimDB::createSchema() {
    exec(R"(
CREATE TABLE IF NOT EXISTS run_info (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    record_uid     TEXT NOT NULL,
    description    TEXT,
    created_at     TEXT NOT NULL DEFAULT (datetime('now')),
    n_frames       INTEGER,
    n_pipe_configs INTEGER
);

CREATE TABLE IF NOT EXISTS pipeline_configs (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    run_info_id INTEGER NOT NULL REFERENCES run_info(id),
    pipeline_id TEXT NOT NULL,
    config_json TEXT
);

CREATE TABLE IF NOT EXISTS results (
    id                            INTEGER PRIMARY KEY AUTOINCREMENT,
    run_info_id                   INTEGER NOT NULL REFERENCES run_info(id),
    pipeline_config_id            INTEGER NOT NULL REFERENCES pipeline_configs(id),
    pipeline_id                   TEXT NOT NULL,
    eta                           REAL,
    zeta                          REAL,
    mean_area                     REAL,
    area_tolerance_violation_rate REAL,
    n_movement_events             INTEGER,
    n_jumps                       INTEGER,
    n_roi_violations              INTEGER,
    n_speed_violations            INTEGER,
    n_frames_scored               INTEGER,
    n_frames_detected             INTEGER,
    passed                        INTEGER,
    fail_reason                   TEXT,
    processing_time_ms            REAL
);
CREATE INDEX IF NOT EXISTS idx_results_run ON results(run_info_id);
CREATE INDEX IF NOT EXISTS idx_results_passed_score ON results(passed, n_movement_events);
)");
}

int64_t RealSimDB::insertRunInfo(const std::string& record_uid, const std::string& description,
                                  int n_frames, int n_pipe_configs) {
    sqlite3_stmt* s = nullptr;
    checkRc(sqlite3_prepare_v2(m_db,
        "INSERT INTO run_info (record_uid, description, n_frames, n_pipe_configs) VALUES (?,?,?,?);",
        -1, &s, nullptr), "prepare insertRunInfo", m_db);
    sqlite3_bind_text(s, 1, record_uid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 2, description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(s, 3, n_frames);
    sqlite3_bind_int(s, 4, n_pipe_configs);
    checkRc(sqlite3_step(s), "step insertRunInfo", m_db);
    sqlite3_finalize(s);
    return sqlite3_last_insert_rowid(m_db);
}

int64_t RealSimDB::insertPipelineConfig(int64_t run_info_id, const std::string& pipeline_id,
                                         const std::string& config_json) {
    sqlite3_stmt* s = nullptr;
    checkRc(sqlite3_prepare_v2(m_db,
        "INSERT INTO pipeline_configs (run_info_id, pipeline_id, config_json) VALUES (?,?,?);",
        -1, &s, nullptr), "prepare insertPipelineConfig", m_db);
    sqlite3_bind_int64(s, 1, run_info_id);
    sqlite3_bind_text(s, 2, pipeline_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 3, config_json.c_str(), -1, SQLITE_TRANSIENT);
    checkRc(sqlite3_step(s), "step insertPipelineConfig", m_db);
    sqlite3_finalize(s);
    return sqlite3_last_insert_rowid(m_db);
}

int64_t RealSimDB::insertResult(int64_t run_info_id, int64_t pipeline_config_id,
                                 const std::string& pipeline_id, const RealMetrics& m) {
    sqlite3_stmt* s = nullptr;
    checkRc(sqlite3_prepare_v2(m_db,
        "INSERT INTO results (run_info_id, pipeline_config_id, pipeline_id, eta, zeta, mean_area, "
        " area_tolerance_violation_rate, n_movement_events, n_jumps, n_roi_violations, "
        " n_speed_violations, n_frames_scored, n_frames_detected, passed, fail_reason, "
        " processing_time_ms) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);",
        -1, &s, nullptr), "prepare insertResult", m_db);
    sqlite3_bind_int64(s, 1, run_info_id);
    sqlite3_bind_int64(s, 2, pipeline_config_id);
    sqlite3_bind_text(s, 3, pipeline_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(s, 4, m.eta);
    sqlite3_bind_double(s, 5, m.zeta);
    sqlite3_bind_double(s, 6, m.mean_area);
    sqlite3_bind_double(s, 7, m.area_tolerance_violation_rate);
    sqlite3_bind_int(s, 8, m.n_movement_events);
    sqlite3_bind_int(s, 9, m.n_jumps);
    sqlite3_bind_int(s, 10, m.n_roi_violations);
    sqlite3_bind_int(s, 11, m.n_speed_violations);
    sqlite3_bind_int(s, 12, m.n_frames_scored);
    sqlite3_bind_int(s, 13, m.n_frames_detected);
    sqlite3_bind_int(s, 14, m.passed ? 1 : 0);
    sqlite3_bind_text(s, 15, m.fail_reason.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(s, 16, m.processing_time_ms);
    checkRc(sqlite3_step(s), "step insertResult", m_db);
    sqlite3_finalize(s);
    return sqlite3_last_insert_rowid(m_db);
}

std::vector<RealSimDB::ResultRow> RealSimDB::getResults(int64_t run_info_id, int limit) {
    const char* sql = run_info_id >= 0
        ? "SELECT pipeline_id, eta, zeta, mean_area, area_tolerance_violation_rate, n_movement_events, "
          " n_jumps, n_roi_violations, n_speed_violations, n_frames_scored, n_frames_detected, passed, "
          " fail_reason, processing_time_ms "
          "FROM results WHERE run_info_id=? ORDER BY passed DESC, n_movement_events DESC LIMIT ?;"
        : "SELECT pipeline_id, eta, zeta, mean_area, area_tolerance_violation_rate, n_movement_events, "
          " n_jumps, n_roi_violations, n_speed_violations, n_frames_scored, n_frames_detected, passed, "
          " fail_reason, processing_time_ms "
          "FROM results ORDER BY passed DESC, n_movement_events DESC LIMIT ?;";
    sqlite3_stmt* s = nullptr;
    checkRc(sqlite3_prepare_v2(m_db, sql, -1, &s, nullptr), "prepare getResults", m_db);
    int bindIdx = 1;
    if (run_info_id >= 0) sqlite3_bind_int64(s, bindIdx++, run_info_id);
    sqlite3_bind_int(s, bindIdx, limit);

    std::vector<ResultRow> rows;
    while (sqlite3_step(s) == SQLITE_ROW) {
        ResultRow r;
        r.pipeline_id = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
        r.metrics.eta = sqlite3_column_double(s, 1);
        r.metrics.zeta = sqlite3_column_double(s, 2);
        r.metrics.mean_area = sqlite3_column_double(s, 3);
        r.metrics.area_tolerance_violation_rate = sqlite3_column_double(s, 4);
        r.metrics.n_movement_events = sqlite3_column_int(s, 5);
        r.metrics.n_jumps = sqlite3_column_int(s, 6);
        r.metrics.n_roi_violations = sqlite3_column_int(s, 7);
        r.metrics.n_speed_violations = sqlite3_column_int(s, 8);
        r.metrics.n_frames_scored = sqlite3_column_int(s, 9);
        r.metrics.n_frames_detected = sqlite3_column_int(s, 10);
        r.metrics.passed = sqlite3_column_int(s, 11) != 0;
        const unsigned char* reason = sqlite3_column_text(s, 12);
        r.metrics.fail_reason = reason ? reinterpret_cast<const char*>(reason) : "";
        r.metrics.processing_time_ms = sqlite3_column_double(s, 13);
        rows.push_back(r);
    }
    sqlite3_finalize(s);
    return rows;
}

std::string RealSimDB::getPipelineConfigJson(const std::string& pipeline_id) {
    sqlite3_stmt* s = nullptr;
    checkRc(sqlite3_prepare_v2(m_db,
        "SELECT config_json FROM pipeline_configs WHERE pipeline_id=? ORDER BY id DESC LIMIT 1;",
        -1, &s, nullptr), "prepare getPipelineConfigJson", m_db);
    sqlite3_bind_text(s, 1, pipeline_id.c_str(), -1, SQLITE_TRANSIENT);
    std::string result;
    if (sqlite3_step(s) == SQLITE_ROW) {
        const unsigned char* txt = sqlite3_column_text(s, 0);
        if (txt) result = reinterpret_cast<const char*>(txt);
    }
    sqlite3_finalize(s);
    return result;
}
