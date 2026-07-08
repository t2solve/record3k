#include "LandingDialog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDebug>

LandingDialog::LandingDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("record3k - Pipeline Viewer");

    auto* layout = new QVBoxLayout(this);

    auto* titleLabel = new QLabel("What would you like to do?");
    titleLabel->setStyleSheet("font-size: 14pt; font-weight: bold;");
    layout->addWidget(titleLabel);

    auto* debugBtn = new QPushButton("Debug Pipeline");
    debugBtn->setMinimumHeight(48);
    connect(debugBtn, &QPushButton::clicked, this, [this]() {
        qDebug() << "[LandingDialog] Debug Pipeline selected";
        mode_ = Mode::DebugPipeline;
        accept();
    });
    layout->addWidget(debugBtn);

    auto* debugInfo = new QLabel(
        "Step through a recorded pipeline, inspect intermediate frames, tune parameters.");
    debugInfo->setWordWrap(true);
    debugInfo->setStyleSheet("color: gray; font-size: 9pt;");
    layout->addWidget(debugInfo);

    auto* tagBtn = new QPushButton("Tag Record");
    tagBtn->setMinimumHeight(48);
    connect(tagBtn, &QPushButton::clicked, this, [this]() {
        qDebug() << "[LandingDialog] Tag Record selected";
        mode_ = Mode::TagRecord;
        accept();
    });
    layout->addWidget(tagBtn);

    auto* tagInfo = new QLabel(
        "Open a recorded .tar.gz archive and mark ground-truth data for real-record "
        "pipeline tuning.");
    tagInfo->setWordWrap(true);
    tagInfo->setStyleSheet("color: gray; font-size: 9pt;");
    layout->addWidget(tagInfo);

    setMinimumWidth(360);
}
