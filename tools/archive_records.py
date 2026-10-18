#!/usr/bin/env python3
"""
Archive the data/ folder to a remote share, then delete old records directly on disk.

Flow:
  1. rsync data/ -> remote (incremental, additive only - never passes --delete).
  2. rsync again in --itemize-changes --dry-run mode as a confirmation pass:
     if it reports any pending changes, the remote copy is not yet complete
     and the run aborts before touching any records.
  3. Fetch /info/records/list from the API, keep records whose reference
     timestamp is older than --delta-days (default 7) and that aren't
     currently recording. For each one, confirm it via
     /info/records/get/{recordUID} and then delete it directly from disk
     (this script runs on the same host as data/): the tar.gz archive under
     data/files/bin/, the FileInfo json under data/files/, and the record's
     own json under data/records/. This bypasses /do/camera/record/delete,
     which has been unreliable (spurious 404s) - deleting the files this
     script itself just verified/archived is simpler and doesn't depend on
     that route.

Usage:
  python tools/archive_records.py --remote /mnt/nas/record3k_archive
  python tools/archive_records.py --remote user@nas:/archive/record3k --delta-days 14
  python tools/archive_records.py --remote /mnt/nas/record3k_archive --dry-run

Requirements: rsync in PATH. The API server must be reachable at --api-base
(default http://127.0.0.1:8080).
"""

import argparse
import datetime as dt
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


def log(msg: str) -> None:
    stamp = dt.datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    print(f'[{stamp}] {msg}')


def run(cmd: list, **kwargs) -> subprocess.CompletedProcess:
    flat = ' '.join(str(c) for c in cmd)
    log(f'$ {flat}')
    return subprocess.run(cmd, check=True, text=True, capture_output=True, **kwargs)


def rsync_base_args(rsync_bin: str, ssh: str | None) -> list:
    # --inplace: write directly to the destination file instead of rsync's
    # default create-a-temp-file-then-rename dance. Some FUSE mounts (e.g. an
    # rclone SMB mount, especially behind a DFS redirect) choke on that
    # temp-file pattern for lots of small files, surfacing as
    # `mkstemp "..." failed: Input/output error (5)`. --inplace avoids
    # mkstemp() entirely.
    #
    # -rltD instead of -a: SMB/CIFS mounts enforce their own fixed
    # file_mode/dir_mode (and often a fixed uid/gid) and silently ignore
    # chmod()/chown() calls. With -a (which implies -p -o -g), every real
    # copy "sets" permissions that the mount then reports back unchanged, so
    # the --itemize-changes confirmation pass in rsync_confirm() finds a
    # permission-only mismatch on every file forever and the archive can
    # never be confirmed. Dropping p/o/g leaves recursion, symlinks,
    # timestamps and device/special files preserved, which the confirmation
    # pass can actually converge on.
    args = [rsync_bin, '-rltD', '--inplace']
    if ssh:
        args += ['-e', ssh]
    return args


def rsync_copy(rsync_bin: str, ssh: str | None, src: Path, dest: str,
               dry_run: bool, extra_args: list) -> None:
    """Incremental, additive copy of the whole data/ tree (never passes --delete).

    Uses --partial so a transfer interrupted mid-file (e.g. a dropped
    connection to a network share) leaves the partial data in place on the
    remote instead of discarding it - the next run resumes from there rather
    than re-copying the whole file.
    """
    args = rsync_base_args(rsync_bin, ssh) + ['--info=stats2', '--partial']
    if dry_run:
        args.append('--dry-run')
    args += extra_args
    args += [f'{src}/', dest if dest.endswith('/') else dest + '/']
    result = run(args)
    print(result.stdout)
    if result.stderr.strip():
        print(result.stderr, file=sys.stderr)


