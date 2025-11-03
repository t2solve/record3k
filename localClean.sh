#!/usr/bin/env bash
set -euo pipefail

# Simple local clean script for testrecord3k
# - Removes build artifacts to force a fresh configure/build on next run
# - By default removes ./build only (safe). Use --all for full clean.
# - Use --dry-run to preview deletions.
#
# Usage:
#   ./localClean.sh           # remove build/
#   ./localClean.sh --all     # remove build/, bin/, lib/
#   ./localClean.sh --dry-run --all

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"
DRY_RUN=0
ALL=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run)
      DRY_RUN=1; shift ;;
    --all|--distclean)
      ALL=1; shift ;;
    *)
      echo "Unknown option: $1" >&2
      exit 1 ;;
  esac
done

rm_dir() {
  local target="$1"
  if [[ -d "$target" ]]; then
    if [[ "$DRY_RUN" == 1 ]]; then
      echo "[dry-run] rm -rf $target"
    else
      echo "[clean] rm -rf $target"
      rm -rf "$target"
    fi
  fi
}

# Always clean build directory
rm_dir "$PROJECT_ROOT/build"

if [[ "$ALL" == 1 ]]; then
  rm_dir "$PROJECT_ROOT/bin"
  rm_dir "$PROJECT_ROOT/lib"
  # qmake/moc caches sometimes end up under .qmake.stash etc.
  rm_dir "$PROJECT_ROOT/.qmake.stash"
fi

echo "[clean] Done."
