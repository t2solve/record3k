#include <api/controllers/docs_controller.h>
#include <drogon/drogon.h>
#include <json/json.h>
#include <vector>

namespace {
Json::Value buildSpec() {
    Json::Value root;
    root["openapi"] = "3.0.0";
    root["info"]["title"] = "Testrecord3k API";
    root["info"]["version"] = "1.0.0";
    root["info"]["description"] = "Manual OpenAPI document (fallback until native swagger available)";

    // Basic component schemas (very simplified)
    Json::Value components(Json::objectValue);
    Json::Value schemas(Json::objectValue);
    auto simpleObj = [](std::vector<std::pair<std::string,std::string>> fields){
        Json::Value s(Json::objectValue);
        s["type"] = "object";
        Json::Value props(Json::objectValue);
        for (auto &f : fields) {
            props[f.first]["type"] = f.second;
        }
        s["properties"] = props;
        return s;
    };
    schemas["CameraInfo"]      = simpleObj({{"camUID","string"},{"macAddress","string"},{"description","string"},{"status","string"},{"datetimeLastSeen","string"},{"cameraType","string"}});
    schemas["PipelineInfo"]    = simpleObj({{"pipelineUID","string"},{"description","string"},{"status","string"},{"createdAt","string"},{"fileUID","string"}});
    schemas["CalibrationInfo"] = simpleObj({{"calibrationUID","string"},{"camUID","string"},{"description","string"},{"status","string"},{"createdAt","string"},{"fileUID","string"}});
    schemas["RecordInfo"]      = simpleObj({{"recordUID","string"},{"calibrationUID","string"},{"isTest","boolean"},{"pipelineUID","string"},{"studyInfoUID","string"},{"status","string"},{"durationInSeconds","number"},{"startTime","string"},{"endTime","string"},{"fileUID","string"}});
    schemas["FileInfo"]        = simpleObj({{"fileUID","string"},{"createdAt","string"},{"status","string"},{"description","string"}});
    schemas["StudyMetaInfo"]   = simpleObj({{"studyInfoUID","string"},{"description","string"},{"individualScientificName","string"},{"weightInMg","number"}});
    components["schemas"] = schemas;

    Json::Value paths(Json::objectValue);
    auto addGet = [&](const std::string &path, const std::string &summary, const std::string &respSchema){
        paths[path]["get"]["summary"] = summary;
        paths[path]["get"]["responses"]["200"]["description"] = "OK";
        if(!respSchema.empty()) {
            paths[path]["get"]["responses"]["200"]["content"]["application/json"]["schema"] = Json::objectValue;
            if(respSchema[0]=='[') { // array of schema name
                std::string name = respSchema.substr(1, respSchema.size()-2);
                Json::Value arr(Json::objectValue);
                arr["type"] = "array";
                arr["items"]["$ref"] = std::string("#/components/schemas/") + name;
                paths[path]["get"]["responses"]["200"]["content"]["application/json"]["schema"] = arr;
            } else {
                paths[path]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] = std::string("#/components/schemas/") + respSchema;
            }
        }
    };
    auto addPost = [&](const std::string &path, const std::string &summary, const std::string &respSchema, int code){
        paths[path]["post"]["summary"] = summary;
        std::string codeStr = std::to_string(code);
        paths[path]["post"]["responses"][codeStr]["description"] = "Accepted";
        if(!respSchema.empty()) {
            paths[path]["post"]["responses"][codeStr]["content"]["application/json"]["schema"]["$ref"] = std::string("#/components/schemas/") + respSchema;
        }
        // Basic requestBody marker (not fully described)
        paths[path]["post"]["requestBody"]["required"] = false;
        paths[path]["post"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
    };

    // Info endpoints
    addGet("/info/cameras/list","List cameras","[CameraInfo]");
    addGet("/info/cameras/get/{camUID}","Get camera by UID","CameraInfo");
    addGet("/info/pipelines/list","List pipelines","[PipelineInfo]");
    addGet("/info/pipelines/get/{pipelineUID}","Get pipeline","PipelineInfo");
    addGet("/info/calibrations/list","List calibrations","[CalibrationInfo]");
    addGet("/info/calibrations/get/{calibrationUID}","Get calibration","CalibrationInfo");
    addGet("/info/records/list","List records","[RecordInfo]");
    addGet("/info/records/get/{recordUID}","Get record","RecordInfo");
    addGet("/info/studies/list","List studies","[StudyMetaInfo]");
    addGet("/info/studies/get/{studyInfoUID}","Get study","StudyMetaInfo");

    // File info endpoints (moved under /info)
    addGet("/info/files/list","List files","[FileInfo]");
    addGet("/info/files/get/{fileUID}","Get file info","FileInfo");
    addGet("/get/file/binary/{fileUID}","Download file binary",""); // binary stream

    // Add endpoints
    addPost("/add/pipeline","Add pipeline","PipelineInfo",201);
    addPost("/add/studyMetaInfo","Add study meta info","StudyMetaInfo",201);

    // Do endpoints
    addPost("/do/camera/calibrate/{camUID}","Trigger calibration","CalibrationInfo",202);
    addPost("/do/camera/pipelinetest/{camUID}","Pipeline test record","RecordInfo",202);
    addPost("/do/camera/record/start/{camUID}","Start recording","RecordInfo",201);
    addPost("/do/camera/record/{recordUID}/stop","Stop recording","RecordInfo",200);
    addPost("/do/camera/updatelist","Update & probe camera list","CameraInfo",200);

    root["paths"] = paths;
    root["components"] = components;
    return root;
}
} // namespace

static std::string swaggerHtml(const std::string &specUrl) {
    // Simple embedded Swagger UI via CDN
    return std::string(R"(<!DOCTYPE html><html><head><title>Swagger UI</title>
    <link rel="stylesheet" href="https://unpkg.com/swagger-ui-dist@5/swagger-ui.css" />
    </head><body><div id="swagger-ui"></div>
    <script src="https://unpkg.com/swagger-ui-dist@5/swagger-ui-bundle.js"></script>
    <script>window.onload = () => { window.ui = SwaggerUIBundle({ url: ')")
        + specUrl + R"(', dom_id: '#swagger-ui'}); };</script></body></html>)";
}

void DocsController::openapi(const drogon::HttpRequestPtr& /*req*/, std::function<void (const drogon::HttpResponsePtr &)> &&callback)
{
    auto spec = buildSpec();
    Json::StreamWriterBuilder w;
    auto body = Json::writeString(w, spec);
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(drogon::k200OK);
    resp->addHeader("Content-Type", "application/json");
    resp->setBody(body);
    callback(resp);
}

void DocsController::swagger(const drogon::HttpRequestPtr& /*req*/, std::function<void (const drogon::HttpResponsePtr &)> &&callback)
{
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(drogon::k200OK);
    resp->addHeader("Content-Type", "text/html; charset=utf-8");
    resp->setBody(swaggerHtml("/openapi.json"));
    callback(resp);
}
