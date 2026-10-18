#!/usr/bin/env bash
#
# Wrapper for archive_records.py, meant to run unattended (cron). It always
# passes --yes (no interactive prompt); use DRY_RUN=1 to simulate.
#
# Configuration: every parameter below can be set as an env var, or in
# tools/run.env (sourced if present, gitignored — copy tools/run.env.example).
# Keep run.env chmod 600 if it holds a real API_TOKEN.
#
# Usage:
#   ./tools/run.sh            # real run, deletes records older than DELTA_DAYS
#   DRY_RUN=1 ./tools/run.sh  # simulate only, no copy/delete side effects
#   DEBUG=1 ./tools/run.sh    # print raw API responses to stdout
#
# crontab entry
# 0 2 * * * /path/to/record3k/tools/run.sh >> /path/to/record3k/logs/cron_archive.log 2>&1

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# shellcheck source=/dev/null
[[ -f "$SCRIPT_DIR/run.env" ]] && source "$SCRIPT_DIR/run.env"

# ---- parameters (env var or run.env overrides the default) ----------------
DATA_DIR="${DATA_DIR:-data}"                                  # relative to repo root, or an absolute path
REMOTE="${REMOTE:-/mnt/nas/record3k_archive}"                 # net-share mount point, or user@host:/path
API_BASE="${API_BASE:-http://127.0.0.1:8080}"                 # base URL of the running API server
API_TOKEN="${API_TOKEN:-}"                                    # bearer token for the API
DELTA_DAYS="${DELTA_DAYS:-7}"                                 # delete records older than this many days
SSH="${SSH:-}"                                                # e.g. "ssh -i ~/.ssh/nas_key" (empty for local/mounted paths)
RSYNC_BIN="${RSYNC_BIN:-rsync}"
if [[ -z "${EXTRA_RSYNC_ARGS+x}" ]]; then EXTRA_RSYNC_ARGS=(); fi  # array, set in run.env: (--bwlimit=5000 --exclude=*.tmp)

# Optional rclone mount of the net share before archiving (skipped if RCLONE_REMOTE is empty)
RCLONE_REMOTE="${RCLONE_REMOTE:-}"                            # e.g. "record3k_archive:camflow01"
RCLONE_BIN="${RCLONE_BIN:-rclone}"
MOUNT_DIR="${MOUNT_DIR:-$HOME/mnt/record3k_archive}"

DRY_RUN="${DRY_RUN:-0}"
DEBUG="${DEBUG:-0}"

# ---------------------------------------------------------------------------
# mount netshare
if [[ -n "$RCLONE_REMOTE" ]]; then
    if mountpoint -q "$MOUNT_DIR"; then
        echo "Already mounted: $MOUNT_DIR"
    else
        echo "Not mounted, mounting now..."
        mkdir -p "$MOUNT_DIR"
        "$RCLONE_BIN" mount "$RCLONE_REMOTE" "$MOUNT_DIR" --daemon --vfs-cache-mode writes

        # rclone --daemon returns almost immediately but the mount takes a moment to come up
        for i in $(seq 1 10); do
            mountpoint -q "$MOUNT_DIR" && break
            sleep 1
        done

        if ! mountpoint -q "$MOUNT_DIR"; then
            echo "ERROR: failed to mount $MOUNT_DIR" >&2
            exit 1
        fi
    fi
fi

# ---------------------------------------------------------------------------

LOG_DIR="$REPO_ROOT/logs"
mkdir -p "$LOG_DIR"
LOG_FILE="$LOG_DIR/archive_records_$(date +%Y%m%d_%H%M%S).log"

cd "$REPO_ROOT"

# Exported as an env var (not passed as --api-token) so the token never shows
# up in `ps` output; archive_records.py picks it up via RECORD3K_API_TOKEN.
export RECORD3K_API_TOKEN="$API_TOKEN"

cmd=(python3 "$SCRIPT_DIR/archive_records.py"
    --data-dir "$DATA_DIR"
    --remote "$REMOTE"
    --api-base "$API_BASE"
    --delta-days "$DELTA_DAYS"
    --rsync-bin "$RSYNC_BIN"
    --yes)

if [[ -n "$SSH" ]]; then
    cmd+=(--ssh "$SSH")
fi

for arg in "${EXTRA_RSYNC_ARGS[@]:-}"; do
    [[ -n "$arg" ]] && cmd+=(--rsync-arg "$arg")
done

if [[ "$DRY_RUN" == "1" ]]; then
    cmd+=(--dry-run)
fi

if [[ "$DEBUG" == "1" ]]; then
    cmd+=(--debug)
fi

echo "Logging to: $LOG_FILE"
"${cmd[@]}" 2>&1 | tee "$LOG_FILE"
exit "${PIPESTATUS[0]}"
