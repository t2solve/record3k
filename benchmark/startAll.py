#!/usr/bin/env python3
"""
startAll.py — Start recording on all TYPE_CAM_VIMBA cameras via the API.

For each camera:
  - Picks the latest calibration for that camera
  - Uses pipeline pipe-default-42
  - Creates a shared test study entry (foo data)
  - Starts a recording with a 1-hour duration cap

Active record UIDs are saved to benchmark/active_records.json so stopAll.py can stop them.

Usage:
  python3 benchmark/startAll.py [--base-url http://127.0.0.1:8080] [--token foobartest42] [--fps 5]
"""

import argparse
import json
import random
import time
import sys
from pathlib import Path

try:
    import requests
except ImportError:
    sys.exit("requests library not found — run: pip install requests")

ACTIVE_RECORDS_FILE = Path(__file__).with_name("active_records.json")
PIPELINE_UID = "pipe-default-42"
DURATION_MS = 3_600_000  # 1 hour; use stopAll.py to end early


def parse_args():
    p = argparse.ArgumentParser(description="Start recording on all benchmark cameras")
    p.add_argument("--base-url", default="http://127.0.0.1:8080")
    p.add_argument("--token", default="foobartest42")
    p.add_argument("--fps", type=float, default=5.0,
                   help="Target frame rate for all cameras (1–10, default: 5)")
    p.add_argument("--delay", type=float, default=2.0,
                   help="Seconds to wait between starting each camera (default: 2)")
    args = p.parse_args()
    if not (1.0 <= args.fps <= 10.0):
        p.error("--fps must be between 1 and 10")
    if args.delay < 0:
        p.error("--delay must be >= 0")
    return args


def api(session, base, path):
    r = session.get(f"{base}{path}")
    r.raise_for_status()
    return r.json()


def post_api(session, base, path, body):
    r = session.post(f"{base}{path}", json=body)
    r.raise_for_status()
    return r.json()


def latest_calibration_for_camera(calibrations, cam_uid):
    """Return the calibrationUID with the newest datetimeCreated for the given camera."""
    cam_cals = [c for c in calibrations if c.get("cameraUID") == cam_uid]
    if not cam_cals:
        return None
    # Sort by datetimeCreated string (ISO-8601 sorts lexicographically)
    cam_cals.sort(key=lambda c: c.get("datetimeCreated", ""), reverse=True)
    return cam_cals[0]["calibrationUID"]


def main():
    args = parse_args()
    base = args.base_url.rstrip("/")

    session = requests.Session()
    session.headers.update({
        "Authorization": f"Bearer {args.token}",
        "Content-Type": "application/json",
    })

    # ---- 1. Load cameras ----
    print("[startAll] Fetching camera list...")
    cameras = api(session, base, "/info/cameras/list")
    vimba_cams = [c for c in cameras if c.get("cameraType") == "TYPE_CAM_VIMBA"]
    if not vimba_cams:
        sys.exit("[startAll] No TYPE_CAM_VIMBA cameras found — aborting.")
    print(f"[startAll] Found {len(vimba_cams)} VimbaX camera(s):")
    for c in vimba_cams:
        print(f"  {c['camUID']}  ({c.get('description', '')})")

    random.shuffle(vimba_cams)
    print("[startAll] Shuffled start order:")
    for i, c in enumerate(vimba_cams, 1):
        print(f"  {i}. {c['camUID']}")

    # ---- 2. Load calibrations ----
    print("[startAll] Fetching calibrations...")
    calibrations = api(session, base, "/info/calibrations/list")
    print(f"[startAll] {len(calibrations)} calibration(s) available")

    # ---- 3. Create a shared test study entry ----
    print("[startAll] Creating test study entry...")
    study_uid = f"foo-bench-{int(time.time())}"
    study_body = {
        "studyInfoUID": study_uid,
        "description": "foo benchmark test run",
        "individualScientificName": "Foo testus",
        "weightInMg": 0.0,
    }
    study = post_api(session, base, "/add/study_meta_info", study_body)
    study_uid = study.get("studyInfoUID", study_uid)
    print(f"[startAll] Study created: {study_uid}")

    # ---- 4. Start recording on each camera ----
    print(f"[startAll] Target FPS: {args.fps}")
    print(f"[startAll] Delay between cameras: {args.delay}s")
    active_records = []
    for cam in vimba_cams:
        cam_uid = cam["camUID"]
        cal_uid = latest_calibration_for_camera(calibrations, cam_uid)
        if cal_uid is None:
            print(f"[startAll] WARNING: No calibration found for {cam_uid} — skipping")
            continue

        print(f"[startAll] Starting recording for {cam_uid}  (cal={cal_uid}) ...")
        body = {
            "pipelineUID": PIPELINE_UID,
            "calibrationUID": cal_uid,
            "studyInfoUID": study_uid,
            "durationInMS": DURATION_MS,
            "targetFPS": args.fps,
            "description": f"benchmark test — {cam_uid}",
        }
        try:
            rec = post_api(session, base, f"/do/camera/record/start/{cam_uid}", body)
            record_uid = rec.get("recordUID") or rec.get("record_uid")
            if not record_uid:
                print(f"[startAll] WARNING: No recordUID in response for {cam_uid}: {rec}")
                continue
        except requests.HTTPError as e:
            print(f"[startAll] ERROR starting {cam_uid}: {e.response.status_code} {e.response.text.strip()}")
            print(f"[startAll]   Running camera diagnostic test for {cam_uid} ...")
            try:
                test_result = post_api(session, base, f"/do/camera/test/{cam_uid}", {})
                success = test_result.get("success", False)
                test_res = test_result.get("testResult", "unknown")
                detail = test_result.get("errorDetail", "")
                icon = "✓" if success else "✗"
                print(f"[startAll]   Camera test: {icon} testResult={test_res}"
                      + (f"  detail={detail}" if detail else ""))
            except requests.HTTPError as te:
                print(f"[startAll]   Camera test also failed: {te.response.status_code} {te.response.text.strip()}")
            except Exception as te:
                print(f"[startAll]   Camera test request error: {te}")
            continue
        except requests.exceptions.ConnectionError as e:
            # The API server closed the connection without a response — it likely
            # crashed or ran out of threads (too many concurrent recording starts).
            print(f"[startAll] ERROR starting {cam_uid}: API disconnected — {e}")
            print(f"[startAll]   The server may have crashed; remaining cameras skipped.")
            break
        active_records.append({
            "camUID": cam_uid,
            "recordUID": record_uid,
            "calibrationUID": cal_uid,
            "studyInfoUID": study_uid,
        })
        print(f"[startAll]   ✓ {cam_uid} → recordUID={record_uid}")
        if args.delay > 0:
            print(f"[startAll]   Waiting {args.delay}s before next camera...")
            time.sleep(args.delay)

    # ---- 5. Persist active record UIDs ----
    ACTIVE_RECORDS_FILE.write_text(json.dumps(active_records, indent=2))
    print(f"\n[startAll] Active records saved to {ACTIVE_RECORDS_FILE}")
    print(f"[startAll] {len(active_records)}/{len(vimba_cams)} camera(s) recording.")
    print("[startAll] Run  python3 benchmark/stopAll.py  to stop all recordings.")


if __name__ == "__main__":
    main()
