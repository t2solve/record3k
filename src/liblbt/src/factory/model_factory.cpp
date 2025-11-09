#include <liblbt/factory/model_factory.h>
#include <algorithm>
#include <cctype>

namespace lbt {

std::string ModelFactory::trim(std::string s) {
    auto notSpace = [](unsigned char c){ return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}
bool ModelFactory::nonEmpty(const std::string& s) { return !trim(s).empty(); }
bool ModelFactory::isReasonableUID(const std::string& s) {
    if (s.size() < 3 || s.size() > 64) return false;
    return std::all_of(s.begin(), s.end(), [](unsigned char c){ return std::isalnum(c) || c=='-' || c=='_' ; });
}
bool ModelFactory::isIso8601Like(const std::string& s) {
    // Very light: expect a 'T' separator and a trailing 'Z' or offset sign
    return s.size() >= 10 && s.find('T') != std::string::npos;
}
bool ModelFactory::isValidMac(const std::string& s) {
    // Accept forms like AA:BB:CC:DD:EE:FF
    if (s.size() != 17) return false;
    for (size_t i=0;i<s.size();++i) {
        if ((i+1)%3==0) { if (s[i] != ':') return false; }
        else if (!std::isxdigit((unsigned char)s[i])) return false;
    }
    return true;
}
std::string ModelFactory::normalizeMac(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::toupper(c); });
    return s;
}

// CameraInfo
StatusOr<api::CameraInfo> ModelFactory::makeCameraInfo(std::string camUID, std::string macAddress, std::optional<std::string> description, std::string status) {
    camUID = trim(camUID);
    macAddress = trim(macAddress);
    status = trim(status);
    if (!isReasonableUID(camUID)) return {"Invalid camUID"};
    if (!isValidMac(macAddress)) return {"Invalid MAC address"};
    macAddress = normalizeMac(macAddress);
    if (!nonEmpty(status)) status = "unknown";
    api::CameraInfo c{camUID, macAddress, description, status};
    return c;
}
StatusOr<api::CameraInfo> ModelFactory::fromJsonCamera(const Json::Value& j) {
    return makeCameraInfo(j.get("camUID","" ).asString(), j.get("macAddress","" ).asString(),
                          j.isMember("description")? std::optional<std::string>(j["description"].asString()) : std::nullopt,
                          j.get("status","" ).asString());
}

// PipelineInfo
StatusOr<api::PipelineInfo> ModelFactory::makePipelineInfo(std::string pipelineUID, std::optional<std::string> description, std::string status, std::string dateBuild, std::string fileUID) {
    pipelineUID = trim(pipelineUID);
    status = trim(status);
    dateBuild = trim(dateBuild);
    fileUID = trim(fileUID);
    if (!isReasonableUID(pipelineUID)) return {"Invalid pipelineUID"};
    if (!isIso8601Like(dateBuild)) return {"dateBuild not ISO8601-like"};
    if (!isReasonableUID(fileUID)) return {"Invalid fileUID"};
    if (!nonEmpty(status)) status = "unknown";
    api::PipelineInfo p{pipelineUID, description, status, dateBuild, fileUID};
    return p;
}
StatusOr<api::PipelineInfo> ModelFactory::fromJsonPipeline(const Json::Value& j) {
    return makePipelineInfo(j.get("pipelineUID","" ).asString(),
                            j.isMember("description")? std::optional<std::string>(j["description"].asString()) : std::nullopt,
                            j.get("status","" ).asString(),
                            j.get("dateBuild","" ).asString(),
                            j.get("fileUID","" ).asString());
}

// CalibrationInfo
StatusOr<api::CalibrationInfo> ModelFactory::makeCalibrationInfo(std::string calibrationUID, std::string cameraUID, std::optional<std::string> description, std::string status, std::string datetimeCreated, std::string fileUID) {
    calibrationUID = trim(calibrationUID);
    cameraUID = trim(cameraUID);
    status = trim(status);
    datetimeCreated = trim(datetimeCreated);
    fileUID = trim(fileUID);
    if (!isReasonableUID(calibrationUID)) return {"Invalid calibrationUID"};
    if (!isReasonableUID(cameraUID)) return {"Invalid cameraUID"};
    if (!isIso8601Like(datetimeCreated)) return {"datetimeCreated not ISO8601-like"};
    if (!isReasonableUID(fileUID)) return {"Invalid fileUID"};
    if (!nonEmpty(status)) status = "unknown";
    api::CalibrationInfo c{calibrationUID, cameraUID, description, status, datetimeCreated, fileUID};
    return c;
}
StatusOr<api::CalibrationInfo> ModelFactory::fromJsonCalibration(const Json::Value& j) {
    return makeCalibrationInfo(j.get("calibrationUID","" ).asString(),
                               j.get("cameraUID","" ).asString(),
                               j.isMember("description")? std::optional<std::string>(j["description"].asString()) : std::nullopt,
                               j.get("status","" ).asString(),
                               j.get("datetimeCreated","" ).asString(),
                               j.get("fileUID","" ).asString());
}

