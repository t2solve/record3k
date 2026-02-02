#include "ConfigDialog.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFileDialog>
#include <QLabel>

ConfigDialog::ConfigDialog(QWidget* parent)
    : QDialog(parent)
    , settings_("record3k", "pipelineviewer")
{
    setWindowTitle("Configuration");
    
    auto* layout = new QVBoxLayout(this);
    auto* formLayout = new QFormLayout;
    
    // API URL
    apiUrlEdit_ = new QLineEdit;
    apiUrlEdit_->setPlaceholderText("http://127.0.0.1:8080");
    formLayout->addRow("API Server URL:", apiUrlEdit_);
    
    // API Token
    apiTokenEdit_ = new QLineEdit;
    apiTokenEdit_->setPlaceholderText("default-dev-token");
    apiTokenEdit_->setEchoMode(QLineEdit::Password);
    formLayout->addRow("API Token:", apiTokenEdit_);
    
    // Working directory
    auto* workDirLayout = new QHBoxLayout;
    workDirEdit_ = new QLineEdit;
    workDirEdit_->setPlaceholderText("/tmp/pipelineviewer");
    auto* browseBtn = new QPushButton("Browse...");
    connect(browseBtn, &QPushButton::clicked, this, &ConfigDialog::onBrowseWorkDir);
    workDirLayout->addWidget(workDirEdit_);
    workDirLayout->addWidget(browseBtn);
    formLayout->addRow("Working Directory:", workDirLayout);
    
    layout->addLayout(formLayout);
    
    // Info label
    auto* infoLabel = new QLabel(
        "The working directory is used to store downloaded records and temporary files."
    );
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: gray; font-size: 10pt;");
    layout->addWidget(infoLabel);
    
    // Buttons
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &ConfigDialog::onAccepted);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);
    
    loadSettings();
}

QString ConfigDialog::apiUrl() const {
    QString url = apiUrlEdit_->text().trimmed();
    return url.isEmpty() ? "http://127.0.0.1:8080" : url;
}

QString ConfigDialog::apiToken() const {
    QString token = apiTokenEdit_->text().trimmed();
    return token.isEmpty() ? "default-dev-token" : token;
}

QString ConfigDialog::workDir() const {
    QString dir = workDirEdit_->text().trimmed();
    return dir.isEmpty() ? "/tmp/pipelineviewer" : dir;
}

void ConfigDialog::setApiUrl(const QString& url) {
    apiUrlEdit_->setText(url);
}

void ConfigDialog::setApiToken(const QString& token) {
    apiTokenEdit_->setText(token);
}

void ConfigDialog::setWorkDir(const QString& dir) {
    workDirEdit_->setText(dir);
}

void ConfigDialog::loadSettings() {
    apiUrlEdit_->setText(settings_.value("api/url", "http://127.0.0.1:8080").toString());
    apiTokenEdit_->setText(settings_.value("api/token", "default-dev-token").toString());
    workDirEdit_->setText(settings_.value("working/directory", "/tmp/pipelineviewer").toString());
}

void ConfigDialog::saveSettings() {
    settings_.setValue("api/url", apiUrl());
    settings_.setValue("api/token", apiToken());
    settings_.setValue("working/directory", workDir());
    settings_.sync();
}

void ConfigDialog::onBrowseWorkDir() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Working Directory", 
                                                     workDirEdit_->text());
    if (!dir.isEmpty()) {
        workDirEdit_->setText(dir);
    }
}

void ConfigDialog::onAccepted() {
    saveSettings();
    accept();
}
