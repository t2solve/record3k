#!/usr/bin/env python3
"""
Pipeline optimiser — real-record leaderboard aggregation (plan.md step 3).

Merges the per-record sqlite3 files produced by run-real (one file per record,
see plan.md step 2 -- deliberately not one shared DB, to avoid concurrent-writer
lock contention) into a single cross-record leaderboard.

NOT comparable to the synthetic leaderboard (analyse.py / sim_results.sqlite):
different metric domain -- proxy-based (eta/zeta/area tolerance/movement
events), no ground-truth pixel error. Kept as a wholly separate script/schema
on purpose.

Usage:
    python3 real_leaderboard.py --results-dir data/simulation/bgsub_sweep1 \
                                --out-dir /tmp/bgsub_sweep1_leaderboard
"""

import argparse
import json
import os
import sqlite3
from pathlib import Path

import pandas as pd

# swept params for this campaign (see param_ranges_bgsub.json) -- extend this
# mapping if a future real-record campaign sweeps different pipeline steps.
PARAM_JSON_TO_COL = {
    "BackgroundSubtraction_CUDA": {
        "history":      "bg_history",
        "varThreshold": "bg_var_threshold",
    },
}


def extract_params(config_json: str) -> dict:
    """Pull the swept BackgroundSubtraction_CUDA params out of a pipeline config JSON."""
    out = {"bg_history": float("nan"), "bg_var_threshold": float("nan")}
    try:
        cfg = json.loads(config_json)
        steps = {s["name"]: s["parameters"] for s in cfg["pipeline"]["steps"]}
        params = steps.get("BackgroundSubtraction_CUDA", {})
        out["bg_history"] = int(params.get("history", float("nan")))
        out["bg_var_threshold"] = int(params.get("varThreshold", float("nan")))
    except Exception:
        pass
    return out


def load_one_db(path: Path) -> pd.DataFrame:
    con = sqlite3.connect(path)
    con.row_factory = sqlite3.Row

    run_info = con.execute("SELECT * FROM run_info ORDER BY id DESC LIMIT 1").fetchone()
    if run_info is None:
        con.close()
        return pd.DataFrame()
    record_uid = run_info["record_uid"]

    pipe_cfgs = {
        r["pipeline_id"]: r["config_json"]
        for r in con.execute("SELECT pipeline_id, config_json FROM pipeline_configs")
    }

    rows = con.execute("""
        SELECT pipeline_id, eta, zeta, mean_area, area_tolerance_violation_rate,
               n_movement_events, n_jumps, n_roi_violations, n_speed_violations,
               n_frames_scored, n_frames_detected, passed, fail_reason,
               processing_time_ms
        FROM results
        WHERE run_info_id = ?
    """, (run_info["id"],)).fetchall()
    con.close()

    df = pd.DataFrame([dict(r) for r in rows])
    if df.empty:
        return df
    df["record_uid"] = record_uid
    params_df = pd.DataFrame({pid: extract_params(cfg) for pid, cfg in pipe_cfgs.items()}).T
    params_df.index.name = "pipeline_id"
    params_df = params_df.reset_index()
    return df.merge(params_df, on="pipeline_id", how="left")


def load_all(results_dir: str) -> pd.DataFrame:
    paths = sorted(Path(results_dir).glob("*.sqlite3"))
    if not paths:
        raise SystemExit(f"no *.sqlite3 files found in {results_dir}")
    frames = [load_one_db(p) for p in paths]
    frames = [f for f in frames if not f.empty]
    print(f"[load_all] {len(frames)}/{len(paths)} record DBs loaded")
    return pd.concat(frames, ignore_index=True)