def rsync_confirm(rsync_bin: str, ssh: str | None, src: Path, dest: str,
                   extra_args: list) -> list:
    """Second rsync call: dry-run + itemize-changes to prove nothing is pending.

    Returns the list of itemized change lines still outstanding (empty means
    the remote is fully in sync with the local data/ folder).
    """
    args = rsync_base_args(rsync_bin, ssh) + ['--dry-run', '--itemize-changes']
    args += extra_args
    args += [f'{src}/', dest if dest.endswith('/') else dest + '/']
    result = run(args)
    pending = [
        line for line in result.stdout.splitlines()
        if line and not line.startswith(('sending incremental', 'sent ', 'total size'))
    ]
    return pending


def auth_headers(api_token: str | None) -> dict:
    return {'Authorization': f'Bearer {api_token}'} if api_token else {}


def dump_response(method: str, url: str, status: int, body) -> None:
    print(f'[debug] {method} {url} -> HTTP {status}')
    print(json.dumps(body, indent=2, sort_keys=True))


def api_get(api_base: str, path: str, timeout: float, api_token: str | None = None, debug: bool = False):
    url = api_base.rstrip('/') + path
    req = urllib.request.Request(url, headers=auth_headers(api_token))
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        body = json.loads(resp.read().decode('utf-8'))
        if debug:
            dump_response('GET', url, resp.status, body)
        return body


def resolve_file_uid(record: dict, uid: str) -> str:
    file_uid = record.get('fileUID') or ''
    if not file_uid or file_uid == 'file-rec-placeholder':
        file_uid = f'file-rec-{uid}'
    return file_uid


def find_record_json_path(data_dir: Path, uid: str) -> Path | None:
    """Resolve the on-disk data/records/<key>.json for a recordUID.

    Mirrors InfoDataManager::resolveRecordKey: normally the filename stem
    equals recordUID, but falls back to scanning for a record whose internal
    "recordUID" field matches if the filename and field have drifted apart.
    """
    records_dir = data_dir / 'records'
    direct = records_dir / f'{uid}.json'
    if direct.is_file():
        return direct
    if not records_dir.is_dir():
        return None
    for path in records_dir.glob('*.json'):
        try:
            with open(path, 'r', encoding='utf-8') as f:
                data = json.load(f)
        except (OSError, json.JSONDecodeError):
            continue
        if data.get('recordUID') == uid:
            return path
    return None


def delete_record_files(data_dir: Path, uid: str, record: dict) -> bool:
    """Delete a record directly from disk: the tar.gz archive, its FileInfo
    json, and the record's own json entry. Runs locally since archive_records.py
    executes on the same host as data/ - no need to go through the API's
    /do/camera/record/delete route.
    """
    ok = True
    file_uid = resolve_file_uid(record, uid)

    archive_path = data_dir / 'files' / 'bin' / f'{file_uid}.tar.gz'
    if archive_path.exists():
        try:
            archive_path.unlink()
            log(f'    deleted archive: {archive_path}')
        except OSError as e:
            log(f'    ERROR: failed to delete archive {archive_path}: {e}')
            ok = False
    else:
        log(f'    archive not found (skipping): {archive_path}')

    file_info_path = data_dir / 'files' / f'{file_uid}.json'
    if file_info_path.exists():
        try:
            file_info_path.unlink()
            log(f'    deleted file-info json: {file_info_path}')
        except OSError as e:
            log(f'    ERROR: failed to delete file-info json {file_info_path}: {e}')
            ok = False
    else:
        log(f'    file-info json not found (skipping): {file_info_path}')

    record_path = find_record_json_path(data_dir, uid)
    if record_path is not None:
        try:
            record_path.unlink()
            log(f'    deleted record json: {record_path}')
        except OSError as e:
            log(f'    ERROR: failed to delete record json {record_path}: {e}')
            ok = False
    else:
        log(f'    ERROR: record json for {uid} not found under {data_dir / "records"}')
        ok = False

    return ok


