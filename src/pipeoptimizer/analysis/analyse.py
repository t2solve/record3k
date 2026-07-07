#!/usr/bin/env python3
"""
Pipeline optimiser — analysis script.

Usage:
    python3 analyse.py --db build/data/run1/sim_results.sqlite \
                       --synth-root build/data/run1/synth \
                       --run-id 11          # last run; omit to use latest
                       --out-dir /tmp/pipe_analysis

Produces:
    01_sensitivity.png      — Spearman ρ heatmap: each pipeline param vs each metric
    02_param_impact.png     — Violin plots per param value for SCORE and alpha
    03_cluster_overview.png — Scatter: SCORE vs alpha, coloured by dominant param
    04_specialised.png      — Best-in-class table per record group
    05_record_groups.png    — Record characterisation (resting %, speed, etc.)
    06_blind_spots.png      — SCORE per pipeline × (body_type, speed_class) — which
                              specific generation combinations each pipeline fails on
    summary.txt             — Text leaderboard per group + per-pipeline blind spots

--synth-root is optional. Record traits (body_type, speed class, resting) are
derived from generation_configs.config_json in the DB by default — this
survives cleanup_synth, unlike ground_truth.json. When --synth-root does still
point at the generated frames, resting_frac/mean_speed_px are additionally
computed empirically from the realized trajectory instead of the analytic
Markov-chain estimate.
"""

import argparse
import json
import math
import os
import sqlite3
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
import numpy as np
import pandas as pd
import seaborn as sns
from scipy import stats

# ── helpers ───────────────────────────────────────────────────────────────────

def load_db(db_path: str, run_id: int | None):
    con = sqlite3.connect(db_path)
    con.row_factory = sqlite3.Row

    if run_id is None:
        run_id = con.execute("SELECT MAX(id) FROM sim_runs").fetchone()[0]
    print(f"[load_db] using sim_run_id={run_id}")

    rows = con.execute("""
        SELECT
            r.record_id, r.pipeline_id,
            r.alpha_mean, r.alpha_p95, r.beta_mean,
            r.gamma_moving, r.gamma_resting,
            r.delta_fpr, r.eta, r.zeta, r.score,
            r.n_animal_frames, r.n_detected_frames,
            r.processing_time_ms,
            g.arena_radius_px, g.config_json AS gen_config_json
        FROM sim_results r
        JOIN generation_configs g ON g.id = r.gen_config_id
        WHERE r.sim_run_id = ?
    """, (run_id,)).fetchall()

    pipe_cfgs = {
        r["pipeline_id"]: r["config_json"]
        for r in con.execute("SELECT pipeline_id, config_json FROM pipeline_configs")
    }
    con.close()
    return pd.DataFrame([dict(r) for r in rows]), pipe_cfgs


def extract_params(config_json: str) -> dict:
    """Pull the four swept pipeline parameters out of a pipeline config JSON."""
    try:
        cfg = json.loads(config_json)
        steps = {s["name"]: s["parameters"] for s in cfg["pipeline"]["steps"]}
        return {
            "ksize":       int(steps.get("MorphologyClose_CUDA",    {}).get("ksize",       15)),
            "threshold":   int(steps.get("BinaryThreshold_CUDA",    {}).get("threshold",   80)),
            "min_area":    int(steps.get("ContourAreaFilter_CUDA",  {}).get("min_area",    7)),
            "max_contours":int(steps.get("ContourAreaFilter_CUDA",  {}).get("max_contours",0)),
            "bg_history":  int(steps.get("BackgroundSubtraction_CUDA",{}).get("history",   500)),
            "varThreshold":  int(steps.get("BackgroundSubtraction_CUDA",{}).get("varThreshold",  16)),
            "detectShadows": int(steps.get("BackgroundSubtraction_CUDA",{}).get("detectShadows",  0)),
            "gauss_kernel_size": int(steps.get("GaussianBlur_CUDA",  {}).get("kernel_size", 3)),
            "gauss_sigma_x":     float(steps.get("GaussianBlur_CUDA",{}).get("sigma_x",     1.0)),
        }
    except Exception:
        return {"ksize": np.nan, "threshold": np.nan, "min_area": np.nan,
                "max_contours": np.nan, "bg_history": np.nan,
                "varThreshold": np.nan, "detectShadows": np.nan,
                "gauss_kernel_size": np.nan, "gauss_sigma_x": np.nan}


