# GPU Support Troubleshooting for Podman

## The Problem

The error `failed to stat CDI host device "/dev/nvidia-uvm": no such file or directory` occurs when:
- NVIDIA kernel modules are not fully loaded
- The nvidia-uvm device file doesn't exist
- Podman's CDI integration can't find the device

## Solutions

### Quick Fix: Use the Helper Script

```bash
./scripts/run-podman.sh
```

This script automatically detects your GPU setup and uses the appropriate method.

### Manual Solutions

#### Solution 1: Load NVIDIA Kernel Modules

```bash
# Load the nvidia-uvm module
sudo modprobe nvidia-uvm

# Verify devices exist
ls -la /dev/nvidia*

# Should show:
# /dev/nvidia0
# /dev/nvidiactl
# /dev/nvidia-modeset
# /dev/nvidia-uvm
# /dev/nvidia-uvm-tools
```

#### Solution 2: Use Explicit Device Mounting (Recommended for Podman)

Instead of `--gpus all`, use explicit device mounts:

```bash
podman run --rm -it --name recorder-3k --network=host \
  --device /dev/dri \
  --device /dev/nvidia0 \
  --device /dev/nvidiactl \
  --device /dev/nvidia-modeset \
  --device /dev/nvidia-uvm \
  --device /dev/nvidia-uvm-tools \
  recorder-3k
```

#### Solution 3: Use CDI with Security Opt

If nvidia-container-toolkit is installed:

```bash
podman run --rm -it --name recorder-3k --network=host \
  --device nvidia.com/gpu=all \
  --security-opt=label=disable \
  recorder-3k
```

#### Solution 4: Run Without GPU (CPU Only)

If you don't need GPU acceleration:

```bash
podman run --rm -it --name recorder-3k --network=host recorder-3k
```

The application will use CPU-based OpenCV processing.

## Verification

### Check NVIDIA Drivers

```bash
nvidia-smi
```

Should show GPU information.

### Check Device Files

```bash
ls -la /dev/nvidia* /dev/dri/
```

All device files should be present and accessible.

### Check Podman Info

```bash
podman info | grep -i nvidia
```

Should show CDI configuration if available.

### Test Inside Container

```bash
podman run --rm -it --name test-gpu --network=host \
  --device /dev/nvidia0 --device /dev/nvidiactl \
  recorder-3k bash

# Inside container:
nvidia-smi  # Should work if drivers are properly mounted
```

## Persistent Setup

To ensure nvidia-uvm loads on boot:

```bash
# Add to /etc/modules-load.d/nvidia-uvm.conf
echo "nvidia-uvm" | sudo tee /etc/modules-load.d/nvidia-uvm.conf

# Or start nvidia-persistenced service
sudo systemctl enable nvidia-persistenced
sudo systemctl start nvidia-persistenced
```

## Common Issues

### Issue: "no such file or directory" for nvidia-uvm

**Cause**: nvidia-uvm kernel module not loaded

**Fix**:
```bash
sudo modprobe nvidia-uvm
sudo nvidia-modprobe -u -c=0
```

### Issue: "permission denied" for device files

**Cause**: User doesn't have access to GPU devices

**Fix**:
```bash
# Add user to video group
sudo usermod -aG video $USER
sudo usermod -aG render $USER

# Re-login or:
newgrp video
```

### Issue: CDI not working in rootless podman

**Cause**: CDI requires additional setup for rootless mode

**Fix**: Use explicit device mounting (Solution 2) or run as root:
```bash
sudo podman run --rm -it --name recorder-3k --network=host \
  --device nvidia.com/gpu=all \
  recorder-3k
```

## Docker vs Podman Differences

| Feature | Docker | Podman |
|---------|--------|--------|
| GPU flag | `--gpus all` | Not supported |
| CDI | Automatic | Requires `--device nvidia.com/gpu=all` |
| Device mount | Optional | Recommended |
| Rootless | Limited GPU support | Requires explicit devices |

## Recommended Commands

**For development (with source mount):**
```bash
./scripts/run-podman.sh bash
```

**For production (run apiserver):**
```bash
./scripts/run-podman.sh
```

**For testing without GPU:**
```bash
podman run --rm -it --name recorder-3k --network=host recorder-3k
```
