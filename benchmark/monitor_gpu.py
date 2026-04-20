#!/usr/bin/env python3
"""
GPU + RAM monitor — samples nvidia-smi and /proc/meminfo at a fixed interval,
writes a CSV to /tmp/, and on exit produces a time-series chart saved to the
script's directory with a timestamp in the filename.

Usage:
    python3 monitor_gpu.py                  # sample every 1 s until Ctrl-C
    python3 monitor_gpu.py -i 0.5 -d 60    # 0.5 s interval, stop after 60 s
    python3 monitor_gpu.py --no-chart       # CSV only, skip chart generation

    # Inside the container:
    podman exec -it record3k-api python3 /app/bin/monitor_gpu.py -i 1

Requirements (host or container):
    apt-get install -y python3 python3-matplotlib
    # matplotlib is only needed for chart generation; CSV still works without it
"""

import argparse
import csv
import os
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------
SCRIPT_DIR = Path(__file__).resolve().parent
CSV_DIR    = Path("/tmp")
CHART_DIR  = SCRIPT_DIR

NVIDIA_SMI_QUERY = (
    "utilization.gpu,"
    "utilization.memory,"
    "memory.used,"
    "memory.free,"
    "memory.total,"
    "power.draw,"
    "temperature.gpu,"
    "clocks.current.sm,"
    "clocks.current.memory"
)
CSV_HEADER = [
    "timestamp_utc",
    "elapsed_s",
    "gpu_util_pct",
    "mem_ctrl_util_pct",
    "vram_used_mb",
    "vram_free_mb",
    "vram_total_mb",
    "power_draw_w",
    "temp_c",
    "sm_clock_mhz",
    "mem_clock_mhz",
    "ram_used_mb",
    "ram_free_mb",
    "ram_total_mb",
]

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
_stop = False

def _sighandler(sig, _frame):
    global _stop
    _stop = True

def _query_nvidia_smi():
    """Returns a dict of GPU metrics, or None if nvidia-smi is unavailable."""
    try:
        out = subprocess.check_output(
            [
                "nvidia-smi",
                f"--query-gpu={NVIDIA_SMI_QUERY}",
                "--format=csv,noheader,nounits",
            ],
            stderr=subprocess.DEVNULL,
            text=True,
        ).strip().splitlines()[0]  # first GPU only
        vals = [v.strip() for v in out.split(",")]
        keys = [
            "gpu_util_pct", "mem_ctrl_util_pct",
            "vram_used_mb", "vram_free_mb", "vram_total_mb",
            "power_draw_w", "temp_c",
            "sm_clock_mhz", "mem_clock_mhz",
        ]
        result = {}
        for k, v in zip(keys, vals):
            try:
                result[k] = float(v)
            except ValueError:
                result[k] = None  # "[Not Supported]" etc.
        return result
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None

def _query_ram_mb():
    """Parse /proc/meminfo for total/free/used RAM in MB."""
    info = {}
    try:
        with open("/proc/meminfo") as f:
            for line in f:
                parts = line.split()
                if len(parts) >= 2:
                    info[parts[0].rstrip(":")] = int(parts[1])  # kB
        total = info.get("MemTotal", 0) / 1024
        free  = info.get("MemAvailable", info.get("MemFree", 0)) / 1024
        used  = total - free
        return round(used, 1), round(free, 1), round(total, 1)
    except OSError:
        return None, None, None

def _safe(v):
    return "" if v is None else v

