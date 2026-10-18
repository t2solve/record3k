#!/usr/bin/env python3
"""
sim_runner.py — automate one full simulation iteration.

Steps (in order):
  generate    — synthesise artificial records with ground-truth positions
  simulate    — run pipeline sweep (LHS) OR a single fixed pipeline against
                those records, write to SQLite
  leaderboard — print two views across every run ever stored in the DB (not
                just this run): the raw per-(pipeline,record) leaderboard, and
                (unless "leaderboard.aggregate": false) the aggregate one —
                one row per pipeline_id averaged over every record it was ever
                tested against, i.e. "best configuration on average" — see
                "simulate.pipeline_configs" below for testing hand-picked
                pipelines against the accumulated history
  analyse     — run the analysis script, produce plots and summary.txt
  cleanup     — (optional) delete the synth folder; keeps DB and analysis output

Usage:
  python3 sim_runner.py run3.json
  python3 sim_runner.py run3.json --only analyse
  python3 sim_runner.py run3.json --skip generate
  python3 sim_runner.py run3.json --n-pipes 50 --n-workers 16 --seed 99
  python3 sim_runner.py run3.json --pipeline-configs candidate.json
  python3 sim_runner.py run3.json --pipeline-configs data/pipelines/*.json

Config JSON (paths are relative to the config file unless absolute):
{
    "run_name":          "run3",
    "description":       "sweep-3 — bg subtraction focus",
    "pipeoptimizer_bin": "build/pipeoptimizer",
    "param_ranges":      "src/pipeoptimizer/param_ranges.json",
    "base_pipeline":     "src/pipeoptimizer/base.json",
    "data_root":         "build/data",
    "generate": {
        "n_records":      13,
        "static_records":  1,
        "body_type":      "both",
        "warmup":         500,
        "animal_frames":  800,
        "seed":           42
    },
    "simulate": {
        "n_pipes":   25,
        "n_workers": 12
    },
    "leaderboard": {
        "limit": 30,
        "aggregate": true
    },
    "cleanup_synth": false
}

"base_pipeline" is optional — the LHS sweep is generated on top of it instead
of pipeoptimizer's hardcoded built-in default pipeline. Required whenever
param_ranges.json varies a step that isn't part of that built-in default (e.g.
GaussianBlur_CUDA) — otherwise that param is silently a no-op (no matching
step to apply it to).

Alternative to the param_ranges-based LHS sweep: set "simulate.pipeline_configs"
to one or more pipeline JSON files (a single string, a list, or a glob pattern)
to test those fixed pipelines as-is against the same generation set — one
`pipeoptimizer run` invocation per pipeline, all writing into the same --db so
they land on one leaderboard together. param_ranges/n_pipes are then ignored:
    "simulate": { "pipeline_configs": "data/pipelines/*.json", "n_workers": 12 }
    "simulate": { "pipeline_configs": ["a.json", "b.json"], "n_workers": 12 }

Derived paths (auto-computed, not needed in config):
    synth_root = <data_root>/<run_name>/synth
    db         = <data_root>/sim_results.sqlite
    out_dir    = <data_root>/<run_name>/analysis
    analyse_py = <this script's dir>/analysis/analyse.py
"""

import argparse
import glob
import json
import os
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from threading import Thread

# ── ANSI colours ──────────────────────────────────────────────────────────────
C_RESET  = "\033[0m"
C_BOLD   = "\033[1m"
C_RED    = "\033[1;31m"
C_GREEN  = "\033[1;32m"
C_YELLOW = "\033[33m"
C_CYAN   = "\033[36m"
C_BCYAN  = "\033[1;36m"
C_BMAGENTA = "\033[1;35m"

STEPS = ["generate", "simulate", "leaderboard", "analyse", "cleanup"]
SEP   = "=" * 72


# ── real-time subprocess streaming ────────────────────────────────────────────

def _pipe_reader(pipe, label: str, colour: str) -> None:
    """Thread target: read lines from a pipe and print timestamped."""
    for raw in pipe:
        ts   = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        line = raw.rstrip("\n")
        print(f"{colour}[{ts}][{label}]{C_RESET} {line}", flush=True)


