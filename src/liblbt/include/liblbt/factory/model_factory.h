#pragma once

#include <liblbt/api/api_models.h>
#include <json/json.h>
#include <optional>
#include <string>

namespace lbt {

// Minimal StatusOr type for validation results
template <class T>
class StatusOr {
public:
    StatusOr(const T& v) : ok_(true), value_(v) {}
    StatusOr(T&& v) : ok_(true), value_(std::move(v)) {}
    StatusOr(std::string err) : ok_(false), error_(std::move(err)) {}

    bool ok() const { return ok_; }
    const T& value() const { return value_; }
    T& value() { return value_; }
    const std::string& error() const { return error_; }

private:
    bool ok_{false};
    T value_{};
    std::string error_;
};

class ModelFactory {
public:
    // Construct validated models (normalize where sensible)
    // UID generators (except cameraUID, which is external)
    static std::string generateCalibrationUID();
    static std::string generatePipelineUID();
    static std::string generateRecordUID();
    static std::string generateFileUID();
    static std::string generateStudyUID();

    static StatusOr<api::CameraInfo> makeCameraInfo(
        std::string camUID,
        std::string macAddress,
        std::optional<std::string> description,
        std::string status);


    static StatusOr<api::PipelineInfo> makePipelineInfo(
        std::optional<std::string> pipelineUID,
        std::optional<std::string> description,
        std::string status,
        std::string dateBuild,
        std::string fileUID);

    static StatusOr<api::CalibrationInfo> makeCalibrationInfo(
        std::optional<std::string> calibrationUID,
        std::string cameraUID,
        std::optional<std::string> description,
        std::string status,
        std::string datetimeCreated,
        std::string fileUID);

    static StatusOr<api::FileInfo> makeFileInfo(
        std::optional<std::string> fileUID,
        std::string datetimeCreated,
        std::string status,
        std::optional<std::string> info);

    static StatusOr<api::StudyMetaInfo> makeStudyMetaInfo(
        std::optional<std::string> studyInfoUID,
        std::optional<std::string> description,
        std::string individualScientificName,
        double weightInMg);

    static StatusOr<api::RecordInfo> makeRecordInfo(
        std::optional<std::string> recordUID,
        std::string calibrationUID,
        bool flagIsPipelineTest,
        std::string pipelineUID,
        std::string studyUID,
        std::string status,
        double durationInSeconds,
        std::string datetimeStart,
        std::string datetimeEnd,
        std::string fileUID);

    // Create from JSON with validation
    static StatusOr<api::CameraInfo> fromJsonCamera(const Json::Value& j);
    static StatusOr<api::PipelineInfo> fromJsonPipeline(const Json::Value& j);
    static StatusOr<api::CalibrationInfo> fromJsonCalibration(const Json::Value& j);
    static StatusOr<api::FileInfo> fromJsonFileInfo(const Json::Value& j);
    static StatusOr<api::StudyMetaInfo> fromJsonStudy(const Json::Value& j);
    static StatusOr<api::RecordInfo> fromJsonRecord(const Json::Value& j);

private:
    // Simple validators/normalizers
    static std::string trim(std::string s);
    static bool nonEmpty(const std::string& s);
    static bool isReasonableUID(const std::string& s);
    static bool isIso8601Like(const std::string& s); // lightweight check
    static bool isValidMac(const std::string& s);
    static std::string normalizeMac(std::string s);
};

} // namespace lbt
