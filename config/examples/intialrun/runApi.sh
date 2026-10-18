#!/bin/bash
# Usage:
#   ./runApi.sh               # normal run
#   ./runApi.sh --valgrind    # memcheck (CPU only, no CUDA)
#   ./runApi.sh --gdb         # run under gdb for segfault post-mortem / interactive debug

# for vimbax
export GENICAM_GENTL64_PATH="../../vimbax/cti/"

export DATA_DIR="data"
echo "Using data directory: $DATA_DIR"

export API_VERBOSE=1 #enable log

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="$SCRIPT_DIR/../lib:$LD_LIBRARY_PATH"

VALGRIND=0
GDB=0
PASSTHROUGH_ARGS=()

for arg in "$@"; do
  case "$arg" in
    --valgrind) VALGRIND=1 ;;
    --gdb)      GDB=1 ;;
    *)          PASSTHROUGH_ARGS+=("$arg") ;;
  esac
done

APISERVER="$SCRIPT_DIR/apiserver"

if [[ "$VALGRIND" == 1 ]]; then
  LOG="$SCRIPT_DIR/memcheck_apiserver.log"
  echo "[runApi] Running under valgrind/memcheck — log: $LOG"
  echo "[runApi] NOTE: CUDA kernels are not inspected by valgrind."
  exec valgrind \
    --tool=memcheck \
    --leak-check=full \
    --track-origins=yes \
    --show-leak-kinds=all \
    --num-callers=30 \
    --log-file="$LOG" \
    "$APISERVER" "${PASSTHROUGH_ARGS[@]}"
elif [[ "$GDB" == 1 ]]; then
  echo "[runApi] Running under gdb — type 'run' to start, 'bt' after crash for backtrace."
  exec gdb --args "$APISERVER" "${PASSTHROUGH_ARGS[@]}"
else
  exec "$APISERVER" "${PASSTHROUGH_ARGS[@]}"
fi
