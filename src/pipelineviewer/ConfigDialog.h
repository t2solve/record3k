#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QSettings>

/**
 * ConfigDialog - Settings dialog for pipelineviewer
 * 
 * Allows configuring:
 * - API server URL
 * - Working directory
 */
class ConfigDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConfigDialog(QWidget* parent = nullptr);
    
    QString apiUrl() const;
    QString apiToken() const;
    QString workDir() const;
    
    void setApiUrl(const QString& url);
    void setApiToken(const QString& token);
    void setWorkDir(const QString& dir);
    
    /**
     * Load settings from QSettings
     */
    void loadSettings();
    
    /**
     * Save settings to QSettings
     */
    void saveSettings();

private slots:
    void onBrowseWorkDir();
    void onAccepted();

private:
    QLineEdit* apiUrlEdit_;
    QLineEdit* apiTokenEdit_;
    QLineEdit* workDirEdit_;
    QSettings settings_;
};
