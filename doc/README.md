# record3k documentation

Build and run instructions are in the [top-level README](../README.md). This folder has the details.

## Architecture

| Document | Content |
|---|---|
| [overview-components.png](architecture/overview-components.png) | Component overview (source: `overview-components.dia`, edit with Dia) |
| [use-cases.png](architecture/use-cases.png) | Use-case diagram |
| [processing-steps.md](architecture/processing-steps.md) | Reference for every pipeline `ProcessStep` and its parameters (CPU and CUDA variants) |
| [record-manager.md](architecture/record-manager.md) | `RecordManager`: recording sessions, FPS control, output layout |
| [result-logger.md](architecture/result-logger.md) | `ResultLogger`: per-frame result and debug logging modes |
| [frame-timestamps.md](architecture/frame-timestamps.md) | Hardware frame timestamps on VimbaX GigE cameras |
| [vimbax-acquisition-refactor.md](architecture/vimbax-acquisition-refactor.md) | `AcquireSingleImage` vs continuous streaming: problems at scale, migration path |

## How-to

| Document | Content |
|---|---|
| [podman-quickstart.md](howto/podman-quickstart.md) | Run the `recorder-3k` image with GPU and camera devices |
| [podman-gpu-troubleshooting.md](howto/podman-gpu-troubleshooting.md) | Fix CDI / `nvidia-uvm` errors with podman |
| [setup-cameras-static.md](howto/setup-cameras-static.md) | Set a persistent static IP on a GigE camera |
| [mount-network-share.md](howto/mount-network-share.md) | Mount an SMB share as the archive target for `tools/archive_records.py` |

## Elsewhere

- Web client: [client/README.md](../client/README.md)
- Simulation and pipeline tuning: `simulation/README.md` on branch `feature/simulation`
- API spec: [client/openapi.json](../client/openapi.json), also served live by the API at `/openapi.json` and `/swagger`

`processing-steps.pdf` is generated and gitignored: `pandoc architecture/processing-steps.md -o architecture/processing-steps.pdf`.