// FileInfo
StatusOr<api::FileInfo> ModelFactory::makeFileInfo(std::string fileUID, std::string datetimeCreated, std::string status, std::optional<std::string> info) {
    fileUID = trim(fileUID);
    datetimeCreated = trim(datetimeCreated);
    status = trim(status);
    if (!isReasonableUID(fileUID)) return {"Invalid fileUID"};
    if (!isIso8601Like(datetimeCreated)) return {"datetimeCreated not ISO8601-like"};
    if (!nonEmpty(status)) status = "available";
    api::FileInfo f{fileUID, datetimeCreated, status, info};
    return f;
}
StatusOr<api::FileInfo> ModelFactory::fromJsonFileInfo(const Json::Value& j) {
    return makeFileInfo(j.get("fileUID","" ).asString(),
                        j.get("datetimeCreated","" ).asString(),
                        j.get("status","" ).asString(),
                        j.isMember("info")? std::optional<std::string>(j["info"].asString()) : std::nullopt);
}

// StudyMetaInfo
StatusOr<api::StudyMetaInfo> ModelFactory::makeStudyMetaInfo(std::string studyInfoUID, std::optional<std::string> description, std::string individualScientificName, double weightInMg) {
    studyInfoUID = trim(studyInfoUID);
    individualScientificName = trim(individualScientificName);
    if (!isReasonableUID(studyInfoUID)) return {"Invalid studyInfoUID"};
    if (individualScientificName.empty()) return {"individualScientificName required"};
    if (weightInMg < 0.0) return {"weightInMg negative"};
    api::StudyMetaInfo s{studyInfoUID, description, individualScientificName, weightInMg};
    return s;
}
StatusOr<api::StudyMetaInfo> ModelFactory::fromJsonStudy(const Json::Value& j) {
    return makeStudyMetaInfo(j.get("studyInfoUID","" ).asString(),
                             j.isMember("description")? std::optional<std::string>(j["description"].asString()) : std::nullopt,
                             j.get("individualScientificName","" ).asString(),
                             j.get("weightInMg",0.0 ).asDouble());
}

// RecordInfo
StatusOr<api::RecordInfo> ModelFactory::makeRecordInfo(std::string recordUID, std::string calibrationUID, bool flagIsPipelineTest, std::string pipelineUID, std::string studyUID, std::string status, double durationInSeconds, std::string datetimeStart, std::string datetimeEnd, std::string fileUID) {
    recordUID = trim(recordUID);
    calibrationUID = trim(calibrationUID);
    pipelineUID = trim(pipelineUID);
    studyUID = trim(studyUID);
    status = trim(status);
    datetimeStart = trim(datetimeStart);
    datetimeEnd = trim(datetimeEnd);
    fileUID = trim(fileUID);
    if (!isReasonableUID(recordUID)) return {"Invalid recordUID"};
    if (!isReasonableUID(calibrationUID)) return {"Invalid calibrationUID"};
    if (!isReasonableUID(pipelineUID)) return {"Invalid pipelineUID"};
    if (!isReasonableUID(studyUID)) return {"Invalid studyUID"};
    if (!isIso8601Like(datetimeStart)) return {"datetimeStart not ISO8601-like"};
    if (!datetimeEnd.empty() && !isIso8601Like(datetimeEnd)) return {"datetimeEnd not ISO8601-like"};
    if (!isReasonableUID(fileUID)) return {"Invalid fileUID"};
    if (durationInSeconds < 0.0) return {"duration negative"};
    if (!nonEmpty(status)) status = "unknown";
    api::RecordInfo r{recordUID, calibrationUID, flagIsPipelineTest, pipelineUID, studyUID, status, durationInSeconds, datetimeStart, datetimeEnd, fileUID};
    return r;
}
StatusOr<api::RecordInfo> ModelFactory::fromJsonRecord(const Json::Value& j) {
    return makeRecordInfo(j.get("recordUID","" ).asString(),
                          j.get("calibrationUID","" ).asString(),
                          j.get("flagIsPipelineTest", false).asBool(),
                          j.get("pipelineUID","" ).asString(),
                          j.get("studyUID","" ).asString(),
                          j.get("status","" ).asString(),
                          j.get("durationInSeconds",0.0 ).asDouble(),
                          j.get("datetimeStart","" ).asString(),
                          j.get("datetimeEnd","" ).asString(),
                          j.get("fileUID","" ).asString());
}

} // namespace lbt
