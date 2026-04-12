
// test7 — Multi-camera benchmark recording (VimbaX)
// ./test7bench --pipeline pipe-1776000434701-6235 --duration 600 --fps 10
// Loads all TYPE_CAM_VIMBA cameras from cameras-dir, probes which are online,
// asks for confirmation, then records all online cameras in parallel for a
// fixed duration using a single shared pipeline UID.
//
// Usage:
//   test7bench --pipeline <uid> [options]
//
// Options:
//   --cameras-dir <path>    Camera JSON directory    (default: data/cameras)
//   --pipelines-dir <path>  Pipeline JSON directory  (default: data/pipelines)
//   --pipeline <uid>        Pipeline UID (required)
//   --cti <path>            VimbaX .cti file         (default: ../../vimbax/cti/VimbaGigETL.cti)
//   --duration <seconds>    Recording duration        (default: 600)
//   --fps <value>           Target frame rate         (default: 10)
//   --output-dir <path>     Base output directory     (default: /tmp/record3k-bench)
//   --help                  Show this help

#include <QCoreApplication>

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>
#include <iomanip>
#include <sstream>
#include <memory>

#include <liblbt/camera_manager.h>
#include <liblbt/camera_manager_vimbax.h>
#include <liblbt/pipeline_config_loader.h>
#include <liblbt/record_manager.h>
#include <liblbt/factory/model_factory.h>
#include <liblbt/api/api_models.h>

#ifdef VIMBAX_ENABLED
#include <liblbt/vimbax_frame_source.h>
#endif

using namespace lbt;

// ---------------------------------------------------------------------------
// Args
// ---------------------------------------------------------------------------
struct Args {
    std::string camerasDir   = "data/cameras";
    std::string pipelinesDir = "data/pipelines";
    std::string pipelineUID;
    std::string ctiPath      = "../../vimbax/cti/VimbaGigETL.cti";
    double durationSeconds   = 600.0;
    double targetFPS         = 10.0;
    std::string outputDir    = "/tmp/record3k-bench";
    bool saveFrames          = false;  // --save-frames: save raw JPEGs to disk (DEBUG mode)
};

static void printUsage(const char* prog) {
    std::cout
        << "Usage: " << prog << " --pipeline <uid> [options]\n"
        << "\nOptions:\n"
        << "  --cameras-dir <path>    Camera JSON directory    (default: data/cameras)\n"
        << "  --pipelines-dir <path>  Pipeline JSON directory  (default: data/pipelines)\n"
        << "  --pipeline <uid>        Pipeline UID (required)\n"
        << "  --cti <path>            VimbaX .cti file         (default: ../../vimbax/cti/VimbaGigETL.cti)\n"
        << "  --duration <seconds>    Recording duration        (default: 600)\n"
        << "  --fps <value>           Target frame rate         (default: 10)\n"
        << "  --output-dir <path>     Base output directory     (default: /tmp/record3k-bench)\n"
        << "  --save-frames           Save raw frames to disk (DEBUG mode; default: off / PRODUCTION)\n"
        << "  --help                  Show this help\n";
}