def parse_record_traits(config_json: str) -> dict:
    """
    Pull per-record generation traits out of the record's own synth config JSON
    (generation_configs.config_json — always in the DB, survives cleanup_synth,
    unlike ground_truth.json which only exists while the raw frames do).

    speed_class is a nearest-mean classification of this record's realized
    speed_px against the record's OWN speed_slow_px/normal_px/fast_px (so it
    stays correct even if those class means are changed between runs). Records
    generated without --speed-variation get speed_class="fixed" (every record
    in that batch shares the same speed, so there's no class to distinguish).

    resting_frac_est is the 2-state Markov chain's analytic steady-state
    resting probability (rest_prob / (rest_prob + resume_prob)) — used only as
    a fallback when ground_truth.json isn't available to compute the empirical
    resting fraction of the actually-realized trajectory.
    """
    empty = {"body_type": "?", "speed_px": np.nan, "all_resting": False,
             "speed_class": "unknown", "resting_frac_est": np.nan}
    if not config_json:
        return empty
    try:
        cfg  = json.loads(config_json)
        anim = cfg.get("animal", {})
        traj = cfg.get("trajectory", {})

        body_type   = anim.get("body_type", "?")
        speed_px    = traj.get("speed_px", np.nan)
        all_resting = bool(traj.get("all_resting", False))
        rest_prob   = traj.get("rest_prob")
        resume_prob = traj.get("resume_prob")

        if all_resting:
            speed_class = "static"
        elif traj.get("speed_variation", False):
            classes = {
                "slow":   traj.get("speed_slow_px",   np.nan),
                "normal": traj.get("speed_normal_px", np.nan),
                "fast":   traj.get("speed_fast_px",   np.nan),
            }
            speed_class = ("unknown" if pd.isna(speed_px) else
                           min(classes, key=lambda k: abs(classes[k] - speed_px)))
        else:
            speed_class = "fixed"

        if all_resting:
            resting_frac_est = 1.0
        elif rest_prob is not None and resume_prob is not None and (rest_prob + resume_prob) > 0:
            resting_frac_est = rest_prob / (rest_prob + resume_prob)
        else:
            resting_frac_est = np.nan

        return {"body_type": body_type, "speed_px": speed_px, "all_resting": all_resting,
                "speed_class": speed_class, "resting_frac_est": resting_frac_est}
    except Exception:
        return empty


def _load_record_stats_from_ground_truth(synth_root: str) -> pd.DataFrame:
    """
    Walk synth_root and compute per-record characteristics EMPIRICALLY from
    ground_truth.json (the actually-realized trajectory, not just the
    generative config). Only usable while the raw frames haven't been deleted
    by cleanup_synth. Returns DataFrame indexed by record_id.
    """
    records = []
    for entry in sorted(Path(synth_root).iterdir()):
        if not entry.is_dir():
            continue
        gt = entry / entry.name / "ground_truth.json"
        if not gt.exists():
            continue
        with open(gt) as f:
            data = json.load(f)

        animal_frames = [fr for fr in data["frames"] if not fr.get("is_warmup", True)]
        if not animal_frames:
            continue

        n_total    = len(animal_frames)
        n_resting  = sum(1 for fr in animal_frames if fr.get("is_resting", False))

        # compute per-frame speed from consecutive positions
        speeds = []
        for i in range(1, len(animal_frames)):
            prev, cur = animal_frames[i-1], animal_frames[i]
            if "x_px" in prev and "x_px" in cur:
                speeds.append(math.hypot(cur["x_px"] - prev["x_px"],
                                         cur["y_px"] - prev["y_px"]))

        # detect if this is an all_resting (dead animal) record:
        # all animal frames are resting
        all_resting = (n_resting == n_total)

        records.append({
            "record_id":       entry.name,
            "body_type":       data.get("body_type", "?"),
            "n_animal_frames": n_total,
            "resting_frac":    n_resting / n_total,
            "all_resting":     all_resting,
            "mean_speed_px":   float(np.mean(speeds)) if speeds else 0.0,
            "p95_speed_px":    float(np.percentile(speeds, 95)) if speeds else 0.0,
            "speed_std_px":    float(np.std(speeds)) if speeds else 0.0,
        })

    return pd.DataFrame(records).set_index("record_id")


def load_record_stats(df_raw: pd.DataFrame, synth_root: str | None) -> pd.DataFrame:
    """
    Per-record characteristics, indexed by record_id. Primary source is
    generation_configs.config_json (via parse_record_traits — always
    available). If synth_root still holds the raw ground_truth.json files,
    resting_frac/mean_speed_px/speed_std_px are additionally overlaid with the
    more precise empirical values from the actually-realized trajectory;
    speed_class always comes from config_json since ground_truth.json doesn't
    record the slow/normal/fast class means at all.
    """
    base = (df_raw[["record_id", "gen_config_json"]]
            .drop_duplicates("record_id")
            .set_index("record_id"))
    cfg_traits = base["gen_config_json"].apply(parse_record_traits).apply(pd.Series)

    have_synth = bool(synth_root) and Path(synth_root).is_dir() and any(Path(synth_root).iterdir())
    if have_synth:
        rec_stats = _load_record_stats_from_ground_truth(synth_root)
        rec_stats = rec_stats.reindex(cfg_traits.index)
        rec_stats["speed_class"] = cfg_traits["speed_class"]
        print("    record trait source: ground_truth.json (empirical) + config_json (speed_class)")
    else:
        rec_stats = pd.DataFrame(index=cfg_traits.index)
        rec_stats["body_type"]       = cfg_traits["body_type"]
        rec_stats["all_resting"]     = cfg_traits["all_resting"]
        rec_stats["mean_speed_px"]   = cfg_traits["speed_px"]
        rec_stats["resting_frac"]    = cfg_traits["resting_frac_est"]
        rec_stats["speed_class"]     = cfg_traits["speed_class"]
        rec_stats["n_animal_frames"] = np.nan
        rec_stats["p95_speed_px"]    = np.nan
        rec_stats["speed_std_px"]    = np.nan
        print("    record trait source: generation_configs.config_json only "
              "(synth_root unavailable — resting_frac is an analytic Markov-chain "
              "estimate, not empirical)")

    return rec_stats


