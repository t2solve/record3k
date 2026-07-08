#!/usr/bin/env python3
"""
sim_runner_real.py — run `pipeoptimizer run-real` across every "supervised" record
(tagged via pipelineviewer's Mode B, ROI defined, not marked a complete failure).

Sibling to sim_runner.py (which drives the synthetic generate/simulate/leaderboard/
analyse pipeline) — deliberately separate, since this has different inputs (a tags DB
and a directory of real record archives, not synthetic generation) and a simpler flow
(no leaderboard/analyse steps; step 3 cross-record aggregation is still out of scope,
see src/pipeoptimizer/plan.md).

For each supervised record UID found in the tags DB:
  1. resolve its archive as <records_archive_root>/file-rec-<uid>.tar.gz
     (skip with a warning, not a hard failure, if missing)
  2. skip if <out_dir>/<uid>.sqlite3 already exists (resume behaviour) unless --force
  3. invoke `pipeoptimizer run-real --record-archive ... --db <out_dir>/<uid>.sqlite3`

Pass --record <path/to/file-rec-<uid>.tar.gz> to scope a run to exactly that one
archive instead of discovering the full supervised set from tags_db -- required
whenever records_archive_root is mounted as a directory that also contains OTHER
supervised records' archives (e.g. the whole download folder), since otherwise every
archive found there would get swept, not just the one you meant to run.

Usage:
  python3 sim_runner_real.py run.json
  python3 sim_runner_real.py run.json --force
  python3 sim_runner_real.py run.json --n-pipes 50 --n-workers 8
  python3 sim_runner_real.py run.json --record /data/records/file-rec-rec-123-4.tar.gz

Config JSON (paths are relative to the config file unless absolute):
{
    "run_name":             "supervised_sweep1",
    "description":          "sweep-6 params against all supervised records",
    "pipeoptimizer_bin":    "build/bin/pipeoptimizer",
    "tags_db":              "data/simulation/supervised_records_data.sqlite3",
    "param_ranges":         "data/simulation/param_ranges_sweep6.json",
    "base_pipeline":        "data/simulation/pipelines/pipe-v4-base-sweep6.json",
    "records_archive_root": "/home/tb55xemi/work/idiv/record3kanalyse/data/downloaded_records",
    "out_dir":              "data/simulation/supervised_sweep1",
    "run_real": {
        "n_pipes":   20,
        "n_workers": 4,
        "seed":      42
    }
}

"base_pipeline" is optional — the LHS sweep is generated on top of it instead of
pipeoptimizer's hardcoded built-in default pipeline. Required whenever param_ranges.json
varies a step that isn't part of that built-in default (e.g. GaussianBlur_CUDA), or
relies on a non-default fixed value for an unswept param — otherwise that param is
silently a no-op or wrong (same caveat as sim_runner.py's base_pipeline).

Derived paths (auto-computed, not needed in config):
    out_dir/<record_uid>.sqlite3 — one result DB per record (see RealSimDB)
"""

import argparse
import json
import sqlite3
import sys
import time
from datetime import datetime
from pathlib import Path
from threading import Thread
import subprocess

# ── ANSI colours (same palette as sim_runner.py) ──────────────────────────────
C_RESET    = "\033[0m"
C_BOLD     = "\033[1m"
C_RED      = "\033[1;31m"
C_GREEN    = "\033[1;32m"
C_YELLOW   = "\033[33m"
C_CYAN     = "\033[36m"
C_BCYAN    = "\033[1;36m"
C_BMAGENTA = "\033[1;35m"

SEP = "=" * 72


# ── real-time subprocess streaming (identical to sim_runner.py's run_cmd) ────

def _pipe_reader(pipe, label: str, colour: str) -> None:
    for raw in pipe:
        ts   = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        line = raw.rstrip("\n")
        print(f"{colour}[{ts}][{label}]{C_RESET} {line}", flush=True)


def run_cmd(cmd: list, label: str, cwd: Path | None = None) -> int:
    """Run *cmd*, stream stdout (cyan) and stderr (yellow) in real time with
    timestamps and a step-label prefix. Returns the exit code."""
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
    t_out = Thread(target=_pipe_reader, args=(proc.stdout, label,          C_CYAN),   daemon=True)
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


# ── supervised-record discovery ───────────────────────────────────────────────