# ---------------------------------------------------------------------------
# Chart
# ---------------------------------------------------------------------------
def _make_chart(rows, chart_path):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        import matplotlib.dates as mdates
        from datetime import datetime as _dt
    except ImportError:
        print("[monitor_gpu] matplotlib not available — skipping chart.")
        print("             Install with: pip3 install matplotlib  or  apt-get install python3-matplotlib")
        return

    if not rows:
        print("[monitor_gpu] No data to plot.")
        return

    ts   = [_dt.fromisoformat(r["timestamp_utc"].replace("Z", "+00:00")) for r in rows]
    t0   = ts[0]
    secs = [(t - t0).total_seconds() for t in ts]

    def col(key):
        return [float(r[key]) if r[key] not in ("", None) else float("nan") for r in rows]

    fig, axes = plt.subplots(4, 1, figsize=(14, 12), sharex=True)
    fig.suptitle(
        f"GPU + RAM monitor  |  started {t0.strftime('%Y-%m-%d %H:%M:%S UTC')}  |  {len(rows)} samples",
        fontsize=11,
    )

    # — GPU utilisation
    ax = axes[0]
    ax.plot(secs, col("gpu_util_pct"),      color="tab:green",  label="GPU util %")
    ax.plot(secs, col("mem_ctrl_util_pct"), color="tab:olive",  label="Mem-ctrl util %", linestyle="--")
    ax.set_ylabel("Utilisation (%)")
    ax.set_ylim(0, 105)
    ax.legend(loc="upper right", fontsize=8)
    ax.grid(True, alpha=0.3)
    ax.set_title("GPU utilisation")

    # — VRAM
    ax = axes[1]
    ax.plot(secs, col("vram_used_mb"), color="tab:blue",   label="VRAM used MB")
    ax.plot(secs, col("vram_free_mb"), color="tab:cyan",   label="VRAM free MB", linestyle="--")
    ax.set_ylabel("MB")
    ax.legend(loc="upper right", fontsize=8)
    ax.grid(True, alpha=0.3)
    ax.set_title("VRAM usage")

    # — Power & Temperature
    ax = axes[2]
    ax2 = ax.twinx()
    ax.plot(secs,  col("power_draw_w"), color="tab:red",    label="Power (W)")
    ax2.plot(secs, col("temp_c"),       color="tab:orange", label="Temp (°C)", linestyle=":")
    ax.set_ylabel("Power (W)",    color="tab:red")
    ax2.set_ylabel("Temp (°C)",   color="tab:orange")
    lines1, labels1 = ax.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax.legend(lines1 + lines2, labels1 + labels2, loc="upper right", fontsize=8)
    ax.grid(True, alpha=0.3)
    ax.set_title("Power & Temperature")

    # — System RAM
    ax = axes[3]
    ax.plot(secs, col("ram_used_mb"), color="tab:purple", label="RAM used MB")
    ax.plot(secs, col("ram_free_mb"), color="tab:pink",   label="RAM free MB", linestyle="--")
    ax.set_ylabel("MB")
    ax.set_xlabel("Elapsed (s)")
    ax.legend(loc="upper right", fontsize=8)
    ax.grid(True, alpha=0.3)
    ax.set_title("System RAM")

    plt.tight_layout()
    fig.savefig(chart_path, dpi=150)
    plt.close(fig)
    print(f"[monitor_gpu] Chart saved: {chart_path}")

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="GPU + RAM monitor")
    parser.add_argument("-i", "--interval", type=float, default=1.0,
                        help="Sampling interval in seconds (default: 1.0)")
    parser.add_argument("-d", "--duration", type=float, default=0,
                        help="Stop after N seconds (default: 0 = run until Ctrl-C)")
    parser.add_argument("--no-chart", action="store_true",
                        help="Skip chart generation")
    args = parser.parse_args()

    signal.signal(signal.SIGINT,  _sighandler)
    signal.signal(signal.SIGTERM, _sighandler)

    stamp     = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
    csv_path  = CSV_DIR  / f"gpu_monitor_{stamp}.csv"
    chart_path = CHART_DIR / f"gpu_monitor_{stamp}.png"

    print(f"[monitor_gpu] Interval : {args.interval} s")
    print(f"[monitor_gpu] CSV      : {csv_path}")
    if not args.no_chart:
        print(f"[monitor_gpu] Chart    : {chart_path}")
    if args.duration:
        print(f"[monitor_gpu] Duration : {args.duration} s")
    print("[monitor_gpu] Press Ctrl-C to stop.\n")

    gpu_ok = _query_nvidia_smi() is not None
    if not gpu_ok:
        print("[monitor_gpu] WARNING: nvidia-smi not found or returned no data — GPU columns will be empty.")

    rows     = []
    start_ts = time.monotonic()

    with open(csv_path, "w", newline="") as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=CSV_HEADER)
        writer.writeheader()

        while not _stop:
            now_mono = time.monotonic()
            elapsed  = round(now_mono - start_ts, 3)
            now_utc  = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"

            gpu  = _query_nvidia_smi() or {}
            ru, rf, rt = _query_ram_mb()

            row = {
                "timestamp_utc":      now_utc,
                "elapsed_s":          elapsed,
                "gpu_util_pct":       _safe(gpu.get("gpu_util_pct")),
                "mem_ctrl_util_pct":  _safe(gpu.get("mem_ctrl_util_pct")),
                "vram_used_mb":       _safe(gpu.get("vram_used_mb")),
                "vram_free_mb":       _safe(gpu.get("vram_free_mb")),
                "vram_total_mb":      _safe(gpu.get("vram_total_mb")),
                "power_draw_w":       _safe(gpu.get("power_draw_w")),
                "temp_c":             _safe(gpu.get("temp_c")),
                "sm_clock_mhz":       _safe(gpu.get("sm_clock_mhz")),
                "mem_clock_mhz":      _safe(gpu.get("mem_clock_mhz")),
                "ram_used_mb":        _safe(ru),
                "ram_free_mb":        _safe(rf),
                "ram_total_mb":       _safe(rt),
            }
            writer.writerow(row)
            csvfile.flush()
            rows.append(row)

            # Console summary line
            gpu_u  = f"{gpu.get('gpu_util_pct', '?'):>5}%"
            vram   = f"{gpu.get('vram_used_mb', '?'):>6}/{gpu.get('vram_total_mb', '?')} MB"
            pwr    = f"{gpu.get('power_draw_w', '?'):>6} W"
            tmp    = f"{gpu.get('temp_c', '?'):>4}°C"
            print(f"  {now_utc}  GPU {gpu_u}  VRAM {vram}  {pwr}  {tmp}  RAM {ru}/{rt} MB")

            if args.duration and elapsed >= args.duration:
                break

            # Sleep precisely, accounting for query time
            spent = time.monotonic() - now_mono
            sleep_t = max(0.0, args.interval - spent)
            time.sleep(sleep_t)

    print(f"\n[monitor_gpu] Collected {len(rows)} samples → {csv_path}")

    if not args.no_chart:
        _make_chart(rows, chart_path)


if __name__ == "__main__":
    main()