def classify_record(row) -> str:
    """Assign a group label to a record based on its trajectory characteristics."""
    if row["all_resting"]:
        return "static"
    if row["resting_frac"] > 0.50:
        return "mostly_resting"
    if row["resting_frac"] < 0.15:
        return "mostly_moving"
    return "balanced"


def load_swept_params(path: str) -> list[str]:
    """Read param_ranges.json and return column names for params with 'values' (actively swept)."""
    with open(path) as f:
        cfg = json.load(f)
    swept = []
    for key, val in cfg.items():
        if key.startswith("_") or not isinstance(val, dict):
            continue
        if "values" in val and key in PARAM_JSON_TO_COL:
            swept.append(PARAM_JSON_TO_COL[key])
    return swept


# ── sensitivity analysis ──────────────────────────────────────────────────────

METRICS   = ["score", "alpha_mean", "alpha_p95", "gamma_moving",
             "gamma_resting", "delta_fpr", "eta", "zeta", "beta_mean"]
PARAMS = []          # populated in main() from --param-ranges or auto-detected
PARAM_LABELS = {}    # populated in main() to match PARAMS

# mapping from param_ranges.json key → extracted DataFrame column name
PARAM_JSON_TO_COL = {
    "pipeline.morph_ksize":          "ksize",
    "pipeline.contour_min_area":     "min_area",
    "pipeline.binary_threshold":     "threshold",
    "pipeline.contour_max_contours": "max_contours",
    "pipeline.bg_history":           "bg_history",
    "pipeline.bg_var_threshold":     "varThreshold",
    "pipeline.bg_detect_shadows":    "detectShadows",
    "pipeline.gaussian_kernel_size": "gauss_kernel_size",
    "pipeline.gaussian_sigma_x":     "gauss_sigma_x",
}
ALL_PARAM_LABELS = {
    "ksize":            "morph ksize",
    "threshold":        "binary threshold",
    "min_area":         "contour min_area",
    "max_contours":     "max_contours",
    "bg_history":       "BG history",
    "varThreshold":     "BG varThreshold",
    "detectShadows":    "BG detectShadows",
    "gauss_kernel_size":"Gaussian kernel_size",
    "gauss_sigma_x":    "Gaussian sigma_x",
}
METRIC_LABELS = {
    "score":        "SCORE (px)",
    "alpha_mean":   "α mean (px)",
    "alpha_p95":    "α p95 (px)",
    "gamma_moving": "γ moving",
    "gamma_resting":"γ resting",
    "delta_fpr":    "δ FPR",
    "eta":          "η multi-ctr",
    "zeta":         "ζ continuity",
    "beta_mean":    "β speed err",
}


def compute_sensitivity(df: pd.DataFrame) -> pd.DataFrame:
    """
    Spearman rank correlation between each pipeline parameter and each metric.
    Returns a DataFrame  [param × metric]  of (rho, p-value) pairs stored as rho only
    (mark non-significant cells with a lighter shade).
    """
    rho_mat  = pd.DataFrame(index=PARAMS, columns=METRICS, dtype=float)
    pval_mat = pd.DataFrame(index=PARAMS, columns=METRICS, dtype=float)

    for param in PARAMS:
        for metric in METRICS:
            col = df[metric].dropna()
            par = df.loc[col.index, param].dropna()
            idx = col.index.intersection(par.index)
            if len(idx) < 10:
                rho_mat.loc[param, metric]  = np.nan
                pval_mat.loc[param, metric] = np.nan
                continue
            rho, pval = stats.spearmanr(par.loc[idx], col.loc[idx])
            rho_mat.loc[param, metric]  = rho
            pval_mat.loc[param, metric] = pval

    return rho_mat.astype(float), pval_mat.astype(float)


