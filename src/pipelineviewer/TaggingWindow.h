#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QSlider>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QAction>
#include <QTimer>
#include <QResizeEvent>
#include <memory>
#include <liblbt/disk_image_frame_source.h>
#include <liblbt/frame_memory_object.h>
#include <opencv2/tracking.hpp>

#include "ClickableImageLabel.h"
#include <liblbt/tag_data.h>
#include <liblbt/tag_data_store.h>

/**
 * TaggingWindow - Mode B ("Tag Record") of pipelineviewer.
 *
 * Opens a record .tar.gz archive, extracts it, loads its frames via
 * DiskImageFrameSource, and displays them with prev/next/slider navigation. On top of
 * that: defining a round ROI arena, and two tagging modes, switchable at any time:
 *   - "Tag Animal" (automatic): tracker-assisted click-to-tag ground truth (CSRT
 *     tracker auto-propagates a manual click forward frame-to-frame, stopping and
 *     flagging for a human re-click on loss or on leaving the ROI).
 *   - "Manual Tag": every click places a point on the current frame only (no
 *     tracker involved) and auto-advances to the next frame, for individuals that
 *     move too fast for CSRT to follow reliably.
 * See src/pipeoptimizer/plan.md, step 1.
 */
class TaggingWindow : public QMainWindow {
    Q_OBJECT
public:
    static constexpr const char* kDefaultTagsDbPath = "data/simulation/supervised_records_data.sqlite3";

    // dbPath: path to the sqlite tag-data store; empty string means kDefaultTagsDbPath.
    explicit TaggingWindow(const QString& dbPath = QString(), QWidget* parent = nullptr);

private slots:
    void openRecordArchive();
    void onFrameSliderChanged(int value);
    void onPrevFrame();
    void onNextFrame();
    void onDefineRoiToggled(bool checked);
    void onMoveRoiToggled(bool checked);
    void onResizeRoiToggled(bool checked);
    void onTagAnimalToggled(bool checked);
    void onManualTagToggled(bool checked);
    void onStopTracking();
    void onMarkInvalidFromHere();
    void onMarkRecordFailedToggled(bool checked);
    void onSaveTags();

    void onImagePressed(QPoint pos);
    void onImageMoved(QPoint pos);
    void onImageReleased(QPoint pos);

    void onTrackingTick();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    enum class InteractionMode { None, DefiningROI, MovingROI, ResizingROI, Tagging, ManualTagging };

    void loadRecordDirectory(const QString& recordDir);
    void showFrame(int index);
    void renderCurrentFrame(int index);
    QString ensureWorkDir();
    void seedTrackerAt(int frameIndex, QPoint clickPos);
    void tagCurrentFrameManually(int frameIndex, QPoint clickPos);
    void stopTracking(const QString& reason);
    void setInteractionMode(InteractionMode mode);
    bool isInsideRoi(QPointF pt) const;
    void uncheckOtherRoiTools(QAction* except);
    void updateRoiToolAvailability();
    QPoint toImageCoords(QPoint labelPos) const;

    // UI
    ClickableImageLabel* imageLabel_ {nullptr};
    QScrollArea* imageScroll_ {nullptr};
    QSlider* frameSlider_ {nullptr};
    QLabel* frameLabel_ {nullptr};
    QPushButton* prevBtn_ {nullptr};
    QPushButton* nextBtn_ {nullptr};
    QLabel* statusLabel_ {nullptr};
    QAction* defineRoiAct_ {nullptr};
    QAction* moveRoiAct_ {nullptr};
    QAction* resizeRoiAct_ {nullptr};
    QAction* tagAnimalAct_ {nullptr};
    QAction* manualTagAct_ {nullptr};
    QAction* stopTrackingAct_ {nullptr};
    QAction* markInvalidAct_ {nullptr};
    QAction* markRecordFailedAct_ {nullptr};
    QAction* saveTagsAct_ {nullptr};

    // Data
    std::unique_ptr<DiskImageFrameSource> source_;
    std::unique_ptr<FrameMemoryObject> currentFrame_;
    QSettings settings_;
    QString currentRecordDir_;
    QString currentRecordUID_;
    std::unique_ptr<lbt::TagDataStore> tagStore_;

    // Displayed pixmap size / native frame size, e.g. 0.5 if shown at half resolution
    // to fit the viewport. All ROI/tag data is stored in native image pixel space;
    // mouse positions (label-relative, i.e. displayed-pixel space) must be divided by
    // this before use. Recomputed on every renderCurrentFrame() call.
    double displayScale_ {1.0};

    // ROI definition/move/resize (all use a live pending-preview, committed on release)
    InteractionMode mode_ {InteractionMode::None};
    bool roiDragActive_ {false};    // Define ROI: drawing a brand new circle
    bool roiMoveActive_ {false};    // Move ROI: dragging the existing circle
    bool roiResizeActive_ {false};  // Resize ROI: dragging the existing circle's radius
    QPoint roiDragStart_;
    QPointF roiMoveDragStartCenter_;
    QPointF pendingRoiCenter_;
    double pendingRoiRadius_ {0.0};

    // Tagging / tracking
    lbt::TagData tags_;
    cv::Ptr<cv::TrackerCSRT> tracker_;
    cv::Rect trackerBox_;
    QTimer* trackingTimer_ {nullptr};
    bool trackingActive_ {false};
    int consecutiveRoiViolations_ {0};
    static constexpr int kTagBoxSize = 32;
    static constexpr int kTrackingTickMs = 40;
    static constexpr int kRoiViolationTolerance = 5; // consecutive out-of-ROI frames before stopping
    static constexpr double kJumpThresholdPx = 32.0; // trail segment longer than this is highlighted as a jump
};
