#include "sim_db.h"
#include <sqlite3.h>
#include <stdexcept>
#include <cmath>
#include <iostream>

// ── Helpers ───────────────────────────────────────────────────────────────────

static void checkRc(int rc, const std::string& ctx, sqlite3* db = nullptr) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
        std::string msg = ctx + ": " +
            (db ? sqlite3_errmsg(db) : sqlite3_errstr(rc));
        throw std::runtime_error(msg);
    }
}

// Bind double, treating NaN as NULL.
static void bindDouble(sqlite3_stmt* s, int col, double v) {
    if (std::isnan(v)) sqlite3_bind_null(s, col);
    else               sqlite3_bind_double(s, col, v);
}

// ── Constructor / Destructor ──────────────────────────────────────────────────

SimDB::SimDB(const std::string& path) {
    checkRc(sqlite3_open(path.c_str(), &m_db), "sqlite3_open");
    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA synchronous=NORMAL;");
    createSchema();
}

SimDB::~SimDB() {
    if (m_db) sqlite3_close(m_db);
}

// ── Schema ────────────────────────────────────────────────────────────────────

void SimDB::exec(const std::string& sql) {
    char* err = nullptr;
    int rc = sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = "SQL error: " + std::string(err ? err : "unknown");
        sqlite3_free(err);
        throw std::runtime_error(msg);
    }
}

void SimDB::createSchema() {
    exec(R"(
CREATE TABLE IF NOT EXISTS sim_runs (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    created_at      TEXT    NOT NULL DEFAULT (datetime('now')),
    description     TEXT,
    n_gen_configs   INTEGER,
    n_pipe_configs  INTEGER
);

CREATE TABLE IF NOT EXISTS generation_configs (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    sim_run_id      INTEGER NOT NULL REFERENCES sim_runs(id),
    record_id       TEXT    NOT NULL,
    record_path     TEXT    NOT NULL,
    arena_radius_px REAL,
    config_json     TEXT
);

CREATE TABLE IF NOT EXISTS pipeline_configs (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    sim_run_id      INTEGER NOT NULL REFERENCES sim_runs(id),
    pipeline_id     TEXT    NOT NULL,
    config_json     TEXT
);

CREATE TABLE IF NOT EXISTS sim_results (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    sim_run_id          INTEGER NOT NULL REFERENCES sim_runs(id),
    gen_config_id       INTEGER NOT NULL REFERENCES generation_configs(id),
    pipe_config_id      INTEGER NOT NULL REFERENCES pipeline_configs(id),
    record_id           TEXT    NOT NULL,
    pipeline_id         TEXT    NOT NULL,
    alpha_mean          REAL,
    alpha_p95           REAL,
    beta_mean           REAL,
    gamma_moving        REAL,
    gamma_resting       REAL,
    delta_fpr           REAL,
    eta                 REAL,
    zeta                REAL,
    score               REAL,
    n_animal_frames     INTEGER,
    n_detected_frames   INTEGER,
    n_warmup_frames     INTEGER,
    n_warmup_detections INTEGER,
    processing_time_ms  REAL
);

CREATE TABLE IF NOT EXISTS frame_results (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    sim_result_id       INTEGER NOT NULL REFERENCES sim_results(id),
    record_id           TEXT    NOT NULL,
    pipeline_id         TEXT    NOT NULL,
    frame_idx           INTEGER,
    ts_ns               INTEGER,
    is_warmup           INTEGER,
    is_resting          INTEGER,
    gt_x                REAL,
    gt_y                REAL,
    det_x               REAL,
    det_y               REAL,
    alpha               REAL,
    area_filter_count   INTEGER,
    filtered_count      INTEGER
);

CREATE INDEX IF NOT EXISTS idx_sim_results_run_score
    ON sim_results(sim_run_id, score);
CREATE INDEX IF NOT EXISTS idx_frame_results_sim_result
    ON frame_results(sim_result_id);
)");
}

