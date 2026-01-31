#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <functional>

/**
 * ApiClient - HTTP client for interacting with the record3k API server
 * 
 * Provides methods to:
 * - List and get records
 * - List and get file info
 * - Download files
 */
class ApiClient : public QObject {
    Q_OBJECT
public:
    explicit ApiClient(QObject* parent = nullptr);
    
    /**
     * Set the API base URL (e.g., "http://127.0.0.1:8080")
     */
    void setBaseUrl(const QString& url);
    QString baseUrl() const { return baseUrl_; }
    
    /**
     * Test API connection
     * @param callback Called with true if connection successful, false otherwise
     */
    void ping(std::function<void(bool)> callback);
    
    /**
     * List all records
     * @param callback Called with JSON array of records or empty array on error
     */
    void listRecords(std::function<void(const QJsonArray&)> callback);
    
    /**
     * Get a specific record by UID
     * @param recordUID Record identifier
     * @param callback Called with JSON object or empty object on error
     */
    void getRecord(const QString& recordUID, std::function<void(const QJsonObject&)> callback);
    
    /**
     * List all files
     * @param callback Called with JSON array of files or empty array on error
     */
    void listFiles(std::function<void(const QJsonArray&)> callback);
    
    /**
     * Get file info by UID
     * @param fileUID File identifier
     * @param callback Called with JSON object or empty object on error
     */
    void getFileInfo(const QString& fileUID, std::function<void(const QJsonObject&)> callback);
    
    /**
     * Download a file
     * @param fileUID File identifier (will construct download URL)
     * @param destPath Destination file path
     * @param progressCallback Called periodically with (bytesReceived, bytesTotal)
     * @param finishedCallback Called when complete with success boolean
     */
    void downloadFile(const QString& fileUID, const QString& destPath,
                     std::function<void(qint64, qint64)> progressCallback,
                     std::function<void(bool)> finishedCallback);
    
    /**
     * Upload a pipeline JSON file to the API
     * @param filePath Path to the pipeline JSON file
     * @param callback Called with success boolean and response message
     */
    void uploadPipeline(const QString& filePath, std::function<void(bool, const QString&)> callback);

signals:
    void error(const QString& message);

private:
    QNetworkAccessManager* netMgr_;
    QString baseUrl_;
    QString entryPointString_;
    
    void handleJsonReply(QNetworkReply* reply, std::function<void(const QJsonDocument&)> callback);
};