def discover_supervised_uids(tags_db: Path) -> list:
    """Records ready to be scored: ROI defined, not marked a complete failure."""
    conn = sqlite3.connect(f"file:{tags_db}?mode=ro", uri=True)
    try:
        rows = conn.execute(
            "SELECT record_uid FROM tagged_records "
            "WHERE roi_defined=1 AND record_failed=0 ORDER BY record_uid"
        ).fetchall()
    finally:
        conn.close()
    return [r[0] for r in rows]


def resolve_archive(uid: str, records_archive_root: Path) -> Path | None:
    p = records_archive_root / f"file-rec-{uid}.tar.gz"
    return p if p.exists() else None


def uid_from_archive_name(archive: Path) -> str:
    """Inverse of resolve_archive's naming convention: file-rec-<uid>.tar.gz -> <uid>."""
    name = archive.name
    if not name.startswith("file-rec-") or not name.endswith(".tar.gz"):
        sys.exit(f"--record filename doesn't match file-rec-<uid>.tar.gz: {name}")
    return name[len("file-rec-"):-len(".tar.gz")]


# ── path resolution ────────────────────────────────────────────────────────────

def resolve_paths(cfg: dict, cfg_dir: Path) -> dict:
    def r(val: str) -> Path:
        p = Path(val)
        return p if p.is_absolute() else (cfg_dir / p).resolve()

    base_pipeline_raw = cfg.get("base_pipeline")

    return {
        "bin":                  r(cfg.get("pipeoptimizer_bin",    "build/bin/pipeoptimizer")),
        "tags_db":               r(cfg.get("tags_db",
                                             "data/simulation/supervised_records_data.sqlite3")),
        "param_ranges":          r(cfg.get("param_ranges",
                                             "data/simulation/param_ranges.json")),
        "base_pipeline":         r(base_pipeline_raw) if base_pipeline_raw else None,
        "records_archive_root":  r(cfg.get("records_archive_root", "data/records")),
        "out_dir":               r(cfg.get("out_dir",               "data/simulation/supervised_sweep")),
    }


# ── main ──────────────────────────────────────────────────────────────────────