def build_leaderboard(df: pd.DataFrame) -> pd.DataFrame:
    """
    One row per pipeline_id. Metrics (eta/zeta/area tolerance/etc.) are averaged
    only over records where this pipeline PASSED -- a failed run's metrics are
    degenerate zeros (see real_metrics.cpp) and would just dilute the average.
    pass_rate itself is reported separately since a pipeline failing on many
    records is exactly the kind of thing this leaderboard needs to surface, not
    hide by averaging it away.
    """
    n_records = df["record_uid"].nunique()

    passed = df[df["passed"] == 1]

    agg = passed.groupby("pipeline_id").agg(
        n_passed              = ("record_uid", "nunique"),
        detection_rate        = ("n_frames_detected", lambda s: (s / passed.loc[s.index, "n_frames_scored"]).mean()),
        eta                   = ("eta", "mean"),
        zeta                  = ("zeta", "mean"),
        mean_area             = ("mean_area", "mean"),
        area_violation_rate   = ("area_tolerance_violation_rate", "mean"),
        n_movement_events     = ("n_movement_events", "mean"),
        n_jumps               = ("n_jumps", "mean"),
        n_roi_violations      = ("n_roi_violations", "mean"),
        bg_history            = ("bg_history", "first"),
        bg_var_threshold      = ("bg_var_threshold", "first"),
    )

    all_pipes = df[["pipeline_id", "bg_history", "bg_var_threshold"]].drop_duplicates("pipeline_id").set_index("pipeline_id")
    board = all_pipes.join(agg.drop(columns=["bg_history", "bg_var_threshold"]), how="left")
    board["n_passed"] = board["n_passed"].fillna(0).astype(int)
    board["n_records"] = n_records
    board["pass_rate"] = board["n_passed"] / n_records

    # rank: reliability first (pass_rate), then trajectory quality among the
    # records it did pass on. No single ground-truth SCORE exists here (see
    # module docstring), so this is a heuristic ordering, not a metric.
    board = board.sort_values(
        ["pass_rate", "zeta", "area_violation_rate", "eta"],
        ascending=[False, False, True, True],
    )
    cols = ["bg_history", "bg_var_threshold", "pass_rate", "n_passed", "n_records",
            "detection_rate", "zeta", "eta", "area_violation_rate",
            "n_movement_events", "n_jumps", "n_roi_violations", "mean_area"]
    return board[cols]


def build_record_summary(df: pd.DataFrame) -> pd.DataFrame:
    """Per-record view: how many of the 20 configs passed on this record at all.
    A record where almost nothing passes is either messy footage or a record
    that needs a look during triage (plan.md's 'record triage' section)."""
    return (df.groupby("record_uid")
              .agg(n_configs=("pipeline_id", "nunique"),
                   n_passed=("passed", "sum"))
              .assign(pass_rate=lambda d: d["n_passed"] / d["n_configs"])
              .sort_values("pass_rate"))


def main():
    ap = argparse.ArgumentParser(description="Real-record leaderboard aggregation (plan.md step 3)")
    ap.add_argument("--results-dir", required=True, help="dir containing per-record *.sqlite3 files")
    ap.add_argument("--out-dir", default=".", help="output directory for leaderboard.csv / summary.txt")
    args = ap.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    df = load_all(args.results_dir)
    n_records = df["record_uid"].nunique()
    n_pipes = df["pipeline_id"].nunique()
    print(f"[data] {len(df)} result rows -- {n_pipes} pipeline configs x {n_records} records")

    board = build_leaderboard(df)
    rec_summary = build_record_summary(df)

    board_path = os.path.join(args.out_dir, "leaderboard.csv")
    board.to_csv(board_path, float_format="%.4f")
    print(f"saved {board_path}")

    pd.set_option("display.width", 160)
    print("\n=== Cross-record leaderboard "
          f"({n_records} records, sorted by pass_rate desc, then zeta desc / area_violation_rate & eta asc) ===")
    print(board.to_string(float_format=lambda v: f"{v:.3f}"))

    print(f"\n=== Per-record pass rate ({n_pipes} configs each) -- low values flag messy/triage-worthy records ===")
    print(rec_summary.to_string(float_format=lambda v: f"{v:.3f}"))

    summary_path = os.path.join(args.out_dir, "summary.txt")
    with open(summary_path, "w") as f:
        f.write("=== Real-record leaderboard (plan.md step 3) ===\n")
        f.write(f"results_dir: {args.results_dir}\n")
        f.write(f"records: {n_records}\n")
        f.write(f"pipeline configs: {n_pipes}\n\n")
        if n_records < 40:
            f.write(f"NOTE: plan.md's 30/10 train/holdout split assumes ~40 usable records; "
                     f"only {n_records} are in this run, so no split was applied here -- "
                     f"treat this as a preliminary leaderboard, not a final config choice.\n\n")
        f.write("── Leaderboard (all records pooled) ──\n")
        f.write(board.to_string(float_format=lambda v: f"{v:.3f}") + "\n\n")
        f.write("── Per-record pass rate ──\n")
        f.write(rec_summary.to_string(float_format=lambda v: f"{v:.3f}") + "\n")
    print(f"\nsaved {summary_path}")


if __name__ == "__main__":
    main()
