#include "MainWindow.h"
#include <QFileDialog>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QPixmap>
#include <QImage>
#include <QDir>
#include <QFileInfo>
#include <opencv2/imgproc.hpp>
#include <liblbt/processing_pipeline.h>
#include <liblbt/process_step_factory.h>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    // Toolbar
    auto* tb = addToolBar("Main");
    auto* openDirAct = tb->addAction("Open Images");
    auto* loadXmlAct = tb->addAction("Load XML");
    auto* saveXmlAct = tb->addAction("Save XML");
    forceCpuAct_ = tb->addAction("Force CPU");
    forceCpuAct_->setCheckable(true);
    nextBtn_ = new QPushButton("Next Frame");
    tb->addWidget(nextBtn_);

    connect(openDirAct, &QAction::triggered, this, &MainWindow::openImageDirectory);
    connect(loadXmlAct, &QAction::triggered, this, &MainWindow::loadPipelineXml);
    connect(saveXmlAct, &QAction::triggered, this, &MainWindow::savePipelineXml);
    connect(forceCpuAct_, &QAction::toggled, [this](bool on){ this->forceCpu_ = on; });
    connect(nextBtn_, &QPushButton::clicked, this, &MainWindow::nextFrame);

    // Left: steps list
    stepList_ = new QListWidget;
    connect(stepList_, &QListWidget::currentRowChanged, this, &MainWindow::onStepSelectionChanged);

    // Center: images before/after
    beforeLabel_ = new QLabel; beforeLabel_->setAlignment(Qt::AlignCenter);
    afterLabel_ = new QLabel; afterLabel_->setAlignment(Qt::AlignCenter);

    auto* beforeScroll = new QScrollArea; beforeScroll->setWidgetResizable(true); beforeScroll->setWidget(beforeLabel_);
    auto* afterScroll = new QScrollArea; afterScroll->setWidgetResizable(true); afterScroll->setWidget(afterLabel_);

    auto* imagesSplit = new QSplitter(Qt::Horizontal);
    imagesSplit->addWidget(beforeScroll);
    imagesSplit->addWidget(afterScroll);

    // Right: params editor
    paramsWidget_ = new QWidget;
    paramsForm_ = new QFormLayout(paramsWidget_);
    applyBtn_ = new QPushButton("Apply");
    // Keep the Apply button as a persistent last row; parameter rows are inserted above it
    paramsForm_->addRow(applyBtn_);
    connect(applyBtn_, &QPushButton::clicked, this, &MainWindow::onApplyParameters);

    // Main splitter
    auto* split = new QSplitter;
    split->addWidget(stepList_);
    split->addWidget(imagesSplit);
    split->addWidget(paramsWidget_);
    setCentralWidget(split);

    resize(1200, 700);
}

// Delegating constructor that auto-loads defaults if provided
MainWindow::MainWindow(const QString& imageDir, const QString& pipelineXml, QWidget* parent)
    : MainWindow(parent) {
    // Load pipeline XML first (so step list populates before first frame)
    if (!pipelineXml.isEmpty()) {
        try {
            pipe_ = PipelineConfigLoader::loadFromXML(pipelineXml.toStdString());
            // Keep steps/configs in sync
            if (pipe_.steps.size() != pipe_.configs.size()) {
                size_t m = std::min(pipe_.steps.size(), pipe_.configs.size());
                pipe_.steps.resize(m);
                pipe_.configs.resize(m);
            }
            refreshStepList();
            if (stepList_->count() > 0) stepList_->setCurrentRow(0);
        } catch (const std::exception& e) {
            QMessageBox::warning(this, "Pipeline load", QString("Failed to load '%1': %2").arg(pipelineXml, e.what()));
        }
    }

    // Load image directory if it exists
    if (!imageDir.isEmpty()) {
        QDir dir(imageDir);
        if (dir.exists()) {
            source_ = std::make_unique<DiskImageFrameSource>(imageDir.toStdString(), "*.jpg");
            nextFrame();
        } else {
            QMessageBox::warning(this, "Image directory", QString("Directory does not exist: %1").arg(imageDir));
        }
    }
}

