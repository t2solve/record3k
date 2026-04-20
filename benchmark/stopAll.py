#!/usr/bin/env python3
"""
stopAll.py — Stop all active recordings started by startAll.py.

Reads benchmark/active_records.json (written by startAll.py) and calls
GET /do/camera/record/stop/{recordUID} for each entry.

Usage:
  python3 benchmark/stopAll.py [--base-url http://127.0.0.1:8080] [--token default-dev-token]
"""

import argparse
import json
import sys
from pathlib import Path

try:
    import requests
except ImportError:
    sys.exit("requests library not found — run: pip install requests")

ACTIVE_RECORDS_FILE = Path(__file__).with_name("active_records.json")


def parse_args():
    p = argparse.ArgumentParser(description="Stop all active benchmark recordings")
    p.add_argument("--base-url", default="http://127.0.0.1:8080")
    p.add_argument("--token", default="foobartest42")
    return p.parse_args()


def main():
    args = parse_args()
    base = args.base_url.rstrip("/")

    if not ACTIVE_RECORDS_FILE.exists():
        sys.exit(f"[stopAll] {ACTIVE_RECORDS_FILE} not found — run startAll.py first.")

    active_records = json.loads(ACTIVE_RECORDS_FILE.read_text())
    if not active_records:
        print("[stopAll] No active records to stop.")
        return

    session = requests.Session()
    session.headers.update({
        "Authorization": f"Bearer {args.token}",
    })

    print(f"[stopAll] Stopping {len(active_records)} recording(s)...")
    stopped = 0
    for entry in active_records:
        cam_uid = entry.get("camUID", "?")
        record_uid = entry["recordUID"]
        print(f"[stopAll] Stopping {cam_uid}  recordUID={record_uid} ...")
        try:
            r = session.get(f"{base}/do/camera/record/stop/{record_uid}")
            r.raise_for_status()
            rec = r.json()
            status = rec.get("status", "?")
            duration = rec.get("durationInSeconds", "?")
            print(f"[stopAll]   ✓ {cam_uid} → status={status}  duration={duration}s")
            stopped += 1
        except requests.HTTPError as e:
            print(f"[stopAll] ERROR stopping {cam_uid} ({record_uid}): {e.response.status_code} {e.response.text}")

    # Clear the file after stopping
    ACTIVE_RECORDS_FILE.write_text(json.dumps([], indent=2))
    print(f"\n[stopAll] {stopped}/{len(active_records)} recording(s) stopped.")
    print(f"[stopAll] {ACTIVE_RECORDS_FILE} cleared.")


if __name__ == "__main__":
    main()
