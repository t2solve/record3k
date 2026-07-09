#!/usr/bin/env python3
"""
Motion-energy heatmap audit -- plan.md step 3's post-hoc audit, before trusting
a leaderboard winner.

Method: accumulate per-pixel |frame[t] - frame[t-1]| over the whole record,
directly from raw frames -- no pipeline involved at all. Anywhere the animal
(or anything else) moved lights up; static background stays near zero. This
is a completely independent check on top of real_leaderboard.py's numeric
metrics: it can catch the exact failure mode plan.md warns about, a config
that satisfies "not jumping" + "in ROI" + "same area" perfectly while locked
onto a static artifact (hot pixel, vignette) -- because a hot pixel produces
almost NO motion energy of its own, so a "winning" config whose detections
don't line up with where the energy actually is is a red flag, independent of
whatever its numeric leaderboard row says.

We additionally overlay two things already sitting in the tags DB (plan.md
step 1), no ground truth needed to be freshly generated:
  - the tagged ROI circle (tagged_records.roi_center_x/y/radius)
  - the tracker-assisted trajectory (tagged_points) -- turns out dense
    (~1000/1000 frames for most records here), so despite plan.md's original
    "ground-truth-free scoring" framing for the automatic sweep metric, this
    already amounts to real ground truth for the audit step specifically.

A coherent energy blob that sits inside the ROI and tracks the tagged
trajectory = record looks legit, trust the leaderboard row. A diffuse/
scattered blob, or one that sits outside the ROI or away from the tagged
path = something is off with the record itself (bad footage, wrong ROI tag,
recording artifact) -- go look at the raw frames before reading anything into
that record's leaderboard numbers.

Does NOT need any pipeline's own detected trajectory -- that isn't persisted
anywhere today (RealSimDB only stores aggregate metrics per config, see
real_sim_db.h), so there is nothing to overlay for a specific pipeline_id yet.
This audits the RECORD, not a specific config.

Usage (single record, already-unpacked frames dir):
    python3 motion_energy_audit.py \\
        --record-dir /data/records/file-rec-rec-1783346631829-8302 \\
        --tags-db data/simulation/supervised_records_data.sqlite3 \\
        --out-dir /tmp/audit

Usage (record archive -- extracted to a temp dir, cleaned up after):
    python3 motion_energy_audit.py \\
        --record-archive /data/records/file-rec-rec-1783346631829-8302.tar.gz \\
        --tags-db data/simulation/supervised_records_data.sqlite3 \\
        --out-dir /tmp/audit

Usage (batch -- audit every record used in a leaderboard run in one pass):
    python3 motion_energy_audit.py \\
        --records-root /data/records \\
        --record-uids rec-1783346631829-8302 rec-1783346485661-1273 \\
        --tags-db data/simulation/supervised_records_data.sqlite3 \\
        --out-dir /tmp/audit

Start with the records real_leaderboard.py's per-record pass-rate table
flagged as suspiciously low (e.g. bgsub_sweep1: rec-1783346631829-8302 at
5%, rec-1783346485661-1273 at 60%) -- that's exactly what this is for:
telling apart "bad record" from "bad config" for the ones that stand out.

Requires raw frames, so must run wherever those actually live (the sqlite3
leaderboard inputs alone are not enough) -- typically the machine holding
records_archive_root, not necessarily this analysis machine.
"""

import argparse
import os
import re
import shutil
import sqlite3
import tarfile
import tempfile
from contextlib import contextmanager
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from PIL import Image

FRAME_RE = re.compile(r"frame_(\d+)_")


def find_frames_dir(root: Path) -> Path:
    """Locate the 'frames' directory inside a (possibly nested) unpacked record,
    per recordStruture.txt's layout: file-rec-<uid>/file-rec-<uid>/frames/*.jpg"""
    if (root / "frames").is_dir():
        return root / "frames"
    for candidate in root.rglob("frames"):
        if candidate.is_dir() and any(candidate.glob("*.jpg")):
            return candidate
    raise FileNotFoundError(f"no 'frames' directory with .jpg files found under {root}")


def list_frame_paths(frames_dir: Path) -> list:
    paths = []
    for p in frames_dir.glob("*.jpg"):
        m = FRAME_RE.search(p.name)
        if m:
            paths.append((int(m.group(1)), p))
    paths.sort(key=lambda t: t[0])
    if not paths:
        raise FileNotFoundError(f"no frame_<idx>_*.jpg files found in {frames_dir}")
    return paths


