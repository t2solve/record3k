#!/usr/bin/env python3
"""
delete_record.py — Delete a record and everything referencing it
(record json, FileInfo json, and the packed tar.gz archive) via
DELETE /do/camera/record/delete/{recordUID}.

The API performs the actual cleanup server-side:
  - refuses if the recording is still active (see FileManager::getRecordingState)
  - removes data/files/bin/file-rec-{recordUID}.tar.gz
  - removes data/files/file-rec-{recordUID}.json
  - removes data/records/{recordUID}.json

Usage:
  python3 tools/delete_record.py rec-1787132747181-4636
  python3 tools/delete_record.py rec-1787132747181-4636 --base-url http://127.0.0.1:8080 --token foobartest42
"""

import argparse
import sys

try:
    import requests
except ImportError:
    sys.exit("requests library not found — run: pip install requests")


def parse_args():
    p = argparse.ArgumentParser(description="Delete a record and its referenced files via the API")
    p.add_argument("record_uid", help="recordUID to delete, e.g. rec-1787132747181-4636")
    p.add_argument("--base-url", default="http://127.0.0.1:8080")
    p.add_argument("--token", default="foobartest42")
    p.add_argument("--yes", action="store_true", help="Skip confirmation prompt")
    return p.parse_args()


def main():
    args = parse_args()
    base = args.base_url.rstrip("/")
    record_uid = args.record_uid

    session = requests.Session()
    session.headers.update({"Authorization": f"Bearer {args.token}"})

    try:
        r = session.get(f"{base}/info/records/get/{record_uid}", timeout=5)
    except requests.exceptions.ConnectionError:
        print(f"[deleteRecord] ERROR: Cannot reach API at {base} — is the container running?")
        sys.exit(2)

    if r.status_code == 404:
        print(f"[deleteRecord] Record not found: {record_uid}")
        sys.exit(1)
    r.raise_for_status()
    record = r.json()

    print(f"[deleteRecord] About to delete:")
    print(f"  recordUID = {record.get('recordUID', record_uid)}")
    print(f"  status    = {record.get('status', '?')}")
    print(f"  fileUID   = {record.get('fileUID', '?')}")
    print(f"  cameraUID = {record.get('cameraUID', '?')}")

    if not args.yes:
        answer = input("Proceed with deletion? [y/N] ").strip().lower()
        if answer != "y":
            print("[deleteRecord] Aborted.")
            sys.exit(1)

    try:
        r = session.delete(f"{base}/do/camera/record/delete/{record_uid}", timeout=5)
    except requests.exceptions.ConnectionError:
        print(f"[deleteRecord] ERROR: Cannot reach API at {base} — is the container running?")
        sys.exit(2)

    if r.status_code == 409:
        print(f"[deleteRecord] ERROR: Recording is still active — stop it before deleting: {r.text.strip()}")
        sys.exit(2)
    if r.status_code == 404:
        print(f"[deleteRecord] ERROR: Record not found: {record_uid}")
        sys.exit(1)
    try:
        r.raise_for_status()
    except requests.HTTPError as e:
        print(f"[deleteRecord] ERROR: {e.response.status_code} {e.response.text.strip()}")
        sys.exit(2)

    print(f"[deleteRecord] Deleted successfully: {r.json()}")


if __name__ == "__main__":
    main()