def verify_record_gone(api_base: str, uid: str, timeout: float, api_token: str | None,
                        debug: bool, attempts: int = 3, interval: float = 1.0) -> bool:
    """Poll /info/records/get/{uid} until the API no longer reports it.

    InfoDataManager's JsonStore reloads via inotify (src/liblbt/src/json_store.cpp),
    reacting to a deleted file almost immediately - the watcher's select() loop has a
    1s *max* wait, not a fixed poll delay - so the common case resolves on the first
    check. The retries/sleep here are just a safety margin for the worst case.
    """
    for attempt in range(1, attempts + 1):
        try:
            record = api_get(api_base, f'/info/records/get/{uid}', timeout, api_token, debug=debug)
        except (urllib.error.URLError, urllib.error.HTTPError) as e:
            log(f'    WARNING: post-delete verification GET /info/records/get/{uid} failed ({e}); cannot confirm')
            return False
        if not record:
            return True
        if attempt < attempts:
            time.sleep(interval)
    log(f'    WARNING: {uid} is still listed by /info/records/get/{uid} after deleting it from disk '
        f'(body={record!r}) - the API may be slow to notice, or something recreated the file')
    return False


def parse_iso8601(value: str) -> dt.datetime:
    return dt.datetime.fromisoformat(value.replace('Z', '+00:00'))


def record_reference_time(record: dict):
    for field in ('datetimeEnd', 'datetimeStart'):
        value = record.get(field)
        if value:
            try:
                return parse_iso8601(value)
            except ValueError:
                continue
    return None


def select_records_to_delete(records: list, delta_days: int) -> list:
    cutoff = dt.datetime.now(dt.timezone.utc) - dt.timedelta(days=delta_days)
    selected = []
    for record in records:
        uid = record.get('recordUID', '<unknown>')
        status = (record.get('status') or '').lower()
        if status == 'started':
            log(f'  skip {uid}: still recording (status=started)')
            continue
        ref_time = record_reference_time(record)
        if ref_time is None:
            log(f'  skip {uid}: no datetimeStart/datetimeEnd to judge age')
            continue
        if ref_time > cutoff:
            continue
        selected.append((uid, ref_time, status))
    return selected


