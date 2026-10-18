#!/bin/sh
# Start the Allied Vision VimbaX Viewer from an unpacked VimbaX SDK.
# Override the location: VIMBAX_DIR=/path/to/VimbaX ./scripts/start-vimbax.sh
PROJECT_DIR="${VIMBAX_DIR:-$HOME/work/VimbaX_2026-1}"
export GENICAM_GENTL64_PATH="$GENICAM_GENTL64_PATH:$PROJECT_DIR/cti"
exec "$PROJECT_DIR/bin/VimbaXViewer" "$@"