def main() -> None:
    ap = argparse.ArgumentParser(
        description="sim_runner_real — run-real across every supervised record",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    ap.add_argument("config", help="path to run config JSON")
    ap.add_argument("--record", metavar="FILE",
                     help="run against exactly this one record archive (.tar.gz) "
                          "instead of discovering the full supervised set from "
                          "tags_db — the archive's filename must still follow the "
                          "file-rec-<uid>.tar.gz convention so its record UID can "
                          "be looked up in tags_db")
    ap.add_argument("--force", action="store_true",
                     help="re-run records that already have an output sqlite "
                          "(default: skip them — resume behaviour)")
    ap.add_argument("--n-pipes",     type=int, metavar="N", help="override run_real.n_pipes")
    ap.add_argument("--n-workers",   type=int, metavar="N", help="override run_real.n_workers")
    ap.add_argument("--seed",        type=int,               help="override run_real.seed")
    ap.add_argument("--description", metavar="TEXT",         help="override description tag")
    args = ap.parse_args()

    cfg_path = Path(args.config).resolve()
    if not cfg_path.exists():
        sys.exit(f"Config not found: {cfg_path}")
    with open(cfg_path) as f:
        cfg = json.load(f)

    rr = cfg.setdefault("run_real", {})
    if args.n_pipes    is not None: rr["n_pipes"]   = args.n_pipes
    if args.n_workers  is not None: rr["n_workers"] = args.n_workers
    if args.seed       is not None: rr["seed"]      = args.seed
    if args.description is not None: cfg["description"] = args.description

    paths = resolve_paths(cfg, cfg_path.parent)
    base_description = cfg.get("description", cfg.get("run_name", "supervised-sweep"))

    print(f"\n{C_BMAGENTA}{SEP}{C_RESET}")
    print(f"{C_BMAGENTA}  sim_runner_real  |  config : {cfg_path.name}{C_RESET}")
    print(f"{C_BMAGENTA}                   |  run    : {cfg.get('run_name', '?')}{C_RESET}")
    print(f"{C_BMAGENTA}{SEP}{C_RESET}\n")
    col_w = max(len(k) for k in paths)
    for key, val in paths.items():
        if val is None:
            print(f"  {key:{col_w}s}  -  (not set)")
        else:
            exists = "✓" if Path(val).exists() else "✗"
            print(f"  {key:{col_w}s}  {exists}  {val}")
    print()

    if not paths["tags_db"].exists():
        sys.exit(f"tags_db not found: {paths['tags_db']}")

    # --record scopes this invocation to exactly one archive, bypassing tags_db
    # discovery entirely -- without this, mounting a directory that happens to
    # contain other supervised records' archives (e.g. the whole downloaded_records
    # folder) would silently sweep all of them, not just the one intended.
    archives: dict[str, Path] = {}
    if args.record:
        record_path = Path(args.record).resolve()
        if not record_path.exists():
            sys.exit(f"--record not found: {record_path}")
        uid = uid_from_archive_name(record_path)
        uids = [uid]
        archives[uid] = record_path
        print(f"{C_BOLD}Single-record mode: {uid} ({record_path.name}){C_RESET}\n")
    else:
        uids = discover_supervised_uids(paths["tags_db"])
        print(f"{C_BOLD}Supervised records found: {len(uids)}{C_RESET}\n")
        if not uids:
            print(f"{C_YELLOW}Nothing to do — no supervised records in {paths['tags_db']}{C_RESET}")
            return

    paths["out_dir"].mkdir(parents=True, exist_ok=True)

    summary = []  # (uid, status, elapsed_s)
    total_t0 = time.time()

    for uid in uids:
        out_db = paths["out_dir"] / f"{uid}.sqlite3"

        if out_db.exists() and not args.force:
            print(f"{C_YELLOW}[{uid}] SKIP — {out_db.name} already exists (use --force to re-run){C_RESET}")
            summary.append((uid, "SKIPPED", 0.0))
            continue

        archive = archives.get(uid) or resolve_archive(uid, paths["records_archive_root"])
        if archive is None:
            print(f"{C_YELLOW}[{uid}] SKIP — no archive found under {paths['records_archive_root']}{C_RESET}")
            summary.append((uid, "MISSING", 0.0))
            continue

        cmd = [
            str(paths["bin"]), "run-real",
            "--record-archive", str(archive),
            "--param-ranges",   str(paths["param_ranges"]),
            "--n-pipes",        str(rr.get("n_pipes",   20)),
            "--n-workers",      str(rr.get("n_workers",  4)),
            "--seed",           str(rr.get("seed",      42)),
            "--tags-db",        str(paths["tags_db"]),
            "--db",             str(out_db),
            "--description",    f"{base_description} :: {uid}",
        ]
        if paths["base_pipeline"] is not None:
            # LHS-sweep configs are generated on top of this pipeline instead of
            # pipeoptimizer's built-in default -- required whenever param_ranges.json
            # varies a step (e.g. GaussianBlur_CUDA) that isn't part of that default,
            # or relies on a non-default fixed value for an unswept param (e.g.
            # binary_threshold) -- otherwise that param is silently a no-op or wrong.
            cmd += ["--base-pipeline", str(paths["base_pipeline"])]
        t0 = time.time()
        rc = run_cmd(cmd, f"RUN-REAL/{uid}")
        elapsed = time.time() - t0
        summary.append((uid, "OK" if rc == 0 else "FAILED", elapsed))

    total_elapsed = time.time() - total_t0

    print(f"{C_BMAGENTA}{SEP}{C_RESET}")
    print(f"{C_BMAGENTA}  SUMMARY{C_RESET}")
    print(f"{C_BMAGENTA}{SEP}{C_RESET}")
    status_colour = {"OK": C_GREEN, "SKIPPED": C_YELLOW, "MISSING": C_YELLOW, "FAILED": C_RED}
    for uid, status, elapsed in summary:
        colour = status_colour.get(status, C_RESET)
        elapsed_str = f"{elapsed:6.1f}s" if elapsed else "      -"
        print(f"  {uid:28s}  {colour}{status:8s}{C_RESET}  {elapsed_str}")
    n_ok      = sum(1 for _, s, _ in summary if s == "OK")
    n_failed  = sum(1 for _, s, _ in summary if s == "FAILED")
    n_skipped = sum(1 for _, s, _ in summary if s in ("SKIPPED", "MISSING"))
    print(f"\n  {n_ok} ok, {n_failed} failed, {n_skipped} skipped — total {total_elapsed:.1f}s")
    print(f"{C_BMAGENTA}{SEP}{C_RESET}\n")

    if n_failed > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
