# ResultLogger Class - Implementation Summary

## Overview
Created a new `ResultLogger` class to handle comprehensive logging of frame processing results during recording sessions. The logger is integrated into `RecordManager` and provides different logging modes for debugging and production use.

## Files Created

### 1. `/src/liblbt/include/liblbt/result_logger.h`
Header file defining the `ResultLogger` class with:
- **LogMode enum**: NONE, DEBUG, PRODUCTION
- **Config struct**: Configuration for logging behavior
- **Public methods**:
  - `logFrame()`: Log processed frames and input frames
  - `logMetadata()`: Log frame metadata to file
  - `writeSummary()`: Write final summary statistics

### 2. `/src/liblbt/src/result_logger.cpp`
Implementation providing:
- Directory structure creation (`others/debug/`)
- Metadata logging to `others/metadata_log.txt`
- Debug image generation with mass center visualization
- Summary generation with statistics

## Integration with RecordManager

### Changes to `record_manager.h`:
- Added `#include <liblbt/result_logger.h>`
- Added `logMode` parameter to `RecordConfig` struct
- Added `std::unique_ptr<ResultLogger> m_resultLogger` member

### Changes to `record_manager.cpp`:
- Initialize `ResultLogger` in constructor based on `logMode`
- Call `m_resultLogger->logFrame()` for each processed frame
- Call `m_resultLogger->writeSummary()` at recording completion
- Removed manual debug image saving code (now handled by ResultLogger)

### Changes to `do_controller.cpp`:
- Set `recordConfig.logMode = lbt::ResultLogger::LogMode::DEBUG` in `recordStart()`

## DEBUG Mode Features

When enabled, the ResultLogger creates:

1. **Directory Structure**:
   ```
   file-rec-{recordUID}/
   ├── frames/              (processed frames from pipeline)
   ├── others/
   │   ├── debug/
   │   │   ├── input_000000.jpg  (original input frames)
   │   │   ├── mass_center_000000.jpg  (debug visualizations)
   │   │   └── ...
   │   ├── log.txt          (recording log)
   │   ├── metadata_log.txt (frame-by-frame metadata)
   │   └── result_logger_summary.txt
   ```

2. **Metadata Log** (`metadata_log.txt`):
   ```
   [000000] TS:123456789ns | mass_center_pixel=(320.5,240.2) mass_center_world_mm=(45.23,67.89)
   [000001] TS:123456890ns | mass_center_pixel=(321.1,240.8) mass_center_world_mm=(45.67,68.12)
   ```

3. **Debug Visualizations**:
   - Original input frame saved
   - Mass center marked with red circle
   - Green crosshair overlay
   - Coordinate text annotation

4. **Summary File** (`result_logger_summary.txt`):
   - Completion timestamp
   - Total frames logged
   - Duration and average FPS
   - Configuration settings

## Usage

### Enable DEBUG logging:
```cpp
recordConfig.logMode = lbt::ResultLogger::LogMode::DEBUG;
```

### Disable logging:
```cpp
recordConfig.logMode = lbt::ResultLogger::LogMode::NONE;  // Default
```

### PRODUCTION mode (future):
```cpp
recordConfig.logMode = lbt::ResultLogger::LogMode::PRODUCTION;
```

## Benefits

1. **Centralized logging**: All result logging in one class
2. **Configurable**: Easy to switch between modes
3. **Organized output**: Structured directory layout
4. **Extensible**: Easy to add new metadata types
5. **Performance**: Only processes what's needed for the selected mode
6. **Debugging**: Complete frame-by-frame analysis capability

## Next Steps

To compile and test:
```bash
cd <repo-root>
./localBuild.sh --no-cuda --no-vimbax -j 8
```

The logger will automatically create debug output for all recordings started via the API.
