# Quick Start: Running recorder-3k with Podman

## TL;DR - Just run this:

```bash
./scripts/run-podman.sh
```

This automatically handles GPU and camera device mounting.

## Manual Commands

### Run the API server (default):
```bash
podman run --rm --name recorder-3k --network=host \
  --device /dev/nvidia0 --device /dev/nvidiactl \
  --device /dev/nvidia-modeset --device /dev/nvidia-uvm \
  --device /dev/nvidia-uvm-tools --device /dev/dri \
  --device /dev/video0 --device /dev/video1 \
  recorder-3k
```

### Interactive bash shell:
```bash
./scripts/run-podman.sh bash
```

Or manually:
```bash
podman run --rm -it --name recorder-3k --network=host \
  --device /dev/nvidia0 --device /dev/nvidiactl \
  --device /dev/nvidia-modeset --device /dev/nvidia-uvm \
  --device /dev/nvidia-uvm-tools --device /dev/dri \
  --device /dev/video0 --device /dev/video1 \
  recorder-3k bash
```

## Important Notes

1. **"NVIDIA Driver not detected" warning**: This is normal and can be ignored. Your CUDA-enabled application will still work because the devices are explicitly mounted.

2. **Device mounting**: Podman requires explicit device mounts unlike Docker's `--gpus all` flag.

3. **Camera access**: All `/dev/video*` devices must be mounted for cameras to work.

4. **Network**: `--network=host` is required for the API server to be accessible.

## Troubleshooting

If you get device errors:
```bash
# Load NVIDIA modules
sudo modprobe nvidia-uvm

# Verify devices
ls -la /dev/nvidia* /dev/video*
```

See [PODMAN_GPU_TROUBLESHOOTING.md](podman-gpu-troubleshooting.md) for detailed help.

## Development Workflow

Mount your local source for live development:
```bash
podman run --rm -it --name recorder-3k --network=host \
  -v "$PWD":/app -w /app \
  --device /dev/nvidia0 --device /dev/nvidiactl \
  --device /dev/nvidia-modeset --device /dev/nvidia-uvm \
  --device /dev/nvidia-uvm-tools --device /dev/dri \
  recorder-3k bash

# Inside container:
export LD_LIBRARY_PATH=/app/lib:/app/vimbax/api/lib:$LD_LIBRARY_PATH
cd bin
./apiserver data
```
