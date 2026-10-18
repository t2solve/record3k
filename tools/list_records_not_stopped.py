#!/usr/bin/env python3
"""
list_records_not_stopped.py — Show all records whose status is not "stopped" via /info/records/list.

Usage:
  python3 tools/list_records_not_stopped.py
  python3 tools/list_records_not_stopped.py --base-url http://127.0.0.1:8080 --token foobartest42
"""

import argparse
import sys

try:
    import requests
except ImportError:
    sys.exit("requests library not found — run: pip install requests")


def parse_args():
    p = argparse.ArgumentParser(description="List records that are not stopped")
    p.add_argument("--base-url", default="http://127.0.0.1:8080")
    p.add_argument("--token", default="foobartest42")
    return p.parse_args()


def main():
    args = parse_args()
    base = args.base_url.rstrip("/")

    session = requests.Session()
    session.headers.update({"Authorization": f"Bearer {args.token}"})

    try:
        r = session.get(f"{base}/info/records/list", timeout=5)
        r.raise_for_status()
        records = r.json()
    except requests.exceptions.ConnectionError:
        print(f"[listRecordsNotStopped] ERROR: Cannot reach API at {base} — is the container running?")
        sys.exit(2)
    except requests.HTTPError as e:
        print(f"[listRecordsNotStopped] ERROR: {e.response.status_code} {e.response.text.strip()}")
        sys.exit(2)

    not_stopped = [rec for rec in records if rec.get("status") != "stopped"]

    if not not_stopped:
        print(f"[listRecordsNotStopped] All {len(records)} record(s) are stopped.")
        sys.exit(0)

    print(f"[listRecordsNotStopped] {len(not_stopped)} of {len(records)} record(s) are not stopped:\n")
    for rec in not_stopped:
        uid       = rec.get("recordUID", "?")
        cam       = rec.get("cameraUID", "?")
        status    = rec.get("status", "?")
        pipeline  = rec.get("pipelineUID", "")
        start     = rec.get("datetimeStart", "?")
        end       = rec.get("datetimeEnd", "")
        study     = rec.get("studyUID", "")
        line = f"  {uid}  status={status}  cam={cam}  start={start}"
        if end:
            line += f"  end={end}"
        if pipeline:
            line += f"  pipeline={pipeline}"
        if study:
            line += f"  study={study}"
        print(line)

    sys.exit(1)


if __name__ == "__main__":
    main()
