#pragma once

#include <QDialog>

/**
 * LandingDialog - startup mode picker for pipelineviewer
 *
 * Mode A (DebugPipeline): the existing MainWindow pipeline-debug viewer.
 * Mode B (TagRecord): the new ground-truth tagging tool (TaggingWindow).
 */
class LandingDialog : public QDialog {
    Q_OBJECT
public:
    enum class Mode {
        DebugPipeline,
        TagRecord
    };

    explicit LandingDialog(QWidget* parent = nullptr);

    Mode selectedMode() const { return mode_; }

private:
    Mode mode_ {Mode::DebugPipeline};
};
