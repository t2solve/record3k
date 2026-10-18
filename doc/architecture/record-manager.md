# RecordManager Class Documentation

## Overview

The `RecordManager` class provides a high-level interface for managing recording sessions with automatic frame acquisition, pipeline processing, and output management.

## Features

- **Thread-safe recording**: Runs in a separate thread with graceful shutdown
- **FPS control**: Maintains target frame rate using timing logic from the former test4 program (branch `archive/test-targets`)
- **Pipeline processing**: Integrates with existing ProcessingPipeline infrastructure
- **State management**: Clear state transitions (preparation → recording → stopped → packed)
- **Statistics tracking**: Real-time FPS monitoring and frame counting
- **Automatic packaging**: Compress recordings into tar.gz archives
- **Metadata generation**: Automatically saves record metadata as JSON

## State Flow

```
UNKNOWN (default)
    ↓
PREPARATION (initialization)
    ↓
RECORDING (actively recording)
    ↓
STOPPED (recording completed) or FAILURE (error occurred)
    ↓
PACKED (archived to data/files)
```

## Output Structure

When recording with UID `test-001`, the following structure is created in `/tmp/record3k/`:

```
file-rec-test-001/
├── record-test-001.json      # Recording metadata (timestamps, FPS, stats)
├── study-test-001.json        # Optional: study information
├── pipeline-test-001.json     # Pipeline configuration used
├── frames/                    # Output frames from FrameWriter step
│   ├── frame_0001.jpg
│   ├── frame_0002.jpg
│   └── ...
└── others/                    # Optional files
    └── calibration-test-001.xml
```

After calling `pack()`, this directory is compressed to:
```
data/files/file-rec-test-001.tar.gz
```

## Usage Example

### Basic Recording

```cpp
#include <liblbt/record_manager.h>
#include <liblbt/opencv_frame_source.h>
#include <liblbt/process_step_factory.h>

using namespace lbt;

// 1. Create frame source
auto frameSource = std::make_shared<OpenCVFrameSource>(0);

// 2. Create pipeline
std::vector<std::shared_ptr<ProcessStep>> pipeline;
std::vector<FilterConfig> configs;

pipeline.push_back(ProcessStepFactory::createFrameWriter(ProcessingMode::CPU));
FilterConfig config;
config.setParameter("output_dir", "/tmp/record3k/file-rec-test-001/frames");
config.setParameter("format", "jpg");
configs.push_back(config);

PipelineConfigLoader::PipelineConfig pipeConfig;
pipeConfig.steps = pipeline;
pipeConfig.configs = configs;
pipeConfig.mode = ProcessingMode::CPU;

// 3. Configure recording
RecordManager::RecordConfig recordConfig;
recordConfig.frameSource = frameSource;
recordConfig.pipeline = pipeConfig;
recordConfig.totalTimeSeconds = 60.0;  // 1 minute
recordConfig.targetFPS = 30.0;         // 30 FPS
recordConfig.recordUID = "test-001";
recordConfig.outputDir = "/tmp/record3k";

// 4. Create and start recording
RecordManager recorder(std::move(recordConfig));
recorder.start();

// 5. Monitor progress
while (recorder.getState() == RecordManager::State::RECORDING) {
    auto stats = recorder.getStatistics();
    std::cout << "Recorded " << stats.framesRecorded << " frames" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

// 6. Pack recording
if (recorder.getState() == RecordManager::State::STOPPED) {
    auto archivePath = recorder.pack();
    std::cout << "Archived to: " << *archivePath << std::endl;
}
```

### With Calibration and Study Files

```cpp
RecordManager::RecordConfig recordConfig;
// ... basic configuration ...
recordConfig.calibrationFile = "data/calibrations/cal-001.xml";
recordConfig.studyFile = "data/studies/study-001.json";

RecordManager recorder(std::move(recordConfig));
recorder.start();
```

### Manual Stop

```cpp
RecordManager recorder(recordConfig);
recorder.start();

// Record for some time...
std::this_thread::sleep_for(std::chrono::seconds(10));

// Stop early
recorder.stop();
```

## API Reference

### Constructor

```cpp
explicit RecordManager(RecordConfig config);
```

Creates a RecordManager instance with the given configuration. Initial state is `PREPARATION`.

### Public Methods

#### `bool start()`
Starts the recording in a separate thread.
- **Returns**: `true` if started successfully, `false` on error
- **Thread-safe**: Yes
- **State requirement**: `PREPARATION` or `STOPPED`

#### `void stop()`
Stops the recording gracefully and waits for the thread to finish.
- **Thread-safe**: Yes
- **Blocking**: Yes (waits for thread to complete)

#### `std::optional<std::filesystem::path> pack()`
Compresses the recording directory into a tar.gz archive and moves it to `data/files/`.
- **Returns**: Path to archive on success, `nullopt` on failure
- **Thread-safe**: Yes
- **State requirement**: `STOPPED`
- **Final state**: `PACKED` on success, `FAILURE` on error

#### `State getState() const`
Returns the current state.
- **Thread-safe**: Yes

#### `std::string getStateString() const`
Returns human-readable state name.
- **Thread-safe**: Yes

#### `std::filesystem::path getOutputPath() const`
Returns the output directory path for this recording.
- **Thread-safe**: Yes