def main() -> int:
    parser = argparse.ArgumentParser(
        description='Archive data/ to a remote share via rsync, then delete records older than N days directly from disk.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument('--data-dir', default=str(REPO_ROOT / 'data'),
                        help='Local data folder to archive (default: repo data/)')
    parser.add_argument('--remote', required=True,
                        help='rsync destination: a local/mounted net-share path, or user@host:/path')
    parser.add_argument('--api-base', default='http://127.0.0.1:8080',
                        help='Base URL of the running API server (default: http://127.0.0.1:8080)')
    parser.add_argument('--api-token', default=os.environ.get('RECORD3K_API_TOKEN'),
                        help='Bearer token for the API (sent as "Authorization: Bearer <token>"). '
                             'Defaults to the RECORD3K_API_TOKEN env var, so it need not appear on the command line.')
    parser.add_argument('--delta-days', type=int, default=7,
                        help='Delete only records older than this many days (default: 7)')
    parser.add_argument('--dry-run', action='store_true',
                        help='Simulate everything: rsync runs with --dry-run and no files are deleted from disk')
    parser.add_argument('--yes', '-y', action='store_true',
                        help='Skip the interactive confirmation prompt before deleting records')
    parser.add_argument('--rsync-bin', default='rsync', help='rsync executable to use')
    parser.add_argument('--ssh', default=None, help='SSH command to pass to rsync -e, e.g. "ssh -i key.pem"')
    parser.add_argument('--rsync-arg', action='append', default=[],
                        help='Extra raw argument to pass to rsync, may be repeated (e.g. --rsync-arg=--bwlimit=5000)')
    parser.add_argument('--api-timeout', type=float, default=15.0, help='HTTP timeout in seconds (default: 15)')
    parser.add_argument('--debug', action='store_true',
                        help='Print raw API responses to stdout for debugging')
    args = parser.parse_args()

    data_dir = Path(args.data_dir).resolve()
    if not data_dir.is_dir():
        log(f'ERROR: data dir not found: {data_dir}')
        return 1

    if args.dry_run:
        log('DRY RUN MODE: rsync will use --dry-run, no records will be deleted')

    # Step 1: incremental, additive copy of the whole data/ tree.
    log(f'Step 1/3: rsync copy {data_dir} -> {args.remote}')
    try:
        rsync_copy(args.rsync_bin, args.ssh, data_dir, args.remote, args.dry_run, args.rsync_arg)
    except subprocess.CalledProcessError as e:
        log(f'ERROR: rsync copy failed (exit {e.returncode}):\n{e.stderr}')
        return 1

    # Step 2: confirmation pass - prove nothing is left to sync.
    log('Step 2/3: rsync confirmation pass (dry-run + itemize-changes)')
    try:
        pending = rsync_confirm(args.rsync_bin, args.ssh, data_dir, args.remote, args.rsync_arg)
    except subprocess.CalledProcessError as e:
        log(f'ERROR: rsync confirmation pass failed (exit {e.returncode}):\n{e.stderr}')
        return 1

    if pending:
        log(f'Confirmation pass found {len(pending)} pending change(s), remote is not fully in sync:')
        for line in pending[:20]:
            print(f'    {line}')
        if not args.dry_run:
            log('ABORTING: refusing to delete records until archive is fully confirmed.')
            return 1
        log('(dry-run: continuing anyway since no real copy was performed)')
    else:
        log('Confirmation pass clean: remote matches data/ exactly.')

    # Step 3: delete records older than --delta-days.
    log(f'Step 3/3: selecting records older than {args.delta_days} day(s) for deletion')
    try:
        records = api_get(args.api_base, '/info/records/list', args.api_timeout, args.api_token, debug=args.debug)
    except (urllib.error.URLError, urllib.error.HTTPError) as e:
        log(f'ERROR: could not fetch /info/records/list from {args.api_base}: {e}')
        return 1

    to_delete = select_records_to_delete(records, args.delta_days)
    if not to_delete:
        log('No records old enough to delete. Done.')
        return 0

    log(f'{len(to_delete)} record(s) selected for deletion:')
    for uid, ref_time, status in to_delete:
        print(f'    {uid}  (status={status}, reference_time={ref_time.isoformat()})')

    if args.dry_run:
        log('DRY RUN: no files will be deleted.')
        return 0

    if not args.yes:
        reply = input(f'Delete {len(to_delete)} record(s) listed above? [y/N] ').strip().lower()
        if reply != 'y':
            log('Aborted by user, no records deleted.')
            return 1

    deleted, failed, unverified = 0, 0, 0
    for uid, _, _ in to_delete:
        # Confirm the record is resolvable via the API before touching disk -
        # catches a data/records/*.json filename vs recordUID field mismatch
        # (or a record that's already gone) before we go looking for its files.
        try:
            record = api_get(args.api_base, f'/info/records/get/{uid}', args.api_timeout, args.api_token, debug=args.debug)
        except (urllib.error.URLError, urllib.error.HTTPError) as e:
            log(f'  SKIPPING {uid}: pre-delete GET /info/records/get/{uid} failed ({e}); not attempting delete')
            failed += 1
            continue
        if not record or record.get('recordUID') != uid:
            log(f'  SKIPPING {uid}: pre-delete GET /info/records/get/{uid} returned no matching record '
                f'(body={record!r}); the API cannot resolve this UID even though /info/records/list reported it - '
                f'likely a data/records/*.json filename vs recordUID field mismatch; not attempting delete')
            failed += 1
            continue
        log(f'  pre-delete check: /info/records/get/{uid} OK (status={record.get("status")}, '
            f'fileUID={record.get("fileUID")!r})')

        log(f'  deleting {uid} directly from disk under {data_dir}')
        if delete_record_files(data_dir, uid, record):
            log(f'  deleted {uid}; verifying API no longer lists it')
            deleted += 1
            if verify_record_gone(args.api_base, uid, args.api_timeout, args.api_token, args.debug):
                log(f'    verified: {uid} no longer resolvable via /info/records/get')
            else:
                unverified += 1
        else:
            log(f'  FAILED to fully delete {uid} (see errors above)')
            failed += 1

    log(f'Done: {deleted} deleted ({unverified} not yet confirmed gone via API), '
        f'{failed} failed out of {len(to_delete)} selected.')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
