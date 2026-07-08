#pragma once

#include <QString>
#include <vector>

namespace lbt {

// A single ground-truth point for one frame, either placed by hand or produced by the
// tracker while auto-propagating from the last manual click.
struct TaggedPoint {
    int frameIndex = -1;
    double x = 0.0;
    double y = 0.0;
    QString source; // "manual" | "tracked"
};

// Per-record tagging state: the round ROI arena, the valid-range cutoff, whether the
// whole record was rejected outright, and the sparse-but-real trajectory built up via
// tracker-assisted click-to-tag. Persisted via TagDataStore (sqlite), not directly --
// this struct is plain in-memory data.
// See src/pipeoptimizer/plan.md, step 1.
struct TagData {
    bool roiDefined = false;
    double roiCenterX = 0.0;
    double roiCenterY = 0.0;
    double roiRadius = 0.0;

    int validUntilFrame = -1; // -1 = no cutoff set (whole record considered usable)
    bool recordFailed = false; // whole record rejected (e.g. animal escaped/died early,
                                // camera fault) -- distinct from a partial validUntilFrame
                                // cutoff, means "don't use this record at all"

    std::vector<TaggedPoint> points;

    // Replaces any existing point for this frame (a reseed/re-click always overwrites
    // rather than appending a duplicate for the same frame).
    void setPoint(int frameIndex, double x, double y, const QString& source) {
        for (TaggedPoint& tp : points) {
            if (tp.frameIndex == frameIndex) {
                tp.x = x;
                tp.y = y;
                tp.source = source;
                return;
            }
        }
        points.push_back({frameIndex, x, y, source});
    }

    const TaggedPoint* pointAt(int frameIndex) const {
        for (const TaggedPoint& tp : points) {
            if (tp.frameIndex == frameIndex) return &tp;
        }
        return nullptr;
    }
};

} // namespace lbt
