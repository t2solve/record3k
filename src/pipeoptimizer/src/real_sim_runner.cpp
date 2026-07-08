#include "real_sim_runner.h"
#include "real_metrics.h"
#include "real_sim_db.h"
#include "pipe_config_gen.h"

#include <liblbt/buffered_frame_source.h>
#include <liblbt/disk_image_frame_source.h>
#include <liblbt/tag_data_store.h>
#include <liblbt/pipeline_config_loader.h>
#include <liblbt/processing_pipeline.h>
#include <liblbt/frame_memory_object.h>

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

bool hasBadShellChars(const std::string& s) {
    return s.find('\'') != std::string::npos || s.find('"') != std::string::npos ||
           s.find('$') != std::string::npos || s.find('`') != std::string::npos ||
           s.find(';') != std::string::npos || s.find('|') != std::string::npos ||
           s.find('&') != std::string::npos || s.find('\n') != std::string::npos;
}

std::string readWholeFile(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

RealSimRunner::RealSimRunner(Options opts) : m_opts(std::move(opts)) {}

std::string RealSimRunner::resolveRecordDir(bool& didExtract, std::string& extractedTempDir) const {
    didExtract = false;
    if (!m_opts.record_dir_path.empty()) {
        return m_opts.record_dir_path;
    }

    const std::string& archive = m_opts.record_archive_path;
    if (hasBadShellChars(archive)) {
        std::cerr << "[RealSimRunner] Security: invalid characters in --record-archive path\n";
        return "";
    }

    fs::path archivePath(archive);
    std::string baseName = archivePath.stem().string(); // strip ".gz"
    if (baseName.size() > 4 && baseName.substr(baseName.size() - 4) == ".tar") {
        baseName = baseName.substr(0, baseName.size() - 4);
    }
    fs::path tempDir = fs::temp_directory_path() / ("pipeoptimizer-real-" + baseName);
    fs::create_directories(tempDir);

    std::string tempDirStr = tempDir.string();
    if (hasBadShellChars(tempDirStr)) {
        std::cerr << "[RealSimRunner] Security: invalid characters in temp dir path\n";
        return "";
    }

    std::ostringstream cmd;
    cmd << "tar -xzf '" << archive << "' -C '" << tempDirStr << "'";
    std::cout << "[RealSimRunner] Extracting: " << cmd.str() << std::endl;
    int rc = std::system(cmd.str().c_str());
    if (rc != 0) {
        std::cerr << "[RealSimRunner] tar extraction failed (exit " << rc << ")\n";
        return "";
    }

    // The archive may unpack a "frames" dir directly, or wrap everything in a single
    // top-level directory (e.g. file-rec-<uid>/) -- handle both, same logic
    // TaggingWindow/rerun already use for this identical archive format.
    fs::path recordDir = tempDir;
    if (!fs::exists(recordDir / "frames")) {
        std::vector<fs::path> subdirs;
        for (const auto& entry : fs::directory_iterator(tempDir)) {
            if (entry.is_directory()) subdirs.push_back(entry.path());
        }
        if (subdirs.size() == 1) recordDir = subdirs.front();
    }
    if (!fs::exists(recordDir / "frames")) {
        std::cerr << "[RealSimRunner] Extracted archive but no 'frames' directory found in "
                  << tempDirStr << " (or its single subdirectory)\n";
        return "";
    }

    didExtract = true;
    extractedTempDir = tempDirStr;
    return recordDir.string();
}

std::string RealSimRunner::deriveRecordUID(const std::string& recordDir) const {
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
    return fs::path(recordDir).filename().string();
}

int RealSimRunner::run() {
    // --- resolve record dir (extract if an archive was given) ---
    bool didExtract = false;
    std::string extractedTempDir;
    std::string recordDir = resolveRecordDir(didExtract, extractedTempDir);
    if (recordDir.empty()) {
        std::cerr << "[RealSimRunner] Could not resolve a record directory.\n";
        return 1;
    }
    std::string recordUID = deriveRecordUID(recordDir);
    std::cout << "[RealSimRunner] Record UID: " << recordUID << std::endl;

    auto cleanupExtracted = [&]() {
        if (didExtract && !extractedTempDir.empty()) {
            std::error_code ec;
            fs::remove_all(extractedTempDir, ec);
            if (ec) {
                std::cerr << "[RealSimRunner] Warning: failed to clean up " << extractedTempDir
                          << ": " << ec.message() << std::endl;
            } else {
                std::cout << "[RealSimRunner] Cleaned up extracted dir: " << extractedTempDir << std::endl;
            }
        }
    };

    // --- tag store lookup: ROI / valid-until cutoff / complete-failure refusal ---
    RoiConfig roi;
    int validUntilFrame = -1;
    std::string tagsDbPath = m_opts.tags_db_path.empty()
        ? "data/simulation/supervised_records_data.sqlite3"
        : m_opts.tags_db_path;
    try {
        lbt::TagDataStore tagStore(tagsDbPath);
        lbt::TagData tagData;
        if (tagStore.load(recordUID, tagData)) {
            if (tagData.recordFailed) {
                std::cerr << "[RealSimRunner] Record " << recordUID
                          << " is marked as a complete failure in the tag store (" << tagsDbPath
                          << ") -- refusing to run a sweep on it.\n";
                cleanupExtracted();
                return 1;
            }
            roi.roi_defined = tagData.roiDefined;
            roi.center_x = tagData.roiCenterX;
            roi.center_y = tagData.roiCenterY;
            roi.radius = tagData.roiRadius;
            validUntilFrame = tagData.validUntilFrame;
            std::cout << "[RealSimRunner] Loaded tag data: roiDefined=" << roi.roi_defined
                      << " validUntilFrame=" << validUntilFrame << std::endl;
        } else {
            std::cout << "[RealSimRunner] No tag data found for " << recordUID << " in " << tagsDbPath
                      << " -- proceeding without ROI/cutoff checks.\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "[RealSimRunner] Warning: failed to open tag data store (" << tagsDbPath
                  << "): " << e.what() << " -- proceeding without ROI/cutoff checks.\n";
    }

    // --- effective scoring cutoff: explicit validUntilFrame from the tag store, or the
    // tagging tool's own review ceiling if no explicit cutoff was ever set. "-1 ==
    // unbounded" is not accurate here: TaggingWindow opens records via
    // DiskImageFrameSource, which hard-caps preload at DISK_IMAGE_MAX_PRELOAD, so no
    // human has ever reviewed (ROI containment, catastrophic-failure judgement, etc.)
    // past that frame either way -- scoring further than that would trust the pipeline
    // on frames nobody ever verified.
    size_t effectiveMaxFrames;
    if (validUntilFrame >= 0) {
        effectiveMaxFrames = static_cast<size_t>(validUntilFrame) + 1;
        std::cout << "[RealSimRunner] Explicit cutoff at frame " << validUntilFrame << std::endl;
    } else {
        effectiveMaxFrames = DISK_IMAGE_MAX_PRELOAD;
        std::cout << "[RealSimRunner] No explicit cutoff -- capping at frame "
                  << (DISK_IMAGE_MAX_PRELOAD - 1) << " (tagging tool's review limit)" << std::endl;
    }

    // --- decode frames once into RAM, up to the effective cutoff ---
    std::string framesDir = (fs::path(recordDir) / "frames").string();
    BufferedFrameSource frameSource(framesDir, "*.jpg", effectiveMaxFrames);
    if (!frameSource.isReady()) {
        std::cerr << "[RealSimRunner] No frames found in " << framesDir << std::endl;
        cleanupExtracted();
        return 1;
    }
    size_t frameCount = frameSource.frameCount();
    size_t lastFrameIdx = frameCount; // already capped at construction time
    std::cout << "[RealSimRunner] Buffered " << frameCount << " frame(s), scoring " << lastFrameIdx
              << " of them" << std::endl;

    // --- build the sweep (or load the single fixed pipeline) ---
    std::vector<PipelineConfigGen::GeneratedConfig> configs;
    if (!m_opts.pipeline_config_path.empty()) {
        configs.push_back({"fixed-" + recordUID, readWholeFile(m_opts.pipeline_config_path)});
    } else {
        std::string basePipelineJson = m_opts.base_pipeline_path.empty()
            ? PipelineConfigGen::defaultBasePipelineJson()
            : readWholeFile(m_opts.base_pipeline_path);
        configs = PipelineConfigGen::generate(basePipelineJson, m_opts.param_ranges_path,
                                               m_opts.n_pipeline_configs, m_opts.seed);
    }
    if (configs.empty()) {
        std::cerr << "[RealSimRunner] No pipeline configs generated.\n";
        cleanupExtracted();
        return 1;
    }
    std::cout << "[RealSimRunner] " << configs.size() << " pipeline config(s) to test" << std::endl;

    // --- db + scratch dir for config JSON files (PipelineConfigLoader only reads paths) ---
    std::string dbPath = m_opts.db_path.empty()
        ? (fs::path(recordDir).parent_path() / ("run_test_param_" + recordUID + ".sqlite3")).string()
        : m_opts.db_path;
    RealSimDB db(dbPath);
    std::cout << "[RealSimRunner] Writing results to: " << dbPath << std::endl;

    fs::path scratchDir = fs::temp_directory_path() / ("pipeoptimizer-real-configs-" + recordUID);
    fs::create_directories(scratchDir);
    std::vector<std::string> configPaths(configs.size());
    for (size_t i = 0; i < configs.size(); ++i) {
        fs::path p = scratchDir / (configs[i].pipeline_id + ".json");
        std::ofstream out(p);
        out << configs[i].config_json;
        configPaths[i] = p.string();
    }

    std::string description = m_opts.description.empty() ? ("real-sweep-" + recordUID) : m_opts.description;
    int64_t runInfoId = db.insertRunInfo(recordUID, description, static_cast<int>(lastFrameIdx),
                                          static_cast<int>(configs.size()));
    std::vector<int64_t> pipeConfigIds(configs.size());
    for (size_t i = 0; i < configs.size(); ++i) {
        pipeConfigIds[i] = db.insertPipelineConfig(runInfoId, configs[i].pipeline_id, configs[i].config_json);
    }

    // --- thread pool: atomic work cursor over pipeline configs (mirrors SimRunner::run(),
    //     but the work unit here is just "one config", not a (record,pipeline) pair,
    //     since frames are shared read-only across every worker). ---
    int nWorkers = std::max(1, std::min(m_opts.n_workers, static_cast<int>(configs.size())));
    std::atomic<size_t> workCursor{0};
    std::mutex dbMutex;
    std::mutex printMutex;
    std::atomic<int> nPassed{0};

    auto worker = [&]() {
        try { cv::cuda::setDevice(0); } catch (...) {}

        while (true) {
            size_t idx = workCursor.fetch_add(1);
            if (idx >= configs.size()) break;

            const auto& gen = configs[idx];
            auto startTime = std::chrono::high_resolution_clock::now();

            PipelineConfigLoader::PipelineConfig pipelineCfg;
            try {
                pipelineCfg = PipelineConfigLoader::loadFromJSON(configPaths[idx]);
            } catch (const std::exception& e) {
                std::lock_guard<std::mutex> lock(printMutex);
                std::cerr << "[RealSimRunner] Failed to load config " << gen.pipeline_id << ": " << e.what()
                          << " -- skipping\n";
                continue;
            }

            std::vector<RealFrameObservation> observations;
            observations.reserve(lastFrameIdx);
            for (size_t f = 0; f < lastFrameIdx; ++f) {
                RealFrameObservation obs;
                obs.frame_idx = static_cast<int>(f);
                obs.ts_ns = static_cast<long long>(frameSource.timestampAt(f));

                try {
                    FrameMemoryObject input(frameSource.frameAt(f));
                    FrameMemoryObject output =
                        ProcessingPipeline::processLine(pipelineCfg.steps, input, pipelineCfg.configs);

                    cv::Point2f center(-1.f, -1.f);
                    bool haveCenter = false;
                    if (output.hasMetadata("mass_center_point_pixel")) {
                        center = output.getMetadata<cv::Point2f>("mass_center_point_pixel");
                        haveCenter = true;
                    } else if (output.hasMetadata("mass_center_point")) {
                        center = output.getMetadata<cv::Point2f>("mass_center_point");
                        haveCenter = true;
                    }
                    if (haveCenter && center.x >= 0 && center.y >= 0) {
                        obs.det_x = center.x;
                        obs.det_y = center.y;
                    }

                    if (output.hasMetadata("area_filter_count")) {
                        obs.area_filter_count = output.getMetadata<int>("area_filter_count");
                    }
                    if (output.hasMetadata("filtered_contour_count")) {
                        obs.filtered_count = output.getMetadata<int>("filtered_contour_count");
                    }
                    if (output.hasMetadata("filtered_contours")) {
                        auto contours =
                            output.getMetadata<std::vector<std::vector<cv::Point>>>("filtered_contours");
                        if (!contours.empty()) {
                            obs.contour_area = cv::contourArea(contours[0]);
                        }
                    }
                } catch (const std::exception&) {
                    // Treat as "no detection this frame" -- matches sim_runner.cpp's convention.
                }

                observations.push_back(obs);
            }

            auto endTime = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double, std::milli>(endTime - startTime).count();

            RealMetrics metrics = computeRealMetrics(observations, m_opts.metrics_config, roi, ms);
            if (metrics.passed) ++nPassed;

            {
                std::lock_guard<std::mutex> lock(dbMutex);
                db.insertResult(runInfoId, pipeConfigIds[idx], gen.pipeline_id, metrics);
            }
            {
                std::lock_guard<std::mutex> lock(printMutex);
                std::cout << "[RealSimRunner] " << gen.pipeline_id << (metrics.passed ? " PASSED" : " FAILED")
                          << " movements=" << metrics.n_movement_events
                          << " detected=" << metrics.n_frames_detected << "/" << metrics.n_frames_scored << " ("
                          << ms << " ms)" << (metrics.passed ? "" : (" -- " + metrics.fail_reason))
                          << std::endl;
            }
        }
    };

    std::cout << "[RealSimRunner] Running " << configs.size() << " config(s) across " << nWorkers
              << " worker(s)..." << std::endl;
    std::vector<std::thread> threads;
    threads.reserve(nWorkers);
    for (int i = 0; i < nWorkers; ++i) threads.emplace_back(worker);
    for (auto& t : threads) t.join();

    std::error_code ec;
    fs::remove_all(scratchDir, ec);

    std::cout << "[RealSimRunner] Done: " << nPassed.load() << "/" << configs.size()
              << " config(s) passed the hard-fail check." << std::endl;

    cleanupExtracted();
    return 0;
}