@contextmanager
def resolve_frames_dir(record_dir: str = None, record_archive: str = None):
    """Yields a Path to the record's frames dir, extracting to a temp dir (and
    cleaning it up) if given an archive -- mirrors RealSimRunner's own
    extract-once/cleanup-after behaviour (plan.md step 2), never leaves temp
    frames lying around after the audit is done."""
    if record_dir:
        yield find_frames_dir(Path(record_dir))
        return

    tmp = tempfile.mkdtemp(prefix="motion_energy_audit_")
    try:
        with tarfile.open(record_archive) as tf:
            tf.extractall(tmp, filter="data")
        yield find_frames_dir(Path(tmp))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def compute_motion_energy(frame_paths: list, stride: int = 1, downscale: float = 0.25):
    """
    Streaming accumulation of |frame[t] - frame[t-1]| in grayscale -- holds only
    the running accumulator plus one previous frame in memory, not all frames at
    once (records run to ~5000 frames at 1936x1216, per plan.md step 2's own
    note about full-resolution memory cost).

    stride subsamples frames (every Nth) for speed on long records; downscale
    shrinks each frame before diffing. Returns (accum, scale) where scale is the
    downscale factor actually used -- callers must apply the same scale to ROI/
    trajectory coordinates (which are in original-frame pixel space) before
    overlaying them on accum.
    """
    accum = None
    prev = None
    n_diffs = 0
    for idx, p in frame_paths[::stride]:
        img = Image.open(p).convert("L")
        if downscale != 1.0:
            w, h = img.size
            img = img.resize((max(1, int(w * downscale)), max(1, int(h * downscale))),
                              Image.BILINEAR)
        arr = np.asarray(img, dtype=np.float32)
        if accum is None:
            accum = np.zeros_like(arr, dtype=np.float64)
        if prev is not None:
            accum += np.abs(arr - prev)
            n_diffs += 1
        prev = arr
    if n_diffs == 0:
        raise ValueError("fewer than 2 frames sampled -- check stride vs frame count")
    return accum, n_diffs


def load_tags(tags_db: str, record_uid: str):
    """Returns (roi_dict_or_None, list_of_(frame_index, x, y)) for one record."""
    con = sqlite3.connect(tags_db)
    con.row_factory = sqlite3.Row
    rec = con.execute(
        "SELECT roi_defined, roi_center_x, roi_center_y, roi_radius, record_failed "
        "FROM tagged_records WHERE record_uid = ?", (record_uid,)
    ).fetchone()
    roi = None
    if rec and rec["roi_defined"]:
        roi = {"cx": rec["roi_center_x"], "cy": rec["roi_center_y"], "r": rec["roi_radius"]}
    points = con.execute(
        "SELECT frame_index, x, y FROM tagged_points WHERE record_uid = ? ORDER BY frame_index",
        (record_uid,)
    ).fetchall()
    con.close()
    return roi, [(r["frame_index"], r["x"], r["y"]) for r in points], (bool(rec["record_failed"]) if rec else None)


def sanity_stats(accum: np.ndarray, roi: dict, scale: float) -> dict:
    """Fraction of total motion energy that falls inside the tagged ROI, plus
    the distance from the energy-weighted centroid to the ROI center. High
    frac + small centroid offset = the thing that moved in this footage is
    where we think the arena/animal is; low frac or a big offset means the
    ROI tag, or the record itself, deserves a second look before trusting any
    leaderboard row computed against it."""
    h, w = accum.shape
    yy, xx = np.mgrid[0:h, 0:w]
    total_energy = float(accum.sum())
    if total_energy <= 0:
        return {"energy_frac_inside_roi": float("nan"), "centroid_dist_px": float("nan")}

    cx, cy, r = roi["cx"] * scale, roi["cy"] * scale, roi["r"] * scale
    inside = (xx - cx) ** 2 + (yy - cy) ** 2 <= r ** 2
    frac_inside = float(accum[inside].sum() / total_energy)

    centroid_x = float((xx * accum).sum() / total_energy)
    centroid_y = float((yy * accum).sum() / total_energy)
    centroid_dist = float(np.hypot(centroid_x - cx, centroid_y - cy)) / scale  # back to original px

    return {"energy_frac_inside_roi": frac_inside, "centroid_dist_px": centroid_dist}


def render(accum: np.ndarray, roi: dict, points: list, scale: float,
           record_uid: str, out_path: str, stats: dict):
    fig, ax = plt.subplots(figsize=(9, 7))
    im = ax.imshow(accum, cmap="inferno")
    fig.colorbar(im, ax=ax, label="accumulated |Δframe|", shrink=0.8)

    if roi is not None:
        circ = plt.Circle((roi["cx"] * scale, roi["cy"] * scale), roi["r"] * scale,
                           fill=False, edgecolor="cyan", linewidth=1.5, linestyle="--",
                           label="tagged ROI")
        ax.add_patch(circ)

    if points:
        xs = [p[1] * scale for p in points]
        ys = [p[2] * scale for p in points]
        order = np.arange(len(points))
        ax.scatter(xs, ys, c=order, cmap="viridis", s=3, alpha=0.6,
                    label="tagged trajectory (dark=early, light=late)")

    title = f"{record_uid}  --  motion-energy heatmap"
    if roi is not None and not np.isnan(stats.get("energy_frac_inside_roi", float("nan"))):
        title += (f"\nenergy inside ROI: {stats['energy_frac_inside_roi']:.1%}   "
                  f"centroid offset from ROI center: {stats['centroid_dist_px']:.1f}px")
    ax.set_title(title, fontsize=10)
    ax.legend(fontsize=7, loc="upper right")
    ax.set_xticks([]); ax.set_yticks([])
    fig.tight_layout()
    fig.savefig(out_path, dpi=140)
    plt.close(fig)


