#include "MainWindow.h"
#include "ConfigDialog.h"
#include "ImageUtils.h"
#include <QFileDialog>
#include <QInputDialog>
#include <QDateTime>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QPixmap>
#include <QImage>
#include <QDir>
#include <QFileInfo>
#include <QDialogButtonBox>
#include <QProgressDialog>
#include <QTextEdit>
#include <QStatusBar>
#include <QRandomGenerator>
#include <QSlider>
#include <QApplication>
#include <chrono>
#include <opencv2/imgproc.hpp>
#include <liblbt/processing_pipeline.h>
#include <liblbt/process_step_factory.h>

MainWindow::MainWindow(QWidget* parent) 
    : QMainWindow(parent)
    , settings_("record3k", "pipelineviewer")
    , isPlaying_(false)
    , playbackSpeed_(1.0)
{
    loadConfiguration();
    ensureWorkDir();
    
    // Create playback timer
    playbackTimer_ = new QTimer(this);
    connect(playbackTimer_, &QTimer::timeout, this, &MainWindow::onPlaybackTick);

    // Resize debounce timer: prevents running pipeline on every resize pixel
    resizeDebounceTimer_ = new QTimer(this);
    resizeDebounceTimer_->setSingleShot(true);
    resizeDebounceTimer_->setInterval(150); // ms after last resize event
    connect(resizeDebounceTimer_, &QTimer::timeout, this, [this]() {
        int idx = stepList_->currentRow();
        if (currentFrame_) {
            if (idx >= 0) showBeforeAfter(idx);
            else {
                auto mat = currentFrame_->getCpuMat();
                QImage img = ImageUtils::matToQImage(mat);
                QSize beforeSize = beforeLabel_->size();
                QSize afterSize = afterLabel_->size();
                if (beforeSize.width() <= 0 || beforeSize.height() <= 0) beforeSize = QSize(800,600);
                if (afterSize.width() <= 0 || afterSize.height() <= 0) afterSize = QSize(800,600);
                beforeLabel_->setPixmap(QPixmap::fromImage(img).scaled(beforeSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                afterLabel_->setPixmap(QPixmap::fromImage(img).scaled(afterSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
        }
    });
    
    apiClient_ = std::make_unique<ApiClient>(this);
    apiClient_->setBaseUrl(settings_.value("api/url", "http://127.0.0.1:8080").toString());
    apiClient_->setApiToken(settings_.value("api/token", "default-dev-token").toString());
    
    // Connect to error signal for verbose error messages
    connect(apiClient_.get(), &ApiClient::error, this, [this](const QString& msg) {
        statusBar()->showMessage("API Error: " + msg, 5000);
        qWarning() << "ApiClient error:" << msg;
    });
    
    // Toolbar
    auto* tb = addToolBar("Main");
    auto* configAct = tb->addAction("⚙️ Config");
    auto* browseRecordsAct = tb->addAction("📋 Browse Records");
    tb->addSeparator();
    auto* openDirAct = tb->addAction("Open Images");
    auto* loadXmlAct = tb->addAction("Load XML");
    auto* saveXmlAct = tb->addAction("Save XML");
    forceCpuAct_ = tb->addAction("Force CPU");
    forceCpuAct_->setCheckable(true);
    nextBtn_ = new QPushButton("Next Frame");
    tb->addWidget(nextBtn_);
    
    // Add toolbar break for frame navigation section
    addToolBarBreak();
    auto* navTb = addToolBar("Navigation");
    
    // Frame navigation slider
    frameLabel_ = new QLabel("Frame: 0/0");
    navTb->addWidget(frameLabel_);
    frameSlider_ = new QSlider(Qt::Horizontal);
    frameSlider_->setMinimum(0);
    frameSlider_->setMaximum(0);
    frameSlider_->setValue(0);
    frameSlider_->setEnabled(false);
    frameSlider_->setMinimumWidth(200);
    navTb->addWidget(frameSlider_);
    
    // Playback controls
    navTb->addSeparator();
    playPauseBtn_ = new QPushButton("▶️ Play");
    playPauseBtn_->setEnabled(false);
    navTb->addWidget(playPauseBtn_);
    
    speedCombo_ = new QComboBox();
    speedCombo_->addItem("1x", 1.0);
    speedCombo_->addItem("2x", 2.0);
    speedCombo_->addItem("4x", 4.0);
    speedCombo_->setCurrentIndex(0);
    navTb->addWidget(speedCombo_);
    
    connect(playPauseBtn_, &QPushButton::clicked, this, &MainWindow::onPlayPauseToggled);
    connect(speedCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index) {
        playbackSpeed_ = speedCombo_->itemData(index).toDouble();
    });

    connect(configAct, &QAction::triggered, this, &MainWindow::showConfiguration);
    connect(browseRecordsAct, &QAction::triggered, this, &MainWindow::showRecordsBrowser);
    connect(openDirAct, &QAction::triggered, this, &MainWindow::openImageDirectory);
    connect(loadXmlAct, &QAction::triggered, this, &MainWindow::loadPipelineXml);
    connect(saveXmlAct, &QAction::triggered, this, &MainWindow::savePipelineXml);
    connect(forceCpuAct_, &QAction::toggled, [this](bool on){ this->forceCpu_ = on; });
    connect(nextBtn_, &QPushButton::clicked, this, &MainWindow::nextFrame);
    connect(frameSlider_, &QSlider::valueChanged, this, &MainWindow::onFrameSliderChanged);
    
    // Add toolbar break for pipeline editing section
    addToolBarBreak();
    
    // Pipeline Edit Toolbar
    auto* pipeEditTb = addToolBar("Pipeline Edit");
    auto* nextFrameAct = pipeEditTb->addAction("▶️ Next Frame");
    auto* savePipelineAct = pipeEditTb->addAction("💾 Save XML");
    auto* savePipelineJsonAct = pipeEditTb->addAction("📦 Save JSON");
    auto* uploadPipelineAct = pipeEditTb->addAction("☁️ Upload to API");
    pipeEditTb->addSeparator();
    auto* removeStepAct = pipeEditTb->addAction("➖ Remove Step");
    auto* addStepAct = pipeEditTb->addAction("➕ Add Step");
    
    connect(nextFrameAct, &QAction::triggered, this, &MainWindow::nextFrame);
    connect(savePipelineAct, &QAction::triggered, this, &MainWindow::savePipelineXml);
    connect(savePipelineJsonAct, &QAction::triggered, this, &MainWindow::savePipelineJson);
    connect(uploadPipelineAct, &QAction::triggered, this, &MainWindow::uploadPipeline);
    connect(removeStepAct, &QAction::triggered, this, &MainWindow::onRemoveStep);
    connect(addStepAct, &QAction::triggered, this, &MainWindow::onAddStep);

    // Left: steps list
    stepList_ = new QListWidget;
    connect(stepList_, &QListWidget::currentRowChanged, this, &MainWindow::onStepSelectionChanged);

    // Center: three image views (input, before, after)
    inputLabel_ = new QLabel; inputLabel_->setAlignment(Qt::AlignCenter);
    beforeLabel_ = new QLabel; beforeLabel_->setAlignment(Qt::AlignCenter);
    afterLabel_ = new QLabel; afterLabel_->setAlignment(Qt::AlignCenter);

    auto* inputScroll = new QScrollArea; inputScroll->setWidgetResizable(true); inputScroll->setWidget(inputLabel_);
    auto* beforeScroll = new QScrollArea; beforeScroll->setWidgetResizable(true); beforeScroll->setWidget(beforeLabel_);
    auto* afterScroll = new QScrollArea; afterScroll->setWidgetResizable(true); afterScroll->setWidget(afterLabel_);
    
    // Add labels to the scroll areas
    auto* inputLayout = new QVBoxLayout();
    auto* inputTitle = new QLabel("<b>Input (Original)</b>");
    inputTitle->setAlignment(Qt::AlignCenter);
    auto* inputWidget = new QWidget();
    inputLayout->addWidget(inputTitle);
    inputLayout->addWidget(inputScroll);
    inputWidget->setLayout(inputLayout);
    
    auto* beforeLayout = new QVBoxLayout();
    auto* beforeTitle = new QLabel("<b>Before Filter</b>");
    beforeTitle->setAlignment(Qt::AlignCenter);
    auto* beforeWidget = new QWidget();
    beforeLayout->addWidget(beforeTitle);
    beforeLayout->addWidget(beforeScroll);
    beforeWidget->setLayout(beforeLayout);
    
    auto* afterLayout = new QVBoxLayout();
    auto* afterTitle = new QLabel("<b>After Filter</b>");
    afterTitle->setAlignment(Qt::AlignCenter);
    auto* afterWidget = new QWidget();
    afterLayout->addWidget(afterTitle);
    afterLayout->addWidget(afterScroll);
    afterWidget->setLayout(afterLayout);

    auto* imagesSplit = new QSplitter(Qt::Horizontal);
    imagesSplit->addWidget(inputWidget);
    imagesSplit->addWidget(beforeWidget);
    imagesSplit->addWidget(afterWidget);

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
    
    // Test API connection on startup
    testApiConnection();
}

void MainWindow::testApiConnection() {
    apiClient_->ping([this](bool success) {
        if (success) {
            statusBar()->showMessage("✓ Connected to API: " + apiClient_->baseUrl(), 3000);
        } else {
            statusBar()->showMessage("✗ Cannot connect to API: " + apiClient_->baseUrl() + " - Check configuration", 5000);
            QMessageBox::warning(this, "API Connection", 
                "Cannot connect to API at " + apiClient_->baseUrl() + 
                "\n\nPlease check:\n- API server is running\n- URL is correct in Config");
        }
    });
}

void MainWindow::openImageDirectory() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select image directory");
    if (dir.isEmpty()) return;
    source_ = std::make_unique<DiskImageFrameSource>(dir.toStdString(), "*.jpg");
    
    // Enable playback controls
    playPauseBtn_->setEnabled(true);
    
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

void MainWindow::savePipelineJson() {
    // Prompt for pipeline description
    bool ok;
    QString description = QInputDialog::getText(this, "Pipeline Description",
        "Enter a description for this pipeline:", QLineEdit::Normal,
        "Custom pipeline", &ok);
    
    if (!ok) return;  // User cancelled
    
    // Generate random UID for pipeline file
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    int random = QRandomGenerator::global()->bounded(10000);
    QString pipelineUID = QString("pipeline-%1-%2").arg(timestamp).arg(random);
    
    // Format timestamp as ISO-8601
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::gmtime(&time);
    char buffer[100];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    
    // Set pipeline metadata
    pipe_.description = description.toStdString();
    pipe_.datetimeCreated = std::string(buffer);
    
    QString defaultFilename = workDir_ + "/" + pipelineUID + ".json";
    QString file = QFileDialog::getSaveFileName(this, "Save pipeline JSON", 
        defaultFilename, "JSON (*.json)");
    
    if (file.isEmpty()) return;
    
    try {
        PipelineConfigLoader::saveToJSON(file.toStdString(), pipe_);
        statusBar()->showMessage("Pipeline saved to: " + file, 3000);
        QMessageBox::information(this, "Success", 
            QString("Pipeline saved successfully!\n\nUID: %1\nFile: %2\nDescription: %3\nCreated: %4")
            .arg(pipelineUID, file, description, QString::fromStdString(pipe_.datetimeCreated)));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Error", 
            QString("Failed to save pipeline: %1").arg(QString::fromStdString(e.what())));
    }
}

void MainWindow::uploadPipeline() {
    // Check if pipeline has steps
    if (pipe_.steps.empty()) {
        QMessageBox::warning(this, "Upload Pipeline", "Pipeline is empty. Add at least one step before uploading.");
        return;
    }
    
    // Prompt for pipeline description
    bool ok;
    QString description = QInputDialog::getText(this, "Pipeline Description",
        "Enter a description for this pipeline:", QLineEdit::Normal,
        "Custom pipeline", &ok);
    
    if (!ok) return;  // User cancelled
    
    // Format timestamp as ISO-8601
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::gmtime(&time);
    char buffer[100];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    
    // Save to temp file
    QString tempFile = QDir::temp().filePath(QString("pipe-upload-%1.json").arg(QDateTime::currentMSecsSinceEpoch()));
    
    try {
        // Validate before upload (metadata not required for validation)
        std::string validationError;
        if (!PipelineConfigLoader::validate(pipe_, validationError)) {
            QMessageBox::critical(this, "Validation Error", 
                QString("Pipeline validation failed:\n\n%1").arg(QString::fromStdString(validationError)));
            return;
        }

        // Generate unique pipelineUID
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        int random = QRandomGenerator::global()->bounded(10000);
        QString pipelineUID = QString("pipe-%1-%2").arg(timestamp).arg(random);
        
        // Set all metadata ONCE before saving
        pipe_.pipelineUID = pipelineUID.toStdString();
        pipe_.description = description.toStdString();
        pipe_.datetimeCreated = std::string(buffer);
        pipe_.status = "active";
        
        qDebug() << "[MainWindow::uploadPipeline] Saving pipeline with:";
        qDebug() << "  pipelineUID:" << QString::fromStdString(pipe_.pipelineUID);
        qDebug() << "  description:" << QString::fromStdString(pipe_.description);
        qDebug() << "  datetimeCreated:" << QString::fromStdString(pipe_.datetimeCreated);
        qDebug() << "  status:" << QString::fromStdString(pipe_.status);
        qDebug() << "  steps:" << pipe_.steps.size();
      
        // Save pipeline config
        if (!PipelineConfigLoader::saveToJSON(tempFile.toStdString(), pipe_)) {
            QMessageBox::critical(this, "Error", "Failed to save pipeline to temporary file");
            return;
        }
        
        // Debug: Read back to verify FULL content
        QFile file(tempFile);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray content = file.readAll();
            qDebug() << "[MainWindow::uploadPipeline] Saved file size:" << content.size() << "bytes";
            qDebug() << "[MainWindow::uploadPipeline] First 1000 chars:" << content.left(1000);
            qDebug() << "[MainWindow::uploadPipeline] Last 200 chars:" << content.right(200);
            file.close();
            
            // Keep a copy for debugging
            QString debugCopy = tempFile + ".debug";
            QFile::copy(tempFile, debugCopy);
            qDebug() << "[MainWindow::uploadPipeline] Debug copy saved to:" << debugCopy;
        }

        auto* progress = new QProgressDialog("Uploading pipeline to API...", "Cancel", 0, 0, this);
        progress->setWindowModality(Qt::WindowModal);
        progress->show();
        
        apiClient_->uploadPipeline(tempFile, [this, progress, tempFile, description](bool success, const QString& message) {
            progress->close();
            progress->deleteLater();
            
            // Clean up temp file
            QFile::remove(tempFile);
            
            if (success) {
                statusBar()->showMessage("✓ Pipeline uploaded successfully", 3000);
                QMessageBox::information(this, "Upload Success", message);
            } else {
                QMessageBox::critical(this, "Upload Failed", message);
            }
        });
        
    } catch (const std::exception& e) {
        QFile::remove(tempFile);
        QMessageBox::critical(this, "Error", 
            QString("Failed to prepare pipeline for upload: %1").arg(QString::fromStdString(e.what())));
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
        // Stop playback if running
        if (isPlaying_) {
            isPlaying_ = false;
            playbackTimer_->stop();
            playPauseBtn_->setText("▶️ Play");
        }
        return;
    }
    
    // Update slider position (block signals to prevent recursion)
    if (frameSlider_->isEnabled()) {
        frameSlider_->blockSignals(true);
        int currentValue = frameSlider_->value();
        if (currentValue < frameSlider_->maximum()) {
            frameSlider_->setValue(currentValue + 1);
            frameLabel_->setText(QString("Frame: %1/%2").arg(currentValue + 1).arg(frameSlider_->maximum()));
        }
        frameSlider_->blockSignals(false);
    }
    
    // Always show original input in the input label
    auto inputMat = currentFrame_->getCpuMat();
    QImage inputImg = ImageUtils::matToQImage(inputMat);
    QSize inputSize = inputLabel_->size();
    if (inputSize.width() <= 0 || inputSize.height() <= 0) inputSize = QSize(400, 400);
    inputLabel_->setPixmap(QPixmap::fromImage(inputImg).scaled(inputSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    
    if (stepList_->currentRow() >= 0) showBeforeAfter(stepList_->currentRow());
    else {
        // Show original in both before/after panes initially (scaled)
        QSize beforeSize = beforeLabel_->size();
        QSize afterSize = afterLabel_->size();
        if (beforeSize.width() <= 0 || beforeSize.height() <= 0) beforeSize = QSize(400,400);
        if (afterSize.width() <= 0 || afterSize.height() <= 0) afterSize = QSize(400,400);
        beforeLabel_->setPixmap(QPixmap::fromImage(inputImg).scaled(beforeSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        afterLabel_->setPixmap(QPixmap::fromImage(inputImg).scaled(afterSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}

void MainWindow::onFrameSliderChanged(int value) {
    if (!source_ || !source_->isReady()) {
        return;
    }
    
    // Cast to DiskImageFrameSource to access seekToFrame
    auto* diskSource = dynamic_cast<DiskImageFrameSource*>(source_.get());
    if (!diskSource) {
        return; // Not a disk source, can't seek
    }
    
    // Seek to the frame and load it
    diskSource->seekToFrame(value);
    currentFrame_ = diskSource->nextFrame();
    
    if (currentFrame_) {
        // Update label
        frameLabel_->setText(QString("Frame: %1/%2").arg(value).arg(frameSlider_->maximum()));
        
        // Display the frame
        if (stepList_->currentRow() >= 0) {
            showBeforeAfter(stepList_->currentRow());
        } else {
            // Show original in both panes
            auto mat = currentFrame_->getCpuMat();
            QImage img = ImageUtils::matToQImage(mat);
            QSize beforeSize = beforeLabel_->size();
            QSize afterSize = afterLabel_->size();
            if (beforeSize.width() <= 0 || beforeSize.height() <= 0) beforeSize = QSize(800,600);
            if (afterSize.width() <= 0 || afterSize.height() <= 0) afterSize = QSize(800,600);
            beforeLabel_->setPixmap(QPixmap::fromImage(img).scaled(beforeSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            afterLabel_->setPixmap(QPixmap::fromImage(img).scaled(afterSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    }
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    // Debounce: restart the timer so showBeforeAfter only runs once after resizing stops
    resizeDebounceTimer_->start();
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
    QImage beforeImg = ImageUtils::matToQImage(before.getCpuMat());
    QImage afterImg = ImageUtils::matToQImage(after.getCpuMat());

    QSize beforeSize = beforeLabel_->size();
    QSize afterSize = afterLabel_->size();
    if (beforeSize.width() <= 0 || beforeSize.height() <= 0) beforeSize = QSize(800, 600);
    if (afterSize.width() <= 0 || afterSize.height() <= 0) afterSize = QSize(800, 600);

    beforeLabel_->setPixmap(QPixmap::fromImage(beforeImg).scaled(beforeSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    afterLabel_->setPixmap(QPixmap::fromImage(afterImg).scaled(afterSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::loadConfiguration() {
    workDir_ = settings_.value("working/directory", "~/tmp/pipelineviewer").toString();
}

void MainWindow::ensureWorkDir() {
    QDir dir;
    if (!dir.mkpath(workDir_)) {
        QMessageBox::warning(this, "Warning", 
            QString("Failed to create working directory: %1").arg(workDir_));
    }
}

void MainWindow::showConfiguration() {
    ConfigDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        loadConfiguration();
        ensureWorkDir();
        apiClient_->setBaseUrl(settings_.value("api/url", "http://127.0.0.1:8080").toString());
        apiClient_->setApiToken(settings_.value("api/token", "default-dev-token").toString());
    }
}

void MainWindow::showRecordsBrowser() {
    if (!recordsDialog_) {
        recordsDialog_ = new QDialog(this);
        recordsDialog_->setWindowTitle("Browse Records");
        recordsDialog_->resize(800, 600);
        
        auto* layout = new QVBoxLayout(recordsDialog_);
        
        // Records list
        recordsList_ = new QListWidget;
        connect(recordsList_, &QListWidget::currentRowChanged, this, &MainWindow::onRecordSelected);
        layout->addWidget(new QLabel("Available Records:"));
        layout->addWidget(recordsList_);
        
        // Record details
        recordDetailsLabel_ = new QLabel;
        recordDetailsLabel_->setWordWrap(true);
        recordDetailsLabel_->setTextFormat(Qt::RichText);
        recordDetailsLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        auto* detailsScroll = new QScrollArea;
        detailsScroll->setWidget(recordDetailsLabel_);
        detailsScroll->setWidgetResizable(true);
        detailsScroll->setMaximumHeight(150);
        layout->addWidget(new QLabel("Record Details:"));
        layout->addWidget(detailsScroll);
        
        // Buttons
        auto* btnLayout = new QHBoxLayout;
        downloadBtn_ = new QPushButton("Download && Open");
        downloadBtn_->setEnabled(false);
        connect(downloadBtn_, &QPushButton::clicked, this, &MainWindow::onDownloadRecord);
        auto* refreshBtn = new QPushButton("Refresh");
        connect(refreshBtn, &QPushButton::clicked, [this]() { showRecordsBrowser(); });
        auto* closeBtn = new QPushButton("Close");
        connect(closeBtn, &QPushButton::clicked, recordsDialog_, &QDialog::close);
        
        btnLayout->addWidget(downloadBtn_);
        btnLayout->addWidget(refreshBtn);
        btnLayout->addStretch();
        btnLayout->addWidget(closeBtn);
        layout->addLayout(btnLayout);
    }
    
    // Fetch records from API
    recordsList_->clear();
    recordDetailsLabel_->clear();
    downloadBtn_->setEnabled(false);
    
    apiClient_->listRecords([this](const QJsonArray& records) {
        for (const auto& rec : records) {
            QJsonObject obj = rec.toObject();
            QString recordUID = obj["recordUID"].toString();
            QString status = obj["status"].toString();
            QString dateStart = obj["datetimeStart"].toString();
            
            auto* item = new QListWidgetItem(QString("%1 - %2 (%3)")
                .arg(recordUID, status, dateStart));
            item->setData(Qt::UserRole, QVariant::fromValue(obj));
            recordsList_->addItem(item);
        }
    });
    
    recordsDialog_->show();
    recordsDialog_->raise();
    recordsDialog_->activateWindow();
}

void MainWindow::onRecordSelected() {
    int row = recordsList_->currentRow();
    if (row < 0) {
        recordDetailsLabel_->clear();
        downloadBtn_->setEnabled(false);
        return;
    }
    
    QListWidgetItem* item = recordsList_->item(row);
    QJsonObject record = item->data(Qt::UserRole).toJsonObject();
    
    // Display record details
    QString details = "<table>";
    details += QString("<tr><td><b>Record UID:</b></td><td>%1</td></tr>").arg(record["recordUID"].toString());
    details += QString("<tr><td><b>Status:</b></td><td>%1</td></tr>").arg(record["status"].toString());
    details += QString("<tr><td><b>Camera:</b></td><td>%1</td></tr>").arg(record["cameraUID"].toString());
    details += QString("<tr><td><b>Pipeline:</b></td><td>%1</td></tr>").arg(record["pipelineUID"].toString());
    details += QString("<tr><td><b>Start Time:</b></td><td>%1</td></tr>").arg(record["datetimeStart"].toString());
    details += QString("<tr><td><b>End Time:</b></td><td>%1</td></tr>").arg(record["datetimeEnd"].toString());
    
    if (record.contains("framesRecorded")) {
        details += QString("<tr><td><b>Frames:</b></td><td>%1</td></tr>").arg(record["framesRecorded"].toInt());
    }
    if (record.contains("actualFPS")) {
        details += QString("<tr><td><b>FPS:</b></td><td>%1</td></tr>")
            .arg(record["actualFPS"].toDouble(), 0, 'f', 2);
    }
    
    details += "</table>";
    recordDetailsLabel_->setText(details);
    downloadBtn_->setEnabled(true);
}

void MainWindow::onDownloadRecord() {
    int row = recordsList_->currentRow();
    if (row < 0) return;
    
    QListWidgetItem* item = recordsList_->item(row);
    QJsonObject record = item->data(Qt::UserRole).toJsonObject();
    QString recordUID = record["recordUID"].toString();
    QString fileUID = record["fileUID"].toString();
    
    if (fileUID.isEmpty()) {
        QMessageBox::warning(this, "Error", "Record has no associated file.");
        return;
    }
    
    // Download file
    QString destDir = workDir_ + "/records/" + recordUID;
    QString destFile = destDir + "/" + fileUID + ".tar.gz";
    
    QDir().mkpath(destDir);
    
    auto* progress = new QProgressDialog("Downloading record...", "Cancel", 0, 100, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->show();
    
    // Use shared_ptr to keep error message alive for async callback
    auto lastError = std::make_shared<QString>();
    auto errorConnection = connect(apiClient_.get(), &ApiClient::error, [lastError](const QString& msg) {
        *lastError = msg;
    });
    
    apiClient_->downloadFile(fileUID, destFile,
        [progress](qint64 received, qint64 total) {
            if (total > 0) {
                progress->setValue((received * 100) / total);
            }
        },
        [this, progress, destFile, destDir, recordUID, fileUID, errorConnection, lastError](bool success) {
            disconnect(errorConnection);
            progress->close();
            progress->deleteLater();
            
            if (!success) {
                QString errorMsg = "Failed to download record file.";
                if (!lastError->isEmpty()) {
                    errorMsg += "\n\nDetails: " + *lastError;
                }
                errorMsg += "\n\nFile UID: " + fileUID;
                errorMsg += "\nEndpoint: " + apiClient_->baseUrl() + "/files/download/" + fileUID;
                QMessageBox::critical(this, "Download Error", errorMsg);
                return;
            }
            
            // Show extraction progress
            auto* extractProgress = new QProgressDialog("Extracting archive...", nullptr, 0, 0, this);
            extractProgress->setWindowModality(Qt::WindowModal);
            extractProgress->setCancelButton(nullptr); // No cancel during extraction
            extractProgress->setMinimumDuration(0);
            extractProgress->setValue(0);
            extractProgress->show();
            QApplication::processEvents(); // Force UI update
            
            // Extract tar.gz asynchronously via QProcess (avoids blocking the UI/event loop)
            statusBar()->showMessage("Extracting archive...");
            auto* tarProcess = new QProcess(this);
            tarProcess->setWorkingDirectory(destDir);
            connect(tarProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, tarProcess, extractProgress, destDir, destFile, recordUID](int exitCode, QProcess::ExitStatus) {
                    tarProcess->deleteLater();
                    extractProgress->close();
                    extractProgress->deleteLater();

                    if (exitCode != 0) {
                        QMessageBox::critical(this, "Error",
                            QString("Failed to extract archive (tar exit %1):\n%2")
                                .arg(exitCode)
                                .arg(QString::fromLocal8Bit(tarProcess->readAllStandardError())));
                        return;
                    }

                    QString extractedDir = destDir + "/file-rec-" + recordUID;
                    if (!QDir(extractedDir).exists()) {
                        QMessageBox::warning(this, "Error",
                            "Extracted archive but expected directory 'file-rec-" + recordUID + "' not found.");
                        return;
                    }

                    loadRecordData(recordUID, extractedDir);
                    recordsDialog_->close();
                });
            tarProcess->start("tar", {QStringLiteral("-xzf"), QFileInfo(destFile).fileName()});
        }
    );
}

void MainWindow::loadRecordData(const QString& recordUID, const QString& extractPath) {
    // Look for frames directory
    QString framesDir = extractPath + "/frames";
    if (!QDir(framesDir).exists()) {
        QMessageBox::warning(this, "Error", "Frames directory not found in record.");
        return;
    }
    
    // Load images
    source_ = std::make_unique<DiskImageFrameSource>(framesDir.toStdString(), "*.jpg");
    
    // Count frames and setup slider
    int frameCount = QDir(framesDir).entryList(QStringList() << "*.jpg", QDir::Files).count();
    if (frameCount > 0) {
        frameSlider_->setEnabled(true);
        frameSlider_->setMaximum(frameCount - 1);
        frameSlider_->setValue(0);
        frameLabel_->setText(QString("Frame: 0/%1").arg(frameCount - 1));
        
        // Enable playback controls
        playPauseBtn_->setEnabled(true);
    } else {
        frameSlider_->setEnabled(false);
        frameLabel_->setText("Frame: 0/0");
        playPauseBtn_->setEnabled(false);
    }
    
    nextFrame();
    statusBar()->showMessage("Loaded frames from: " + framesDir);
    
    // Try to load pipeline JSON (new format: pipeline-{recordUID}.json)
    QString pipelineFile = extractPath + "/pipeline-" + recordUID + ".json";
    if (!QFile::exists(pipelineFile)) {
        // Try old XML format as fallback
        pipelineFile = extractPath + "/pipeline-" + recordUID + ".xml";
    }
    
    if (QFile::exists(pipelineFile)) {
        try {
            if (pipelineFile.endsWith(".json")) {
                pipe_ = PipelineConfigLoader::loadFromJSON(pipelineFile.toStdString());
            } else {
                pipe_ = PipelineConfigLoader::loadFromXML(pipelineFile.toStdString());
            }
            
            if (forceCpu_) {
                std::vector<std::shared_ptr<ProcessStep>> newSteps;
                for (const auto& step : pipe_.steps) {
                    FilterType ft = ProcessStepFactory::nameToFilterType(step->getName());
                    if (ft == FilterType::NONE) {
                        newSteps.push_back(step);
                    } else {
                        newSteps.push_back(ProcessStepFactory::createStep(ft, ProcessingMode::CPU_ONLY));
                    }
                }
                pipe_.steps = std::move(newSteps);
            }
            
            if (pipe_.steps.size() != pipe_.configs.size()) {
                size_t m = std::min(pipe_.steps.size(), pipe_.configs.size());
                pipe_.steps.resize(m);
                pipe_.configs.resize(m);
            }
            
            refreshStepList();
            if (stepList_->count() > 0) stepList_->setCurrentRow(0);
            
            statusBar()->showMessage("✓ Loaded record: " + recordUID + " with pipeline", 3000);
            QMessageBox::information(this, "Success", 
                QString("Record loaded successfully!\n\nRecord: %1\nFrames: %2\nPipeline: %3")
                .arg(recordUID)
                .arg(QDir(framesDir).entryList(QStringList() << "*.jpg", QDir::Files).count())
                .arg(QFileInfo(pipelineFile).fileName()));
        } catch (const std::exception& e) {
            QMessageBox::warning(this, "Pipeline", 
                QString("Loaded frames but failed to load pipeline: %1").arg(e.what()));
        }
    } else {
        QMessageBox::information(this, "Success", 
            QString("Record loaded (frames only)!\n\nRecord: %1\nFrames: %2\n\nNote: No pipeline file found")
            .arg(recordUID)
            .arg(QDir(framesDir).entryList(QStringList() << "*.jpg", QDir::Files).count()));
    }
}

void MainWindow::onRemoveStep() {
    int row = stepList_->currentRow();
    if (row < 0 || row >= (int)pipe_.steps.size()) {
        QMessageBox::warning(this, "Remove Step", "Please select a step to remove.");
        return;
    }
    
    QString stepName = QString::fromStdString(pipe_.steps[row]->getName());
    auto reply = QMessageBox::question(this, "Remove Step",
        QString("Remove step %1: %2?").arg(row + 1).arg(stepName),
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        pipe_.steps.erase(pipe_.steps.begin() + row);
        pipe_.configs.erase(pipe_.configs.begin() + row);
        refreshStepList();
        
        // Select previous or next step
        if (stepList_->count() > 0) {
            int newRow = std::min(row, stepList_->count() - 1);
            stepList_->setCurrentRow(newRow);
        }
        
        statusBar()->showMessage("Removed step: " + stepName, 3000);
    }
}

void MainWindow::onAddStep() {
    // Create dialog to choose filter type
    QDialog dialog(this);
    dialog.setWindowTitle("Add Step");
    auto* layout = new QVBoxLayout(&dialog);
    
    layout->addWidget(new QLabel("Select filter type:"));
    
    auto* listWidget = new QListWidget;
    // Add available filter types (matching FilterType enum in process_step.h)
    listWidget->addItem("Background Subtraction MOG2");
    listWidget->addItem("Gaussian Blur");
    listWidget->addItem("Binary Threshold");
    listWidget->addItem("Morphology Close");
    listWidget->addItem("Edge Detection");
    listWidget->addItem("Contour Detection");
    listWidget->addItem("Bilateral Filter");
    listWidget->addItem("Median Filter");
    listWidget->addItem("Sharpen");
    listWidget->addItem("Lens Correction");
    listWidget->addItem("ROI Circle Crop");
    
    layout->addWidget(listWidget);
    
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);
    
    if (dialog.exec() == QDialog::Accepted && listWidget->currentItem()) {
        QString filterName = listWidget->currentItem()->text();
        FilterType ft = FilterType::NONE;
        
        // Map display name to FilterType (matching process_step.h enum)
        if (filterName == "Background Subtraction MOG2") ft = FilterType::BACKGROUND_SUBTRACTION_MOG2;
        else if (filterName == "Gaussian Blur") ft = FilterType::GAUSSIAN_BLUR;
        else if (filterName == "Binary Threshold") ft = FilterType::BINARY_THRESHOLD;
        else if (filterName == "Morphology Close") ft = FilterType::MORPHOLOGY_CLOSE;
        else if (filterName == "Edge Detection") ft = FilterType::EDGE_DETECTION;
        else if (filterName == "Contour Detection") ft = FilterType::CONTOUR_DETECTION;
        else if (filterName == "Bilateral Filter") ft = FilterType::BILATERAL_FILTER;
        else if (filterName == "Median Filter") ft = FilterType::MEDIAN_FILTER;
        else if (filterName == "Sharpen") ft = FilterType::SHARPEN;
        else if (filterName == "Lens Correction") ft = FilterType::LENS_CORRECTION;
        else if (filterName == "ROI Circle Crop") ft = FilterType::ROI_CIRCLE_CROP;
        
        if (ft != FilterType::NONE) {
            // Create step with CPU mode if Force CPU is enabled
            ProcessingMode mode = forceCpu_ ? ProcessingMode::CPU_ONLY : ProcessingMode::CUDA_PREFERRED;
            auto step = ProcessStepFactory::createStep(ft, mode);
            
            if (step) {
                pipe_.steps.push_back(step);
                // Add empty config that will be populated with default parameters
                pipe_.configs.push_back(FilterConfig());
                refreshStepList();
                stepList_->setCurrentRow(stepList_->count() - 1);
                statusBar()->showMessage("Added step: " + filterName, 3000);
            } else {
                QMessageBox::warning(this, "Add Step", "Failed to create step: " + filterName);
            }
        }
    }
}
void MainWindow::onPlayPauseToggled() {
    if (!source_ || !source_->isReady()) {
        return;
    }
    
    isPlaying_ = !isPlaying_;
    
    if (isPlaying_) {
        playPauseBtn_->setText("⏸️ Pause");
        // Start playback timer - will calculate interval based on timestamps
        onPlaybackTick(); // Show first frame immediately
    } else {
        playPauseBtn_->setText("▶️ Play");
        playbackTimer_->stop();
    }
}

void MainWindow::onPlaybackTick() {
    if (!isPlaying_ || !source_ || !source_->isReady()) {
        return;
    }
    
    // Get current frame index
    auto* diskSource = dynamic_cast<DiskImageFrameSource*>(source_.get());
    if (!diskSource) {
        return;
    }
    
    size_t currentIndex = diskSource->getCurrentIndex();
    size_t totalFrames = diskSource->getFrameCount();
    
    // Check if we're at the end
    if (currentIndex >= totalFrames - 1) {
        isPlaying_ = false;
        playbackTimer_->stop();
        playPauseBtn_->setText("▶️ Play");
        return;
    }
    
    // Extract timestamp from current filename
    auto getCurrentTimestamp = [diskSource]() -> uint64_t {
        // Get current frame's file path
        const auto& files = diskSource->getImageFiles();
        size_t idx = diskSource->getCurrentIndex();
        if (idx >= files.size()) return 0;
        
        std::string filename = files[idx].filename().string();
        // Extract timestamp from filename: frame_XXXXXX_<timestamp>.jpg
        size_t lastUnderscore = filename.rfind('_');
        size_t dotPos = filename.rfind('.');
        if (lastUnderscore != std::string::npos && dotPos != std::string::npos) {
            std::string tsStr = filename.substr(lastUnderscore + 1, dotPos - lastUnderscore - 1);
            try {
                return std::stoull(tsStr);
            } catch (...) {
                return 0;
            }
        }
        return 0;
    };
    
    // Get next frame timestamp
    auto getNextTimestamp = [diskSource]() -> uint64_t {
        const auto& files = diskSource->getImageFiles();
        size_t idx = diskSource->getCurrentIndex() + 1;
        if (idx >= files.size()) return 0;
        
        std::string filename = files[idx].filename().string();
        size_t lastUnderscore = filename.rfind('_');
        size_t dotPos = filename.rfind('.');
        if (lastUnderscore != std::string::npos && dotPos != std::string::npos) {
            std::string tsStr = filename.substr(lastUnderscore + 1, dotPos - lastUnderscore - 1);
            try {
                return std::stoull(tsStr);
            } catch (...) {
                return 0;
            }
        }
        return 0;
    };
    
    uint64_t currentTs = getCurrentTimestamp();
    uint64_t nextTs = getNextTimestamp();
    
    // Calculate delay to next frame (in nanoseconds, convert to milliseconds)
    int delayMs = 33; // Default 30fps if timestamps unavailable
    if (currentTs > 0 && nextTs > currentTs) {
        uint64_t deltaMs = (nextTs - currentTs) / 1000000; // ns to ms
        delayMs = static_cast<int>(deltaMs / playbackSpeed_);
        
        // Clamp to reasonable range: min 16ms (~60fps max) to avoid starving the event loop
        if (delayMs < 16) delayMs = 16;
        if (delayMs > 5000) delayMs = 33; // Fallback if timestamps are wrong
    }
    
    // Show next frame
    nextFrame();
    
    // Schedule next tick
    playbackTimer_->setSingleShot(true);
    playbackTimer_->start(delayMs);
}