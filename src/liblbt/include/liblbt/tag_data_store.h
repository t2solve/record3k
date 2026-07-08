#pragma once

#include "tag_data.h"
#include <string>

struct sqlite3;

namespace lbt {

// SQLite-backed store for per-record tagging data (ROI, valid-range cutoff,
// complete-failure flag, tagged trajectory points), keyed by recordUID. One row per
// record in `tagged_records`, one row per point in `tagged_points`. Default path is
// data/simulation/supervised_records_data.sqlite3 (overridable, see pipelineviewer's
// main.cpp --tags-db, and pipeoptimizer's --tags-db). Mirrors SimDB's style
// (src/pipeoptimizer/include/sim_db.h): raw sqlite3 C API, single open connection for
// the object's lifetime, WAL mode.
// Thread-unsafe -- use from a single thread, or guard with an external mutex.
class TagDataStore {
public:
    explicit TagDataStore(const std::string& path);
    ~TagDataStore();

    // Returns true and fills `out` if a row exists for recordUID; false (out left
    // untouched) if no existing data for this record.
    bool load(const std::string& recordUID, TagData& out);

    // Full replace: deletes any existing row(s) for recordUID and re-inserts current
    // state, inside one transaction.
    void save(const std::string& recordUID, const TagData& data);

private:
    void createSchema();
    void exec(const std::string& sql);

    sqlite3* m_db {nullptr};
};

} // namespace lbt
