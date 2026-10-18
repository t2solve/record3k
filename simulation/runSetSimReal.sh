#!/usr/bin/env bash
# runSetSimReal.sh — sweep a hand-picked set of real records, MAX_PARALLEL containers at a time
# (each via startSimRealForX.sh). Usage: [RECORDS_DIR=/path/to/archives] ./simulation/runSetSimReal.sh

# How many records to sweep concurrently. Each container peaks around 11GB VRAM
# (observed) -- check `nvidia-smi` for your total and existing usage before raising
# this; 3 * 11GB = 33GB comfortably fits a 49GB card with room for other processes.
MAX_PARALLEL=3

# Archives live at RECORDS_DIR/file-rec-<record-id>.tar.gz -- list just the IDs
# below, the path gets built from RECORDS_DIR per record.
#
# To pull this list from the tags DB instead of hardcoding it (e.g. once you want
# "every supervised record" rather than a hand-picked set), the equivalent query is:
#   sqlite3 data/simulation/supervised_records_data.sqlite3 \
#     "SELECT record_uid FROM tagged_records WHERE roi_defined=1 AND record_failed=0;"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RECORDS_DIR="${RECORDS_DIR:-$REPO_ROOT/data/files/bin}"   # override: RECORDS_DIR=/path/to/archives ./simulation/runSetSimReal.sh

RECORD_IDS=(
rec-1781178083697-2560
rec-1781181091997-1872
rec-1781539878044-2536
rec-1781625092143-3394
rec-1781689645732-5052
rec-1781863243674-2188
rec-1781863303160-4990
rec-1781866368987-8562
rec-1781866429623-9237
rec-1781869324354-4951
rec-1781869375154-5115
rec-1783078531624-2915
rec-1783340363986-2228
rec-1783340413685-3179
rec-1783340467630-2280
rec-1783340517168-2919
rec-1783343441536-4945
rec-1783343605207-7005
rec-1783346485661-1273
rec-1783346530827-2379
rec-1783346584716-9078
rec-1783346631829-8302
rec-1783426427545-7149
rec-1783426600269-6325
rec-1783432744349-4683
rec-1783432795555-1531
rec-1783432839319-1175
rec-1783506508854-4974
)

total=${#RECORD_IDS[@]}
launched=0
# Completed-count is tracked via a temp file rather than a shell variable, since
# each background waiter below runs in its own subshell and can't write back to
# this shell's variables -- appends are small enough to be atomic (< PIPE_BUF).
DONE_LOG="$(mktemp)"

for uid in "${RECORD_IDS[@]}"; do
  launched=$((launched + 1))
  record="${RECORDS_DIR}/file-rec-${uid}.tar.gz"

  echo "[launch $launched/$total] $uid"
  "$SCRIPT_DIR/startSimRealForX.sh" "$record"

  # One background waiter per record, tracked as a job of THIS shell -- each just
  # blocks until that record's container disappears from `podman ps` (finished +
  # auto-removed via --rm), then prints. This is what lets us cap concurrency below.
  (
    while podman ps --format '{{.Names}}' | grep -q "^sim-real-${uid}-"; do
      sleep 5
    done
    echo "$uid" >> "$DONE_LOG"
    echo "[done $(wc -l < "$DONE_LOG")/$total] $uid"
  ) &

  # Throttle: once MAX_PARALLEL waiters (== MAX_PARALLEL containers) are in flight,
  # block here until at least one finishes before launching the next record's
  # container -- this is what actually limits how many run at the same time.
  while [ "$(jobs -rp | wc -l)" -ge "$MAX_PARALLEL" ]; do
    wait -n
  done
done

# Wait for the last batch (fewer than MAX_PARALLEL) to finish too.
wait
rm -f "$DONE_LOG"
echo "all $total records processed"
