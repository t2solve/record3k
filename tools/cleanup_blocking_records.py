#!/usr/bin/env python3
"""
cleanup_blocking_records.py — List all records that are not "stopped" (these
block new recordings), then ask the user whether to delete the record and
everything referencing it (FileInfo json + packed tar.gz archive) via
DELETE /do/camera/record/delete/{recordUID}.

Intended for the PRODUCTION API — --base-url and --token are required
(no localhost/dev-token default) so this can't accidentally be pointed
at the wrong system.

Usage:
  python3 tools/cleanup_blocking_records.py --base-url https://<prod-host> --token <prod-token>
  python3 tools/cleanup_blocking_records.py --base-url https://<prod-host> --token <prod-token> --yes-all
"""

import argparse
import sys

try:
    import requests
except ImportError:
    sys.exit("requests library not found — run: pip install requests")


def parse_args():
    p = argparse.ArgumentParser(description="List non-stopped records and optionally delete them")
    p.add_argument("--base-url", required=True, help="Production API base URL, e.g. https://record3k.example.org")
    p.add_argument("--token", required=True, help="Production API bearer token")
    p.add_argument("--yes-all", action="store_true",
                    help="Delete all listed records without per-record confirmation "
                         "(still shows the list and asks for one overall confirmation)")
    return p.parse_args()


def fetch_not_stopped(session, base):
    try:
        r = session.get(f"{base}/info/records/list", timeout=10)
        r.raise_for_status()
        records = r.json()
    except requests.exceptions.ConnectionError:
        print(f"[cleanupBlockingRecords] ERROR: Cannot reach API at {base}")
        sys.exit(2)
    except requests.HTTPError as e:
        print(f"[cleanupBlockingRecords] ERROR: {e.response.status_code} {e.response.text.strip()}")
        sys.exit(2)

    return [rec for rec in records if rec.get("status") != "stopped"], len(records)


def print_records(not_stopped):
    print(f"[cleanupBlockingRecords] {len(not_stopped)} record(s) are not stopped and may block new recordings:\n")
    for i, rec in enumerate(not_stopped, 1):
        uid    = rec.get("recordUID", "?")
        cam    = rec.get("cameraUID", "?")
        status = rec.get("status", "?")
        start  = rec.get("datetimeStart", "?")
        print(f"  [{i}] {uid}  status={status}  cam={cam}  start={start}")
    print()


def check_camera_status(session, base, camera_uids):
    print(f"\n[cleanupBlockingRecords] Checking status of {len(camera_uids)} affected camera(s):\n")
    for cam_uid in sorted(camera_uids):
        if not cam_uid or cam_uid == "?":
            continue
        try:
            r = session.get(f"{base}/info/cameras/get/{cam_uid}", timeout=10)
        except requests.exceptions.ConnectionError:
            print(f"  {cam_uid}  ERROR: cannot reach API at {base}")
            continue

        if r.status_code == 404:
            print(f"  {cam_uid}  NOT FOUND")
            continue
        try:
            r.raise_for_status()
        except requests.HTTPError as e:
            print(f"  {cam_uid}  ERROR: {e.response.status_code} {e.response.text.strip()}")
            continue

        cam = r.json()
        status = cam.get("status", "?")
        seen = cam.get("datetimeLastSeen", "?")
        print(f"  {cam_uid}  status={status}  lastSeen={seen}")


def delete_record(session, base, record_uid):
    try:
        r = session.delete(f"{base}/do/camera/record/delete/{record_uid}", timeout=10)
    except requests.exceptions.ConnectionError:
        print(f"    ERROR: Cannot reach API at {base}")
        return False

    if r.status_code == 409:
        print(f"    SKIPPED: recording is still active, stop it first ({r.text.strip()})")
        return False
    if r.status_code == 404:
        print(f"    SKIPPED: record not found (already deleted?)")
        return False
    try:
        r.raise_for_status()
    except requests.HTTPError as e:
        print(f"    ERROR: {e.response.status_code} {e.response.text.strip()}")
        return False

    print(f"    deleted: {r.json()}")
    return True


def main():
    args = parse_args()
    base = args.base_url.rstrip("/")

    session = requests.Session()
    session.headers.update({"Authorization": f"Bearer {args.token}"})

    not_stopped, total = fetch_not_stopped(session, base)

    if not not_stopped:
        print(f"[cleanupBlockingRecords] All {total} record(s) are stopped — nothing blocks new recordings.")
        sys.exit(0)

    print_records(not_stopped)

    if args.yes_all:
        answer = input(f"Delete all {len(not_stopped)} record(s) listed above on {base}? [y/N] ").strip().lower()
        if answer != "y":
            print("[cleanupBlockingRecords] Aborted.")
            sys.exit(1)
        to_delete = not_stopped
    else:
        to_delete = []
        for rec in not_stopped:
            uid = rec.get("recordUID", "?")
            answer = input(f"Delete {uid} (status={rec.get('status', '?')})? [y/N] ").strip().lower()
            if answer == "y":
                to_delete.append(rec)

    if not to_delete:
        print("[cleanupBlockingRecords] Nothing selected for deletion.")
        sys.exit(0)

    print(f"\n[cleanupBlockingRecords] Deleting {len(to_delete)} record(s) on {base}:\n")
    deleted = 0
    affected_cameras = set()
    for rec in to_delete:
        uid = rec.get("recordUID", "?")
        affected_cameras.add(rec.get("cameraUID", "?"))
        print(f"  {uid}")
        if delete_record(session, base, uid):
            deleted += 1

    print(f"\n[cleanupBlockingRecords] Deleted {deleted} of {len(to_delete)} selected record(s).")

    if affected_cameras:
        check_camera_status(session, base, affected_cameras)

    sys.exit(0 if deleted == len(to_delete) else 1)


if __name__ == "__main__":
    main()
