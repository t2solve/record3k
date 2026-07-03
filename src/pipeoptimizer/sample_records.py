#!/usr/bin/env python3
"""
Runs ON the remote server. Copy this script there, then run:

  python3 sample_records.py \
    --records-root /path/to/records \
    --output /tmp/pipeopt_samples \
    --n-windows 5

Strategy: for each record directory, pick N random 10-second windows
and copy every frame that falls inside. Also copies all JSON / txt
metadata from the record root and the other/ log files (not debug images).

Produces: <output>.tar.gz  — scp that back locally.
"""

import argparse
import random
import shutil
import tarfile
from pathlib import Path

WINDOW_NS = 10 * 1_000_000_000  # 10 seconds in nanoseconds


def ts_from_frame(name: str) -> int:
    # frame_000000_607664135409900.jpg  ->  607664135409900
    return int(Path(name).stem.rsplit('_', 1)[-1])


def find_content_dir(outer: Path) -> Path:
    """
    Handle the double-directory layout:
        file-rec-XXX/
            file-rec-XXX/    <- actual content lives here
    Falls back to outer itself if content is directly inside.
    """
    subdirs = [d for d in outer.iterdir() if d.is_dir()]
    if len(subdirs) == 1 and subdirs[0].name == outer.name:
        return subdirs[0]
    return outer


def sample_record(content_dir: Path, n_windows: int, output_dir: Path) -> int:
    frames_src = content_dir / 'frames'
    if not frames_src.exists():
        return 0

    # Build sorted list of (timestamp_ns, path)
    frames = []
    for f in frames_src.iterdir():
        if f.suffix in ('.jpg', '.png', '.tiff') and f.stem.startswith('frame_'):
            try:
                frames.append((ts_from_frame(f.name), f))
            except (ValueError, IndexError):
                pass
    if not frames:
        return 0
    frames.sort()

    t0, t1 = frames[0][0], frames[-1][0]

    if t1 - t0 <= WINDOW_NS:
        # Record shorter than one window — take everything
        windows = [(t0, t1)]
    else:
        max_start = t1 - WINDOW_NS
        starts = [random.randint(t0, max_start) for _ in range(n_windows)]
        windows = [(s, s + WINDOW_NS) for s in starts]

    frames_dst = output_dir / 'frames'
    frames_dst.mkdir(parents=True, exist_ok=True)

    seen: set[str] = set()
    copied = 0
    for ws, we in windows:
        for ts, f in frames:
            if ws <= ts <= we and f.name not in seen:
                shutil.copy2(f, frames_dst / f.name)
                seen.add(f.name)
                copied += 1

    # JSON + txt metadata files at the record root
    for f in content_dir.iterdir():
        if f.is_file() and f.suffix in ('.json', '.txt'):
            shutil.copy2(f, output_dir / f.name)

    # Log files from other/ — skip other/debug/ (too many images)
    other_src = content_dir / 'other'
    if other_src.exists():
        other_dst = output_dir / 'other'
        other_dst.mkdir(exist_ok=True)
        for f in other_src.iterdir():
            if f.is_file():
                shutil.copy2(f, other_dst / f.name)

    return copied


def main() -> None:
    parser = argparse.ArgumentParser(description='Sample frames from record directories on server')
    parser.add_argument('--records-root', required=True,
                        help='Directory containing the file-rec-* folders')
    parser.add_argument('--output', required=True,
                        help='Output directory (will be tar-gzipped afterwards)')
    parser.add_argument('--n-windows', type=int, default=5,
                        help='Number of 10-second windows to sample per record (default 5)')
    parser.add_argument('--window-sec', type=int, default=10,
                        help='Window duration in seconds (default 10)')
    parser.add_argument('--seed', type=int, default=42,
                        help='Random seed for reproducibility')
    args = parser.parse_args()

    global WINDOW_NS
    WINDOW_NS = args.window_sec * 1_000_000_000

    random.seed(args.seed)

    records_root = Path(args.records_root)
    output_root = Path(args.output)
    output_root.mkdir(parents=True, exist_ok=True)

    total_frames = 0
    record_count = 0

    candidates = sorted(p for p in records_root.iterdir()
                        if p.is_dir() and p.name.startswith('file-rec-'))
    print(f'Found {len(candidates)} record directories under {records_root}')

    for outer in candidates:
        content = find_content_dir(outer)
        rec_out = output_root / outer.name
        rec_out.mkdir(exist_ok=True)
        n = sample_record(content, args.n_windows, rec_out)
        status = f'{n} frames' if n > 0 else 'skipped (no frames found)'
        print(f'  {outer.name}: {status}')
        if n > 0:
            total_frames += n
            record_count += 1

    print(f'\nTotal: {record_count} records, {total_frames} frames')

    # Pack into tar.gz next to the output dir
    tar_path = str(output_root) + '.tar.gz'
    print(f'Packing -> {tar_path} ...')
    with tarfile.open(tar_path, 'w:gz') as tar:
        tar.add(output_root, arcname=output_root.name)
    print(f'Done: {tar_path}')


if __name__ == '__main__':
    main()
