// rerun — reprocess an already-captured record's frames with a different
// pipeline JSON, without touching a live camera. See src/rerun/plan.md for the
// original design note.
//
// Usage: rerun <record_dir> <new_pipeline.json>

#include "sequential_frame_source.h"

#include <liblbt/filter_config.h>
#include <liblbt/frame_memory_object.h>
#include <liblbt/iframe_source.h>
#include <liblbt/pipeline_config_loader.h>
#include <liblbt/process_step.h>
#include <liblbt/processing_pipeline.h>
#include <liblbt/result_logger.h>
#include <liblbt/step_logger.h>

#include <opencv2/opencv.hpp>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string nowIso8601(std::chrono::system_clock::time_point tp = std::chrono::system_clock::now()) {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf;
    gmtime_r(&time_t, &tm_buf);
    std::ostringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

// Finds record-<uid>.json in recordDir and returns <uid>, or "" if not found.
std::string deriveRecordUID(const fs::path& recordDir) {
    for (const auto& entry : fs::directory_iterator(recordDir)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        const std::string prefix = "record-";
        const std::string suffix = ".json";
        if (name.size() > prefix.size() + suffix.size() &&
            name.rfind(prefix, 0) == 0 &&
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
        }
    }
    return "";
}

fs::path nextOldRunDir(const fs::path& recordDir) {
    int n = 1;
    while (true) {
        fs::path candidate = recordDir / ("oldrun" + std::to_string(n));
        if (!fs::exists(candidate)) return candidate;
        ++n;
    }
}

void moveIfExists(const fs::path& src, const fs::path& dstDir) {
    if (!fs::exists(src)) return;
    fs::path dst = dstDir / src.filename();
    fs::rename(src, dst);
    std::cout << "[rerun] Archived " << src << " -> " << dst << std::endl;
}

