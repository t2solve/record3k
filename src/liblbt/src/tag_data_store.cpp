#include <liblbt/tag_data_store.h>

#include <sqlite3.h>
#include <stdexcept>
#include <filesystem>

namespace lbt {

namespace {
void checkRc(int rc, const std::string& ctx, sqlite3* db = nullptr) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
        std::string msg = ctx + ": " + (db ? sqlite3_errmsg(db) : sqlite3_errstr(rc));
        throw std::runtime_error(msg);
    }
}
} // namespace

TagDataStore::TagDataStore(const std::string& path) {
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    checkRc(sqlite3_open(path.c_str(), &m_db), "sqlite3_open");
    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA synchronous=NORMAL;");
    createSchema();
}

TagDataStore::~TagDataStore() {
    if (m_db) sqlite3_close(m_db);
}

void TagDataStore::exec(const std::string& sql) {
    char* err = nullptr;
    int rc = sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = "SQL error: " + std::string(err ? err : "unknown");
        sqlite3_free(err);
        throw std::runtime_error(msg);
    }
}

void TagDataStore::createSchema() {
    exec(R"(
CREATE TABLE IF NOT EXISTS tagged_records (
    record_uid        TEXT PRIMARY KEY,
    roi_defined       INTEGER NOT NULL DEFAULT 0,
    roi_center_x      REAL,
    roi_center_y      REAL,
    roi_radius        REAL,
    valid_until_frame INTEGER NOT NULL DEFAULT -1,
    record_failed     INTEGER NOT NULL DEFAULT 0,
    updated_at        TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS tagged_points (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    record_uid  TEXT NOT NULL REFERENCES tagged_records(record_uid),
    frame_index INTEGER NOT NULL,
    x           REAL NOT NULL,
    y           REAL NOT NULL,
    source      TEXT NOT NULL,
    UNIQUE(record_uid, frame_index)
);
CREATE INDEX IF NOT EXISTS idx_tagged_points_record ON tagged_points(record_uid);
)");
}

bool TagDataStore::load(const std::string& recordUID, TagData& out) {
    sqlite3_stmt* s = nullptr;
    checkRc(sqlite3_prepare_v2(m_db,
        "SELECT roi_defined, roi_center_x, roi_center_y, roi_radius, valid_until_frame, record_failed "
        "FROM tagged_records WHERE record_uid=?;",
        -1, &s, nullptr), "prepare load record", m_db);
    sqlite3_bind_text(s, 1, recordUID.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(s);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(s);
        return false;
    }

    TagData data;
    data.roiDefined = sqlite3_column_int(s, 0) != 0;
    data.roiCenterX = sqlite3_column_double(s, 1);
    data.roiCenterY = sqlite3_column_double(s, 2);
    data.roiRadius = sqlite3_column_double(s, 3);
    data.validUntilFrame = sqlite3_column_int(s, 4);
    data.recordFailed = sqlite3_column_int(s, 5) != 0;
    sqlite3_finalize(s);

    sqlite3_stmt* ps = nullptr;
    checkRc(sqlite3_prepare_v2(m_db,
        "SELECT frame_index, x, y, source FROM tagged_points WHERE record_uid=? ORDER BY frame_index;",
        -1, &ps, nullptr), "prepare load points", m_db);
    sqlite3_bind_text(ps, 1, recordUID.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(ps) == SQLITE_ROW) {
        TaggedPoint tp;
        tp.frameIndex = sqlite3_column_int(ps, 0);
        tp.x = sqlite3_column_double(ps, 1);
        tp.y = sqlite3_column_double(ps, 2);
        const unsigned char* src = sqlite3_column_text(ps, 3);
        tp.source = QString::fromUtf8(src ? reinterpret_cast<const char*>(src) : "");
        data.points.push_back(tp);
    }
    sqlite3_finalize(ps);

    out = data;
    return true;
}

void TagDataStore::save(const std::string& recordUID, const TagData& data) {
    exec("BEGIN TRANSACTION;");
    try {
        sqlite3_stmt* s = nullptr;
        checkRc(sqlite3_prepare_v2(m_db,
            "INSERT INTO tagged_records "
            "(record_uid, roi_defined, roi_center_x, roi_center_y, roi_radius, valid_until_frame, "
            " record_failed, updated_at) "
            "VALUES (?,?,?,?,?,?,?,datetime('now')) "
            "ON CONFLICT(record_uid) DO UPDATE SET "
            "  roi_defined=excluded.roi_defined, roi_center_x=excluded.roi_center_x, "
            "  roi_center_y=excluded.roi_center_y, roi_radius=excluded.roi_radius, "
            "  valid_until_frame=excluded.valid_until_frame, record_failed=excluded.record_failed, "
            "  updated_at=excluded.updated_at;",
            -1, &s, nullptr), "prepare save record", m_db);
        sqlite3_bind_text(s, 1, recordUID.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(s, 2, data.roiDefined ? 1 : 0);
        sqlite3_bind_double(s, 3, data.roiCenterX);
        sqlite3_bind_double(s, 4, data.roiCenterY);
        sqlite3_bind_double(s, 5, data.roiRadius);
        sqlite3_bind_int(s, 6, data.validUntilFrame);
        sqlite3_bind_int(s, 7, data.recordFailed ? 1 : 0);
        checkRc(sqlite3_step(s), "step save record", m_db);
        sqlite3_finalize(s);

        sqlite3_stmt* del = nullptr;
        checkRc(sqlite3_prepare_v2(m_db, "DELETE FROM tagged_points WHERE record_uid=?;", -1, &del, nullptr),
                "prepare delete points", m_db);
        sqlite3_bind_text(del, 1, recordUID.c_str(), -1, SQLITE_TRANSIENT);
        checkRc(sqlite3_step(del), "step delete points", m_db);
        sqlite3_finalize(del);

        sqlite3_stmt* ins = nullptr;
        checkRc(sqlite3_prepare_v2(m_db,
            "INSERT INTO tagged_points (record_uid, frame_index, x, y, source) VALUES (?,?,?,?,?);",
            -1, &ins, nullptr), "prepare insert point", m_db);
        for (const TaggedPoint& tp : data.points) {
            sqlite3_reset(ins);
            sqlite3_bind_text(ins, 1, recordUID.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(ins, 2, tp.frameIndex);
            sqlite3_bind_double(ins, 3, tp.x);
            sqlite3_bind_double(ins, 4, tp.y);
            std::string src = tp.source.toStdString();
            sqlite3_bind_text(ins, 5, src.c_str(), -1, SQLITE_TRANSIENT);
            checkRc(sqlite3_step(ins), "step insert point", m_db);
        }
        sqlite3_finalize(ins);

        exec("COMMIT;");
    } catch (...) {
        exec("ROLLBACK;");
        throw;
    }
}

} // namespace lbt
