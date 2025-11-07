#pragma once

#include <string>
#include <optional>
#include <json/json.h>

namespace api {

// Helper: set value if present
inline void setIfPresent(Json::Value& obj, const char* key, const std::optional<std::string>& v) {
    if (v && !v->empty()) obj[key] = *v;
}

struct FileInfo {
    std::string fileUID;
    std::string datetimeCreated; // ISO-8601 string
    std::string status;          // e.g., active/archived
    std::optional<std::string> info; // free-form

    Json::Value toJson() const {
        Json::Value j;
        j["fileUID"] = fileUID;
        j["datetimeCreated"] = datetimeCreated;
        j["status"] = status;
        setIfPresent(j, "info", info);
        return j;
    }
    static FileInfo fromJson(const Json::Value& j) {
        FileInfo x;
        x.fileUID = j.get("fileUID", "").asString();
        x.datetimeCreated = j.get("datetimeCreated", "").asString();
        x.status = j.get("status", "").asString();
        if (j.isMember("info")) x.info = j["info"].asString();
        return x;
    }
};

struct CameraInfo {
    std::string camUID;
    std::string macAddress;   // keeping conventional spelling
    std::optional<std::string> description;
    std::string status;

    Json::Value toJson() const {
        Json::Value j;
        j["camUID"] = camUID;
        j["macAddress"] = macAddress;
        j["status"] = status;
        setIfPresent(j, "description", description);
        return j;
    }
    static CameraInfo fromJson(const Json::Value& j) {
        CameraInfo x;
        x.camUID = j.get("camUID", "").asString();
        x.macAddress = j.get("macAddress", "").asString();
        x.status = j.get("status", "").asString();
        if (j.isMember("description")) x.description = j["description"].asString();
        return x;
    }
};

struct PipelineInfo {
    std::string pipelineUID;
    std::optional<std::string> description;
    std::string status;
    std::string dateBuild;   // ISO date/time
    std::string fileUID;     // link to compressed xml (tar.gz)

    Json::Value toJson() const {
        Json::Value j;
        j["pipelineUID"] = pipelineUID;
        j["status"] = status;
        j["dateBuild"] = dateBuild;
        j["fileUID"] = fileUID;
        setIfPresent(j, "description", description);
        return j;
    }
    static PipelineInfo fromJson(const Json::Value& j) {
        PipelineInfo x;
        x.pipelineUID = j.get("pipelineUID", "").asString();
        x.status = j.get("status", "").asString();
        x.dateBuild = j.get("dateBuild", "").asString();
        x.fileUID = j.get("fileUID", "").asString();
        if (j.isMember("description")) x.description = j["description"].asString();
        return x;
    }
};

struct CalibrationInfo {
    std::string calibrationUID;
    std::string cameraUID;
    std::optional<std::string> description;
    std::string status;
    std::string datetimeCreated;
    std::string fileUID; // link to compressed xml

    Json::Value toJson() const {
        Json::Value j;
        j["calibrationUID"] = calibrationUID;
        j["cameraUID"] = cameraUID;
        j["status"] = status;
        j["datetimeCreated"] = datetimeCreated;
        j["fileUID"] = fileUID;
        setIfPresent(j, "description", description);
        return j;
    }
    static CalibrationInfo fromJson(const Json::Value& j) {
        CalibrationInfo x;
        x.calibrationUID = j.get("calibrationUID", "").asString();
        x.cameraUID = j.get("cameraUID", "").asString();
        x.status = j.get("status", "").asString();
        x.datetimeCreated = j.get("datetimeCreated", "").asString();
        x.fileUID = j.get("fileUID", "").asString();
        if (j.isMember("description")) x.description = j["description"].asString();
        return x;
    }
};

struct StudyMetaInfo {
    std::string studyInfoUID;
    std::optional<std::string> description;
    std::string individualScientificName;
    double weightInMg{0.0};

    Json::Value toJson() const {
        Json::Value j;
        j["studyInfoUID"] = studyInfoUID;
        j["individualScientificName"] = individualScientificName;
        j["weightInMg"] = weightInMg;
        setIfPresent(j, "description", description);
        return j;
    }
    static StudyMetaInfo fromJson(const Json::Value& j) {
        StudyMetaInfo x;
        x.studyInfoUID = j.get("studyInfoUID", "").asString();
        x.individualScientificName = j.get("individualScientificName", "").asString();
        x.weightInMg = j.get("weightInMg", 0.0).asDouble();
        if (j.isMember("description")) x.description = j["description"].asString();
        return x;
    }
};

struct RecordInfo {
    std::string recordUID;
    std::string calibrationUID;
    bool flagIsPipelineTest{false};
    std::string pipelineUID;
    std::string studyUID;
    std::string status;
    double durationInSeconds{0.0};
    std::string datetimeStart;
    std::string datetimeEnd;
    std::string fileUID; // link to compressed xml

    Json::Value toJson() const {
        Json::Value j;
        j["recordUID"] = recordUID;
        j["calibrationUID"] = calibrationUID;
        j["flagIsPipelineTest"] = flagIsPipelineTest;
        j["pipelineUID"] = pipelineUID;
        j["studyUID"] = studyUID;
        j["status"] = status;
        j["durationInSeconds"] = durationInSeconds;
        j["datetimeStart"] = datetimeStart;
        j["datetimeEnd"] = datetimeEnd;
        j["fileUID"] = fileUID;
        return j;
    }
    static RecordInfo fromJson(const Json::Value& j) {
        RecordInfo x;
        x.recordUID = j.get("recordUID", "").asString();
        x.calibrationUID = j.get("calibrationUID", "").asString();
        x.flagIsPipelineTest = j.get("flagIsPipelineTest", false).asBool();
        x.pipelineUID = j.get("pipelineUID", "").asString();
        x.studyUID = j.get("studyUID", "").asString();
        x.status = j.get("status", "").asString();
        x.durationInSeconds = j.get("durationInSeconds", 0.0).asDouble();
        x.datetimeStart = j.get("datetimeStart", "").asString();
        x.datetimeEnd = j.get("datetimeEnd", "").asString();
        x.fileUID = j.get("fileUID", "").asString();
        return x;
    }
};

} // namespace api