#### `Statistics getStatistics() const`
Returns current recording statistics.
- **Thread-safe**: Yes

### RecordConfig Structure

```cpp
struct RecordConfig {
    std::shared_ptr<IFrameSource> frameSource;      // Required
    PipelineConfigLoader::PipelineConfig pipeline;  // Required
    double totalTimeSeconds;                         // Required
    double targetFPS;                                // Required
    std::optional<std::string> calibrationFile;     // Optional
    std::optional<std::string> studyFile;           // Optional
    std::string recordUID;                           // Required (unique identifier)
    std::string outputDir = "/tmp/record3k";        // Optional (default provided)
};
```

### Statistics Structure

```cpp
struct Statistics {
    int framesRecorded = 0;
    double actualFPS = 0.0;
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point endTime;
    bool completed = false;
};
```

## Record Metadata (JSON)

The `record-<uid>.json` file contains:

```json
{
  "recordUID": "test-001",
  "targetFPS": 30.0,
  "targetDurationSeconds": 60.0,
  "framesRecorded": 1800,
  "actualFPS": 29.87,
  "completed": true,
  "datetimeStart": "2026-01-21T10:30:00Z",
  "datetimeEnd": "2026-01-21T10:31:00Z",
  "calibrationFile": "data/calibrations/cal-001.xml",
  "studyFile": "data/studies/study-001.json",
  "state": "STOPPED"
}
```

## FPS Timing Logic

The recording loop implements the same FPS control logic as the former `test4/main.cpp` (branch `archive/test-targets`):

1. Calculate target interval: `interval = 1000ms / targetFPS`
2. Process frame through pipeline
3. Calculate actual FPS using exponential moving average (α=0.1)
4. Sleep for remaining time to maintain target FPS

```cpp
auto interval = std::chrono::milliseconds(static_cast<int>(1000.0 / targetFPS));
auto processingTime = std::chrono::steady_clock::now() - frameStartTime;
auto sleepTime = interval - processingTime;
if (sleepTime.count() > 0) {
    std::this_thread::sleep_for(sleepTime);
}
```

## Error Handling

The class transitions to `FAILURE` state on:
- Invalid configuration (no frame source, empty pipeline)
- Frame source not ready
- Frame acquisition failure
- Pipeline processing exception
- Directory creation failure

Check state after operations:
```cpp
if (recorder.getState() == RecordManager::State::FAILURE) {
    std::cerr << "Recording failed!" << std::endl;
}
```

## Thread Safety

- All public methods are thread-safe
- State changes are atomic
- Statistics access is protected by mutex
- Recording runs in separate thread with graceful shutdown

## Integration with Existing Code

### With VimbaX Cameras

```cpp
#include <liblbt/vimbax_frame_source.h>

auto frameSource = std::make_shared<VimbaXFrameSource>(
    "", 
    "00:0F:31:03:38:D4",  // MAC address
    true                   // debug mode
);

recordConfig.frameSource = frameSource;
```

### With FrameSourceFactory

```cpp
#include <liblbt/factory/frame_source_factory.h>
#include <liblbt/factory/model_factory.h>

// Load camera from JSON
auto cameraResult = ModelFactory::fromJsonCamera(cameraJson);
auto frameSource = FrameSourceFactory::createFromCameraInfo(
    cameraResult.value(), 
    true  // debug mode
);

recordConfig.frameSource = frameSource;
```

### Custom Pipeline

```cpp
// Multi-step pipeline
pipeline.push_back(ProcessStepFactory::createGaussianBlur(mode));
FilterConfig blurConfig;
blurConfig.setParameter("kernel_size", 5.0);
configs.push_back(blurConfig);

pipeline.push_back(ProcessStepFactory::createBackgroundSubtraction(mode));
FilterConfig bgsConfig;
bgsConfig.setParameter("history", 500.0);
bgsConfig.setParameter("threshold", 16.0);
configs.push_back(bgsConfig);

pipeline.push_back(ProcessStepFactory::createFrameWriter(mode));
FilterConfig writerConfig;
writerConfig.setParameter("output_dir", outputDir);
writerConfig.setParameter("format", "png");
configs.push_back(writerConfig);
```

## Files

- Header: [src/liblbt/include/liblbt/record_manager.h](../../src/liblbt/include/liblbt/record_manager.h)
- Implementation: [src/liblbt/src/record_manager.cpp](../../src/liblbt/src/record_manager.cpp)
- Caller: `src/api/controllers/do_controller.cpp` (record start/stop endpoints)

## Dependencies

- C++17 (for `std::filesystem`, `std::optional`)
- OpenCV (for pipeline processing)
- JsonCpp (for metadata generation)
- Standard threading library (`<thread>`, `<mutex>`, `<atomic>`)
- System `tar` command (for packing)

## Building

The RecordManager is automatically included in the liblbt library build. No changes to build files are needed.

## Notes

- The recording thread will automatically stop after `totalTimeSeconds` elapsed
- Call `stop()` to halt recording early
- `pack()` can only be called after recording is `STOPPED`
- The destructor automatically calls `stop()` to ensure clean shutdown
- Frame numbering and file naming is handled by the FrameWriter step