static Args parseArgs(int argc, char* argv[]) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--help") { printUsage(argv[0]); std::exit(0); }
        else if (a == "--cameras-dir"   && i+1 < argc) args.camerasDir    = argv[++i];
        else if (a == "--pipelines-dir" && i+1 < argc) args.pipelinesDir  = argv[++i];
        else if (a == "--pipeline"      && i+1 < argc) args.pipelineUID   = argv[++i];
        else if (a == "--cti"           && i+1 < argc) args.ctiPath       = argv[++i];
        else if (a == "--duration"      && i+1 < argc) args.durationSeconds = std::stod(argv[++i]);
        else if (a == "--fps"           && i+1 < argc) args.targetFPS     = std::stod(argv[++i]);
        else if (a == "--output-dir"    && i+1 < argc) args.outputDir     = argv[++i];
        else if (a == "--save-frames")                    args.saveFrames   = true;
        else { std::cerr << "[test7] Unknown argument: " << a << "\n"; printUsage(argv[0]); std::exit(1); }
    }
    return args;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static std::string nowISO8601() {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    std::tm buf{};
    gmtime_r(&t, &buf);
    std::ostringstream oss;
    oss << std::put_time(&buf, "%Y%m%dT%H%M%SZ");
    return oss.str();
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    Args args = parseArgs(argc, argv);

    std::cout << "=== test7 — Multi-Camera Benchmark Recording ===" << std::endl;

    // -----------------------------------------------------------------------
    // 1. Validate required arguments
    // -----------------------------------------------------------------------
    if (args.pipelineUID.empty()) {
        std::cerr << "[test7] ERROR: --pipeline <uid> is required\n";
        printUsage(argv[0]);
        return 1;
    }

    // -----------------------------------------------------------------------
    // 2. Load pipeline config (validate it exists before touching cameras)
    // -----------------------------------------------------------------------
    std::string pipelinePath = args.pipelinesDir + "/" + args.pipelineUID + ".json";
    if (!std::filesystem::exists(pipelinePath)) {
        std::cerr << "[test7] ERROR: Pipeline file not found: " << pipelinePath << std::endl;
        return 1;
    }
    std::cout << "\n[test7] Loading pipeline: " << pipelinePath << std::endl;
    try {
        auto probe = PipelineConfigLoader::loadFromJSON(pipelinePath);
        std::cout << "[test7] Pipeline OK: \"" << probe.description
                  << "\"  steps=" << probe.steps.size() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[test7] ERROR: Failed to parse pipeline: " << e.what() << std::endl;
        return 1;
    }

    // -----------------------------------------------------------------------
    // 3. Load camera JSON files
    // -----------------------------------------------------------------------
    std::cout << "\n[test7] Loading cameras from: " << args.camerasDir << std::endl;
    if (!std::filesystem::exists(args.camerasDir)) {
        std::cerr << "[test7] ERROR: cameras directory does not exist: " << args.camerasDir << std::endl;
        return 1;
    }
    CameraManager camMgr(args.camerasDir);
    camMgr.load();
    auto allCameras = camMgr.cameras();
    std::cout << "[test7] Loaded " << allCameras.size() << " camera definition(s) total" << std::endl;

    std::vector<api::CameraInfo> vimbaCameras;
    for (const auto& cam : allCameras) {
        if (cam.cameraType == "TYPE_CAM_VIMBA")
            vimbaCameras.push_back(cam);
    }
    if (vimbaCameras.empty()) {
        std::cerr << "[test7] ERROR: No TYPE_CAM_VIMBA cameras found in " << args.camerasDir << std::endl;
        return 1;
    }
    std::cout << "[test7] " << vimbaCameras.size() << " VimbaX camera(s) in config" << std::endl;

#ifdef VIMBAX_ENABLED
    // -----------------------------------------------------------------------
    // 4. Start VimbaX system
    // -----------------------------------------------------------------------
    std::cout << "\n[test7] Starting CameraManagerVimbax..." << std::endl;
    auto& vmbMgr = CameraManagerVimbax::getInstance();
    if (!vmbMgr.startup(args.ctiPath)) {
        std::cerr << "[test7] ERROR: Failed to start VimbaX system" << std::endl;
        return 1;
    }
    std::cout << "[test7] Waiting 5s for GigE broadcast discovery..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // -----------------------------------------------------------------------
    // 5. Probe each VimbaX camera
    // -----------------------------------------------------------------------
    struct CameraProbe {
        api::CameraInfo info;
        std::shared_ptr<VimbaXFrameSource> source;
        bool online = false;
    };
    std::vector<CameraProbe> probed;

    std::cout << "\n[test7] Probing " << vimbaCameras.size() << " camera(s)..." << std::endl;
    for (const auto& cam : vimbaCameras) {
        CameraProbe p;
        p.info = cam;
        std::string serial = cam.serialNumber.value_or("");
        std::string ip     = cam.ip.value_or("");
        std::cout << "  " << cam.camUID
                  << "  mac=" << cam.macAddress
                  << "  ip=" << ip << " ... " << std::flush;

        p.source = std::make_shared<VimbaXFrameSource>(serial, cam.macAddress, /*debug=*/false, ip);
        p.online = p.source->isReady();

        std::cout << (p.online ? "ONLINE" : "OFFLINE") << std::endl;
        if (!p.online)
            p.source.reset();

        probed.push_back(std::move(p));
    }

    // -----------------------------------------------------------------------
    // 6. Summary + user confirmation
    // -----------------------------------------------------------------------
    int onlineCount  = 0;
    int offlineCount = 0;
    std::cout << "\n=== Camera Probe Summary ===" << std::endl;
    for (const auto& p : probed) {
        std::cout << "  [" << (p.online ? "ONLINE " : "OFFLINE") << "]  "
                  << p.info.camUID
                  << "  mac=" << p.info.macAddress;
        if (p.info.ip) std::cout << "  ip=" << *p.info.ip;
        std::cout << std::endl;
        if (p.online) ++onlineCount; else ++offlineCount;
    }
    std::cout << "\n  Total: " << onlineCount << " online, " << offlineCount << " offline" << std::endl;

    if (onlineCount == 0) {
        std::cerr << "[test7] No cameras are online — nothing to record." << std::endl;
        vmbMgr.shutdown();
        return 1;
    }

    std::cout << "\nPipeline:   " << args.pipelineUID << std::endl;
    std::cout << "Duration:   " << args.durationSeconds << "s  ("
              << std::fixed << std::setprecision(1) << (args.durationSeconds / 60.0) << " min)" << std::endl;
    std::cout << "Target FPS: " << args.targetFPS << std::endl;
    std::cout << "Output dir: " << args.outputDir << std::endl;
    std::cout << "Log mode:   " << (args.saveFrames ? "DEBUG (frames saved)" : "PRODUCTION (no frame files)") << std::endl;
    std::cout << "\nStart recording on " << onlineCount << " camera(s)? [y/N] " << std::flush;

    std::string answer;
    std::getline(std::cin, answer);
    answer.erase(std::remove_if(answer.begin(), answer.end(), ::isspace), answer.end());
    std::transform(answer.begin(), answer.end(), answer.begin(), ::tolower);

    if (answer != "y" && answer != "yes") {
        std::cout << "[test7] Aborted by user." << std::endl;
        vmbMgr.shutdown();
        return 0;
    }

    // -----------------------------------------------------------------------
    // 7. Create and start one RecordManager per online camera
    // -----------------------------------------------------------------------
    std::string ts = nowISO8601();
    std::vector<std::unique_ptr<RecordManager>> recorders;
    std::vector<std::string> recorderUIDs;

    std::cout << "\n[test7] Starting recordings..." << std::endl;
    for (auto& p : probed) {
        if (!p.online) continue;

        // Each camera gets its own fresh pipeline instance so stateful steps
        // (BackgroundSubtractor, etc.) don't share internal state across cameras.
        PipelineConfigLoader::PipelineConfig camPipe;
        try {
            camPipe = PipelineConfigLoader::loadFromJSON(pipelinePath);
        } catch (const std::exception& e) {
            std::cerr << "[test7] WARNING: Could not reload pipeline for "
                      << p.info.camUID << ": " << e.what() << " — skipping" << std::endl;
            continue;
        }

        std::string uid = "bench-" + p.info.camUID + "-" + ts;

        RecordManager::RecordConfig cfg;
        cfg.frameSource      = p.source;
        cfg.pipeline         = std::move(camPipe);
        cfg.totalTimeSeconds = args.durationSeconds;
        cfg.targetFPS        = args.targetFPS;
        cfg.recordUID        = uid;
        cfg.cameraUID        = p.info.camUID;
        cfg.outputDir        = args.outputDir;
        cfg.logMode          = args.saveFrames ? ResultLogger::LogMode::DEBUG
                                               : ResultLogger::LogMode::PRODUCTION;

        auto rec = std::make_unique<RecordManager>(std::move(cfg));
        if (!rec->start()) {
            std::cerr << "[test7] WARNING: Failed to start recording for "
                      << p.info.camUID << std::endl;
            continue;
        }
        std::cout << "  [STARTED] " << uid << std::endl;
        recorderUIDs.push_back(uid);
        recorders.push_back(std::move(rec));
    }

    if (recorders.empty()) {
        std::cerr << "[test7] ERROR: No recordings could be started." << std::endl;
        vmbMgr.shutdown();
        return 1;
    }

    std::cout << "\n[test7] " << recorders.size() << " recording(s) running — "
              << args.durationSeconds << "s each\n" << std::endl;

    // -----------------------------------------------------------------------
    // 8. Monitor loop — print progress table every 5 s
    // -----------------------------------------------------------------------
    auto allDone = [&]() {
        for (auto& r : recorders)
            if (r->getState() == RecordManager::State::RECORDING) return false;
        return true;
    };

    while (!allDone()) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::cout << "\n--- " << nowISO8601() << " ---\n";
        std::cout << std::left
                  << std::setw(34) << "RecordUID"
                  << std::setw(13) << "State"
                  << std::setw(9)  << "Frames"
                  << std::setw(9)  << "AvgFPS"
                  << "\n" << std::string(65, '-') << "\n";
        for (size_t i = 0; i < recorders.size(); ++i) {
            auto stats = recorders[i]->getStatistics();
            std::cout << std::left
                      << std::setw(34) << recorderUIDs[i]
                      << std::setw(13) << recorders[i]->getStateString()
                      << std::setw(9)  << stats.framesRecorded
                      << std::setw(9)  << std::fixed << std::setprecision(2) << stats.actualFPS
                      << "\n";
        }
        std::cout << std::flush;
    }

    // -----------------------------------------------------------------------
    // 9. Final summary
    // -----------------------------------------------------------------------
    std::cout << "\n=== Final Recording Summary ===" << std::endl;
    int successCount = 0;
    for (size_t i = 0; i < recorders.size(); ++i) {
        auto stats = recorders[i]->getStatistics();
        bool ok    = (recorders[i]->getState() == RecordManager::State::STOPPED);
        if (ok) ++successCount;
        std::cout << "  " << recorderUIDs[i]
                  << "\n    State:   " << recorders[i]->getStateString()
                  << "\n    Frames:  " << stats.framesRecorded
                  << "\n    Avg FPS: " << std::fixed << std::setprecision(2) << stats.actualFPS
                  << "\n    Output:  " << recorders[i]->getOutputPath().string()
                  << "\n    Result:  " << (ok ? "SUCCESS" : "FAILURE")
                  << "\n";
    }
    std::cout << "\n  " << successCount << "/" << recorders.size()
              << " recordings completed successfully." << std::endl;

    // -----------------------------------------------------------------------
    // 10. Cleanup
    // -----------------------------------------------------------------------
    const int totalStarted = static_cast<int>(recorders.size());
    recorders.clear();  // ~RecordManager calls stop() for each
    vmbMgr.shutdown();

    std::cout << "\n=== test7 complete ===" << std::endl;
    return (successCount == totalStarted) ? 0 : 1;

#else
    (void)args;
    std::cerr << "[test7] ERROR: Built without VIMBAX_ENABLED.\n"
              << "         Rebuild without --no-vimbax:\n"
              << "           bash localBuild.sh -j8\n";
    return 1;
#endif
}