def run_cmd(cmd: list, label: str, cwd: Path | None = None) -> int:
    """
    Run *cmd*, stream stdout (cyan) and stderr (yellow) in real time with
    timestamps and a step-label prefix.  Returns the exit code.
    """
    cmd_str = " ".join(str(c) for c in cmd)
    print(f"\n{C_BCYAN}{SEP}{C_RESET}")
    print(f"{C_BCYAN}[{label}] CMD : {cmd_str}{C_RESET}")
    if cwd:
        print(f"{C_BCYAN}[{label}] CWD : {cwd}{C_RESET}")
    print(f"{C_BCYAN}{SEP}{C_RESET}\n")

    t0   = time.time()
    proc = subprocess.Popen(
        cmd,
        cwd=str(cwd) if cwd else None,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
    )
    t_out = Thread(target=_pipe_reader, args=(proc.stdout, label,         C_CYAN),   daemon=True)
    t_err = Thread(target=_pipe_reader, args=(proc.stderr, label + "/err", C_YELLOW), daemon=True)
    t_out.start()
    t_err.start()
    proc.wait()
    t_out.join()
    t_err.join()

    elapsed = time.time() - t0
    colour  = C_GREEN if proc.returncode == 0 else C_RED
    print(f"\n{colour}[{label}] exit={proc.returncode}  elapsed={elapsed:.1f}s{C_RESET}\n")
    return proc.returncode


# ── individual steps ──────────────────────────────────────────────────────────

def step_generate(cfg: dict, paths: dict) -> int:
    g   = cfg.get("generate", {})
    cmd = [
        str(paths["bin"]), "generate",
        "--output",         str(paths["synth_root"]),
        "--n-records",      str(g.get("n_records",      13)),
        "--static-records", str(g.get("static_records",  1)),
        "--body-type",           g.get("body_type",    "both"),
        "--warmup",         str(g.get("warmup",         500)),
        "--animal-frames",  str(g.get("animal_frames",  800)),
        "--seed",           str(g.get("seed",            42)),
    ]
    paths["synth_root"].mkdir(parents=True, exist_ok=True)
    return run_cmd(cmd, "GENERATE")


def step_simulate(cfg: dict, paths: dict) -> int:
    s = cfg.get("simulate", {})
    base_description = cfg.get("description", cfg.get("run_name", "run"))
    paths["db"].parent.mkdir(parents=True, exist_ok=True)

    if paths["pipeline_configs"]:
        # One or more fixed pipelines tested as-is (no sweep) against the same
        # generation set — one `run` invocation per pipeline, same --db, so they
        # all land on one leaderboard for comparison.
        for pc in paths["pipeline_configs"]:
            cmd = [
                str(paths["bin"]), "run",
                "--synth-root",     str(paths["synth_root"]),
                "--n-workers",      str(s.get("n_workers", 12)),
                "--db",             str(paths["db"]),
                "--description",    f"{base_description} :: {pc.stem}",
                "--pipeline-config", str(pc),
            ]
            rc = run_cmd(cmd, f"SIMULATE/{pc.stem}")
            if rc != 0:
                return rc
        return 0

    cmd = [
        str(paths["bin"]), "run",
        "--synth-root",   str(paths["synth_root"]),
        "--n-workers",    str(s.get("n_workers", 12)),
        "--db",           str(paths["db"]),
        "--description",  base_description,
        "--param-ranges", str(paths["param_ranges"]),
        "--n-pipes",      str(s.get("n_pipes", 25)),
    ]
    if paths["base_pipeline"] is not None:
        # LHS-sweep configs are generated on top of this pipeline instead of the
        # hardcoded built-in default — required whenever param_ranges.json varies
        # a step (e.g. GaussianBlur_CUDA) that isn't part of the built-in default.
        cmd += ["--base-pipeline", str(paths["base_pipeline"])]
    return run_cmd(cmd, "SIMULATE")


def step_leaderboard(cfg: dict, paths: dict) -> int:
    lb    = cfg.get("leaderboard", {})
    limit = str(lb.get("limit", 30))

    rc = run_cmd(
        [str(paths["bin"]), "leaderboard", "--db", str(paths["db"]), "--limit", limit],
        "LEADERBOARD",
    )
    if rc != 0:
        return rc

    if lb.get("aggregate", True):
        # Best-on-average configuration across every record it was tested against —
        # see "leaderboard.aggregate": false to skip this (raw per-record view only).
        rc = run_cmd(
            [str(paths["bin"]), "leaderboard", "--db", str(paths["db"]),
             "--limit", limit, "--aggregate"],
            "LEADERBOARD/aggregate",
        )
    return rc


