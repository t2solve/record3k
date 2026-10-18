#!/bin/bash
# Helper script to run recorder-3k container with podman and GPU support

set -e

IMAGE_NAME="recorder-3k"
CONTAINER_NAME="recorder-3k"

echo "=== Podman GPU Setup Helper ==="
echo

# Check if nvidia-smi is available
if ! command -v nvidia-smi &> /dev/null; then
    echo "WARNING: nvidia-smi not found. GPU support may not be available."
    echo "Running in CPU-only mode..."
    echo
    
    podman run --rm -it \
        --name "$CONTAINER_NAME" \
        --network=host \
        "$IMAGE_NAME"
    exit 0
fi

echo "Checking NVIDIA GPU availability..."
nvidia-smi -L || echo "WARNING: Could not list GPUs"
echo

# Check for nvidia device files
echo "Checking NVIDIA device files:"
DEVICES=""
MISSING_DEVICES=0

for dev in /dev/nvidia0 /dev/nvidiactl /dev/nvidia-modeset /dev/nvidia-uvm /dev/nvidia-uvm-tools; do
    if [ -e "$dev" ]; then
        echo "  ✓ $dev exists"
        DEVICES="$DEVICES --device $dev"
    else
        echo "  ✗ $dev missing"
        MISSING_DEVICES=$((MISSING_DEVICES + 1))
    fi
done

# Check for DRI devices
if [ -d /dev/dri ]; then
    echo "  ✓ /dev/dri exists"
    DEVICES="$DEVICES --device /dev/dri"
else
    echo "  ✗ /dev/dri missing"
fi

# Check for video devices (cameras)
echo
echo "Checking video devices (cameras):"
VIDEO_DEVICES=$(ls /dev/video* 2>/dev/null || true)
if [ -n "$VIDEO_DEVICES" ]; then
    for dev in $VIDEO_DEVICES; do
        echo "  ✓ $dev exists"
        DEVICES="$DEVICES --device $dev"
    done
else
    echo "  ⚠ No video devices found (cameras won't work)"
fi
echo

# Determine which method to use
if [ $MISSING_DEVICES -eq 0 ]; then
    echo "Method: Using explicit device mounts (all devices present)"
    echo "Command: podman run --rm -it --name $CONTAINER_NAME --network=host $DEVICES $IMAGE_NAME"
    echo
    
    podman run --rm -it \
        --name "$CONTAINER_NAME" \
        --network=host \
        $DEVICES \
        "$IMAGE_NAME" "$@"
        
elif command -v podman-compose &> /dev/null && podman info | grep -q "nvidia"; then
    echo "Method: Using CDI (Container Device Interface)"
    echo "Command: podman run --rm -it --name $CONTAINER_NAME --network=host --device nvidia.com/gpu=all --security-opt=label=disable $IMAGE_NAME"
    echo
    
    podman run --rm -it \
        --name "$CONTAINER_NAME" \
        --network=host \
        --device nvidia.com/gpu=all \
        --security-opt=label=disable \
        "$IMAGE_NAME" "$@"
        
else
    echo "WARNING: Some NVIDIA devices are missing!"
    echo "This may be because:"
    echo "  1. NVIDIA drivers are not installed"
    echo "  2. nvidia-persistenced is not running (try: sudo systemctl start nvidia-persistenced)"
    echo "  3. You need to run: sudo nvidia-modprobe -u -c=0"
    echo
    echo "Attempting to run anyway with available devices..."
    echo "Command: podman run --rm -it --name $CONTAINER_NAME --network=host $DEVICES $IMAGE_NAME"
    echo
    
    podman run --rm -it \
        --name "$CONTAINER_NAME" \
        --network=host \
        $DEVICES \
        "$IMAGE_NAME" "$@"
fi
