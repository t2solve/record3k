# VimbaX Acquisition: AcquireSingleImage vs Continuous Streaming

## Background

All git history of `VimbaXFrameSource` has used `AcquireSingleImage` exclusively.
This document captures an analysis of why this causes problems at scale and what the
recommended migration path is.

---

## What `AcquireSingleImage` Does Internally

Every call executes a full DMA lifecycle:

```
AnnounceFrame → QueueFrame → StartCapture → CaptureFrameWait → FlushCaptureQueue → RevokeAllFrames → EndCapture
```

This means the GigE stream is **torn down and restarted on every frame**.
At 10 FPS with 6 cameras, that is 60 teardown/announce cycles per second
hitting the switch and NIC simultaneously — the root cause of intermittent
`VmbErrorTimeout` returns.

**Observed symptom (test4 log, 2026-04-12):**

```
[VimbaXFrameSource] AcquireSingleImage timeout
[RecordManager] Failed to acquire frame, stopping
Final state: FAILURE
Frames recorded: 2
```

---

## Recommended Alternative: Manual Continuous Streaming (`CaptureFrameWait`)

This is the lower-level VmbCPP API that matches a pull-based `nextFrame()` interface
while keeping the DMA stream permanently alive.

### Lifecycle

| Phase | API calls |
|---|---|
| `initialize()` | `AnnounceFrame` × N (3 buffers recommended), `StartCapture`, `QueueFrame` × N, set `AcquisitionFrameRate` feature |
| `nextFrame()` | `CaptureFrameWait(timeout)` → copy pixels → `QueueFrame` (re-queue same slot) |
| `shutdown()` | `FlushCaptureQueue`, `EndCapture`, `RevokeAllFrames` |

The DMA engine runs continuously. No per-frame teardown.

---

## Side-by-Side Comparison

| Criterion | `AcquireSingleImage` (current) | `CaptureFrameWait` continuous |
|---|---|---|
| Per-frame driver overhead | Full start/stop cycle | Near zero — just wait for next filled buffer slot |
| GigE stability | Fragile — stream teardown causes packet loss | Stable — stream kept hot between frames |
| FPS control | External `sleep` in RecordManager; imprecise | Camera HW feature `AcquisitionFrameRate` governs rate; RecordManager sleep is a safety guard only |
| 6-camera scaling | Poor — 60 teardown cycles/s under NIC contention | Good — 6 independent stable streams |
| `nextFrame()` interface fit | Natural | Natural — `CaptureFrameWait` is still a blocking call |
| Risk / failure modes | Timeouts on transient packet loss | Must re-queue frame after each use; must flush correctly on stop |
| Code complexity | Low (1 buffer, no state) | Medium — lifecycle state machine in `.cpp` |

---

## FPS Accuracy

### Current approach
FPS is controlled entirely by `RecordManager`'s sleep loop.  
The camera free-runs at max firmware FPS internally; the per-frame teardown/restart
cycle is what actually determines the real delivery rate — imprecise and sensitive
to CPU scheduling jitter.

### Continuous mode
Set once during `initialize()`:
```cpp
FeaturePtr fpsEnable, fps;
camera_->GetFeatureByName("AcquisitionFrameRateEnable", fpsEnable);
fpsEnable->SetValue(true);
camera_->GetFeatureByName("AcquisitionFrameRate", fps);
fps->SetValue(targetFps_);
```
The camera firmware timestamps and gates frames to exactly the target FPS in hardware.
`CaptureFrameWait` delivers them at that cadence deterministically.
RecordManager's sleep loop becomes a backstop for slow processing, not the
primary clock.

---

## Estimated Scope of Changes

| File | What changes | Approx. lines |
|---|---|---|
| `src/liblbt/include/liblbt/vimbax_frame_source.h` | Add `framePool_` (`std::vector<FramePtr>`), `captureStarted_` bool, `targetFps_` double; remove `preallocFrame_`; rename `ensurePreallocFrame` | ~15 |
| `src/liblbt/src/vimbax_frame_source.cpp` | Rewrite end of `initialize()` (announce+start+queue+fps feature), rewrite `nextFrame()` (CaptureFrameWait+re-queue), rewrite `shutdown()` (flush+end+revoke) | ~90 |
| `src/liblbt/src/record_manager.cpp` | Pass `targetFPS` into `VimbaXFrameSource` constructor so it can set the camera feature | ~5 |

**Total: ~110 lines changed.**  
The public `IFrameSource` interface (`nextFrame()`, `isReady()`) is **unchanged**.  
`test4/main.cpp` (now on branch `archive/test-targets`) and all callers are **unchanged**.

---

## Short-term Workaround Already Applied

While this refactor is pending, two mitigations were added (2026-04-12):

1. **`vimbax_frame_source.cpp` — `nextFrame()` retries:**  
   Timeout increased from 1000 ms to 3000 ms; up to 3 retry attempts on
   `VmbErrorTimeout` before returning `nullptr`. Non-timeout errors still fail fast.

2. **`record_manager.cpp` — consecutive-failure tolerance:**  
   Up to 5 consecutive `nullptr` returns are logged as warnings and skipped
   (`continue`). Only after 5 in a row does the recording abort with `FAILURE`.

These do not fix the underlying DMA teardown instability; they paper over
transient packet loss until the continuous acquisition refactor is done.

---

## References

- VmbCPP Asynchronous example: `vimbax/api/examples/VmbCPP/AsynchronousGrab/`
- VmbCPP Synchronous example: `vimbax/api/examples/VmbCPP/SynchronousGrab/`
- Allied Vision VmbCPP header: `vimbax/api/include/VmbCPP/Camera.h`
