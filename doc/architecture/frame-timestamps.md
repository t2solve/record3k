# Frame Timestamp Discussion — VimbaX GigE Cameras

## Background

Allied Vision GigE cameras expose two ways to obtain a per-frame hardware timestamp.
Both are based on the camera's internal free-running tick counter
(`GevTimestampTickFrequency`, 1 GHz on Allied Vision → **1 tick = 1 ns**).
The counter starts at camera power-on and never wraps within a practical recording
session (64-bit at 1 GHz overflows after ~585 years).

The value is **not** a wall-clock datetime.  It is a monotonic offset in
nanoseconds from camera boot, shared across all frames produced by the same camera.
Inter-camera alignment requires a separate PTP/IEEE-1588 or PPS synchronisation step.

## Observed behaviour — Alvium G1-240c (SFNC 2.7.1, 2026-04-19)

| Method | Frames | % |
|---|---|---|
| Method 1 — GVSP-embedded | 7 | 1 % |
| Method 2 — TimestampLatch fallback | 693 | 99 % |
| Missed — both zero | 0 | 0 % |

**Summary:** 0 missed timestamps — all frames receive a camera hardware timestamp.
Method 2 dominates because the Alvium G1-240c does **not** populate the
`VmbFrameFlagsTimestamp` field in the GVSP stream leader and does **not** expose
`ChunkModeActive` (chunk data is unsupported on this model), so Method 1 only fires
when the transport layer happens to embed the field (rarely).  The SFNC 2.x latch
feature names (`TimestampLatch` / `TimestampLatchValue`) are used because the
legacy GEV 1.x names (`GevTimestampControlLatch` / `GevTimestampValue`) are absent
on SFNC 2.7+ cameras.

The 1–50 ms timestamp offset introduced by Method 2 is acceptable for 10 FPS
recording (100 ms inter-frame interval) but would be unacceptable for
sub-millisecond stereo matching.

---

## Method 1 — GVSP-Embedded Timestamp (preferred)

```
Camera HW trigger
      │
      ▼
Camera stamps tick counter value into GVSP stream leader packet
      │
      ▼  (travels over network — latency does NOT affect the stored value)
Host receives DMA callback → VmbFrame_t::timestamp is already populated
      │
      ▼
frame->GetTimestamp(ticks)   // reads from local buffer, zero network cost
```

**How it works:**  
The camera's image sensor controller latches the tick counter at the exact moment
the frame is triggered and embeds it into the GVSP stream leader packet header.
The value is fixed at capture time and carried passively through the network.

**Transport layer flag:**  
The C struct `VmbFrame_t::receiveFlags` contains `VmbFrameFlagsTimestamp = 0x08`.
`GetTimestamp()` returns `VmbErrorSuccess` but `ticks == 0` when this flag is not
set — meaning the transport layer did not extract the field from the GVSP header
(can happen with some TL versions or non-standard GigE switches that mangle headers).

**Chunk mode (optional enhancement):**  
On cameras that support `ChunkModeActive`, enabling the Timestamp chunk guarantees
the tick counter is carried in the frame payload rather than only in the stream
leader.  `startContinuousCapture()` attempts this automatically before
`GetPayloadSize()`; if the camera reports `ChunkModeActive` not found (e.g.
Alvium G1-240c), capture continues without chunks and Method 2 takes over.

**Network hops:**  
➜ **No effect.**  The timestamp is embedded by the camera before the first packet
leaves the NIC.  Routers, switches, VPN tunnels, and hop count are irrelevant.

---

## Method 2 — TimestampLatch Fallback

```
Host receives DMA callback (frame already in buffer)
      │
      ▼
Host sends GEV command TimestampLatch → network → camera
      │
      ▼  (camera latches current tick counter into TimestampLatchValue register)
Camera sends GEV ACK → network → host
      │
      ▼
Host reads TimestampLatchValue    ← this is the latch time, NOT capture time
```

**How it works:**  
Used only when Method 1 returns `ticks == 0`.  The host sends a request-response
round-trip to the camera asking it to snapshot its current tick counter.
The resulting value reflects **when the latch command was processed**, not when
the frame was captured — the frame may have been sitting in the DMA buffer for
several hundred microseconds to tens of milliseconds before the callback fires.

**Feature name compatibility:**  
- **GEV 1.x / legacy cameras:** `GevTimestampControlLatch` (Command) + `GevTimestampValue` (Int64)  
- **SFNC 2.x cameras (e.g. Alvium G1-240c, SFNC 2.7.1):** `TimestampLatch` (Command) + `TimestampLatchValue` (Int64)  

The implementation tries GEV 1.x names first, then falls back to SFNC 2.x names.

**Network hops:**  
➜ **Each additional hop adds propagation + switching latency to the round-trip.**

> ⚠️ **WARNING — multi-hop networks:**  If the camera is accessed through a VPN,
> VLAN, or multiple managed switches, the Method 2 timestamp offset can reach
> **tens of milliseconds**.  At 10 FPS (100 ms inter-frame), this is still within
> one frame period.  At higher frame rates or for stereo matching, consider
> enabling PTP (see Recommendations).