// ── Inserts ───────────────────────────────────────────────────────────────────

int64_t SimDB::insertSimRun(const std::string& description, int n_gen, int n_pipe) {
    const char* sql =
        "INSERT INTO sim_runs(description, n_gen_configs, n_pipe_configs) "
        "VALUES(?,?,?);";
    sqlite3_stmt* s = nullptr;
    checkRc(sqlite3_prepare_v2(m_db, sql, -1, &s, nullptr), "prepare insertSimRun", m_db);
    sqlite3_bind_text(s, 1, description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(s, 2, n_gen);
    sqlite3_bind_int(s, 3, n_pipe);
    checkRc(sqlite3_step(s), "step insertSimRun", m_db);
    sqlite3_finalize(s);
    return sqlite3_last_insert_rowid(m_db);
}

int64_t SimDB::insertGenerationConfig(int64_t sim_run_id,
                                       const std::string& record_id,
                                       const std::string& config_json,
                                       const std::string& record_path,
                                       double arena_radius_px) {
    const char* sql =
        "INSERT INTO generation_configs"
        "(sim_run_id,record_id,record_path,arena_radius_px,config_json) "
        "VALUES(?,?,?,?,?);";
    sqlite3_stmt* s = nullptr;
    checkRc(sqlite3_prepare_v2(m_db, sql, -1, &s, nullptr), "prepare insertGenConfig", m_db);
    sqlite3_bind_int64(s, 1, sim_run_id);
    sqlite3_bind_text (s, 2, record_id.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (s, 3, record_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(s, 4, arena_radius_px);
    sqlite3_bind_text (s, 5, config_json.c_str(), -1, SQLITE_TRANSIENT);
    checkRc(sqlite3_step(s), "step insertGenConfig", m_db);
    sqlite3_finalize(s);
    return sqlite3_last_insert_rowid(m_db);
}

int64_t SimDB::insertPipelineConfig(int64_t sim_run_id,
                                     const std::string& pipeline_id,
                                     const std::string& config_json) {
    const char* sql =
        "INSERT INTO pipeline_configs(sim_run_id,pipeline_id,config_json) "
        "VALUES(?,?,?);";
    sqlite3_stmt* s = nullptr;
    checkRc(sqlite3_prepare_v2(m_db, sql, -1, &s, nullptr), "prepare insertPipeConfig", m_db);
    sqlite3_bind_int64(s, 1, sim_run_id);
    sqlite3_bind_text (s, 2, pipeline_id.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (s, 3, config_json.c_str(),  -1, SQLITE_TRANSIENT);
    checkRc(sqlite3_step(s), "step insertPipeConfig", m_db);
    sqlite3_finalize(s);
    return sqlite3_last_insert_rowid(m_db);
}

int64_t SimDB::insertSimResult(int64_t sim_run_id,
                                int64_t gen_id, int64_t pipe_id,
                                const std::string& record_id,
                                const std::string& pipeline_id,
                                const Metrics& m) {
    const char* sql =
        "INSERT INTO sim_results("
        " sim_run_id,gen_config_id,pipe_config_id,record_id,pipeline_id,"
        " alpha_mean,alpha_p95,beta_mean,gamma_moving,gamma_resting,"
        " delta_fpr,eta,zeta,score,"
        " n_animal_frames,n_detected_frames,n_warmup_frames,n_warmup_detections,"
        " processing_time_ms)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
    sqlite3_stmt* s = nullptr;
    checkRc(sqlite3_prepare_v2(m_db, sql, -1, &s, nullptr), "prepare insertSimResult", m_db);
    sqlite3_bind_int64 (s,  1, sim_run_id);
    sqlite3_bind_int64 (s,  2, gen_id);
    sqlite3_bind_int64 (s,  3, pipe_id);
    sqlite3_bind_text  (s,  4, record_id.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (s,  5, pipeline_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(s,  6, m.alpha_mean);
    sqlite3_bind_double(s,  7, m.alpha_p95);
    sqlite3_bind_double(s,  8, m.beta_mean);
    sqlite3_bind_double(s,  9, m.gamma_moving);
    sqlite3_bind_double(s, 10, m.gamma_resting);
    sqlite3_bind_double(s, 11, m.delta_fpr);
    sqlite3_bind_double(s, 12, m.eta);
    sqlite3_bind_double(s, 13, m.zeta);
    sqlite3_bind_double(s, 14, m.score);
    sqlite3_bind_int   (s, 15, m.n_animal_frames);
    sqlite3_bind_int   (s, 16, m.n_detected_frames);
    sqlite3_bind_int   (s, 17, m.n_warmup_frames);
    sqlite3_bind_int   (s, 18, m.n_warmup_detections);
    sqlite3_bind_double(s, 19, m.processing_time_ms);
    checkRc(sqlite3_step(s), "step insertSimResult", m_db);
    sqlite3_finalize(s);
    return sqlite3_last_insert_rowid(m_db);
}

void SimDB::insertFrameResults(int64_t sim_result_id,
                                const std::vector<FrameResult>& frames,
                                const std::string& record_id,
                                const std::string& pipeline_id) {
    exec("BEGIN;");
    const char* sql =
        "INSERT INTO frame_results("
        " sim_result_id,record_id,pipeline_id,"
        " frame_idx,ts_ns,is_warmup,is_resting,"
        " gt_x,gt_y,det_x,det_y,alpha,"
        " area_filter_count,filtered_count)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
    sqlite3_stmt* s = nullptr;
    checkRc(sqlite3_prepare_v2(m_db, sql, -1, &s, nullptr), "prepare insertFrameResult", m_db);

    for (const auto& f : frames) {
        sqlite3_reset(s);
        sqlite3_bind_int64(s,  1, sim_result_id);
        sqlite3_bind_text (s,  2, record_id.c_str(),   -1, SQLITE_TRANSIENT);
        sqlite3_bind_text (s,  3, pipeline_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int  (s,  4, f.frame_idx);
        sqlite3_bind_int64(s,  5, f.ts_ns);
        sqlite3_bind_int  (s,  6, f.is_warmup  ? 1 : 0);
        sqlite3_bind_int  (s,  7, f.is_resting ? 1 : 0);
        bindDouble(s,  8, f.gt_x);
        bindDouble(s,  9, f.gt_y);
        bindDouble(s, 10, f.det_x);
        bindDouble(s, 11, f.det_y);
        // alpha: only valid when both gt and det are present
        if (!std::isnan(f.gt_x) && !std::isnan(f.det_x) && f.det_x >= 0.0f) {
            double dx = f.det_x - f.gt_x, dy = f.det_y - f.gt_y;
            sqlite3_bind_double(s, 12, std::sqrt(dx*dx + dy*dy));
        } else {
            sqlite3_bind_null(s, 12);
        }
        sqlite3_bind_int(s, 13, f.area_filter_count);
        sqlite3_bind_int(s, 14, f.filtered_count);
        checkRc(sqlite3_step(s), "step insertFrameResult", m_db);
    }

    sqlite3_finalize(s);
    exec("COMMIT;");
}

// ── Query ─────────────────────────────────────────────────────────────────────

std::vector<LeaderboardRow> SimDB::getLeaderboard(int64_t sim_run_id, int limit) {
    const char* sql =
        "SELECT r.id,r.sim_run_id,s.description,s.created_at,r.pipeline_id,r.record_id,"
        " r.score,r.alpha_mean,r.alpha_p95,r.gamma_moving,r.gamma_resting,"
        " r.delta_fpr,r.eta,r.zeta,r.beta_mean,r.processing_time_ms"
        " FROM sim_results r"
        " JOIN sim_runs s ON s.id = r.sim_run_id"
        " WHERE r.sim_run_id=?"
        " ORDER BY r.score ASC"
        " LIMIT ?;";
    sqlite3_stmt* s = nullptr;
    checkRc(sqlite3_prepare_v2(m_db, sql, -1, &s, nullptr), "prepare leaderboard", m_db);
    sqlite3_bind_int64(s, 1, sim_run_id);
    sqlite3_bind_int  (s, 2, limit);

    std::vector<LeaderboardRow> rows;
    while (sqlite3_step(s) == SQLITE_ROW) {
        LeaderboardRow r;
        r.sim_result_id   = sqlite3_column_int64(s, 0);
        r.sim_run_id      = sqlite3_column_int64(s, 1);
        const unsigned char* desc = sqlite3_column_text(s, 2);
        r.run_description = desc ? (const char*)desc : "";
        const unsigned char* created = sqlite3_column_text(s, 3);
        r.run_created_at  = created ? (const char*)created : "";
        r.pipeline_id         = (const char*)sqlite3_column_text(s, 4);
        r.record_id           = (const char*)sqlite3_column_text(s, 5);
        r.score               = sqlite3_column_double(s,  6);
        r.alpha_mean          = sqlite3_column_double(s,  7);
        r.alpha_p95           = sqlite3_column_double(s,  8);
        r.gamma_moving        = sqlite3_column_double(s,  9);
        r.gamma_resting       = sqlite3_column_double(s, 10);
        r.delta_fpr           = sqlite3_column_double(s, 11);
        r.eta                 = sqlite3_column_double(s, 12);
        r.zeta                = sqlite3_column_double(s, 13);
        r.beta_mean           = sqlite3_column_double(s, 14);
        r.processing_time_ms  = sqlite3_column_double(s, 15);
        rows.push_back(r);
    }
    sqlite3_finalize(s);
    return rows;
}

std::vector<LeaderboardRow> SimDB::getGlobalLeaderboard(int limit) {
    const char* sql =
        "SELECT r.id,r.sim_run_id,s.description,s.created_at,r.pipeline_id,r.record_id,"
        " r.score,r.alpha_mean,r.alpha_p95,r.gamma_moving,r.gamma_resting,"
        " r.delta_fpr,r.eta,r.zeta,r.beta_mean,r.processing_time_ms"
        " FROM sim_results r"
        " JOIN sim_runs s ON s.id = r.sim_run_id"
        " ORDER BY r.score ASC"
        " LIMIT ?;";
    sqlite3_stmt* s = nullptr;
    checkRc(sqlite3_prepare_v2(m_db, sql, -1, &s, nullptr), "prepare globalLeaderboard", m_db);
    sqlite3_bind_int(s, 1, limit);

    std::vector<LeaderboardRow> rows;
    while (sqlite3_step(s) == SQLITE_ROW) {
        LeaderboardRow r;
        r.sim_result_id   = sqlite3_column_int64(s, 0);
        r.sim_run_id      = sqlite3_column_int64(s, 1);
        const unsigned char* desc = sqlite3_column_text(s, 2);
        r.run_description = desc ? (const char*)desc : "";
        const unsigned char* created = sqlite3_column_text(s, 3);
        r.run_created_at  = created ? (const char*)created : "";
        r.pipeline_id        = (const char*)sqlite3_column_text(s, 4);
        r.record_id          = (const char*)sqlite3_column_text(s, 5);
        r.score               = sqlite3_column_double(s,  6);
        r.alpha_mean          = sqlite3_column_double(s,  7);
        r.alpha_p95           = sqlite3_column_double(s,  8);
        r.gamma_moving        = sqlite3_column_double(s,  9);
        r.gamma_resting       = sqlite3_column_double(s, 10);
        r.delta_fpr           = sqlite3_column_double(s, 11);
        r.eta                 = sqlite3_column_double(s, 12);
        r.zeta                = sqlite3_column_double(s, 13);
        r.beta_mean           = sqlite3_column_double(s, 14);
        r.processing_time_ms  = sqlite3_column_double(s, 15);
        rows.push_back(r);
    }
    sqlite3_finalize(s);
    return rows;
}

std::vector<AggregateLeaderboardRow> SimDB::getAggregateLeaderboard(int limit, int64_t sim_run_id) {
    const std::string sql =
        "SELECT pipeline_id,"
        " COUNT(*)                    AS n_results,"
        " COUNT(DISTINCT record_id)   AS n_records,"
        " COUNT(DISTINCT sim_run_id)  AS n_sim_runs,"
        " AVG(score)                  AS avg_score,"
        " AVG(alpha_mean)             AS avg_alpha_mean,"
        " AVG(alpha_p95)              AS avg_alpha_p95,"
        " AVG(gamma_moving)           AS avg_gamma_moving,"
        " AVG(gamma_resting)          AS avg_gamma_resting,"
        " AVG(delta_fpr)              AS avg_delta_fpr,"
        " AVG(eta)                    AS avg_eta,"
        " AVG(zeta)                   AS avg_zeta,"
        " AVG(beta_mean)              AS avg_beta_mean,"
        " AVG(processing_time_ms)     AS avg_processing_time_ms"
        " FROM sim_results"
        + std::string(sim_run_id >= 0 ? " WHERE sim_run_id = ?" : "") +
        " GROUP BY pipeline_id"
        " ORDER BY avg_score ASC"
        " LIMIT ?;";

    sqlite3_stmt* s = nullptr;
    checkRc(sqlite3_prepare_v2(m_db, sql.c_str(), -1, &s, nullptr), "prepare aggregateLeaderboard", m_db);
    int col = 1;
    if (sim_run_id >= 0) sqlite3_bind_int64(s, col++, sim_run_id);
    sqlite3_bind_int(s, col, limit);

    std::vector<AggregateLeaderboardRow> rows;
    while (sqlite3_step(s) == SQLITE_ROW) {
        AggregateLeaderboardRow r;
        r.pipeline_id             = (const char*)sqlite3_column_text(s, 0);
        r.n_results               = sqlite3_column_int64(s, 1);
        r.n_records               = sqlite3_column_int64(s, 2);
        r.n_sim_runs              = sqlite3_column_int64(s, 3);
        r.avg_score               = sqlite3_column_double(s, 4);
        r.avg_alpha_mean          = sqlite3_column_double(s, 5);
        r.avg_alpha_p95           = sqlite3_column_double(s, 6);
        r.avg_gamma_moving        = sqlite3_column_double(s, 7);
        r.avg_gamma_resting       = sqlite3_column_double(s, 8);
        r.avg_delta_fpr           = sqlite3_column_double(s, 9);
        r.avg_eta                 = sqlite3_column_double(s, 10);
        r.avg_zeta                = sqlite3_column_double(s, 11);
        r.avg_beta_mean           = sqlite3_column_double(s, 12);
        r.avg_processing_time_ms  = sqlite3_column_double(s, 13);
        rows.push_back(r);
    }
    sqlite3_finalize(s);
    return rows;
}

std::string SimDB::getPipelineConfigJson(const std::string& pipeline_id) {
    const char* sql =
        "SELECT config_json FROM pipeline_configs"
        " WHERE pipeline_id=? ORDER BY id DESC LIMIT 1;";
    sqlite3_stmt* s = nullptr;
    checkRc(sqlite3_prepare_v2(m_db, sql, -1, &s, nullptr), "prepare getPipelineConfigJson", m_db);
    sqlite3_bind_text(s, 1, pipeline_id.c_str(), -1, SQLITE_TRANSIENT);

    std::string result;
    if (sqlite3_step(s) == SQLITE_ROW) {
        const unsigned char* txt = sqlite3_column_text(s, 0);
        if (txt) result = (const char*)txt;
    }
    sqlite3_finalize(s);
    return result;
}
