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
#include <QProgressDialog>
#include <QSettings>
#include <memory>
#include <liblbt/pipeline_config_loader.h>
#include <liblbt/disk_image_frame_source.h>
#include "ApiClient.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void openImageDirectory();
    void loadPipelineXml();
    void savePipelineXml();
    void savePipelineJson();
    void uploadPipeline();
    void onStepSelectionChanged();
    void onApplyParameters();
    void nextFrame();
    void showConfiguration();
    void showRecordsBrowser();
    void onRecordSelected();
    void onDownloadRecord();
    void testApiConnection();
    void onRemoveStep();
    void onAddStep();
    void onFrameSliderChanged(int value);

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
    QSlider* frameSlider_ {nullptr};
    QLabel* frameLabel_ {nullptr};

    // Records browser UI
    QDialog* recordsDialog_ {nullptr};
    QListWidget* recordsList_ {nullptr};
    QLabel* recordDetailsLabel_ {nullptr};
    QPushButton* downloadBtn_ {nullptr};

    // Data
    PipelineConfigLoader::PipelineConfig pipe_;
    std::unique_ptr<IFrameSource> source_;
    std::unique_ptr<FrameMemoryObject> currentFrame_;
    std::unique_ptr<ApiClient> apiClient_;
    QSettings settings_;
    QString workDir_;

    // Helpers
    static QImage matToQImage(const cv::Mat& mat);
    void refreshStepList();
    void buildParamsEditor(int index);
    void showBeforeAfter(int index);
    void loadConfiguration();
    void ensureWorkDir();
    void loadRecordData(const QString& recordUID, const QString& extractPath);
    
protected:
    void resizeEvent(QResizeEvent* event) override;
};