def eta_squared(df: pd.DataFrame, param: str, metric: str) -> float:
    """One-way ANOVA η² — fraction of variance explained by the parameter level."""
    col = df[[param, metric]].dropna()
    groups = [g[metric].values for _, g in col.groupby(param)]
    if len(groups) < 2 or any(len(g) < 2 for g in groups):
        return np.nan
    f, p = stats.f_oneway(*groups)
    # η² = SS_between / SS_total
    grand_mean = col[metric].mean()
    ss_between = sum(len(g) * (g.mean() - grand_mean)**2 for g in groups)
    ss_total   = ((col[metric] - grand_mean)**2).sum()
    return ss_between / ss_total if ss_total > 0 else np.nan


# ── plotting ──────────────────────────────────────────────────────────────────

def plot_sensitivity(rho_mat, pval_mat, out_dir):
    fig, axes = plt.subplots(1, 2, figsize=(16, 4))

    # --- left: Spearman ρ heatmap ---
    ax = axes[0]
    mask = pval_mat > 0.05   # grey-out non-significant
    sns.heatmap(
        rho_mat,
        annot=True, fmt=".2f",
        center=0, vmin=-1, vmax=1,
        cmap="RdBu_r",
        linewidths=0.5,
        xticklabels=[METRIC_LABELS.get(m, m) for m in METRICS],
        yticklabels=[PARAM_LABELS.get(p, p) for p in PARAMS],
        ax=ax,
    )
    # cross-hatch insignificant cells
    for (i, j), val in np.ndenumerate(mask.values):
        if val:
            ax.add_patch(plt.Rectangle((j, i), 1, 1, fill=True,
                                       color='white', alpha=0.55, linewidth=0))
    ax.set_title("Spearman ρ  (grey = p > 0.05, per row = result)")
    ax.set_xticklabels(ax.get_xticklabels(), rotation=35, ha="right", fontsize=8)

    # --- right: η² effect-size bars ---
    ax2 = axes[1]
    key_metrics = ["score", "alpha_mean", "gamma_moving", "delta_fpr", "eta", "zeta"]
    eta_data = pd.DataFrame(index=PARAMS, columns=key_metrics, dtype=float)
    # we need the merged df — we'll pass it via closure trick below
    ax2.set_title("η² effect size (ANOVA) — will be filled in main()")
    ax2.set_visible(False)   # filled later

    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, "01_sensitivity.png"), dpi=130)
    plt.close(fig)
    return eta_data


def plot_sensitivity_full(df, rho_mat, pval_mat, out_dir):
    fig, axes = plt.subplots(1, 2, figsize=(17, 4.5))

    # Spearman heatmap
    ax = axes[0]
    mask = pval_mat > 0.05
    sns.heatmap(
        rho_mat,
        annot=True, fmt=".2f",
        center=0, vmin=-1, vmax=1,
        cmap="RdBu_r",
        linewidths=0.5,
        xticklabels=[METRIC_LABELS.get(m, m) for m in METRICS],
        yticklabels=[PARAM_LABELS.get(p, p) for p in PARAMS],
        ax=ax,
    )
    for (i, j), val in np.ndenumerate(mask.values):
        if val:
            ax.add_patch(plt.Rectangle((j, i), 1, 1, fill=True,
                                       color='white', alpha=0.55, linewidth=0))
    ax.set_title("Spearman ρ  (faded = p > 0.05)")
    ax.set_xticklabels(ax.get_xticklabels(), rotation=35, ha="right", fontsize=8)

    # η² bar chart
    ax2 = axes[1]
    key_metrics = ["score", "alpha_mean", "gamma_moving", "delta_fpr", "eta"]
    eta_mat = pd.DataFrame(index=PARAMS, columns=key_metrics, dtype=float)
    for param in PARAMS:
        for metric in key_metrics:
            eta_mat.loc[param, metric] = eta_squared(df, param, metric)

    x  = np.arange(len(key_metrics))
    w  = 0.18
    colors = plt.cm.Set2(np.linspace(0, 0.8, len(PARAMS)))
    for i, param in enumerate(PARAMS):
        vals = eta_mat.loc[param].values.astype(float)
        ax2.bar(x + i * w, vals, w, label=PARAM_LABELS.get(param, param),
                color=colors[i], edgecolor='white')

    ax2.set_xticks(x + w * (len(PARAMS) - 1) / 2)
    ax2.set_xticklabels([METRIC_LABELS.get(m, m) for m in key_metrics],
                        rotation=20, ha="right", fontsize=8)
    ax2.set_ylabel("η²  (fraction of variance explained)")
    ax2.set_title("ANOVA η²  per pipeline parameter")
    ax2.legend(fontsize=7)
    ax2.set_ylim(0, 1)

    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, "01_sensitivity.png"), dpi=130)
    plt.close(fig)
    print("  saved 01_sensitivity.png")


