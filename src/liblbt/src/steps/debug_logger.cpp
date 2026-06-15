#include "steps/debug_logger.h"
#include <sqlite3.h>
#include <sstream>
#include <iostream>

DebugLoggerStep::DebugLoggerStep() {}

// Serialize one contour to a compact JSON array: [[x,y],[x,y],...]
static std::string contourToJson(const std::vector<cv::Point>& pts) {
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < pts.size(); ++i) {
        if (i) ss << ",";
        ss << "[" << pts[i].x << "," << pts[i].y << "]";
    }
    ss << "]";
    return ss.str();
}

FrameMemoryObject DebugLoggerStep::process(const FrameMemoryObject& input, const FilterConfig& config) {
    bool enabled = config.getParameter("enabled", true);
    if (!enabled)
        return input;

    FrameLogEntry entry;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        entry.frame_idx = m_frameCounter++;
    }
    entry.timestamp_ns = input.timestampNs();

    if (input.hasMetadata("filtered_contours")) {
        try {
            entry.contours = input.getMetadata<std::vector<std::vector<cv::Point>>>("filtered_contours");
        } catch (...) {}
    }
    if (input.hasMetadata("filtered_contour_centers")) {
        try {
            entry.centers = input.getMetadata<std::vector<cv::Point2f>>("filtered_contour_centers");
        } catch (...) {}
    }
    if (input.hasMetadata("filtered_contour_rects")) {
        try {
            entry.rects = input.getMetadata<std::vector<cv::RotatedRect>>("filtered_contour_rects");
        } catch (...) {}
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_entries.push_back(std::move(entry));
    }

    return input;
}

size_t DebugLoggerStep::entryCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries.size();
}

bool DebugLoggerStep::flush(const std::string& dbPath) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_entries.empty()) {
        logInfo("[DebugLogger] No entries to flush.");
        return true;
    }

    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        logError("[DebugLogger] Cannot open SQLite db: " + dbPath + " — " + sqlite3_errmsg(db));
        sqlite3_close(db);
        return false;
    }

    const char* schema = R"sql(
        CREATE TABLE IF NOT EXISTS frames (
            frame_idx    INTEGER PRIMARY KEY,
            timestamp_ns INTEGER NOT NULL,
            contour_count INTEGER NOT NULL
        );
        CREATE TABLE IF NOT EXISTS detections (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            frame_idx   INTEGER NOT NULL,
            contour_idx INTEGER NOT NULL,
            center_x    REAL,
            center_y    REAL,
            rect_x      REAL,
            rect_y      REAL,
            rect_w      REAL,
            rect_h      REAL,
            rect_angle  REAL,
            area        REAL,
            polygon_json TEXT,
            FOREIGN KEY(frame_idx) REFERENCES frames(frame_idx)
        );
    )sql";

    char* errMsg = nullptr;
    if (sqlite3_exec(db, schema, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        logError("[DebugLogger] Schema error: " + std::string(errMsg));
        sqlite3_free(errMsg);
        sqlite3_close(db);
        return false;
    }

    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    sqlite3_stmt* frameStmt = nullptr;
    sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO frames(frame_idx, timestamp_ns, contour_count) VALUES(?,?,?);",
        -1, &frameStmt, nullptr);

    sqlite3_stmt* detStmt = nullptr;
    sqlite3_prepare_v2(db,
        "INSERT INTO detections(frame_idx, contour_idx, center_x, center_y, "
        "rect_x, rect_y, rect_w, rect_h, rect_angle, area, polygon_json) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?);",
        -1, &detStmt, nullptr);

    bool ok = true;
    for (const auto& e : m_entries) {
        int nContours = static_cast<int>(e.contours.size());

        sqlite3_reset(frameStmt);
        sqlite3_bind_int(frameStmt, 1, e.frame_idx);
        sqlite3_bind_int64(frameStmt, 2, static_cast<sqlite3_int64>(e.timestamp_ns));
        sqlite3_bind_int(frameStmt, 3, nContours);
        if (sqlite3_step(frameStmt) != SQLITE_DONE) {
            logError("[DebugLogger] Insert frame failed: " + std::string(sqlite3_errmsg(db)));
            ok = false;
        }

        for (int ci = 0; ci < nContours; ++ci) {
            const auto& contour = e.contours[ci];
            double area = cv::contourArea(contour);
            std::string poly = contourToJson(contour);

            float cx = (ci < static_cast<int>(e.centers.size())) ? e.centers[ci].x : 0.f;
            float cy = (ci < static_cast<int>(e.centers.size())) ? e.centers[ci].y : 0.f;

            float rx = 0, ry = 0, rw = 0, rh = 0, ra = 0;
            if (ci < static_cast<int>(e.rects.size())) {
                rx = e.rects[ci].center.x;
                ry = e.rects[ci].center.y;
                rw = e.rects[ci].size.width;
                rh = e.rects[ci].size.height;
                ra = e.rects[ci].angle;
            }

            sqlite3_reset(detStmt);
            sqlite3_bind_int(detStmt, 1, e.frame_idx);
            sqlite3_bind_int(detStmt, 2, ci);
            sqlite3_bind_double(detStmt, 3, cx);
            sqlite3_bind_double(detStmt, 4, cy);
            sqlite3_bind_double(detStmt, 5, rx);
            sqlite3_bind_double(detStmt, 6, ry);
            sqlite3_bind_double(detStmt, 7, rw);
            sqlite3_bind_double(detStmt, 8, rh);
            sqlite3_bind_double(detStmt, 9, ra);
            sqlite3_bind_double(detStmt, 10, area);
            sqlite3_bind_text(detStmt, 11, poly.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(detStmt) != SQLITE_DONE) {
                logError("[DebugLogger] Insert detection failed: " + std::string(sqlite3_errmsg(db)));
                ok = false;
            }
        }
    }

    sqlite3_finalize(frameStmt);
    sqlite3_finalize(detStmt);
    sqlite3_exec(db, ok ? "COMMIT;" : "ROLLBACK;", nullptr, nullptr, nullptr);
    sqlite3_close(db);

    if (ok) {
        logInfo("[DebugLogger] Flushed " + std::to_string(m_entries.size()) +
                " frames to " + dbPath);
        m_entries.clear();
    }
    return ok;
}