def step_analyse(cfg: dict, paths: dict) -> int:
    paths["out_dir"].mkdir(parents=True, exist_ok=True)
    cmd = [
        sys.executable,
        str(paths["analyse_py"]),
        "--db",           str(paths["db"]),
        "--synth-root",   str(paths["synth_root"]),
        "--out-dir",      str(paths["out_dir"]),
    ]
    if not paths["pipeline_configs"]:
        # Fixed-pipeline runs have no swept params; let analyse.py auto-detect instead.
        cmd += ["--param-ranges", str(paths["param_ranges"])]
    return run_cmd(cmd, "ANALYSE")


def step_cleanup(cfg: dict, paths: dict) -> int:
    if not cfg.get("cleanup_synth", False):
        print(f"[CLEANUP] cleanup_synth=false — synth data kept at {paths['synth_root']}")
        return 0
    synth = paths["synth_root"]
    if not synth.exists():
        print(f"[CLEANUP] {synth} does not exist — nothing to delete")
        return 0
    return run_cmd(["rm", "-rf", str(synth)], "CLEANUP")


STEP_FNS = {
    "generate":    step_generate,
    "simulate":    step_simulate,
    "leaderboard": step_leaderboard,
    "analyse":     step_analyse,
    "cleanup":     step_cleanup,
}


# ── path resolution ────────────────────────────────────────────────────────────

def _expand_pipeline_pattern(pattern: str, cfg_dir: Path) -> list:
    """Expand one pipeline_configs entry — a plain path or a glob pattern —
    into a sorted list of resolved Paths, anchored to cfg_dir unless absolute."""
    pat = pattern if os.path.isabs(pattern) else str(cfg_dir / pattern)
    if "*" in pattern or "?" in pattern:
        matches = sorted(glob.glob(pat))
        if not matches:
            raise FileNotFoundError(f"pipeline_configs pattern matched no files: {pattern}")
        return [Path(m).resolve() for m in matches]
    return [Path(pat).resolve()]


def resolve_paths(cfg: dict, cfg_dir: Path) -> dict:
    """Resolve all paths from the config, anchored to the config file's directory."""
    def r(val: str) -> Path:
        p = Path(val)
        return p if p.is_absolute() else (cfg_dir / p).resolve()

    data_root    = r(cfg.get("data_root",         "build/data"))
    run_name     = cfg.get("run_name",             "run")
    synth_root   = data_root / run_name / "synth"
    db           = data_root / "sim_results.sqlite"
    out_dir      = data_root / run_name / "analysis"
    param_ranges = r(cfg.get("param_ranges",       "src/pipeoptimizer/param_ranges.json"))
    bin_path     = r(cfg.get("pipeoptimizer_bin",  "build/pipeoptimizer"))
    analyse_py   = Path(__file__).resolve().parent / "analysis" / "analyse.py"

    base_pipeline_raw = cfg.get("base_pipeline")
    base_pipeline      = r(base_pipeline_raw) if base_pipeline_raw else None

    # simulate.pipeline_configs: a single path, a glob pattern, or a list of either.
    pc_raw = cfg.get("simulate", {}).get("pipeline_configs")
    if isinstance(pc_raw, str):
        pc_raw = [pc_raw]
    pipeline_configs = []
    for entry in (pc_raw or []):
        pipeline_configs.extend(_expand_pipeline_pattern(entry, cfg_dir))

    return {
        "bin":               bin_path,
        "data_root":         data_root,
        "synth_root":        synth_root,
        "db":                db,
        "out_dir":           out_dir,
        "param_ranges":      param_ranges,
        "base_pipeline":     base_pipeline,
        "pipeline_configs":  pipeline_configs,
        "analyse_py":        analyse_py,
    }


# ── main ──────────────────────────────────────────────────────────────────────

