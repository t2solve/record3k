#!/bin/sh
# Container entrypoint — run before apiserver starts
# a) Copy VimbaX CTI + XML files into /app/bin so the TL loader finds them
#    alongside the binary (useful when /app is a bind-mount that overlays the image)
# b) Ensure GENICAM_GENTL64_PATH is exported for this process tree

set -e

CTI_SRC="/app/vimbax/cti"
CTI_DST="/app/bin"

echo "[entrypoint] Copying VimbaX CTI files: ${CTI_SRC} -> ${CTI_DST}"
cp -f "${CTI_SRC}"/*.cti "${CTI_DST}/" 2>/dev/null && echo "[entrypoint]   .cti files copied" || echo "[entrypoint]   WARNING: no .cti files found in ${CTI_SRC}"
cp -f "${CTI_SRC}"/*.xml "${CTI_DST}/" 2>/dev/null && echo "[entrypoint]   .xml files copied" || true

# Set / override GENICAM_GENTL64_PATH so the process launched below inherits it.
# Prepend CTI_DST (bin dir) so locally-copied .cti takes priority, then fall
# back to the original CTI_SRC entry that was baked in at image build time.
export GENICAM_GENTL64_PATH="${CTI_DST}:${CTI_SRC}${GENICAM_GENTL64_PATH:+:${GENICAM_GENTL64_PATH}}"
echo "[entrypoint] GENICAM_GENTL64_PATH=${GENICAM_GENTL64_PATH}"

# Add secondary IP alias on the GigE NIC so VimbaX can reach the camera subnet.
# Cameras are on 172.18.227.0/24; host bond0 is on 172.18.205.0/24 — without
# this alias VimbaX sends unicast probes from the wrong subnet and cameras ignore them.
# Requires --cap-add=NET_ADMIN on the podman/docker run command.
CAMERA_SUBNET_ALIAS="${CAMERA_SUBNET_ALIAS:-172.18.227.1/24}"
CAMERA_NIC="${CAMERA_NIC:-bond0}"
# Only attempt the IP alias if we actually have NET_ADMIN capability.
# Rootless podman does not grant NET_ADMIN even if requested in compose;
# in that case skip silently — GigE discovery will still work if the
# host NIC already has a route to the camera subnet.
if ! ip link show "${CAMERA_NIC}" > /dev/null 2>&1; then
    echo "[entrypoint] WARNING: NIC '${CAMERA_NIC}' not found — skipping IP alias (set CAMERA_NIC to the correct interface)"
elif ip addr show "${CAMERA_NIC}" 2>/dev/null | grep -q "${CAMERA_SUBNET_ALIAS%%/*}"; then
    echo "[entrypoint] IP alias ${CAMERA_SUBNET_ALIAS} already present on ${CAMERA_NIC}"
else
    echo "[entrypoint] Adding IP alias ${CAMERA_SUBNET_ALIAS} to ${CAMERA_NIC}..."
    if ip addr add "${CAMERA_SUBNET_ALIAS}" dev "${CAMERA_NIC}" 2>/dev/null; then
        echo "[entrypoint]   Done."
    else
        echo "[entrypoint]   WARNING: could not add IP alias — running rootless or without NET_ADMIN."
        echo "[entrypoint]   For rootful podman add: --cap-add=NET_ADMIN"
        echo "[entrypoint]   For rootless: add the alias on the host before starting the container:"
        echo "[entrypoint]     sudo ip addr add ${CAMERA_SUBNET_ALIAS} dev ${CAMERA_NIC}"
    fi
fi

echo ""
echo "======================================================"
echo "  record3k API server starting up..."
echo "  Web UI (via Caddy):  http://localhost:3000"
echo "  API direct:          http://localhost:8080"
echo "======================================================"
echo ""

# ---------------------------------------------------------------------------
# Optional GDB wrapping — set DEBUG_GDB in the environment to enable.
#
#   DEBUG_GDB=batch       — run, print full backtrace on crash, then exit.
#                           Best for catching the crash that kills Xorg.
#                           Use: environment: [DEBUG_GDB=batch] in compose.yaml
#
#   DEBUG_GDB=server      — start a gdbserver on port 2345 so you can attach
#                           from the host with:
#                             gdb ./apiserver
#                             (gdb) target remote localhost:2345
#                           Requires exposing port 2345 in compose.yaml.
#
# CUDA_LAUNCH_BLOCKING=1 forces synchronous kernel launches so GPU errors are
# reported at the exact offending call instead of a random later point.
# ---------------------------------------------------------------------------
if [ -n "${DEBUG_GDB:-}" ]; then
    # Enable synchronous CUDA error reporting to get precise crash location
    export CUDA_LAUNCH_BLOCKING=1
    echo "[entrypoint] CUDA_LAUNCH_BLOCKING=1 set (synchronous error reporting)"

    case "${DEBUG_GDB}" in
        batch)
            echo "[entrypoint] DEBUG_GDB=batch — running under gdb (batch mode, prints backtrace on crash)"
            exec gdb \
                --batch \
                -ex "set pagination off" \
                -ex "set print thread-events off" \
                -ex "handle SIGUSR1 nostop noprint pass" \
                -ex "handle SIGUSR2 nostop noprint pass" \
                -ex "handle SIGIO  nostop noprint pass" \
                -ex "run" \
                -ex "thread apply all backtrace full" \
                -ex "quit" \
                --args "$@"
            ;;
        server)
            GDB_PORT="${GDB_PORT:-2345}"
            echo "[entrypoint] DEBUG_GDB=server — starting gdbserver on port ${GDB_PORT}"
            echo "[entrypoint]   Connect from host: gdb ./apiserver"
            echo "[entrypoint]     (gdb) target remote localhost:${GDB_PORT}"
            exec gdbserver ":${GDB_PORT}" "$@"
            ;;
        *)
            echo "[entrypoint] Unknown DEBUG_GDB value '${DEBUG_GDB}' — expected 'batch' or 'server'. Running normally."
            exec "$@"
            ;;
    esac
else
    # Normal launch
    exec "$@"
fi