void MainWindow::openImageDirectory() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select image directory");
    if (dir.isEmpty()) return;
    source_ = std::make_unique<DiskImageFrameSource>(dir.toStdString(), "*.jpg");
    nextFrame();
}

void MainWindow::loadPipelineXml() {
    QString file = QFileDialog::getOpenFileName(this, "Load pipeline XML", QString(), "XML (*.xml)");
    if (file.isEmpty()) return;
    try {
        pipe_ = PipelineConfigLoader::loadFromXML(file.toStdString());
        // If user requested forcing CPU implementations, recreate steps as CPU-only
        if (forceCpu_) {
            // helper to map step names to FilterType
            std::vector<std::shared_ptr<ProcessStep>> newSteps;
            for (const auto& step : pipe_.steps) {
                FilterType ft = ProcessStepFactory::nameToFilterType(step->getName());
                if (ft == FilterType::NONE) {
                    // keep original if unknown
                    newSteps.push_back(step);
                } else {
                    newSteps.push_back(ProcessStepFactory::createStep(ft, ProcessingMode::CPU_ONLY));
                }
            }
            pipe_.steps = std::move(newSteps);
        }
        // keep steps/configs in sync
        if (pipe_.steps.size() != pipe_.configs.size()) {
            size_t m = std::min(pipe_.steps.size(), pipe_.configs.size());
            pipe_.steps.resize(m);
            pipe_.configs.resize(m);
        }
        refreshStepList();
        if (stepList_->count() > 0) stepList_->setCurrentRow(0);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Error", QString::fromStdString(e.what()));
    }
}

void MainWindow::savePipelineXml() {
    QString file = QFileDialog::getSaveFileName(this, "Save pipeline XML", QString(), "XML (*.xml)");
    if (file.isEmpty()) return;
    try {
        PipelineConfigLoader::saveToXML(file.toStdString(), pipe_);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Error", QString::fromStdString(e.what()));
    }
}