def main() -> None:
    ap = argparse.ArgumentParser(
        description="sim_runner — one command for a full simulation iteration",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    ap.add_argument("config",
                    help="path to run config JSON")
    ap.add_argument("--only",  nargs="+", choices=STEPS, metavar="STEP",
                    help="run only these steps  (e.g. --only analyse)")
    ap.add_argument("--skip",  nargs="+", choices=STEPS, metavar="STEP",
                    help="skip these steps       (e.g. --skip generate)")
    # per-run overrides so you don't need to edit the JSON for quick experiments
    ap.add_argument("--n-pipes",     type=int, metavar="N",
                    help="override simulate.n_pipes")
    ap.add_argument("--n-workers",   type=int, metavar="N",
                    help="override simulate.n_workers")
    ap.add_argument("--n-records",   type=int, metavar="N",
                    help="override generate.n_records")
    ap.add_argument("--seed",        type=int,
                    help="override generate.seed")
    ap.add_argument("--description", metavar="TEXT",
                    help="override description tag stored in the DB")
    ap.add_argument("--pipeline-configs", nargs="+", metavar="FILE",
                    help="override simulate.pipeline_configs — test one or more fixed "
                         "pipelines (no sweep, one run per pipeline) instead of the "
                         "param_ranges LHS sweep; accepts glob patterns")
    ap.add_argument("--base-pipeline", metavar="FILE",
                    help="override base_pipeline — LHS-sweep configs are generated on "
                         "top of this pipeline instead of the built-in default")
    args = ap.parse_args()

    cfg_path = Path(args.config).resolve()
    if not cfg_path.exists():
        sys.exit(f"Config not found: {cfg_path}")

    with open(cfg_path) as f:
        cfg = json.load(f)

    # apply CLI overrides
    if args.n_pipes    is not None: cfg.setdefault("simulate",  {})["n_pipes"]    = args.n_pipes
    if args.n_workers  is not None: cfg.setdefault("simulate",  {})["n_workers"]  = args.n_workers
    if args.n_records  is not None: cfg.setdefault("generate",  {})["n_records"]  = args.n_records
    if args.seed       is not None: cfg.setdefault("generate",  {})["seed"]       = args.seed
    if args.description is not None: cfg["description"] = args.description
    if args.pipeline_configs is not None:
        cfg.setdefault("simulate", {})["pipeline_configs"] = args.pipeline_configs
    if args.base_pipeline is not None: cfg["base_pipeline"] = args.base_pipeline

    paths  = resolve_paths(cfg, cfg_path.parent)
    active = list(args.only) if args.only else list(STEPS)
    if args.skip:
        active = [s for s in active if s not in args.skip]

    # header
    print(f"\n{C_BMAGENTA}{SEP}{C_RESET}")
    print(f"{C_BMAGENTA}  sim_runner  |  config : {cfg_path.name}{C_RESET}")
    print(f"{C_BMAGENTA}              |  run    : {cfg.get('run_name', '?')}{C_RESET}")
    print(f"{C_BMAGENTA}              |  steps  : {' → '.join(active)}{C_RESET}")
    print(f"{C_BMAGENTA}{SEP}{C_RESET}\n")
    col_w = max(len(k) for k in paths)
    for key, val in paths.items():
        if val is None or val == []:
            print(f"  {key:{col_w}s}  -  (not set)")
        elif isinstance(val, list):
            for i, v in enumerate(val):
                label  = key if i == 0 else ""
                exists = "✓" if Path(v).exists() else "✗"
                print(f"  {label:{col_w}s}  {exists}  {v}")
        else:
            exists = "✓" if Path(val).exists() else "✗"
            print(f"  {key:{col_w}s}  {exists}  {val}")
    print()

    total_t0 = time.time()
    for step in active:
        rc = STEP_FNS[step](cfg, paths)
        if rc != 0:
            print(f"{C_RED}{SEP}{C_RESET}")
            print(f"{C_RED}  FAILED at step '{step}' (exit {rc}) — aborting.{C_RESET}")
            print(f"{C_RED}{SEP}{C_RESET}")
            sys.exit(rc)

    total_elapsed = time.time() - total_t0
    print(f"{C_GREEN}{SEP}{C_RESET}")
    print(f"{C_GREEN}  ALL DONE — total {total_elapsed:.1f}s{C_RESET}")
    print(f"{C_GREEN}{SEP}{C_RESET}\n")


if __name__ == "__main__":
    main()
