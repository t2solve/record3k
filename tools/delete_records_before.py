#!/usr/bin/env python3
"""
delete_records_before.py — Delete all records whose datetimeStart is before a given
date via DELETE /do/camera/record/delete/{recordUID}, after listing them and asking
for confirmation. Afterwards /info/records/list is fetched again to verify that
the deleted records are gone.

The cutoff date is interpreted as 00:00:00 UTC of that day (records store
datetimeStart as ISO8601 UTC, e.g. 2026-10-12T08:15:00Z), so --before 12.10.2026
deletes everything up to and including 11.10.2026.

Usage:
  python3 tools/delete_records_before.py --before 12.10.2026
  python3 tools/delete_records_before.py --before 2026-10-12 --base-url http://127.0.0.1:8080 --token foobartest42
  python3 tools/delete_records_before.py --before 12.10.2026 --dry-run
"""

import argparse
import sys
from datetime import datetime, timezone

try:
    import requests
except ImportError:
    sys.exit("requests library not found — run: pip install requests")

TAG = "[deleteRecordsBefore]"


def parse_cutoff(value):
    for fmt in ("%d.%m.%Y", "%Y-%m-%d"):
        try:
            return datetime.strptime(value, fmt).replace(tzinfo=timezone.utc)
        except ValueError:
            pass
    raise argparse.ArgumentTypeError(f"invalid date '{value}' — use DD.MM.YYYY or YYYY-MM-DD")


def parse_args():
    p = argparse.ArgumentParser(description="Delete all records started before a given date")
    p.add_argument("--before", required=True, type=parse_cutoff,
                   help="Cutoff date (DD.MM.YYYY or YYYY-MM-DD), 00:00 UTC, exclusive")
    p.add_argument("--base-url", default="http://127.0.0.1:8080")
    p.add_argument("--token", default="foobartest42")
    p.add_argument("--dry-run", action="store_true", help="Only list the matching records, delete nothing")
    return p.parse_args()


def parse_record_time(value):
    """Parse an ISO8601 datetime string; returns an aware UTC datetime or None."""
    if not value:
        return None
    try:
        dt = datetime.fromisoformat(value.strip().replace("Z", "+00:00"))
    except ValueError:
        return None
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=timezone.utc)
    return dt.astimezone(timezone.utc)


def fetch_records(session, base):
    try:
        r = session.get(f"{base}/info/records/list", timeout=10)
        r.raise_for_status()
        return r.json()
    except requests.exceptions.ConnectionError:
        print(f"{TAG} ERROR: Cannot reach API at {base} — is the container running?")
        sys.exit(2)
    except requests.HTTPError as e:
        print(f"{TAG} ERROR: {e.response.status_code} {e.response.text.strip()}")
        sys.exit(2)


def select_before(records, cutoff):
    matching, unparsable = [], []
    for rec in records:
        start = parse_record_time(rec.get("datetimeStart", ""))
        if start is None:
            unparsable.append(rec)
        elif start < cutoff:
            matching.append(rec)
    matching.sort(key=lambda rec: parse_record_time(rec["datetimeStart"]))
    return matching, unparsable


def print_records(records):
    for i, rec in enumerate(records, 1):
        uid    = rec.get("recordUID", "?")
        cam    = rec.get("cameraUID", "?")
        status = rec.get("status", "?")
        start  = rec.get("datetimeStart", "?")
        study  = rec.get("studyUID", "")
        line = f"  [{i}] {uid}  status={status}  cam={cam}  start={start}"
        if study:
            line += f"  study={study}"
        print(line)
    print()


def delete_record(session, base, record_uid):
    try:
        r = session.delete(f"{base}/do/camera/record/delete/{record_uid}", timeout=30)
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


def after_check(session, base, cutoff, deleted_uids):
    print(f"\n{TAG} After-check: re-fetching record list from {base} ...")
    records = fetch_records(session, base)
    remaining_uids = {rec.get("recordUID") for rec in records}

    still_present = sorted(uid for uid in deleted_uids if uid in remaining_uids)
    remaining_before, _ = select_before(records, cutoff)

    ok = True
    if still_present:
        ok = False
        print(f"{TAG} FAIL: {len(still_present)} record(s) reported as deleted are still listed:")
        for uid in still_present:
            print(f"  {uid}")
    else:
        print(f"{TAG} OK: all {len(deleted_uids)} deleted record(s) are gone from the list.")

    if remaining_before:
        ok = False
        print(f"{TAG} {len(remaining_before)} record(s) before the cutoff still remain:")
        print_records(remaining_before)
    else:
        print(f"{TAG} OK: no records before {cutoff:%d.%m.%Y} remain ({len(records)} record(s) left in total).")
    return ok


def main():
    args = parse_args()
    base = args.base_url.rstrip("/")
    cutoff = args.before

    session = requests.Session()
    session.headers.update({"Authorization": f"Bearer {args.token}"})

    records = fetch_records(session, base)
    matching, unparsable = select_before(records, cutoff)

    if unparsable:
        print(f"{TAG} WARNING: {len(unparsable)} record(s) have no/unparsable datetimeStart and are ignored:")
        print_records(unparsable)

    if not matching:
        print(f"{TAG} No records before {cutoff:%d.%m.%Y} 00:00 UTC (checked {len(records)} record(s)).")
        sys.exit(0)

    print(f"{TAG} {len(matching)} of {len(records)} record(s) started before "
          f"{cutoff:%d.%m.%Y} 00:00 UTC on {base}:\n")
    print_records(matching)

    if args.dry_run:
        print(f"{TAG} Dry run — nothing deleted.")
        sys.exit(0)

    answer = input(f"Permanently delete these {len(matching)} record(s) (archives + JSON entries)? "
                   f"Type 'yes' to confirm: ").strip().lower()
    if answer != "yes":
        print(f"{TAG} Aborted — nothing deleted.")
        sys.exit(1)

    print(f"\n{TAG} Deleting {len(matching)} record(s):\n")
    deleted_uids = []
    for rec in matching:
        uid = rec.get("recordUID", "?")
        print(f"  {uid}")
        if delete_record(session, base, uid):
            deleted_uids.append(uid)

    print(f"\n{TAG} Deleted {len(deleted_uids)} of {len(matching)} selected record(s).")

    ok = after_check(session, base, cutoff, deleted_uids)
    sys.exit(0 if ok and len(deleted_uids) == len(matching) else 1)


if __name__ == "__main__":
    main()
