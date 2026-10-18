#!/bin/sh
# Start a PipelineViewer release bundle (bin/ + lib/ layout).
# Override the location: PIPELINEVIEWER_DIR=/path/to/release ./scripts/start-pipelineviewer.sh
PROJECT_DIR="${PIPELINEVIEWER_DIR:-$HOME/work/libviewerRelease01}"
export LD_LIBRARY_PATH="$PROJECT_DIR/lib:$LD_LIBRARY_PATH"
exec "$PROJECT_DIR/bin/PipelineViewer" "$@"