def plot_param_impact(df, out_dir):
    if not PARAMS:
        print("  no swept params (single fixed-pipeline run) — skipping 02_param_impact.png")
        return
    fig, axes = plt.subplots(2, len(PARAMS), figsize=(5 * len(PARAMS), 8))

    for col_i, param in enumerate(PARAMS):
        vals = sorted(df[param].dropna().unique())
        for row_i, metric in enumerate(["score", "alpha_mean"]):
            ax = axes[row_i, col_i]
            groups = [df[df[param] == v][metric].dropna().values for v in vals]
            bp = ax.violinplot(groups, positions=range(len(vals)), showmedians=True,
                               showextrema=True)
            for body in bp["bodies"]:
                body.set_alpha(0.6)

            # overlay individual points jittered
            for j, g in enumerate(groups):
                ax.scatter(
                    np.random.normal(j, 0.07, len(g)), g,
                    s=6, alpha=0.4, color="black", zorder=3,
                )
            ax.set_xticks(range(len(vals)))
            ax.set_xticklabels([str(int(v)) for v in vals], fontsize=8)
            ax.set_xlabel(PARAM_LABELS.get(param, param), fontsize=8)
            ax.set_ylabel(METRIC_LABELS.get(metric, metric), fontsize=8)
            if row_i == 0:
                ax.set_title(PARAM_LABELS.get(param, param))

    fig.suptitle("Parameter impact — SCORE and α for each parameter level\n"
                 "(each violin aggregates all records × all other parameters)",
                 fontsize=10)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, "02_param_impact.png"), dpi=130)
    plt.close(fig)
    print("  saved 02_param_impact.png")


def plot_cluster_overview(df_agg, out_dir):
    """Scatter SCORE vs alpha_mean/gamma_moving, coloured by first two swept params."""
    p0 = PARAMS[0] if PARAMS else "ksize"
    p1 = PARAMS[1] if len(PARAMS) > 1 else p0
    fig, axes = plt.subplots(1, 2, figsize=(13, 5))

    def _scatter_by(ax, colour_param, x_col, y_col, cmap_name, legend_fs=8):
        if colour_param not in df_agg.columns:
            ax.text(0.5, 0.5, f"column '{colour_param}' not found",
                    ha="center", va="center", transform=ax.transAxes)
            return
        vals = sorted(df_agg[colour_param].dropna().unique())
        cmap = getattr(plt.cm, cmap_name)(np.linspace(0.1, 0.9, max(len(vals), 1)))
        lbl  = PARAM_LABELS.get(colour_param, colour_param)
        for i, v in enumerate(vals):
            sub = df_agg[df_agg[colour_param] == v]
            ax.scatter(sub[x_col], sub[y_col],
                       label=f"{lbl}={v}", color=cmap[i], s=60, zorder=3)
            for _, row in sub.iterrows():
                ax.annotate(row.name.replace("pipe-lhs-", "").replace("-42", ""),
                            (row[x_col], row[y_col]), fontsize=5.5, alpha=0.7)
        ax.legend(fontsize=legend_fs)

    ax = axes[0]
    _scatter_by(ax, p0, "score", "alpha_mean", "plasma")
    ax.set_xlabel("SCORE (px)  ← lower is better")
    ax.set_ylabel("α mean (px) on detected frames")
    ax.set_title(f"Pipeline clusters — coloured by {PARAM_LABELS.get(p0, p0)}")

    ax2 = axes[1]
    _scatter_by(ax2, p1, "score", "gamma_moving", "viridis", legend_fs=7)
    ax2.set_xlabel("SCORE (px)")
    ax2.set_ylabel("γ moving (detection rate while moving)")
    ax2.set_title(f"Pipeline clusters — coloured by {PARAM_LABELS.get(p1, p1)}")

    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, "03_cluster_overview.png"), dpi=130)
    plt.close(fig)
    print("  saved 03_cluster_overview.png")