void MainWindow::nextFrame() {
    if (!source_ || !source_->isReady()) {
        QMessageBox::information(this, "Info", "No image source ready. Use 'Open Images'.");
        return;
    }
    currentFrame_ = source_->nextFrame();
    if (!currentFrame_) {
        QMessageBox::information(this, "Info", "No more frames.");
        return;
    }
    if (stepList_->currentRow() >= 0) showBeforeAfter(stepList_->currentRow());
    else {
        // Show original in both panes initially (scaled)
        auto mat = currentFrame_->getCpuMat();
        QImage img = matToQImage(mat);
        QSize beforeSize = beforeLabel_->size();
        QSize afterSize = afterLabel_->size();
        if (beforeSize.width() <= 0 || beforeSize.height() <= 0) beforeSize = QSize(800,600);
        if (afterSize.width() <= 0 || afterSize.height() <= 0) afterSize = QSize(800,600);
        beforeLabel_->setPixmap(QPixmap::fromImage(img).scaled(beforeSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        afterLabel_->setPixmap(QPixmap::fromImage(img).scaled(afterSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    int idx = stepList_->currentRow();
    if (currentFrame_) {
        if (idx >= 0) showBeforeAfter(idx);
        else {
            // rescale original view
            auto mat = currentFrame_->getCpuMat();
            QImage img = matToQImage(mat);
            QSize beforeSize = beforeLabel_->size();
            QSize afterSize = afterLabel_->size();
            if (beforeSize.width() <= 0 || beforeSize.height() <= 0) beforeSize = QSize(800,600);
            if (afterSize.width() <= 0 || afterSize.height() <= 0) afterSize = QSize(800,600);
            beforeLabel_->setPixmap(QPixmap::fromImage(img).scaled(beforeSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            afterLabel_->setPixmap(QPixmap::fromImage(img).scaled(afterSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    }
}

void MainWindow::onStepSelectionChanged() {
    int index = stepList_->currentRow();
    if (index < 0) return;
    if (static_cast<size_t>(index) >= pipe_.steps.size() || static_cast<size_t>(index) >= pipe_.configs.size()) return;
    buildParamsEditor(index);
    showBeforeAfter(index);
}

void MainWindow::onApplyParameters() {
    int index = stepList_->currentRow();
    if (index < 0) return;
    if (static_cast<size_t>(index) >= pipe_.configs.size()) return;
    // Collect parameter editors back into pipe_.configs[index]
    for (int i = 0; i < paramsForm_->rowCount() - 1; ++i) { // -1 for Apply row
        auto* itemLabel = paramsForm_->itemAt(i, QFormLayout::LabelRole);
        auto* itemField = paramsForm_->itemAt(i, QFormLayout::FieldRole);
        if (!itemLabel || !itemField) continue;
        auto* label = qobject_cast<QLabel*>(itemLabel->widget());
        auto* spin = qobject_cast<QDoubleSpinBox*>(itemField->widget());
        if (!label || !spin) continue;
        pipe_.configs[index].setParameter(label->text().toStdString(), spin->value());
    }
    showBeforeAfter(index);
}

void MainWindow::refreshStepList() {
    stepList_->clear();
    for (const auto& step : pipe_.steps) {
        stepList_->addItem(QString::fromStdString(step->getName()));
    }
}

void MainWindow::buildParamsEditor(int index) {
    // Clear existing rows except the button
    while (paramsForm_->rowCount() > 1) paramsForm_->removeRow(0);
    if (index < 0 || static_cast<size_t>(index) >= pipe_.configs.size()) return;
    const auto& cfg = pipe_.configs[index].getAllParameters();
    for (const auto& kv : cfg) {
        auto* lbl = new QLabel(QString::fromStdString(kv.first));
        auto* spin = new QDoubleSpinBox;
        spin->setRange(-1e9, 1e9);
        spin->setDecimals(4);
        spin->setSingleStep(1.0);
        spin->setValue(kv.second);
        paramsForm_->addRow(lbl, spin);
    }
    // applyBtn_ is already the last row; no need to re-add here
}

void MainWindow::showBeforeAfter(int index) {
    if (!currentFrame_) return;
    if (pipe_.steps.empty() || pipe_.configs.empty()) return;
    size_t m = std::min(pipe_.steps.size(), pipe_.configs.size());
    if (m == 0) return;
    if (index < 0) index = 0;
    if (static_cast<size_t>(index) >= m) index = static_cast<int>(m - 1);
    // Compute before: run steps up to index-1
    FrameMemoryObject before = currentFrame_->clone();
    try {
        for (int i = 0; i < index; ++i) {
            before = pipe_.steps[static_cast<size_t>(i)]->process(before, pipe_.configs[static_cast<size_t>(i)]);
        }
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Processing error", QString("Error in step %1: %2").arg(index).arg(e.what()));
    }

    // After: apply selected step
    FrameMemoryObject after = before.clone();
    try {
        after = pipe_.steps[static_cast<size_t>(index)]->process(before.clone(), pipe_.configs[static_cast<size_t>(index)]);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Processing error", QString("Error applying step %1: %2").arg(index).arg(e.what()));
    }

    // Convert and scale to fit labels while keeping aspect ratio
    QImage beforeImg = matToQImage(before.getCpuMat());
    QImage afterImg = matToQImage(after.getCpuMat());

    QSize beforeSize = beforeLabel_->size();
    QSize afterSize = afterLabel_->size();
    if (beforeSize.width() <= 0 || beforeSize.height() <= 0) beforeSize = QSize(800, 600);
    if (afterSize.width() <= 0 || afterSize.height() <= 0) afterSize = QSize(800, 600);

    beforeLabel_->setPixmap(QPixmap::fromImage(beforeImg).scaled(beforeSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    afterLabel_->setPixmap(QPixmap::fromImage(afterImg).scaled(afterSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

QImage MainWindow::matToQImage(const cv::Mat& mat) {
    cv::Mat rgb;
    if (mat.channels() == 1) {
        return QImage(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_Grayscale8).copy();
    } else {
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        return QImage(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888).copy();
    }
}
