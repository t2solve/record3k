#!/usr/bin/env bash
set -euo pipefail

# run : e.g. /localBuild.sh --no-cuda --no-vimbax -j 8 
# Simple local builder for the testrecord3k project
# - Generates build files in ./build
# - Builds with qmake + make
# - On success, can optionally run a built app via --x <binary-name>
#
# Usage examples:
#   ./localBuild.sh                 # build with defaults (CUDA+Vimba enabled by default per common.pri)
#   ./localBuild.sh --no-cuda       # force-disable CUDA
#   ./localBuild.sh --no-vimbax     # force-disable VimbaX
#   ./localBuild.sh --clean         # clean build dir before building
#   ./localBuild.sh -j 8            # build with 8 jobs
#   ./localBuild.sh --x pipelineviewer   # build and run pipelineviewer
#   ./localBuild.sh --x xmlsanity --no-cuda  # build and run xmlsanity

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"
BUILD_DIR="$PROJECT_ROOT/build"
JOBS="$(nproc || echo 4)"
EXTRA_CONFIG=()
CLEAN=0
RUN_APP=""
VALGRIND_CALLGRIND=0

# Arg parsing
while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-cuda)
      EXTRA_CONFIG+=("CONFIG+=no_cuda")
      shift ;;
    --no-vimbax)
      EXTRA_CONFIG+=("CONFIG+=no_vimbax")
      shift ;;
    --clean)
      CLEAN=1
      shift ;;
    -j|--jobs)
      JOBS="${2:-$JOBS}"
      shift 2 ;;
    --x)
      RUN_APP="${2:-}"
      if [[ -z "$RUN_APP" ]]; then
        echo "--x requires a binary name (e.g., pipelineviewer)" >&2
        exit 1
      fi
      shift 2 ;;
    --valgrind)
      VALGRIND_CALLGRIND=1
      shift ;;
    *)
      echo "Unknown option: $1" >&2
      exit 1 ;;
  esac
done

# Pick qmake
QMAKE_BIN=""
if command -v qmake6 >/dev/null 2>&1; then
  QMAKE_BIN="qmake6"
elif command -v qmake-qt6 >/dev/null 2>&1; then
  QMAKE_BIN="qmake-qt6"
elif command -v qmake >/dev/null 2>&1; then
  QMAKE_BIN="qmake"
else
  echo "Error: qmake not found (expected qmake6 or qmake)." >&2
  exit 1
fi

echo "[localBuild] Using qmake: $QMAKE_BIN"

echo "[localBuild] Project root: $PROJECT_ROOT"
mkdir -p "$BUILD_DIR"

if [[ "$CLEAN" == 1 ]]; then
  echo "[localBuild] Cleaning build directory..."
  rm -rf "$BUILD_DIR"/*
fi

pushd "$BUILD_DIR" >/dev/null

# Generate Makefiles
set -x
"$QMAKE_BIN" "$PROJECT_ROOT/testrecord3k.pro" CONFIG+=release ${EXTRA_CONFIG[@]:-}
set +x

# Build
set -x
make -j "$JOBS"
set +x

popd >/dev/null

echo "[localBuild] Build succeeded."

# Runtime env (help Qt/LD find libs/plugins if bundled in bin/)
export LD_LIBRARY_PATH="$BUILD_DIR/bin:$BUILD_DIR/lib:$PROJECT_ROOT/bin:$PROJECT_ROOT/lib:${LD_LIBRARY_PATH:-}"
# Prefer system Qt plugins; do not force a bundled Qt5 plugins dir that may be incompatible with Qt6.

# If an app was requested with --x, try to run it; otherwise exit after build
if [[ -n "$RUN_APP" ]]; then
  APP_BIN_BUILD="$BUILD_DIR/bin/$RUN_APP"
  APP_BIN_SRC="$PROJECT_ROOT/bin/$RUN_APP"
  echo "[localBuild] Requested to run: $RUN_APP"
  if [[ "$VALGRIND_CALLGRIND" == 1 ]]; then
    # Run under valgrind/callgrind and optionally open kcachegrind
    if [[ -x "$APP_BIN_BUILD" ]]; then
      TARGET_BIN="$APP_BIN_BUILD"
    elif [[ -x "$APP_BIN_SRC" ]]; then
      TARGET_BIN="$APP_BIN_SRC"
    else
      echo "[localBuild] Binary '$RUN_APP' not found in: \n  - $APP_BIN_BUILD \n  - $APP_BIN_SRC\n[localBuild] Skipping run and finishing after build." >&2
      exit 0
    fi

    # Ensure LD_LIBRARY_PATH includes build/lib and project lib
    export LD_LIBRARY_PATH="$BUILD_DIR/lib:$PROJECT_ROOT/lib:${LD_LIBRARY_PATH:-}"

    CALLGRIND_OUT="$BUILD_DIR/callgrind_${RUN_APP}.out"
    echo "[localBuild] Running under valgrind/callgrind: $TARGET_BIN $*"
    valgrind --tool=callgrind --dump-instr=yes --collect-jumps=yes --callgrind-out-file="$CALLGRIND_OUT" "$TARGET_BIN" ${@:2}

    if command -v callgrind_annotate >/dev/null 2>&1; then
      echo "[localBuild] Launching callgrind_annotate on $CALLGRIND_OUT"
      callgrind_annotate "$CALLGRIND_OUT" &
    else
      echo "[localBuild] callgrind_annotate not found. You can analyze with: callgrind_annotate $CALLGRIND_OUT"
    fi
    exit 0
  else
    if [[ -x "$APP_BIN_BUILD" ]]; then
      exec "$APP_BIN_BUILD"
    elif [[ -x "$APP_BIN_SRC" ]]; then
      exec "$APP_BIN_SRC"
    else
      echo "[localBuild] Binary '$RUN_APP' not found in: \n  - $APP_BIN_BUILD \n  - $APP_BIN_SRC\n[localBuild] Skipping run and finishing after build." >&2
      exit 0
    fi
  fi
fi

exit 0