def plot_specialised(df_merged, rec_stats, out_dir):
    """
    For each record group show the top-3 pipelines ranked by mean SCORE within that group.
    Groups: all, mostly_moving, balanced, mostly_resting, static (if present).
    """
    groups = rec_stats["group"].unique()
    group_order = [g for g in ["all", "mostly_moving", "balanced", "mostly_resting", "static"]
                   if g in list(groups) + ["all"]]

    fig_rows = len(group_order)
    fig, axes = plt.subplots(fig_rows, 1, figsize=(14, 2.5 * fig_rows))
    if fig_rows == 1:
        axes = [axes]

    summary_lines = []

    for ax, group in zip(axes, group_order):
        if group == "all":
            sub = df_merged
            title = "ALL records"
        else:
            rids = rec_stats[rec_stats["group"] == group].index
            sub  = df_merged[df_merged["record_id"].isin(rids)]
            title = f"Group: {group}  ({len(rids)} records)"

        if sub.empty:
            ax.set_visible(False)
            continue

        base_agg = dict(
            score        = ("score",        "mean"),
            alpha_mean   = ("alpha_mean",   "mean"),
            gamma_moving = ("gamma_moving", "mean"),
            delta_fpr    = ("delta_fpr",    "mean"),
            eta          = ("eta",          "mean"),
            zeta         = ("zeta",         "mean"),
        )
        param_agg = {p: (p, "first") for p in PARAMS if p in sub.columns}
        rank = (sub.groupby("pipeline_id")
                   .agg(**base_agg, **param_agg)
                   .sort_values("score")
                   .head(8))

        metric_cols = ["score", "alpha_mean", "gamma_moving", "delta_fpr", "eta", "zeta"]
        metric_hdrs = ["SCORE", "α", "γmov", "δFPR", "η", "ζ"]
        param_cols  = [p for p in PARAMS if p in rank.columns]
        param_hdrs  = [PARAM_LABELS.get(p, p) for p in param_cols]
        cols        = metric_cols + param_cols
        col_labels  = metric_hdrs + param_hdrs

        def _fmt(v):
            if isinstance(v, float) and math.isnan(v):
                return "—"
            try:
                fv = float(v)
                return str(int(fv)) if fv == int(fv) else f"{fv:.3f}"
            except Exception:
                return str(v)

        cell_data = [[
            f"{row['score']:.1f}",
            f"{row['alpha_mean']:.2f}" if not math.isnan(row["alpha_mean"]) else "—",
            f"{row['gamma_moving']:.3f}" if not math.isnan(row.get("gamma_moving", float('nan'))) else "—",
            f"{row['delta_fpr']:.3f}",
            f"{row['eta']:.3f}",
            f"{row['zeta']:.3f}",
        ] + [_fmt(row.get(p, float('nan'))) for p in param_cols]
        for pid, row in rank.iterrows()]

        row_labels = [pid.replace("pipe-lhs-", "").replace("-42", "") for pid in rank.index]

        ax.axis("off")
        tbl = ax.table(
            cellText=cell_data,
            rowLabels=row_labels,
            colLabels=col_labels,
            loc="center",
            cellLoc="center",
        )
        tbl.auto_set_font_size(False)
        tbl.set_fontsize(8)
        tbl.scale(1.0, 1.3)

        # colour top-3 rows
        for ri in range(min(3, len(row_labels))):
            for ci in range(len(col_labels)):
                colors = ["#d4edda", "#d9edf7", "#fcf8e3"]
                tbl[ri + 1, ci].set_facecolor(colors[ri])

        ax.set_title(title, fontsize=9, fontweight="bold", pad=2)

        # text summary
        best = rank.iloc[0]
        param_str = "  ".join(
            f"{PARAM_LABELS.get(p, p)}={_fmt(best.get(p, float('nan')))}"
            for p in param_cols
        )
        summary_lines.append(
            f"\n=== {title} ===\n"
            f"  Best: {rank.index[0]}  SCORE={best['score']:.1f}  "
            f"α={best['alpha_mean']:.2f}  γmov={best['gamma_moving']:.3f}  "
            + param_str
        )

    fig.suptitle("Top-8 pipelines per record group  (green=1st, blue=2nd, yellow=3rd)",
                 fontsize=10, y=1.01)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, "04_specialised.png"), dpi=130, bbox_inches="tight")
    plt.close(fig)
    print("  saved 04_specialised.png")
    return summary_lines


def plot_record_groups(rec_stats, out_dir):
    fig, axes = plt.subplots(1, 3, figsize=(14, 4))

    colors = {"static": "#e74c3c", "mostly_resting": "#e67e22",
              "balanced": "#2ecc71", "mostly_moving": "#3498db"}

    ax = axes[0]
    for g, sub in rec_stats.groupby("group"):
        ax.scatter(sub.index.str[-4:], sub["resting_frac"],
                   label=g, color=colors.get(g, "grey"), s=60)
    ax.set_ylabel("Resting fraction")
    ax.set_title("Record resting fraction")
    ax.legend(fontsize=7)
    ax.tick_params(axis="x", rotation=45)

    ax2 = axes[1]
    for g, sub in rec_stats.groupby("group"):
        ax2.scatter(sub.index.str[-4:], sub["mean_speed_px"],
                    label=g, color=colors.get(g, "grey"), s=60)
    ax2.set_ylabel("Mean speed (px/frame)")
    ax2.set_title("Record mean speed")
    ax2.tick_params(axis="x", rotation=45)

    ax3 = axes[2]
    group_counts = rec_stats["group"].value_counts()
    ax3.bar(group_counts.index,
            group_counts.values,
            color=[colors.get(g, "grey") for g in group_counts.index])
    ax3.set_ylabel("Number of records")
    ax3.set_title("Record group distribution")

    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, "05_record_groups.png"), dpi=130)
    plt.close(fig)
    print("  saved 05_record_groups.png")


