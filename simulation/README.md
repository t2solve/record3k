# Simulation tooling (branch `feature/simulation`)

Tools for tuning processing-pipeline parameters offline, without a live camera.

| Tool | What it does |
|---|---|
| `src/pipeoptimizer` | C++ CLI: generates synthetic records, runs pipeline sweeps (`simulate`, `run-real`) and ranks results (`leaderboard`) |
| `src/pipeoptimizer/sim_runner.py` | Orchestrates a **synthetic** sweep: generate → simulate → leaderboard → analyse → cleanup |
| `src/pipeoptimizer/sim_runner_real.py` | Orchestrates a **real-record** sweep against human-tagged records (no ground truth) |
| `src/pipeoptimizer/analysis/` | Python analysis and plots (`analyse.py`, leaderboards, motion-energy audit) |
| `src/rerun` | C++ CLI: reprocess the frames of a captured record with another pipeline JSON (`rerun <record_dir> <pipeline.json>`) |
| `simulation/Containerfile.sim` | Image `record3k-sim` for synthetic sweeps |
| `simulation/Containerfile.sim-real` | Image `record3k-sim-real` for real-record sweeps |
| `simulation/startSimRealForX.sh` | Run `record3k-sim-real` against **one** record archive |
| `simulation/runSetSimReal.sh` | Run a list of records, `MAX_PARALLEL` containers at a time |
| `simulation/examples/` | Example run configs, `param_ranges_*.json`, base pipelines |

Design notes: [src/pipeoptimizer/plan.md](../src/pipeoptimizer/plan.md) (real-record tuning),
[src/pipeoptimizer/planSynthicSimu.md](../src/pipeoptimizer/planSynthicSimu.md) (synthetic),
[src/pipeoptimizer/steps.md](../src/pipeoptimizer/steps.md) (manual CLI steps),
[src/rerun/plan.md](../src/rerun/plan.md) (record layout).

## Branch rules

- Merge `main` into this branch regularly: `git switch feature/simulation && git merge main`. Don't rebase it.
- Don't merge this branch back into `main` as a whole. Changes to `liblbt` belong on `main` and get merged in from there.

## Setup

All commands run from the repo root. Prerequisite: the `cuda-builder` base image (see the main README, podman build).

```bash
# shared host <-> container working dir (gitignored), seeded from the examples
mkdir -p data/simulation
cp -rn simulation/examples/. data/simulation/

podman build -f simulation/Containerfile.sim      -t record3k-sim .
podman build -f simulation/Containerfile.sim-real -t record3k-sim-real .
```

Rebuild after any change to C++ or Python sources. The examples get baked into the image as the default `/data/simulation`; at runtime the host `data/simulation/` is mounted over them.

Host build without containers: `./localBuild.sh --cuda -j 8` builds `build/bin/pipeoptimizer` and `build/bin/rerun`. Point `pipeoptimizer_bin` in the run config at that binary.

## A. Synthetic sweep (`record3k-sim`)

```bash
podman run --rm --device nvidia.com/gpu=all \
  -v "$(pwd)/data/simulation:/data/simulation" \
  record3k-sim /data/simulation/sweep6.json
```

Long runs: add `-d --name sweep6` and follow with `podman logs -f sweep6 >> data/simulation/sweep6_run.log`.

Flags on any run: `--only <step>`, `--skip <step>`, `--n-pipes N`, `--n-workers N`, `--seed N`.
Example that only re-runs the analysis (no GPU needed): `... record3k-sim /data/simulation/sweep6.json --only analyse`.

There are two kinds of run config:
- **LHS parameter sweep** (`sweep4-6.json`): `base_pipeline` + `param_ranges` + `simulate.n_pipes`. `base_pipeline` is required whenever the ranges vary a step that pipeoptimizer's built-in default pipeline doesn't contain (e.g. `GaussianBlur_CUDA`).
- **Fixed pipelines, no sweep** (`baseline1.json`): `simulate.pipeline_configs: "pipelines/*.json"`.

Results go into `data/simulation/sim_results.sqlite`, with plots and `summary.txt` in `data/simulation/<run_name>/analysis/`.
With `"cleanup_synth": true` the generated frames are deleted at the end. To query the leaderboard across all runs:

```bash
podman run --rm -v "$(pwd)/data/simulation:/data/simulation" \
  --entrypoint /app/bin/pipeoptimizer \
  record3k-sim leaderboard --db /data/simulation/sim_results.sqlite --aggregate
```

## B. Real-record sweep (`record3k-sim-real`)

Inputs (gitignored, not in the repo):
- record archives `file-rec-<record_uid>.tar.gz`, from the API server's `data/files/bin/` or a download folder
- the tags DB `data/simulation/supervised_records_data.sqlite3`, written by pipelineviewer's tagging mode (Mode B)

The run config and the tags DB must be in the same directory, because that directory is mounted as `/data/simulation`.

```bash
# one record (checks inputs first, runs detached, logs to /tmp/log-container-<name>.log)
./simulation/startSimRealForX.sh /path/to/file-rec-<record_uid>.tar.gz \
  data/simulation/bgsub_sweep1.json data/simulation/supervised_records_data.sqlite3 [--force] [--n-pipes 40]

# a hand-picked list of records (edit RECORD_IDS / MAX_PARALLEL in the script)
RECORDS_DIR=/path/to/archives ./simulation/runSetSimReal.sh

# every supervised record in the tags DB
podman run --rm --device nvidia.com/gpu=all \
  -v "$(pwd)/data/simulation:/data/simulation" \
  -v "$RECORDS_DIR:/data/records:ro" \
  record3k-sim-real /data/simulation/bgsub_sweep1.json
```

`--force` re-runs records that already have output. Without it, the run resumes and skips them.

Output is one sqlite per record at `data/simulation/<out_dir>/<record_uid>.sqlite3`:

```bash
sqlite3 -header -column data/simulation/bgsub_sweep1/<record_uid>.sqlite3 \
  "SELECT pipeline_id, eta, mean_area, n_movement_events, n_roi_violations, passed
   FROM results ORDER BY n_movement_events DESC;"
sqlite3 data/simulation/bgsub_sweep1/<record_uid>.sqlite3 \
  "SELECT config_json FROM pipeline_configs WHERE pipeline_id='pipe-lhs-0003-42';"
```
