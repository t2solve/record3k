#!/usr/bin/env python3
"""
Local orchestrator for step A1.

Deploys sample_records.py to the remote server, runs it, and fetches
the resulting tar.gz back to a local data directory.

Usage:
  python fetch_samples.py \
    --server user@remote-host \
    --records-root /data/records \
    --output ./data/samples \
    --n-windows 5

Requirements: ssh + scp available in PATH (key-based auth recommended).
"""

import argparse
import subprocess
import sys
import tarfile
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
SAMPLER = SCRIPT_DIR / 'sample_records.py'
REMOTE_TMP = '/tmp/pipeopt_sampling'
REMOTE_SCRIPT = f'{REMOTE_TMP}/sample_records.py'
REMOTE_OUTPUT = f'{REMOTE_TMP}/samples'
REMOTE_TAR = f'{REMOTE_OUTPUT}.tar.gz'


def run(cmd: list, **kwargs) -> None:
    flat = ' '.join(str(c) for c in cmd)
    print(f'\n$ {flat}')
    subprocess.run(cmd, check=True, **kwargs)


def main() -> None:
    parser = argparse.ArgumentParser(description='Fetch record samples from remote server')
    parser.add_argument('--server', required=True,
                        help='SSH target, e.g. user@hostname')
    parser.add_argument('--records-root', required=True,
                        help='Absolute path to the records directory on the server')
    parser.add_argument('--output', default='./data/samples',
                        help='Local directory where samples will be unpacked (default: ./data/samples)')
    parser.add_argument('--n-windows', type=int, default=5,
                        help='Random 10-second windows per record (default 5)')
    parser.add_argument('--window-sec', type=int, default=10,
                        help='Window duration in seconds (default 10)')
    parser.add_argument('--seed', type=int, default=42,
                        help='Random seed (default 42 — change to get different samples)')
    args = parser.parse_args()

    local_out = Path(args.output)
    local_out.mkdir(parents=True, exist_ok=True)
    local_tar = local_out / 'samples.tar.gz'

    print('=== Step 1: upload sampler script ===')
    run(['ssh', args.server, f'mkdir -p {REMOTE_TMP}'])
    run(['scp', str(SAMPLER), f'{args.server}:{REMOTE_SCRIPT}'])

    print('=== Step 2: run sampler on server ===')
    remote_cmd = (
        f'python3 {REMOTE_SCRIPT}'
        f' --records-root {args.records_root}'
        f' --output {REMOTE_OUTPUT}'
        f' --n-windows {args.n_windows}'
        f' --window-sec {args.window_sec}'
        f' --seed {args.seed}'
    )
    run(['ssh', args.server, remote_cmd])

    print('=== Step 3: fetch tar.gz ===')
    run(['scp', f'{args.server}:{REMOTE_TAR}', str(local_tar)])

    print('=== Step 4: unpack locally ===')
    with tarfile.open(local_tar) as tar:
        tar.extractall(local_out)
    local_tar.unlink()

    print('=== Step 5: clean up remote tmp ===')
    run(['ssh', args.server, f'rm -rf {REMOTE_TMP}'])

    print(f'\nSamples ready in: {local_out.resolve()}')


if __name__ == '__main__':
    main()