def plot_blind_spots(df_merged: pd.DataFrame, out_dir: str, top_n_pipelines: int = 12) -> list:
    """
    Cross-tabulates mean SCORE per pipeline × (body_type, speed_class) combo.
    A single overall SCORE, or even the per-resting-group breakdown in
    04_specialised.png, can hide a pipeline that's excellent on average but
    systematically blind to one specific combination (e.g. round+fast, or a
    body type it was never really tested well against) — this heatmap is
    meant to surface exactly that.
    """
    combo = df_merged.copy()
    combo["combo"] = combo["body_type"].astype(str) + " / " + combo["speed_class"].astype(str)

    overall   = combo.groupby("pipeline_id")["score"].mean().sort_values()
    top_pipes = overall.head(top_n_pipelines).index

    sub    = combo[combo["pipeline_id"].isin(top_pipes)]
    pivot  = sub.pivot_table(index="pipeline_id", columns="combo", values="score", aggfunc="mean")
    counts = sub.pivot_table(index="pipeline_id", columns="combo", values="score", aggfunc="count")
    pivot  = pivot.loc[[p for p in top_pipes if p in pivot.index]]

    if pivot.empty:
        print("  skipping 06_blind_spots.png — no (body_type, speed_class) combinations found")
        return []

    fig, ax = plt.subplots(figsize=(max(8, 1.1 * pivot.shape[1] + 3),
                                     max(4, 0.4 * pivot.shape[0] + 2)))
    sns.heatmap(pivot, annot=True, fmt=".1f", cmap="RdYlGn_r", ax=ax,
                cbar_kws={"label": "mean SCORE (px, lower = better)"},
                linewidths=0.5, linecolor="white")
    ax.set_title("Blind spots — mean SCORE per pipeline × (body_type / speed_class)\n"
                 f"(top {len(pivot)} pipelines by overall SCORE; darker red = "
                 "systematic failure on that combination)")
    ax.set_xlabel("")
    ax.set_ylabel("")
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, "06_blind_spots.png"), dpi=130)
    plt.close(fig)
    print("  saved 06_blind_spots.png")

    # text summary: the true worst combo per pipeline. Deliberately NOT filtered
    # by record count — combos like "static" are inherently low-n by design
    # (generate's --static-records defaults to n_records/5) and are exactly the
    # kind of rare-but-important blind spot this is meant to surface, not hide.
    # The n= annotation lets the reader judge single-record noise for themselves.
    lines = ["\n=== Blind spots (worst body_type/speed_class combo per pipeline) ==="]
    for pid in pivot.index:
        row = pivot.loc[pid].dropna()
        if row.empty:
            continue
        worst = row.idxmax()
        n = int(counts.loc[pid, worst])
        caveat = "  (single record — treat as anecdotal)" if n < 2 else ""
        lines.append(f"  {pid}: worst on [{worst}]  SCORE={row[worst]:.1f}  (n={n}){caveat}")
    return lines


