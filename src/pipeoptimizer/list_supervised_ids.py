#!/usr/bin/env python3
"""
list_supervised_ids.py — print supervised record UIDs not marked a complete failure.

Mini helper around the same tags DB used by sim_runner_real.py
(data/simulation/supervised_records_data.sqlite3). Unlike
discover_supervised_uids() there it does NOT require roi_defined=1 — this
just filters on record_failed=0.

Usage:
    python3 list_supervised_ids.py [--tags-db PATH]
"""

import argparse
import sqlite3
import sys
from pathlib import Path

DEFAULT_TAGS_DB = Path(__file__).resolve().parents[2] / "data/simulation/supervised_records_data.sqlite3"


def fetch_non_failed_uids(tags_db: Path) -> list:
    conn = sqlite3.connect(f"file:{tags_db}?mode=ro", uri=True)
    try:
        rows = conn.execute(
            "SELECT record_uid FROM tagged_records "
            "WHERE record_failed=0 ORDER BY record_uid"
        ).fetchall()
    finally:
        conn.close()
    return [r[0] for r in rows]


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--tags-db", type=Path, default=DEFAULT_TAGS_DB)
    args = ap.parse_args()

    if not args.tags_db.exists():
        sys.exit(f"tags DB not found: {args.tags_db}")

    for uid in fetch_non_failed_uids(args.tags_db):
        print(uid)


if __name__ == "__main__":
    main()