def audit_one(frames_dir: Path, record_uid: str, tags_db: str, out_dir: str,
              stride: int, downscale: float):
    frame_paths = list_frame_paths(frames_dir)
    accum, n_diffs = compute_motion_energy(frame_paths, stride=stride, downscale=downscale)
    roi, points, record_failed = load_tags(tags_db, record_uid)

    stats = sanity_stats(accum, roi, downscale) if roi else {}
    out_path = os.path.join(out_dir, f"{record_uid}_motion_energy.png")
    render(accum, roi, points, downscale, record_uid, out_path, stats)

    print(f"[{record_uid}] {len(frame_paths)} frames ({n_diffs} diffs used, stride={stride}), "
          f"record_failed={record_failed}, roi_defined={roi is not None}, "
          f"n_tagged_points={len(points)}")
    if stats:
        print(f"    energy_frac_inside_roi={stats['energy_frac_inside_roi']:.3f}  "
              f"centroid_dist_px={stats['centroid_dist_px']:.1f}")
    print(f"    saved {out_path}")

    row = {"record_uid": record_uid, "n_frames": len(frame_paths), "n_diffs": n_diffs,
           "roi_defined": roi is not None, "n_tagged_points": len(points)}
    row.update(stats)
    return row


def main():
    ap = argparse.ArgumentParser(description="Motion-energy heatmap audit (plan.md step 3)")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--record-dir", help="single already-unpacked record dir")
    src.add_argument("--record-archive", help="single record .tar.gz")
    src.add_argument("--records-root", help="dir of record archives/dirs for batch mode "
                                             "(used with --record-uids)")
    ap.add_argument("--record-uids", nargs="+", default=None,
                     help="record UIDs to audit under --records-root (batch mode)")
    ap.add_argument("--tags-db", required=True, help="supervised_records_data.sqlite3 path")
    ap.add_argument("--out-dir", default=".", help="output dir for PNGs + stats.csv")
    ap.add_argument("--stride", type=int, default=2,
                     help="use every Nth frame (default 2 -- halves compute, negligible "
                          "loss of accumulated energy signal for a sanity-check heatmap)")
    ap.add_argument("--downscale", type=float, default=0.25,
                     help="shrink frames by this factor before diffing (default 0.25)")
    args = ap.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)
    rows = []

    if args.records_root:
        if not args.record_uids:
            raise SystemExit("--records-root requires --record-uids")
        for uid in args.record_uids:
            root = Path(args.records_root)
            archive = root / f"file-rec-{uid}.tar.gz"
            unpacked = root / f"file-rec-{uid}"
            if archive.exists():
                with resolve_frames_dir(record_archive=str(archive)) as frames_dir:
                    rows.append(audit_one(frames_dir, uid, args.tags_db, args.out_dir,
                                           args.stride, args.downscale))
            elif unpacked.is_dir():
                with resolve_frames_dir(record_dir=str(unpacked)) as frames_dir:
                    rows.append(audit_one(frames_dir, uid, args.tags_db, args.out_dir,
                                           args.stride, args.downscale))
            else:
                print(f"[{uid}] SKIPPED -- no archive or unpacked dir found under {root}")
    else:
        record_dir = args.record_dir
        record_archive = args.record_archive
        uid = Path(record_dir or record_archive).name
        uid = re.sub(r"^file-rec-", "", uid).replace(".tar.gz", "")
        with resolve_frames_dir(record_dir=record_dir, record_archive=record_archive) as frames_dir:
            rows.append(audit_one(frames_dir, uid, args.tags_db, args.out_dir,
                                   args.stride, args.downscale))

    import csv
    stats_path = os.path.join(args.out_dir, "motion_energy_stats.csv")
    with open(stats_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["record_uid", "n_frames", "n_diffs", "roi_defined",
                                                "n_tagged_points", "energy_frac_inside_roi",
                                                "centroid_dist_px"])
        writer.writeheader()
        for r in rows:
            writer.writerow(r)
    print(f"\nsaved {stats_path}")


if __name__ == "__main__":
    main()