# ── main ──────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(description="Pipeline optimiser — analysis")
    ap.add_argument("--db",           required=True,  help="sim_results.sqlite path")
    ap.add_argument("--synth-root",   default=None,
                    help="dir containing synth-* dirs (optional — if missing/deleted, e.g. "
                         "after cleanup_synth, record traits are derived from "
                         "generation_configs.config_json in the DB instead)")
    ap.add_argument("--run-id",       type=int,       help="sim_run_id (default: latest)")
    ap.add_argument("--out-dir",      default=".",    help="output directory for plots")
    ap.add_argument("--param-ranges", default=None,
                    help="param_ranges.json path — determines which params to analyse "
                         "(default: auto-detect from columns that actually vary)")
    args = ap.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    # ── 1. Load data ──────────────────────────────────────────────────────────
    print("[1] Loading database …")
    df_raw, pipe_cfgs = load_db(args.db, args.run_id)
    print(f"    {len(df_raw)} sim_result rows  ({df_raw['pipeline_id'].nunique()} pipes × "
          f"{df_raw['record_id'].nunique()} records)")

    # ── 2. Extract pipeline parameters ───────────────────────────────────────
    print("[2] Extracting pipeline parameters …")
    params_df = pd.DataFrame({pid: extract_params(cfg)
                               for pid, cfg in pipe_cfgs.items()}).T
    params_df.index.name = "pipeline_id"
    params_df = params_df.reset_index()
    df = df_raw.merge(params_df, on="pipeline_id", how="left")

    # ── 2b. Determine which params are swept (drives all plots) ───────────────
    print("[2b] Determining swept parameters …")
    global PARAMS, PARAM_LABELS
    if args.param_ranges:
        PARAMS = load_swept_params(args.param_ranges)
        print(f"      From {args.param_ranges}: {PARAMS}")
    if not PARAMS:
        all_known = list(PARAM_JSON_TO_COL.values())
        PARAMS = [p for p in all_known if p in df.columns and df[p].nunique() > 1]
        src = "auto-detected (nunique > 1)" if not args.param_ranges else "JSON returned empty — auto-detected"
        print(f"      {src}: {PARAMS}")
    PARAM_LABELS = {p: ALL_PARAM_LABELS.get(p, p) for p in PARAMS}

    # ── 3. Load record stats ──────────────────────────────────────────────────
    print("[3] Loading record stats …")
    rec_stats = load_record_stats(df_raw, args.synth_root)
    rec_stats["group"] = rec_stats.apply(classify_record, axis=1)
    print(f"    {len(rec_stats)} records found")
    print("    Group distribution:")
    for g, n in rec_stats["group"].value_counts().items():
        print(f"      {g}: {n}")
    print("    Speed-class distribution:")
    for g, n in rec_stats["speed_class"].value_counts().items():
        print(f"      {g}: {n}")
    plot_record_groups(rec_stats, args.out_dir)

    # ── 4. Merge record info into main df ────────────────────────────────────
    df_merged = df.merge(
        rec_stats[["resting_frac", "mean_speed_px", "body_type", "group", "speed_class"]].reset_index(),
        on="record_id", how="left"
    )

    # ── 5. Per-pipeline aggregate (mean over all records) ────────────────────
    print("[4] Aggregating per pipeline …")
    agg_spec = dict(
        score        = ("score",         "mean"),
        alpha_mean   = ("alpha_mean",    "mean"),
        gamma_moving = ("gamma_moving",  "mean"),
        gamma_resting= ("gamma_resting", "mean"),
        delta_fpr    = ("delta_fpr",     "mean"),
        eta          = ("eta",           "mean"),
        zeta         = ("zeta",          "mean"),
        ksize        = ("ksize",         "first"),
        threshold    = ("threshold",     "first"),
        min_area     = ("min_area",      "first"),
        max_contours = ("max_contours",  "first"),
    )
    # Every swept param must survive into df_agg (one value per pipeline_id,
    # so "first" is safe) — plot_cluster_overview colours by PARAMS[0]/[1],
    # whatever those happen to be for this sweep, not just the fixed set above.
    for p in PARAMS:
        if p not in agg_spec:
            agg_spec[p] = (p, "first")
    df_agg = (df_merged.groupby("pipeline_id")
                       .agg(**agg_spec)
                       .sort_values("score"))
    preview_cols = ["score", "alpha_mean", "gamma_moving", "delta_fpr"] + PARAMS
    print(df_agg[preview_cols].head(10).to_string())

    # ── 6. Sensitivity analysis ───────────────────────────────────────────────
    print("[5] Sensitivity analysis (Spearman ρ + ANOVA η²) …")
    rho_mat, pval_mat = compute_sensitivity(df_merged)
    print("  Spearman ρ matrix:")
    print(rho_mat.to_string())
    plot_sensitivity_full(df_merged, rho_mat, pval_mat, args.out_dir)

    # ── 7. Parameter impact violins ───────────────────────────────────────────
    print("[6] Parameter impact plots …")
    plot_param_impact(df_merged, args.out_dir)

    # ── 8. Cluster overview ───────────────────────────────────────────────────
    print("[7] Cluster overview …")
    plot_cluster_overview(df_agg, args.out_dir)

    # ── 9. Specialised leaderboards ───────────────────────────────────────────
    print("[8] Specialised leaderboards …")
    summary = plot_specialised(df_merged, rec_stats, args.out_dir)

    # ── 9b. Blind spots — body_type × speed_class ─────────────────────────────
    print("[8b] Blind-spot analysis …")
    blind_spot_summary = plot_blind_spots(df_merged, args.out_dir)

    # ── 10. Text summary ──────────────────────────────────────────────────────
    summary_path = os.path.join(args.out_dir, "summary.txt")
    with open(summary_path, "w") as f:
        f.write("=== Pipeline Optimiser — Analysis Summary ===\n")
        f.write(f"DB: {args.db}\n")
        f.write(f"Records: {df_merged['record_id'].nunique()}\n")
        f.write(f"Pipelines: {df_merged['pipeline_id'].nunique()}\n\n")
        f.write("── Record groups ──\n")
        f.write(rec_stats["group"].value_counts().to_string() + "\n")
        f.write("\n── Speed classes ──\n")
        f.write(rec_stats["speed_class"].value_counts().to_string() + "\n")
        f.write("\n── Spearman ρ (param → metric) ──\n")
        f.write(rho_mat.to_string() + "\n")
        for line in summary:
            f.write(line + "\n")
        for line in blind_spot_summary:
            f.write(line + "\n")

    print(f"  saved summary.txt")
    print(f"\nDone. All outputs in {args.out_dir}/")
    print("".join(summary))
    print("\n".join(blind_spot_summary))


if __name__ == "__main__":
    main()