Typical round-trip values:

| Topology | Round-trip (approx.) | Timestamp offset |
|---|---|---|
| Direct GigE (1 hop) | 100–300 µs | small |
| 1 managed switch (2 hops) | 200–600 µs | moderate |
| 2 switches / VLAN (3 hops) | 400–1200 µs | significant |
| VPN tunnel (many hops) | 1–50 ms | large — may exceed one frame period |

The offset is deterministic per-session (network path is stable) but not constant
across sessions or camera restarts.

---

## Implementation in `vimbax_frame_source.cpp`

```cpp
// Inside FrameObserver::FrameReceived() — called from VimbaX DMA thread

VmbUint64_t ticks = 0;

// Method 1: read from the received frame buffer (zero cost, zero latency)
frame->GetTimestamp(ticks);

// Method 2: fallback if TL did not populate VmbFrameFlagsTimestamp
if (ticks == 0 && m_pCamera) {
    FeaturePtr latchCmd, tickVal;
    // GEV 1.x feature names (older cameras / legacy TL)
    if (m_pCamera->GetFeatureByName("GevTimestampControlLatch", latchCmd) == VmbErrorSuccess &&
        m_pCamera->GetFeatureByName("GevTimestampValue",        tickVal)  == VmbErrorSuccess) {
        latchCmd->RunCommand();
        VmbInt64_t t = 0;
        tickVal->GetValue(t);
        ticks = static_cast<VmbUint64_t>(t);
    }
    // SFNC 2.x feature names (e.g. Allied Vision Alvium with SFNC 2.7+)
    if (ticks == 0) {
        FeaturePtr sfncLatch, sfncVal;
        if (m_pCamera->GetFeatureByName("TimestampLatch",      sfncLatch) == VmbErrorSuccess &&
            m_pCamera->GetFeatureByName("TimestampLatchValue", sfncVal)   == VmbErrorSuccess) {
            sfncLatch->RunCommand();
            VmbInt64_t t = 0;
            sfncVal->GetValue(t);
            ticks = static_cast<VmbUint64_t>(t);
        }
    }
}
```

Ticks are converted to nanoseconds in `nextFrame()`:

```cpp
// tickFreqHz_ is read from GevTimestampTickFrequency or DeviceTimestampFrequency
// at startup (default 1e9 = 1 ns/tick for Allied Vision GigE cameras)
uint64_t tsNs = static_cast<uint64_t>(
    static_cast<double>(ticks) * 1e9 / static_cast<double>(tickFreqHz_));
out.setTimestampNs(tsNs);
```

---

## Precision summary

| | Method 1 (GVSP embedded) | Method 2 (Latch fallback) |
|---|---|---|
| **Clock source** | Camera hardware | Camera hardware |
| **Stamped at** | Exact capture trigger | Post-callback latch |
| **Network hops affect value?** | No | Yes — see warning above |
| **Typical error vs capture** | < 1 µs | 100 µs – tens of ms |
| **Requires extra GEV round-trip?** | No | Yes |
| **Chunk mode required?** | No (stream leader) / Yes (chunk) | No |
| **Alvium G1-240c (SFNC 2.7.1)** | ~1 % of frames | ~99 % of frames |
| **Preferred?** | ✓ Yes | Acceptable fallback |

---

## Recommendations

1. **Method 2 at 99 % is acceptable for 10 FPS recording.**  The maximum timestamp
   offset (latch round-trip) over a direct GigE link is ~300 µs, which is well below
   the 100 ms inter-frame interval.  Zero missed timestamps is the operational goal
   and is confirmed on the Alvium G1-240c.

2. **Chunk mode is not available on the Alvium G1-240c.**  `ChunkModeActive` returns
   `VmbErrorNotFound` on this model.  Do not attempt to enable it — the code already
   handles this gracefully and falls through to Method 2.

3. **SFNC 2.x cameras need** `TimestampLatch` / `TimestampLatchValue` **— not the
   GEV 1.x names.**  The implementation tries both automatically.  If you add a new
   camera model and observe 100 % missed timestamps, check whether the camera is
   SFNC 2.x and whether these feature names are present in its settings XML.

4. **Minimise hops if timestamp accuracy matters.**  Over a VPN or multi-switch path
   the latch round-trip can reach 1–50 ms, which may overlap with the next frame at
   frame rates above ~20 FPS.  For such deployments either use a direct GigE
   connection or enable PTP (see below).

5. **Inter-camera sync** requires PTP (IEEE 1588) or a hardware PPS signal to
   align the independent per-camera tick counters.  Without it, two cameras at the
   same nominal FPS will have tick counters that drift relative to each other.
   The Alvium G1-240c supports PTP (`PtpEnable` feature, currently `0` in the
   captured settings); enabling it requires a PTP grandmaster on the network.
