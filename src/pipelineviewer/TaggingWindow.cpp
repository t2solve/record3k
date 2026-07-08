#include "TaggingWindow.h"
#include "ImageUtils.h"

#include <QToolBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QProcess>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QApplication>
#include <QStatusBar>
#include <QDebug>
#include <QPainter>
#include <QPen>
#include <QColor>
#include <QVector>
#include <QLineF>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>

TaggingWindow::TaggingWindow(const QString& dbPath, QWidget* parent)
    : QMainWindow(parent)
    , settings_("record3k", "pipelineviewer")
{
    setWindowTitle("record3k - Tag Record");
    resize(1000, 750);

    QString resolvedDbPath = dbPath.isEmpty() ? QString(kDefaultTagsDbPath) : dbPath;
    try {
        tagStore_ = std::make_unique<lbt::TagDataStore>(resolvedDbPath.toStdString());
        qDebug() << "[TaggingWindow] tag data store opened:" << resolvedDbPath;
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Tag Data Store",
            QString("Failed to open tag data store at:\n%1\n\n%2\n\n"
                    "Tagging will still work, but Save/Load will fail until this is fixed.")
                .arg(resolvedDbPath, e.what()));
        qDebug() << "[TaggingWindow] FAILED to open tag data store:" << resolvedDbPath << e.what();
    }

    auto* toolbar = addToolBar("Main");
    auto* openAct = toolbar->addAction("Open Record (.tar.gz)...");
    connect(openAct, &QAction::triggered, this, &TaggingWindow::openRecordArchive);
    toolbar->addSeparator();

    defineRoiAct_ = toolbar->addAction("Define ROI");
    defineRoiAct_->setCheckable(true);
    connect(defineRoiAct_, &QAction::toggled, this, &TaggingWindow::onDefineRoiToggled);

    moveRoiAct_ = toolbar->addAction("Move ROI");
    moveRoiAct_->setCheckable(true);
    moveRoiAct_->setEnabled(false);
    connect(moveRoiAct_, &QAction::toggled, this, &TaggingWindow::onMoveRoiToggled);

    resizeRoiAct_ = toolbar->addAction("Resize ROI");
    resizeRoiAct_->setCheckable(true);
    resizeRoiAct_->setEnabled(false);
    connect(resizeRoiAct_, &QAction::toggled, this, &TaggingWindow::onResizeRoiToggled);

    tagAnimalAct_ = toolbar->addAction("Tag Animal");
    tagAnimalAct_->setCheckable(true);
    tagAnimalAct_->setToolTip("Automatic: click once to seed the CSRT tracker, which then follows the animal forward.");
    connect(tagAnimalAct_, &QAction::toggled, this, &TaggingWindow::onTagAnimalToggled);

    manualTagAct_ = toolbar->addAction("Manual Tag");
    manualTagAct_->setCheckable(true);
    manualTagAct_->setToolTip("Manual: click the animal's center on each frame yourself; auto-advances to the next "
                              "frame after every click. Use for individuals too fast for the tracker to follow.");
    connect(manualTagAct_, &QAction::toggled, this, &TaggingWindow::onManualTagToggled);

    stopTrackingAct_ = toolbar->addAction("Stop Tracking");
    stopTrackingAct_->setEnabled(false);
    connect(stopTrackingAct_, &QAction::triggered, this, &TaggingWindow::onStopTracking);

    toolbar->addSeparator();
    markInvalidAct_ = toolbar->addAction("Mark Invalid From Here");
    connect(markInvalidAct_, &QAction::triggered, this, &TaggingWindow::onMarkInvalidFromHere);

    markRecordFailedAct_ = toolbar->addAction("Mark Record As Complete Failure");
    markRecordFailedAct_->setCheckable(true);
    connect(markRecordFailedAct_, &QAction::toggled, this, &TaggingWindow::onMarkRecordFailedToggled);

    saveTagsAct_ = toolbar->addAction("Save Tags");
    connect(saveTagsAct_, &QAction::triggered, this, &TaggingWindow::onSaveTags);

    auto* central = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(central);

    statusLabel_ = new QLabel("No record loaded. Use \"Open Record (.tar.gz)...\" to begin.");
    statusLabel_->setWordWrap(true);
    mainLayout->addWidget(statusLabel_);

    imageLabel_ = new ClickableImageLabel;
    imageLabel_->setAlignment(Qt::AlignCenter);
    imageLabel_->setMinimumSize(200, 200);
    connect(imageLabel_, &ClickableImageLabel::pressed, this, &TaggingWindow::onImagePressed);
    connect(imageLabel_, &ClickableImageLabel::moved, this, &TaggingWindow::onImageMoved);
    connect(imageLabel_, &ClickableImageLabel::released, this, &TaggingWindow::onImageReleased);
    imageScroll_ = new QScrollArea;
    imageScroll_->setWidget(imageLabel_);
    // Deliberately NOT setWidgetResizable(true): that would let the scroll area
    // continuously resize imageLabel_ to match its viewport, overriding our own
    // imageLabel_->resize(img.size()) in renderCurrentFrame() and centering the pixmap
    // inside a label sized to the viewport instead of the image -- which breaks the
    // "click position == image pixel position" assumption every mouse handler here
    // relies on. Keeping this false means imageLabel_ is always exactly img.size(),
    // and the scroll area just provides real scrollbars for any overflow instead.
    mainLayout->addWidget(imageScroll_, 1);

    auto* navLayout = new QHBoxLayout;
    prevBtn_ = new QPushButton("<< Prev");
    prevBtn_->setEnabled(false);
    connect(prevBtn_, &QPushButton::clicked, this, &TaggingWindow::onPrevFrame);
    navLayout->addWidget(prevBtn_);

    frameSlider_ = new QSlider(Qt::Horizontal);
    frameSlider_->setEnabled(false);
    connect(frameSlider_, &QSlider::valueChanged, this, &TaggingWindow::onFrameSliderChanged);
    navLayout->addWidget(frameSlider_, 1);

    nextBtn_ = new QPushButton("Next >>");
    nextBtn_->setEnabled(false);
    connect(nextBtn_, &QPushButton::clicked, this, &TaggingWindow::onNextFrame);
    navLayout->addWidget(nextBtn_);

    frameLabel_ = new QLabel("Frame: 0/0");
    navLayout->addWidget(frameLabel_);

    mainLayout->addLayout(navLayout);

    setCentralWidget(central);
    statusBar();

    trackingTimer_ = new QTimer(this);
    trackingTimer_->setSingleShot(true);
    connect(trackingTimer_, &QTimer::timeout, this, &TaggingWindow::onTrackingTick);

    qDebug() << "[TaggingWindow] constructed and shown";
}

void TaggingWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    // Viewport size changed -- rescale the current frame immediately rather than
    // waiting for the next navigation/tracking tick to pick up the new displayScale_.
    if (source_ && source_->isReady() && frameSlider_->isEnabled()) {
        renderCurrentFrame(frameSlider_->value());
    }
}

QString TaggingWindow::ensureWorkDir() {
    QString workDir = settings_.value("working/directory", "/tmp/pipelineviewer").toString();
    QString taggingDir = workDir + "/tagging";
    QDir().mkpath(taggingDir);
    return taggingDir;
}

void TaggingWindow::openRecordArchive() {
    qDebug() << "[TaggingWindow] openRecordArchive: showing file picker";
    QString archivePath = QFileDialog::getOpenFileName(
        this, "Open Record Archive", QString(), "Record Archives (*.tar.gz)");
    if (archivePath.isEmpty()) {
        qDebug() << "[TaggingWindow] openRecordArchive: no file selected (dialog cancelled)";
        return;
    }
    qDebug() << "[TaggingWindow] openRecordArchive: selected archive =" << archivePath;

    QString taggingDir = ensureWorkDir();
    QString archiveBaseName = QFileInfo(archivePath).fileName();
    // strip ".tar.gz"
    if (archiveBaseName.endsWith(".tar.gz")) {
        archiveBaseName.chop(7);
    }
    QString destDir = taggingDir + "/" + archiveBaseName;
    QDir().mkpath(destDir);
    qDebug() << "[TaggingWindow] openRecordArchive: extracting to destDir =" << destDir;

    auto* extractProgress = new QProgressDialog("Extracting archive...", nullptr, 0, 0, this);
    extractProgress->setWindowModality(Qt::WindowModal);
    extractProgress->setCancelButton(nullptr);
    extractProgress->setMinimumDuration(0);
    extractProgress->setValue(0);
    extractProgress->show();
    QApplication::processEvents();

    statusBar()->showMessage("Extracting archive...");
    auto* tarProcess = new QProcess(this);
    tarProcess->setWorkingDirectory(destDir);
    connect(tarProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        this, [this, tarProcess, extractProgress, destDir](int exitCode, QProcess::ExitStatus) {
            qDebug() << "[TaggingWindow] tar finished: exitCode =" << exitCode;
            tarProcess->deleteLater();
            extractProgress->close();
            extractProgress->deleteLater();

            if (exitCode != 0) {
                QString err = QString::fromLocal8Bit(tarProcess->readAllStandardError());
                qDebug() << "[TaggingWindow] tar FAILED, stderr:" << err;
                QMessageBox::critical(this, "Error",
                    QString("Failed to extract archive (tar exit %1):\n%2")
                        .arg(exitCode)
                        .arg(err));
                return;
            }

            // The archive may unpack a "frames" dir directly, or wrap everything in a
            // single top-level directory (e.g. file-rec-<uid>/) -- handle both.
            QString recordDir = destDir;
            if (!QDir(recordDir + "/frames").exists()) {
                QDir d(destDir);
                QStringList subdirs = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                qDebug() << "[TaggingWindow] no frames/ directly in destDir, found subdirs:" << subdirs;
                if (subdirs.size() == 1) {
                    recordDir = destDir + "/" + subdirs.first();
                }
            }
            qDebug() << "[TaggingWindow] resolved recordDir =" << recordDir
                     << " frames/ exists =" << QDir(recordDir + "/frames").exists();

            if (!QDir(recordDir + "/frames").exists()) {
                QMessageBox::warning(this, "Error",
                    "Extracted archive but no 'frames' directory found (looked in:\n" +
                    destDir + "\nand its single subdirectory, if any).");
                return;
            }

            loadRecordDirectory(recordDir);
        });
    tarProcess->start("tar", {QStringLiteral("-xzf"), archivePath});
}

