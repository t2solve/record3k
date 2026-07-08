
# goal: fine tune pipeline config on real world record data

## why
sweep-6's "winner" (pipe-v5-sweep-6-best) scores great in synthetic sim (SCORE=0.386px)
but fails to detect the real animal in most frames. Root cause confirmed via
data/simulation/sim_results.sqlite: n_detected_frames/n_animal_frames == 1.0000 for
*every* sweep-6 config tested (all varThreshold 16-80) -- the synthetic target is always
trivially detectable, so the sweep never had any signal about detection reliability, only
about sub-pixel accuracy given a detection. Real footage (0.1mg individual) is much
harder. Decision: don't fix the generator first -- tune directly against real records
(no ground truth), then feed what we learn back into the generator later.

## basic assumptions on the moving object (for ground-truth-free scoring)
* one moving object of the same area size (tolerance: mean area within +/-X% -- real
  footage bounces between 0-14.5px even in a single record, so this must be an average
  tolerance band, not a per-frame match)
* the object is moving or not moving (resting is allowed)
* movement speed is within a min/max range
* the object is not jumping around (no instant beaming)
* the object cannot move outside the arena (ROI)
* the object must show real movement at least X times across the record, each exceeding
  an epsilon displacement -- otherwise FAIL the run outright (don't just score it low).
  This guards against a second degenerate optimum a naive metric would miss: a pipeline
  locked onto a static artifact (hot pixel, vignette) satisfies "not jumping" + "in ROI"
  + "same area" perfectly while tracking nothing real. epsilon/X to be derived from a
  sanity pass over a known-decent run, not guessed cold.

## step 1: tagging tool (reuses pipelineviewer's frame stepping/display code)
* per-record: define a round ROI arena (center + radius)
* tracker-assisted scrubbing: human clicks the animal once, a lightweight OpenCV tracker
  (KCF/CSRT) propagates the point forward frame-to-frame automatically; tracker
  confidence drop -> auto-flag for human re-click. Produces a sparse-but-real ground
  truth trajectory at a fraction of full manual-labeling cost.
* record triage during tagging: not just accept/reject -- support marking a
  valid-range (valid_until_frame, or explicit bad-segment ranges) so a record isn't
  discarded wholesale over a late-recording failure (e.g. animal escaped at frame 3200
  of 5008 -- keep the first 3200 as usable data).
* log illumination data for sample frames as metadata (diagnostic only, for later
  human review -- not part of scoring for now)
* define a run.json describing which pipeline parameters to sweep (same shape as
  data/simulation/param_ranges_sweep*.json)
* bootstrap note: before building the scrub tool, can get free pseudo-labels sooner by
  running a handful of already-known pipeline configs (existing varThreshold/min_area
  variants) over a record and taking frames where they agree within a few px as
  high-confidence labels, queuing only disagreement frames for human review. Useful to
  validate the scoring metric behaves sanely before investing in the scrub tool.

## step 2: buffered simulation run (per record)
* per record: extract the record tar.gz once, run *all* pipeline options being swept
  against it, write a log file, then delete the extracted tar.gz (cleanup) -- unpack
  once, reuse across every config, don't re-extract per config.
* new frame source: decode all frames of that record into FrameMemoryObject once, held
  in **system RAM** (not VRAM -- a single record's raw buffer is ~33 GiB at this
  camera's 1936x1216x3 resolution x 5008 frames, which does not fit in VRAM even on a
  well-specced GPU, let alone several records buffered in parallel across podman
  containers sharing a GPU). The bottleneck we measured was disk IO + JPEG decode, not
  CPU->GPU transfer, so host-RAM residency removes the actual cost: decode once, run
  every pipeline config in the sweep against the same in-memory frames, each step still
  uploads to GPU per frame as today.
* per frame, read contour/area metadata directly from FrameMemoryObject
  (`filtered_contours`, `area_filter_count`, `filtered_contour_count` -- already
  attached unconditionally on every frame by ContourAreaFilter, success or failure; do
  NOT scrape log.txt, it only logs on total failure)
* compute the new success metric per run, per basic-assumptions above
* write one sqlite per record (e.g. run_test_param_<recordUID>.sqlite3) with per-config
  results + a small summary -- deliberately NOT one shared sqlite written by many
  parallel containers (avoids SQLite concurrent-writer lock contention)

## step 3: leaderboard aggregation (new)
* separate pass: merge the per-record sqlite files into a cross-record leaderboard
* NOT comparable to the synthetic leaderboard (different metric domain -- proxy-based,
  no ground truth pixel error) -- keep it a distinct table/schema, don't conflate scores
* train/holdout split across records (mirrors the synthetic sweep's 28-record
  cross-validation) now that we have ~40 usable records out of 70 -- e.g. ~30 for
  tuning, 10 held out untouched until a final config is chosen
* post-hoc audit before trusting a winner: motion-energy heatmap (accumulated
  frame-differencing) per record, human eyeballs whether the winning config's
  trajectory roughly follows where the animal actually moved -- cheap sanity check,
  catches degenerate optima the numeric metric might still miss

## parallelization (podman, one container per record, per Containerfile.sim pattern)
* GPU contention: `--device nvidia.com/gpu=all` passes the whole GPU to every
  container -- check how many physical GPUs the remote machine actually has before
  assuming N containers = N x throughput; CUDA steps will serialize on a shared GPU
* per-record sqlite (see step 2) sidesteps the concurrent-write problem instead of
  fighting it

## record triage (70 total, ~40 usable)
* some records are "messy" -- worth a cheap automated pre-filter (frame count near
  expected, no catastrophic errors in log.txt, file integrity) before human review,
  rather than manual triage of all 70 from scratch

## open items
* derive epsilon / minimum-movement-frame-count X from a real known-decent run
* pin down mean-area tolerance percentage
* confirm actual GPU count/VRAM on the remote machine before finalizing parallel
  container count
* decide: prove the new metric on 2-3 records with a small manual grid before building
  the full sweep + leaderboard machinery, or set up the full 30/10 split from the start
