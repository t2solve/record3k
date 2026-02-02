#include "ApiClient.h"
#include <QNetworkRequest>
#include <QHttpMultiPart>
#include <QFile>
#include <QFileInfo>
#include <QDir>

ApiClient::ApiClient(QObject* parent)
    : QObject(parent)
    , netMgr_(new QNetworkAccessManager(this))
    , baseUrl_("http://127.0.0.1:8080")
    , apiToken_("default-dev-token")
    , entryPointString_("")
{
}

void ApiClient::setBaseUrl(const QString& url) {
    baseUrl_ = url;
    if (baseUrl_.endsWith('/')) {
        baseUrl_.chop(1);
    }
}

void ApiClient::setApiToken(const QString& token) {
    apiToken_ = token;
}

void ApiClient::ping(std::function<void(bool)> callback) {
    QNetworkRequest req = QNetworkRequest(QUrl(baseUrl_ + entryPointString_ + "/info/cameras/list"));
    req.setRawHeader("Authorization", QString("Bearer %1").arg(apiToken_).toUtf8());
    QNetworkReply* reply = netMgr_->get(req);
    
    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        bool success = (reply->error() == QNetworkReply::NoError);
        reply->deleteLater();
        callback(success);
    });
}

void ApiClient::listRecords(std::function<void(const QJsonArray&)> callback) {
    QNetworkRequest req = QNetworkRequest(QUrl(baseUrl_ + entryPointString_ + "/info/records/list"));
    req.setRawHeader("Authorization", QString("Bearer %1").arg(apiToken_).toUtf8());
    QNetworkReply* reply = netMgr_->get(req);
    
    handleJsonReply(reply, [callback](const QJsonDocument& doc) {
        if (doc.isArray()) {
            callback(doc.array());
        } else {
            callback(QJsonArray());
        }
    });
}

void ApiClient::getRecord(const QString& recordUID, std::function<void(const QJsonObject&)> callback) {
    QString url = baseUrl_ + entryPointString_ + "/info/records/get/" + recordUID;
    QNetworkRequest req = QNetworkRequest(QUrl(url));
    req.setRawHeader("Authorization", QString("Bearer %1").arg(apiToken_).toUtf8());
    QNetworkReply* reply = netMgr_->get(req);
    
    handleJsonReply(reply, [callback](const QJsonDocument& doc) {
        if (doc.isObject()) {
            callback(doc.object());
        } else {
            callback(QJsonObject());
        }
    });
}

void ApiClient::listFiles(std::function<void(const QJsonArray&)> callback) {
    QNetworkRequest req = QNetworkRequest(QUrl(baseUrl_ + entryPointString_ + "/info/files/list"));
    req.setRawHeader("Authorization", QString("Bearer %1").arg(apiToken_).toUtf8());
    QNetworkReply* reply = netMgr_->get(req);
    
    handleJsonReply(reply, [callback](const QJsonDocument& doc) {
        if (doc.isArray()) {
            callback(doc.array());
        } else {
            callback(QJsonArray());
        }
    });
}

void ApiClient::getFileInfo(const QString& fileUID, std::function<void(const QJsonObject&)> callback) {
    QString url = baseUrl_ + entryPointString_ + "/info/files/get/" + fileUID;
    QNetworkRequest req = QNetworkRequest(QUrl(url));
    req.setRawHeader("Authorization", QString("Bearer %1").arg(apiToken_).toUtf8());
    QNetworkReply* reply = netMgr_->get(req);
    
    handleJsonReply(reply, [callback](const QJsonDocument& doc) {
        if (doc.isObject()) {
            callback(doc.object());
        } else {
            callback(QJsonObject());
        }
    });
}

void ApiClient::downloadFile(const QString& fileUID, const QString& destPath,
                             std::function<void(qint64, qint64)> progressCallback,
                             std::function<void(bool)> finishedCallback) {
    // Download from /get/file/binary/{fileUID} (not under /api/v1)
    QString url = baseUrl_ + "/get/file/binary/" + fileUID;
    QNetworkRequest req = QNetworkRequest(QUrl(url));
    req.setRawHeader("Authorization", QString("Bearer %1").arg(apiToken_).toUtf8());
    QNetworkReply* reply = netMgr_->get(req);
    
    // Progress tracking
    if (progressCallback) {
        connect(reply, &QNetworkReply::downloadProgress, this,
                [progressCallback](qint64 received, qint64 total) {
            progressCallback(received, total);
        });
    }
    
    // Completion handling
    connect(reply, &QNetworkReply::finished, this, [this, reply, destPath, finishedCallback]() {
        bool success = false;
        
        if (reply->error() == QNetworkReply::NoError) {
            // Ensure destination directory exists
            QFileInfo fileInfo(destPath);
            QDir().mkpath(fileInfo.absolutePath());
            
            // Write to file
            QFile file(destPath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(reply->readAll());
                file.close();
                success = true;
            } else {
                emit error("Failed to write file: " + destPath);
            }
        } else {
            emit error("Download failed: " + reply->errorString());
        }
        
        reply->deleteLater();
        if (finishedCallback) {
            finishedCallback(success);
        }
    });
}

void ApiClient::uploadPipeline(const QString& filePath, std::function<void(bool, const QString&)> callback) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit error("Failed to open pipeline file: " + filePath);
        callback(false, "Failed to open file");
        return;
    }
    
    QByteArray fileData = file.readAll();
    file.close();
    
    // Create multipart form data
    QHttpMultiPart* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    
    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/json"));
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
        QVariant(QString("form-data; name=\"pipeline\"; filename=\"%1\"")
        .arg(QFileInfo(filePath).fileName())));
    filePart.setBody(fileData);
    multiPart->append(filePart);
    
    QString url = baseUrl_ + "/add/pipeline";
    QNetworkRequest req = QNetworkRequest(QUrl(url));
    req.setRawHeader("Authorization", QString("Bearer %1").arg(apiToken_).toUtf8());
    
    QNetworkReply* reply = netMgr_->post(req, multiPart);
    multiPart->setParent(reply); // Delete multiPart with reply
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        bool success = false;
        QString message;
        
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
            
            if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                QJsonObject obj = doc.object();
                if (obj.contains("pipelineUID")) {
                    success = true;
                    message = QString("Pipeline uploaded successfully!\nUID: %1\nSteps: %2")
                        .arg(obj["pipelineUID"].toString())
                        .arg(obj["stepCount"].toInt());
                } else if (obj.contains("error")) {
                    message = obj["error"].toString();
                } else {
                    message = "Unknown response from server";
                }
            } else {
                message = "Failed to parse server response";
            }
        } else {
            message = "Upload failed: " + reply->errorString();
            emit error(message);
        }
        
        reply->deleteLater();
        callback(success, message);
    });
}

void ApiClient::handleJsonReply(QNetworkReply* reply, std::function<void(const QJsonDocument&)> callback) {
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
            
            if (parseError.error == QJsonParseError::NoError) {
                callback(doc);
            } else {
                emit error("JSON parse error: " + parseError.errorString());
                callback(QJsonDocument());
            }
        } else {
            emit error("Network error: " + reply->errorString());
            callback(QJsonDocument());
        }
        reply->deleteLater();
    });
}