void TaggingWindow::loadRecordDirectory(const QString& recordDir) {
    QString framesDir = recordDir + "/frames";
    qDebug() << "[TaggingWindow] loadRecordDirectory: framesDir =" << framesDir;
    source_ = std::make_unique<DiskImageFrameSource>(framesDir.toStdString(), "*.jpg");
    currentRecordDir_ = recordDir;

    // Reset per-record interaction/tracking state.
    stopTracking("Loading new record");
    uncheckOtherRoiTools(nullptr);
    markRecordFailedAct_->setChecked(false);
    mode_ = InteractionMode::None;
    roiDragActive_ = false;
    roiMoveActive_ = false;
    roiResizeActive_ = false;
    tags_ = lbt::TagData();

    size_t frameCount = source_->getFrameCount();
    qDebug() << "[TaggingWindow] loadRecordDirectory: isReady =" << source_->isReady()
             << " frameCount =" << static_cast<qulonglong>(frameCount);
    if (frameCount == 0) {
        QMessageBox::warning(this, "Error", "No frames found in: " + framesDir);
        frameSlider_->setEnabled(false);
        prevBtn_->setEnabled(false);
        nextBtn_->setEnabled(false);
        return;
    }

    frameSlider_->blockSignals(true);
    frameSlider_->setEnabled(true);
    frameSlider_->setMinimum(0);
    frameSlider_->setMaximum(static_cast<int>(frameCount) - 1);
    frameSlider_->setValue(0);
    frameSlider_->blockSignals(false);
    prevBtn_->setEnabled(true);
    nextBtn_->setEnabled(true);

    // Best-effort metadata for the status label -- record-*.json and study-*.json are
    // both optional/best-effort here, this is a display convenience only.
    QString recordUID;
    QString recordInfo;
    QDir dir(recordDir);
    for (const QString& name : dir.entryList(QStringList() << "record-*.json", QDir::Files)) {
        QFile f(recordDir + "/" + name);
        if (f.open(QIODevice::ReadOnly)) {
            QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
            recordUID = obj.value("recordUID").toString();
            recordInfo = QString("framesRecorded=%1, state=%2")
                .arg(obj.value("framesRecorded").toInt())
                .arg(obj.value("state").toString());
        }
        break;
    }
    QString studyInfo;
    for (const QString& name : dir.entryList(QStringList() << "study-*.json", QDir::Files)) {
        QFile f(recordDir + "/" + name);
        if (f.open(QIODevice::ReadOnly)) {
            QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
            studyInfo = QString(" | study: %1 (%2 mg)")
                .arg(obj.value("individualScientificName").toString())
                .arg(obj.value("weightInMg").toDouble());
        }
        break;
    }
    currentRecordUID_ = recordUID.isEmpty() ? QFileInfo(recordDir).fileName() : recordUID;

    // Auto-load any previously saved tags for this record (resumable tagging session).
    if (tagStore_) {
        lbt::TagData loaded;
        if (tagStore_->load(currentRecordUID_.toStdString(), loaded)) {
            tags_ = loaded;
            markRecordFailedAct_->blockSignals(true);
            markRecordFailedAct_->setChecked(tags_.recordFailed);
            markRecordFailedAct_->blockSignals(false);
            qDebug() << "[TaggingWindow] loaded existing tag data for" << currentRecordUID_
                     << ": points=" << tags_.points.size() << " roiDefined=" << tags_.roiDefined
                     << " validUntilFrame=" << tags_.validUntilFrame << " recordFailed=" << tags_.recordFailed;
            QMessageBox::information(this, "Existing Tags Found",
                QString("Found existing tag data for record %1:\n\n"
                        "  %2 tagged point(s)\n"
                        "  ROI defined: %3\n"
                        "  Valid until frame: %4\n"
                        "  Marked as complete failure: %5")
                    .arg(currentRecordUID_)
                    .arg(tags_.points.size())
                    .arg(tags_.roiDefined ? "yes" : "no")
                    .arg(tags_.validUntilFrame < 0 ? QString("(none)") : QString::number(tags_.validUntilFrame))
                    .arg(tags_.recordFailed ? "yes" : "no"));
            statusBar()->showMessage("Loaded existing tags for: " + currentRecordUID_, 5000);
        }
    }
    updateRoiToolAvailability();

    qDebug() << "[TaggingWindow] loadRecordDirectory: recordUID =" << recordUID
             << " recordInfo =" << recordInfo << " studyInfo =" << studyInfo;

    statusLabel_->setText(QString("Record: %1 | frames: %2 | %3%4")
        .arg(recordUID.isEmpty() ? QFileInfo(recordDir).fileName() : recordUID)
        .arg(frameCount)
        .arg(recordInfo)
        .arg(studyInfo));
    statusBar()->showMessage("Loaded: " + recordDir, 5000);

    showFrame(0);
}

