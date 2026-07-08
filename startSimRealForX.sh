#!/bin/bash
# startSimRealForX.sh — run the record3k-sim-real container against ONE real record.
#
# Hands over, at call time (nothing baked into the image):
#   1. the record's .tar.gz          (full path, lives wherever it was downloaded to)
#   2. the run config JSON           (default: data/simulation/bgsub_sweep1.json)
#   3. the supervised-records tags DB (default: data/simulation/supervised_records_data.sqlite3)
#
# (2) and (3) must live in the same directory — that directory is bind-mounted as
# /data/simulation, and the run config's own "param_ranges"/"base_pipeline"/"out_dir"
# entries are resolved relative to it inside the container (see sim_runner_real.py).
# Output lands at <that same directory>/<out_dir from the run config>/<record_uid>.sqlite3
# (see Containerfile.sim-real).
#
# Runs the container detached (-d --rm): it is auto-removed by podman once the sweep
# process inside it exits (success or failure) — no leftover stopped containers to
# clean up. The script itself returns immediately after starting it; a background
# `podman logs -f` tees the container's output to /tmp/log-container-<name>.log for
# you to monitor separately (the log file is NOT removed when the container is).
#
# Usage:
#   ./startSimRealForX.sh <record.tar.gz> [run_config.json] [tags_db.sqlite3] [extra args...]
#
# Examples:
#   ./startSimRealForX.sh /path/to/file-rec-rec-1783432744349-4683.tar.gz
#   ./startSimRealForX.sh /path/to/file-rec-rec-1783432744349-4683.tar.gz \
#       data/simulation/bgsub_sweep1.json data/simulation/supervised_records_data.sqlite3
#   ./startSimRealForX.sh /path/to/record.tar.gz data/simulation/bgsub_sweep1.json \
#       data/simulation/supervised_records_data.sqlite3 --force --n-pipes 40

set -e

IMAGE_NAME="record3k-sim-real"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

RECORD_TAR_GZ="$1"
RUN_CONFIG="${2:-$SCRIPT_DIR/data/simulation/bgsub_sweep1.json}"
TAGS_DB="${3:-$SCRIPT_DIR/data/simulation/supervised_records_data.sqlite3}"
# Any arguments beyond the first three are forwarded as-is to sim_runner_real.py
# inside the container (e.g. --force, --n-pipes 40).
EXTRA_ARGS=()
if [ "$#" -gt 3 ]; then
    EXTRA_ARGS=("${@:4}")
fi

echo "=== startSimRealForX: single-record sim-real run ==="
echo

if [ -z "$RECORD_TAR_GZ" ]; then
    echo "Usage: $0 <record.tar.gz> [run_config.json] [tags_db.sqlite3] [-- extra args]" >&2
    exit 1
fi

fail=0

check_file() {
    local label="$1" path="$2"
    if [ -f "$path" ]; then
        echo "  ✓ $label : $path"
    else
        echo "  ✗ $label : $path  (NOT FOUND)"
        fail=1
    fi
}

check_file "record archive" "$RECORD_TAR_GZ"
check_file "run config     " "$RUN_CONFIG"
check_file "tags db        " "$TAGS_DB"

if [ "$fail" -ne 0 ]; then
    echo
    echo "Aborting — one or more required files are missing (see ✗ above)."
    exit 1
fi

RUN_CONFIG_DIR="$(cd "$(dirname "$RUN_CONFIG")" && pwd)"
TAGS_DB_DIR="$(cd "$(dirname "$TAGS_DB")" && pwd)"
if [ "$RUN_CONFIG_DIR" != "$TAGS_DB_DIR" ]; then
    echo
    echo "Aborting — run config and tags db must live in the same directory"
    echo "(that directory is what gets mounted as /data/simulation):"
    echo "  run config dir : $RUN_CONFIG_DIR"
    echo "  tags db dir    : $TAGS_DB_DIR"
    exit 1
fi

if ! podman image exists "$IMAGE_NAME" 2>/dev/null; then
    echo
    echo "Aborting — podman image '$IMAGE_NAME' not found. Build it first:"
    echo "  podman build -f Containerfile.sim-real -t $IMAGE_NAME ."
    exit 1
fi

RECORDS_DIR="$(cd "$(dirname "$RECORD_TAR_GZ")" && pwd)"
RUN_CONFIG_BASENAME="$(basename "$RUN_CONFIG")"
RECORD_BASENAME="$(basename "$RECORD_TAR_GZ")"

# file-rec-<uid>.tar.gz -> <uid> (same convention sim_runner_real.py's --record uses)
RECORD_UID="${RECORD_BASENAME#file-rec-}"
RECORD_UID="${RECORD_UID%.tar.gz}"

TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
CONTAINER_NAME="sim-real-${RECORD_UID}-${TIMESTAMP}"
LOG_FILE="/tmp/log-container-${CONTAINER_NAME}.log"

DEVICE_ARGS=()
if command -v nvidia-smi &> /dev/null && nvidia-smi -L &> /dev/null; then
    DEVICE_ARGS=(--device nvidia.com/gpu=all)
else
    echo
    echo "WARNING: no NVIDIA GPU detected — run-real replays CUDA_ENABLED pipeline"
    echo "steps and will likely fail without a GPU. Continuing anyway."
fi

echo
echo "  simulation dir (-> /data/simulation) : $RUN_CONFIG_DIR"
echo "  records dir    (-> /data/records:ro) : $RECORDS_DIR"
echo "  run config                           : $RUN_CONFIG_BASENAME"
echo "  record                               : $RECORD_BASENAME"
echo "  container name                       : $CONTAINER_NAME"
echo "  log file                             : $LOG_FILE"
echo

# --record scopes the run to exactly this archive. Without it, sim_runner_real.py
# would discover every supervised record whose archive happens to also be visible
# under $RECORDS_DIR (e.g. if that's a shared download folder with many records) and
# sweep all of them, not just this one.
podman run -d --rm \
    --name "$CONTAINER_NAME" \
    "${DEVICE_ARGS[@]}" \
    -v "$RUN_CONFIG_DIR:/data/simulation" \
    -v "$RECORDS_DIR:/data/records:ro" \
    "$IMAGE_NAME" \
    "/data/simulation/$RUN_CONFIG_BASENAME" \
    --record "/data/records/$RECORD_BASENAME" \
    "${EXTRA_ARGS[@]}" \
    > /dev/null

# Tee the container's log into $LOG_FILE in the background so it's readable even
# after the container itself is gone (--rm removes the container, not the log file).
# `podman logs -f` exits on its own once the container stops.
nohup podman logs -f "$CONTAINER_NAME" > "$LOG_FILE" 2>&1 &
disown

echo "Started — container is running detached."
echo
echo "  monitor:  tail -f $LOG_FILE"
echo "  or:       podman logs -f $CONTAINER_NAME"
echo "  status:   podman ps --filter name=$CONTAINER_NAME"
echo
echo "The container is removed automatically by podman once the sweep finishes"
echo "(--rm) — the log file at $LOG_FILE is kept."
