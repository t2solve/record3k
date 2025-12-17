#pragma once

#include <QMainWindow>
#include <QListWidget>
#include <QLabel>
#include <QSplitter>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLineEdit>
#include <QToolBar>
#include <QScrollArea>
#include <memory>
#include <liblbt/pipeline_config_loader.h>
#include <liblbt/disk_image_frame_source.h>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    // Optional defaults constructor: auto-load an image directory and/or a pipeline XML if provided
    explicit MainWindow(const QString& imageDir, const QString& pipelineXml, QWidget* parent = nullptr);

private slots:
    void openImageDirectory();
    void loadPipelineXml();
    void savePipelineXml();
    void onStepSelectionChanged();
    void onApplyParameters();
    void nextFrame();

private:
    // UI
    QListWidget* stepList_ {nullptr};
    QLabel* beforeLabel_ {nullptr};
    QLabel* afterLabel_ {nullptr};
    QWidget* paramsWidget_ {nullptr};
    QFormLayout* paramsForm_ {nullptr};
    QPushButton* applyBtn_ {nullptr};
    QPushButton* nextBtn_ {nullptr};
    QLineEdit* dirEdit_ {nullptr};
    QAction* forceCpuAct_ {nullptr};
    bool forceCpu_ {false};

    // Data
    PipelineConfigLoader::PipelineConfig pipe_;
    std::unique_ptr<IFrameSource> source_;
    std::unique_ptr<FrameMemoryObject> currentFrame_;

    // Helpers
    static QImage matToQImage(const cv::Mat& mat);
    void refreshStepList();
    void buildParamsEditor(int index);
    void showBeforeAfter(int index);
protected:
    void resizeEvent(QResizeEvent* event) override;
};