// Pull the metadata fields loadFromJSON doesn't populate (pipelineUID/description/
// status/dateBuild live at the top level of the file, alongside "pipeline") so the
// pipeline-<uid>.json rerun writes carries the same identity as the input JSON.
void readPipelineTopLevelMeta(const std::string& jsonPath, PipelineConfigLoader::PipelineConfig& cfg) {
    cv::FileStorage fs2(jsonPath, cv::FileStorage::READ | cv::FileStorage::FORMAT_JSON);
    if (!fs2.isOpened()) return;
    auto readStr = [&](const char* key) -> std::string {
        cv::FileNode n = fs2[key];
        if (n.empty() || !n.isString()) return "";
        return (std::string)n;
    };
    cfg.pipelineUID = readStr("pipelineUID");
    cfg.description = readStr("description");
    cfg.status = readStr("status");
    cfg.datetimeCreated = readStr("dateBuild");
    fs2.release();
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <record_dir> <new_pipeline.json>\n"
                   << "  record_dir        - already-unpacked record directory "
                      "(contains frames/, others/, pipeline-*.json, record-*.json)\n"
                   << "  new_pipeline.json - corrected pipeline to reprocess the frames with\n";
        return 1;
    }

    fs::path recordDir = argv[1];
    std::string newPipelinePath = argv[2];

    if (!fs::is_directory(recordDir)) {
        std::cerr << "[rerun] ERROR: record_dir does not exist or is not a directory: " << recordDir << std::endl;
        return 1;
    }
    fs::path framesDir = recordDir / "frames";
    if (!fs::is_directory(framesDir)) {
        std::cerr << "[rerun] ERROR: no frames/ directory found under: " << recordDir << std::endl;
        return 1;
    }
    if (!fs::is_regular_file(newPipelinePath)) {
        std::cerr << "[rerun] ERROR: new pipeline JSON not found: " << newPipelinePath << std::endl;
        return 1;
    }

    std::string recordUID = deriveRecordUID(recordDir);
    if (recordUID.empty()) {
        std::cerr << "[rerun] ERROR: could not find a record-*.json in " << recordDir
                   << " to derive the record UID" << std::endl;
        return 1;
    }
    std::cout << "[rerun] Record UID: " << recordUID << std::endl;

    // --- Archive the previous run's output into oldrunN/ ---
    fs::path oldRunDir = nextOldRunDir(recordDir);
    fs::create_directories(oldRunDir);
    std::cout << "[rerun] Archiving previous run output to: " << oldRunDir << std::endl;
    moveIfExists(recordDir / "others", oldRunDir);
    moveIfExists(recordDir / "mass_center_trajectory.csv", oldRunDir);
    moveIfExists(recordDir / "debug_detections.sqlite", oldRunDir);
    moveIfExists(recordDir / ("pipeline-" + recordUID + ".json"), oldRunDir);
    moveIfExists(recordDir / ("record-" + recordUID + ".json"), oldRunDir);

    // --- Load the new pipeline ---
    PipelineConfigLoader::PipelineConfig pipeConfig;
    try {
        pipeConfig = PipelineConfigLoader::loadFromJSON(newPipelinePath);
    } catch (const std::exception& e) {
        std::cerr << "[rerun] ERROR: failed to load pipeline JSON '" << newPipelinePath << "': " << e.what()
                   << std::endl;
        return 1;
    }
    readPipelineTopLevelMeta(newPipelinePath, pipeConfig);

    // Auto-skip FrameWriter_CPU: frames are the input to this tool, not an output,
    // and its baked-in output_dir points at the live-recording's stale /tmp path.
    {
        std::vector<std::shared_ptr<ProcessStep>> filteredSteps;
        std::vector<FilterConfig> filteredConfigs;
        for (size_t i = 0; i < pipeConfig.steps.size(); ++i) {
            if (pipeConfig.steps[i] && pipeConfig.steps[i]->getFilterType() == FilterType::FRAME_WRITER) {
                std::cout << "[rerun] Skipping FrameWriter_CPU step (frames already exist on disk)" << std::endl;
                continue;
            }
            filteredSteps.push_back(pipeConfig.steps[i]);
            filteredConfigs.push_back(pipeConfig.configs[i]);
        }
        pipeConfig.steps = std::move(filteredSteps);
        pipeConfig.configs = std::move(filteredConfigs);
    }

    if (pipeConfig.steps.empty()) {
        std::cerr << "[rerun] ERROR: pipeline has no steps left after removing FrameWriter" << std::endl;
        return 1;
    }

    std::cout << "[rerun] Loaded pipeline with " << pipeConfig.steps.size() << " step(s):" << std::endl;
    for (size_t i = 0; i < pipeConfig.steps.size(); ++i) {
        std::cout << "  [" << i << "] " << pipeConfig.steps[i]->getName() << std::endl;
    }

    // --- Frame source ---
    SequentialFrameSource frameSource(framesDir.string(), "*.jpg");
    if (!frameSource.isReady()) {
        std::cerr << "[rerun] ERROR: no frames found in " << framesDir << std::endl;
        return 1;
    }
    std::cout << "[rerun] Found " << frameSource.getFrameCount() << " frame(s) in " << framesDir << std::endl;

    // --- Fresh others/ output via ResultLogger (DEBUG mode, matching the source record) ---
    fs::path othersDir = recordDir / "others";
    fs::create_directories(othersDir);

    lbt::ResultLogger::Config logConfig;
    logConfig.recordDir = recordDir;
    logConfig.framesDir = framesDir;
    logConfig.othersDir = othersDir;
    logConfig.mode = lbt::ResultLogger::LogMode::DEBUG;
    logConfig.saveMetadata = true;
    logConfig.saveDebugImages = true;
    lbt::ResultLogger resultLogger(logConfig);

    // log.txt + StepLogger wiring, modeled on RecordManager::recordingLoop()
    std::ofstream logFile((othersDir / "log.txt").string());
    auto logToFile = [&logFile](const std::string& message) {
        if (!logFile.is_open()) return;
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        std::tm tm_buf;
        gmtime_r(&time_t, &tm_buf);
        logFile << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S") << "." << std::setfill('0') << std::setw(3)
                << ms.count() << "Z " << message << std::endl;
        logFile.flush();
    };

    auto stepLogger = std::make_shared<lbt::StepLogger>([&logToFile](lbt::StepLogger::Level level, const std::string& message) {
        const char* prefix;
        switch (level) {
            case lbt::StepLogger::Level::DEBUG:   prefix = "[DEBUG] "; break;
            case lbt::StepLogger::Level::INFO:    prefix = "[INFO] "; break;
            case lbt::StepLogger::Level::WARNING: prefix = "[WARN] "; break;
            case lbt::StepLogger::Level::ERROR:   prefix = "[ERROR] "; break;
            default:                                prefix = "[LOG] "; break;
        }
        logToFile(std::string(prefix) + message);
    });
    for (auto& step : pipeConfig.steps) {
        if (step) step->setLogger(stepLogger);
    }

    std::stringstream startMsg;
    startMsg << "Rerun started - RecordUID: " << recordUID << ", Pipeline: " << newPipelinePath
              << ", Source frames: " << frameSource.getFrameCount()
              << ", Superseded run: " << oldRunDir.filename().string() << ", Start Time: " << nowIso8601();
    std::cout << "[rerun] " << startMsg.str() << std::endl;
    logToFile(startMsg.str());

    // --- Main processing loop (un-gated: every frame, no FPS/duration pacing) ---
    auto wallStart = std::chrono::steady_clock::now();
    auto sysStart = std::chrono::system_clock::now();

    std::vector<cv::Point2f> massCenterList;
    std::vector<cv::Point2d> worldCenterList;
    std::vector<uint64_t> frameTimestampsList;
    int frameCounter = 0;
    bool failed = false;

    while (auto frame = frameSource.nextFrame()) {
        try {
            FrameMemoryObject result = ProcessingPipeline::processLine(pipeConfig.steps, *frame, pipeConfig.configs);
            result.setTimestampNs(frame->timestampNs());

            resultLogger.logFrame(frameCounter, *frame, result);

            if (result.hasMetadata("mass_center_point_pixel")) {
                cv::Point2f massCenter = result.getMetadata<cv::Point2f>("mass_center_point_pixel");
                cv::Point2d worldPt = result.getMetadata<cv::Point2d>("mass_center_point_world");
                if (massCenter.x >= 0 && massCenter.y >= 0) {
                    massCenterList.push_back(massCenter);
                    worldCenterList.push_back(worldPt);
                    frameTimestampsList.push_back(result.timestampNs());
                }
            }
            frameCounter++;
        } catch (const std::exception& e) {
            std::string errMsg =
                "Pipeline processing error at frame " + std::to_string(frameCounter) + ": " + e.what();
            std::cerr << "[rerun] " << errMsg << std::endl;
            logToFile("ERROR: " + errMsg);
            failed = true;
            break;
        }

        if (frameCounter % 100 == 0) {
            std::stringstream progressMsg;
            progressMsg << "Progress: " << frameCounter << " / " << frameSource.getFrameCount() << " frames";
            std::cout << "[rerun] " << progressMsg.str() << std::endl;
            logToFile(progressMsg.str());
        }
    }

    auto wallEnd = std::chrono::steady_clock::now();
    auto sysEnd = std::chrono::system_clock::now();
    double totalDuration = std::chrono::duration<double>(wallEnd - wallStart).count();
    double avgFPS = (totalDuration > 0.0) ? (frameCounter / totalDuration) : 0.0;

    if (!failed) {
        std::stringstream finalMsg;
        finalMsg << "Rerun completed successfully:\n"
                  << "  Frames processed: " << frameCounter << "\n"
                  << "  Average FPS: " << std::fixed << std::setprecision(2) << avgFPS << "\n"
                  << "  Total duration: " << std::setprecision(3) << totalDuration << "s\n"
                  << "  Trajectory points: " << massCenterList.size() << "\n"
                  << "  End time: " << nowIso8601();
        std::cout << "[rerun] Rerun completed successfully: " << frameCounter << " frames processed, "
                   << massCenterList.size() << " trajectory points" << std::endl;
        logToFile(finalMsg.str());

        // --- mass_center_trajectory.csv, same format as RecordManager ---
        if (!massCenterList.empty()) {
            std::ofstream trajFile((recordDir / "mass_center_trajectory.csv").string());
            trajFile << "timestamp_ns;world_x_mm;world_y_mm;mass_center_x_px;mass_center_y_px\n";
            for (size_t i = 0; i < massCenterList.size(); ++i) {
                trajFile << frameTimestampsList[i] << ";" << std::fixed << std::setprecision(2)
                          << worldCenterList[i].x << ";" << worldCenterList[i].y << ";" << std::setprecision(1)
                          << massCenterList[i].x << ";" << massCenterList[i].y << "\n";
            }
            trajFile.close();
            std::cout << "[rerun] Trajectory file written with " << massCenterList.size() << " points" << std::endl;
            logToFile("Trajectory file written with " + std::to_string(massCenterList.size()) + " points");
        } else {
            std::cout << "[rerun] WARNING: no trajectory points produced (mass_center_point_pixel was never set - "
                          "check the pipeline includes MassCenterOverlay_CPU followed by "
                          "ProjectPointTo2DSurface_CPU, and that ContourAreaFilter min_area isn't filtering out "
                          "every detection)"
                       << std::endl;
            logToFile("WARNING: no trajectory points produced");
        }

        // Flush DebugLogger only if the new pipeline explicitly includes one
        if (ProcessingPipeline::hasProcessingStep(pipeConfig.steps, FilterType::DEBUG_LOGGER)) {
            std::string dbPath = (recordDir / "debug_detections.sqlite").string();
            bool flushed = ProcessingPipeline::flushDebugLogger(pipeConfig.steps, dbPath);
            if (flushed) {
                std::cout << "[rerun] Debug detections written to: " << dbPath << std::endl;
                logToFile("Debug detections SQLite written to: " + dbPath);
            } else {
                std::cerr << "[rerun] Warning: DebugLogger flush failed or no data" << std::endl;
                logToFile("WARNING: DebugLogger flush failed or no data");
            }
        }

        resultLogger.writeSummary(frameCounter, totalDuration);

        // --- Fresh record-<uid>.json (same ad-hoc shape RecordManager::saveRecordMetadata() writes) ---
        {
            std::string recordJsonPath = (recordDir / ("record-" + recordUID + ".json")).string();
            std::ofstream rf(recordJsonPath);
            rf << "{\n"
               << "  \"recordUID\": \"" << recordUID << "\",\n"
               << "  \"framesRecorded\": " << frameCounter << ",\n"
               << "  \"actualFPS\": " << avgFPS << ",\n"
               << "  \"completed\": true,\n"
               << "  \"datetimeStart\": \"" << nowIso8601(sysStart) << "\",\n"
               << "  \"datetimeEnd\": \"" << nowIso8601(sysEnd) << "\",\n"
               << "  \"state\": \"STOPPED\",\n"
               << "  \"rerun\": true,\n"
               << "  \"rerunSupersedes\": \"" << oldRunDir.filename().string() << "\",\n"
               << "  \"rerunPipelineSource\": \"" << newPipelinePath << "\"\n"
               << "}\n";
            std::cout << "[rerun] Saved record metadata to: " << recordJsonPath << std::endl;
        }

        // --- Fresh pipeline-<uid>.json ---
        {
            std::string pipelineJsonPath = (recordDir / ("pipeline-" + recordUID + ".json")).string();
            if (!PipelineConfigLoader::saveToJSON(pipelineJsonPath, pipeConfig)) {
                std::cerr << "[rerun] Warning: failed to save pipeline configuration to " << pipelineJsonPath
                           << std::endl;
                logToFile("WARNING: Failed to save pipeline config");
            } else {
                std::cout << "[rerun] Saved pipeline configuration to: " << pipelineJsonPath << std::endl;
                logToFile("Pipeline configuration saved");
            }
        }
    }

    logFile.close();

    std::cout << "\n[rerun] ===== Summary =====" << std::endl;
    std::cout << "[rerun] Record dir:        " << recordDir << std::endl;
    std::cout << "[rerun] Frames processed:  " << frameCounter << " / " << frameSource.getFrameCount() << std::endl;
    std::cout << "[rerun] Trajectory points: " << massCenterList.size() << std::endl;
    std::cout << "[rerun] Previous run archived to: " << oldRunDir << std::endl;
    std::cout << "[rerun] Status: " << (failed ? "FAILED" : "OK") << std::endl;

    return failed ? 1 : 0;
}