bool TaggingWindow::isInsideRoi(QPointF pt) const {
    if (!tags_.roiDefined) return true;
    double dx = pt.x() - tags_.roiCenterX;
    double dy = pt.y() - tags_.roiCenterY;
    return std::sqrt(dx * dx + dy * dy) <= tags_.roiRadius;
}

void TaggingWindow::showFrame(int index) {
    if (!source_ || !source_->isReady()) {
        qDebug() << "[TaggingWindow] showFrame(" << index << "): source not ready, ignoring";
        return;
    }
    source_->seekToFrame(static_cast<size_t>(index));
    currentFrame_ = source_->nextFrame();
    if (!currentFrame_) {
        qDebug() << "[TaggingWindow] showFrame(" << index << "): nextFrame() returned null";
        return;
    }
    renderCurrentFrame(index);
}

void TaggingWindow::renderCurrentFrame(int index) {
    if (!currentFrame_) return;

    QImage img = ImageUtils::matToQImage(currentFrame_->getCpuMat());

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // ROI circle: committed value, or the live drag preview while defining/moving/resizing.
    bool previewActive = roiDragActive_ || roiMoveActive_ || roiResizeActive_;
    bool showRoi = tags_.roiDefined || roiDragActive_;
    if (showRoi) {
        QPointF center = QPointF(tags_.roiCenterX, tags_.roiCenterY);
        double radius = tags_.roiRadius;
        if (roiDragActive_) {
            center = QPointF(roiDragStart_);
            radius = pendingRoiRadius_;
        } else if (roiMoveActive_) {
            center = pendingRoiCenter_;
        } else if (roiResizeActive_) {
            radius = pendingRoiRadius_;
        }
        QPen roiPen(previewActive ? QColor(255, 200, 0) : QColor(0, 200, 255));
        roiPen.setWidth(2);
        if (previewActive) roiPen.setStyle(Qt::DashLine);
        painter.setPen(roiPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(center, radius, radius);
    }

    // Full trajectory of the *current tracking run*: walk backward from this frame to
    // the most recent manual seed point (inclusive) rather than an arbitrary fixed
    // window -- shows the whole pathway since the last click, not just a moving
    // 20-frame slice, so a re-click always starts a fresh, easy-to-follow trail.
    std::vector<lbt::TaggedPoint> sortedPoints(tags_.points.begin(), tags_.points.end());
    std::sort(sortedPoints.begin(), sortedPoints.end(),
              [](const lbt::TaggedPoint& a, const lbt::TaggedPoint& b) { return a.frameIndex < b.frameIndex; });

    QVector<lbt::TaggedPoint> trail;
    for (auto it = sortedPoints.rbegin(); it != sortedPoints.rend(); ++it) {
        if (it->frameIndex > index) continue;
        trail.prepend(*it);
        if (it->source == "manual") break; // seed point of this run -- stop walking back
    }

    if (trail.size() > 1) {
        for (int i = 1; i < trail.size(); ++i) {
            QPointF p1(trail[i - 1].x, trail[i - 1].y);
            QPointF p2(trail[i].x, trail[i].y);
            double segLen = QLineF(p1, p2).length();
            bool isJump = segLen > kJumpThresholdPx;
            QPen segPen(isJump ? QColor(255, 60, 60) : QColor(255, 255, 0, 200));
            segPen.setWidth(isJump ? 3 : 2);
            painter.setPen(segPen);
            painter.drawLine(p1, p2);
        }
        // Small marker at every intermediate point so individual frames (and where
        // exactly a jump happens) are visible, not just implied by the connecting line.
        painter.setPen(Qt::NoPen);
        for (int i = 0; i < trail.size() - 1; ++i) {
            painter.setBrush(QColor(255, 255, 0, 220));
            painter.drawEllipse(QPointF(trail[i].x, trail[i].y), 2.5, 2.5);
        }
    }

    // Current tracker search box, while a tracking run is actively propagating --
    // shows exactly what region CSRT is matching each tick, so a jump is visible as
    // "the box relocated to here" rather than just a mystery point displacement.
    if (trackingActive_) {
        QPen boxPen(QColor(255, 0, 255));
        boxPen.setWidth(2);
        painter.setPen(boxPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(trackerBox_.x, trackerBox_.y, trackerBox_.width, trackerBox_.height);
    }

    // Current frame's tag point, color-coded by source.
    if (const lbt::TaggedPoint* tp = tags_.pointAt(index)) {
        QColor color = (tp->source == "manual") ? QColor(255, 0, 0) : QColor(0, 255, 0);
        painter.setPen(QPen(color, 2));
        painter.setBrush(color);
        painter.drawEllipse(QPointF(tp->x, tp->y), 5, 5);
    }

    painter.end();

    // Scale the (already-annotated) frame down to fit the available viewport when it's
    // too big to show at native resolution -- never upscale. All overlay drawing above
    // happened in native image pixel space (matching how tags_ stores coordinates), so
    // this is a single uniform resize of the whole composited image; displayScale_
    // records the factor so mouse coordinates (which arrive in displayed-pixel space)
    // can be converted back to native image pixels in toImageCoords().
    QSize nativeSize = img.size();
    QSize viewportSize = imageScroll_->viewport()->size();
    QSize targetSize = nativeSize;
    if (nativeSize.width() > viewportSize.width() || nativeSize.height() > viewportSize.height()) {
        targetSize = nativeSize.scaled(viewportSize, Qt::KeepAspectRatio);
    }

    QPixmap pixmap = QPixmap::fromImage(img);
    if (targetSize != nativeSize && targetSize.width() > 0 && targetSize.height() > 0) {
        pixmap = pixmap.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    displayScale_ = nativeSize.width() > 0 ? static_cast<double>(pixmap.width()) / nativeSize.width() : 1.0;

    imageLabel_->setPixmap(pixmap);
    imageLabel_->resize(pixmap.size());

    frameLabel_->setText(QString("Frame: %1/%2").arg(index).arg(frameSlider_->maximum()));
}

QPoint TaggingWindow::toImageCoords(QPoint labelPos) const {
    double scale = (displayScale_ > 0.0) ? displayScale_ : 1.0;
    return QPoint(static_cast<int>(labelPos.x() / scale), static_cast<int>(labelPos.y() / scale));
}

void TaggingWindow::onFrameSliderChanged(int value) {
    qDebug() << "[TaggingWindow] onFrameSliderChanged:" << value;
    showFrame(value);
}

void TaggingWindow::onPrevFrame() {
    qDebug() << "[TaggingWindow] onPrevFrame clicked";
    int v = frameSlider_->value();
    if (v > frameSlider_->minimum()) {
        frameSlider_->setValue(v - 1);
    }
}

void TaggingWindow::onNextFrame() {
    qDebug() << "[TaggingWindow] onNextFrame clicked";
    int v = frameSlider_->value();
    if (v < frameSlider_->maximum()) {
        frameSlider_->setValue(v + 1);
    }
}

void TaggingWindow::setInteractionMode(InteractionMode mode) {
    mode_ = mode;
    qDebug() << "[TaggingWindow] interaction mode ->" << static_cast<int>(mode);
}

void TaggingWindow::uncheckOtherRoiTools(QAction* except) {
    for (QAction* a : {defineRoiAct_, moveRoiAct_, resizeRoiAct_, tagAnimalAct_, manualTagAct_}) {
        if (a != except) a->setChecked(false);
    }
}

void TaggingWindow::updateRoiToolAvailability() {
    moveRoiAct_->setEnabled(tags_.roiDefined);
    resizeRoiAct_->setEnabled(tags_.roiDefined);
}

void TaggingWindow::onDefineRoiToggled(bool checked) {
    qDebug() << "[TaggingWindow] onDefineRoiToggled:" << checked;
    if (checked) {
        uncheckOtherRoiTools(defineRoiAct_);
        if (trackingActive_) stopTracking("Switched to Define ROI mode");
        setInteractionMode(InteractionMode::DefiningROI);
        statusBar()->showMessage("Click-drag on the image to define the ROI circle.", 4000);
    } else {
        roiDragActive_ = false;
        if (mode_ == InteractionMode::DefiningROI) setInteractionMode(InteractionMode::None);
    }
}

void TaggingWindow::onMoveRoiToggled(bool checked) {
    qDebug() << "[TaggingWindow] onMoveRoiToggled:" << checked;
    if (checked) {
        uncheckOtherRoiTools(moveRoiAct_);
        if (trackingActive_) stopTracking("Switched to Move ROI mode");
        setInteractionMode(InteractionMode::MovingROI);
        statusBar()->showMessage("Drag anywhere on the image to move the ROI circle.", 4000);
    } else {
        roiMoveActive_ = false;
        if (mode_ == InteractionMode::MovingROI) setInteractionMode(InteractionMode::None);
    }
}

void TaggingWindow::onResizeRoiToggled(bool checked) {
    qDebug() << "[TaggingWindow] onResizeRoiToggled:" << checked;
    if (checked) {
        uncheckOtherRoiTools(resizeRoiAct_);
        if (trackingActive_) stopTracking("Switched to Resize ROI mode");
        setInteractionMode(InteractionMode::ResizingROI);
        statusBar()->showMessage("Drag on the image to resize the ROI circle.", 4000);
    } else {
        roiResizeActive_ = false;
        if (mode_ == InteractionMode::ResizingROI) setInteractionMode(InteractionMode::None);
    }
}

void TaggingWindow::onTagAnimalToggled(bool checked) {
    qDebug() << "[TaggingWindow] onTagAnimalToggled:" << checked;
    if (checked) {
        uncheckOtherRoiTools(tagAnimalAct_);
        setInteractionMode(InteractionMode::Tagging);
        statusBar()->showMessage("Click the animal to tag/seed the tracker.", 4000);
    } else {
        if (trackingActive_) stopTracking("Exited Tag Animal mode");
        if (mode_ == InteractionMode::Tagging) setInteractionMode(InteractionMode::None);
    }
}

void TaggingWindow::onManualTagToggled(bool checked) {
    qDebug() << "[TaggingWindow] onManualTagToggled:" << checked;
    if (checked) {
        uncheckOtherRoiTools(manualTagAct_);
        if (trackingActive_) stopTracking("Switched to Manual Tag mode");
        setInteractionMode(InteractionMode::ManualTagging);
        statusBar()->showMessage(
            "Click the animal's center on each frame; auto-advances to the next frame after every click.", 4000);
    } else {
        if (mode_ == InteractionMode::ManualTagging) setInteractionMode(InteractionMode::None);
    }
}

void TaggingWindow::onImagePressed(QPoint labelPos) {
    QPoint pos = toImageCoords(labelPos);
    if (mode_ == InteractionMode::DefiningROI) {
        roiDragActive_ = true;
        roiDragStart_ = pos;
        pendingRoiRadius_ = 0.0;
        renderCurrentFrame(frameSlider_->value());
    } else if (mode_ == InteractionMode::MovingROI && tags_.roiDefined) {
        roiMoveActive_ = true;
        roiDragStart_ = pos;
        roiMoveDragStartCenter_ = QPointF(tags_.roiCenterX, tags_.roiCenterY);
        pendingRoiCenter_ = roiMoveDragStartCenter_;
        renderCurrentFrame(frameSlider_->value());
    } else if (mode_ == InteractionMode::ResizingROI && tags_.roiDefined) {
        roiResizeActive_ = true;
        pendingRoiRadius_ = QLineF(QPointF(tags_.roiCenterX, tags_.roiCenterY), pos).length();
        renderCurrentFrame(frameSlider_->value());
    } else if (mode_ == InteractionMode::Tagging) {
        seedTrackerAt(frameSlider_->value(), pos);
    } else if (mode_ == InteractionMode::ManualTagging) {
        tagCurrentFrameManually(frameSlider_->value(), pos);
    }
}

void TaggingWindow::onImageMoved(QPoint labelPos) {
    QPoint pos = toImageCoords(labelPos);
    if (mode_ == InteractionMode::DefiningROI && roiDragActive_) {
        pendingRoiRadius_ = QLineF(roiDragStart_, pos).length();
        renderCurrentFrame(frameSlider_->value());
    } else if (mode_ == InteractionMode::MovingROI && roiMoveActive_) {
        QPoint delta = pos - roiDragStart_;
        pendingRoiCenter_ = roiMoveDragStartCenter_ + QPointF(delta.x(), delta.y());
        renderCurrentFrame(frameSlider_->value());
    } else if (mode_ == InteractionMode::ResizingROI && roiResizeActive_) {
        pendingRoiRadius_ = QLineF(QPointF(tags_.roiCenterX, tags_.roiCenterY), pos).length();
        renderCurrentFrame(frameSlider_->value());
    }
}

void TaggingWindow::onImageReleased(QPoint labelPos) {
    QPoint pos = toImageCoords(labelPos);
    if (mode_ == InteractionMode::DefiningROI && roiDragActive_) {
        tags_.roiCenterX = roiDragStart_.x();
        tags_.roiCenterY = roiDragStart_.y();
        tags_.roiRadius = QLineF(roiDragStart_, pos).length();
        tags_.roiDefined = true;
        roiDragActive_ = false;
        updateRoiToolAvailability();
        qDebug() << "[TaggingWindow] ROI defined: center=(" << tags_.roiCenterX << "," << tags_.roiCenterY
                 << ") radius=" << tags_.roiRadius;
        statusBar()->showMessage(
            QString("ROI defined: center=(%1,%2) radius=%3")
                .arg(tags_.roiCenterX).arg(tags_.roiCenterY).arg(tags_.roiRadius),
            4000);
        renderCurrentFrame(frameSlider_->value());
    } else if (mode_ == InteractionMode::MovingROI && roiMoveActive_) {
        tags_.roiCenterX = pendingRoiCenter_.x();
        tags_.roiCenterY = pendingRoiCenter_.y();
        roiMoveActive_ = false;
        qDebug() << "[TaggingWindow] ROI moved: center=(" << tags_.roiCenterX << "," << tags_.roiCenterY << ")";
        statusBar()->showMessage(
            QString("ROI moved: center=(%1,%2)").arg(tags_.roiCenterX).arg(tags_.roiCenterY), 4000);
        renderCurrentFrame(frameSlider_->value());
    } else if (mode_ == InteractionMode::ResizingROI && roiResizeActive_) {
        tags_.roiRadius = QLineF(QPointF(tags_.roiCenterX, tags_.roiCenterY), pos).length();
        roiResizeActive_ = false;
        qDebug() << "[TaggingWindow] ROI resized: radius=" << tags_.roiRadius;
        statusBar()->showMessage(QString("ROI resized: radius=%1").arg(tags_.roiRadius), 4000);
        renderCurrentFrame(frameSlider_->value());
    }
}

void TaggingWindow::seedTrackerAt(int frameIndex, QPoint clickPos) {
    if (!currentFrame_) return;
    cv::Mat mat = currentFrame_->getCpuMat();

    int half = kTagBoxSize / 2;
    int x = std::max(0, std::min(clickPos.x() - half, mat.cols - kTagBoxSize));
    int y = std::max(0, std::min(clickPos.y() - half, mat.rows - kTagBoxSize));
    int w = std::min(kTagBoxSize, mat.cols - x);
    int h = std::min(kTagBoxSize, mat.rows - y);
    cv::Rect box(x, y, w, h);

    tracker_ = cv::TrackerCSRT::create();
    tracker_->init(mat, box);
    trackerBox_ = box;

    tags_.setPoint(frameIndex, clickPos.x(), clickPos.y(), "manual");
    trackingActive_ = true;
    consecutiveRoiViolations_ = 0;
    stopTrackingAct_->setEnabled(true);

    qDebug() << "[TaggingWindow] seeded tracker at frame" << frameIndex << "pos=" << clickPos
             << " box=" << box.x << box.y << box.width << box.height;

    if (!isInsideRoi(QPointF(clickPos))) {
        qDebug() << "[TaggingWindow] WARNING: seed click is outside the defined ROI";
        statusBar()->showMessage(
            QString("Warning: this tag point is outside the ROI -- tracking from frame %1 "
                    "may stop almost immediately.").arg(frameIndex),
            6000);
    } else {
        statusBar()->showMessage(QString("Tagged frame %1, tracking forward...").arg(frameIndex), 3000);
    }

    renderCurrentFrame(frameIndex);
    trackingTimer_->start(kTrackingTickMs);
}

void TaggingWindow::tagCurrentFrameManually(int frameIndex, QPoint clickPos) {
    if (!currentFrame_) return;
    tags_.setPoint(frameIndex, clickPos.x(), clickPos.y(), "manual");
    qDebug() << "[TaggingWindow] manually tagged frame" << frameIndex << "pos=" << clickPos;

    if (!isInsideRoi(QPointF(clickPos))) {
        qDebug() << "[TaggingWindow] WARNING: manual tag is outside the defined ROI";
        statusBar()->showMessage(
            QString("Warning: frame %1 tagged outside the ROI.").arg(frameIndex), 4000);
    } else {
        statusBar()->showMessage(QString("Tagged frame %1.").arg(frameIndex), 2000);
    }

    renderCurrentFrame(frameIndex);

    // Auto-advance so the next click lands on the next frame -- the whole point of
    // manual mode is a rapid click-click-click workflow for individuals too fast for
    // the tracker, so the user shouldn't have to reach for Next between every click.
    if (frameIndex < frameSlider_->maximum()) {
        frameSlider_->setValue(frameIndex + 1);
    }
}

void TaggingWindow::stopTracking(const QString& reason) {
    trackingTimer_->stop();
    bool wasActive = trackingActive_;
    trackingActive_ = false;
    consecutiveRoiViolations_ = 0;
    tracker_.release();
    stopTrackingAct_->setEnabled(false);
    if (wasActive) {
        qDebug() << "[TaggingWindow] stopTracking:" << reason;
        statusBar()->showMessage(reason + " -- click the target to re-tag and resume.", 6000);
    }
}

void TaggingWindow::onStopTracking() {
    stopTracking("Tracking stopped by user");
}

void TaggingWindow::onTrackingTick() {
    if (!trackingActive_ || !tracker_ || !source_) return;

    int nextIndex = frameSlider_->value() + 1;
    if (nextIndex > frameSlider_->maximum()) {
        stopTracking("Reached end of record");
        return;
    }

    source_->seekToFrame(static_cast<size_t>(nextIndex));
    currentFrame_ = source_->nextFrame();
    if (!currentFrame_) {
        stopTracking("No more frames");
        return;
    }

    cv::Mat mat = currentFrame_->getCpuMat();
    cv::Rect box = trackerBox_;
    bool ok = tracker_->update(mat, box);

    if (!ok) {
        // A real tracker failure is a trustworthy signal on its own -- no tolerance needed.
        renderCurrentFrame(nextIndex);
        frameSlider_->blockSignals(true);
        frameSlider_->setValue(nextIndex);
        frameSlider_->blockSignals(false);
        stopTracking("Tracker lost the target");
        return;
    }

    QPointF center(box.x + box.width / 2.0, box.y + box.height / 2.0);
    bool inside = isInsideRoi(center);
    trackerBox_ = box;

    frameSlider_->blockSignals(true);
    frameSlider_->setValue(nextIndex);
    frameSlider_->blockSignals(false);

    if (inside) {
        consecutiveRoiViolations_ = 0;
        tags_.setPoint(nextIndex, center.x(), center.y(), "tracked");
        renderCurrentFrame(nextIndex);
        trackingTimer_->start(kTrackingTickMs);
    } else {
        // Tolerate a few consecutive borderline/out-of-ROI frames (tracker jitter near the
        // boundary, or an ROI drawn a bit tight) before treating it as real drift -- still
        // record the point while within tolerance so a brief excursion doesn't lose data.
        ++consecutiveRoiViolations_;
        tags_.setPoint(nextIndex, center.x(), center.y(), "tracked");
        renderCurrentFrame(nextIndex);
        if (consecutiveRoiViolations_ >= kRoiViolationTolerance) {
            stopTracking(QString("Tracked point left the ROI for %1 consecutive frames")
                             .arg(consecutiveRoiViolations_));
        } else {
            qDebug() << "[TaggingWindow] frame" << nextIndex << "outside ROI ("
                     << consecutiveRoiViolations_ << "/" << kRoiViolationTolerance << "), continuing";
            trackingTimer_->start(kTrackingTickMs);
        }
    }
}

void TaggingWindow::onMarkInvalidFromHere() {
    int idx = frameSlider_->value();
    tags_.validUntilFrame = idx - 1;
    qDebug() << "[TaggingWindow] marked invalid from frame" << idx
             << " -> validUntilFrame=" << tags_.validUntilFrame;
    statusBar()->showMessage(
        QString("Marked frames %1+ as invalid (valid up to frame %2).").arg(idx).arg(tags_.validUntilFrame),
        5000);
}

void TaggingWindow::onMarkRecordFailedToggled(bool checked) {
    tags_.recordFailed = checked;
    qDebug() << "[TaggingWindow] recordFailed ->" << checked;
    statusBar()->showMessage(
        checked ? "Record marked as a complete failure (won't be used for scoring)."
                : "Complete-failure mark removed.",
        5000);
}

void TaggingWindow::onSaveTags() {
    if (currentRecordDir_.isEmpty() || currentRecordUID_.isEmpty()) {
        QMessageBox::information(this, "Save Tags", "No record loaded.");
        return;
    }
    if (!tagStore_) {
        QMessageBox::warning(this, "Save Tags", "Tag data store is not open -- see the warning shown at startup.");
        return;
    }
    try {
        tagStore_->save(currentRecordUID_.toStdString(), tags_);
        qDebug() << "[TaggingWindow] saved" << tags_.points.size() << "point(s) for" << currentRecordUID_;
        statusBar()->showMessage(
            QString("Tags saved for %1 (%2 points).").arg(currentRecordUID_).arg(tags_.points.size()), 5000);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Save Tags", QString("Failed to save tags: %1").arg(e.what()));
    }
}
