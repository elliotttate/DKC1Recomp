#!/usr/bin/env python3
"""Validate a private DKHD level pack and index every raster for eager loading."""
import argparse
from pathlib import Path
import re
import struct


def build(pack: Path) -> tuple[int, int]:
    keys = []
    total = 0
    for path in sorted(pack.glob('*.dkhd')):
        if not re.fullmatch(r'[0-9a-f]{64}', path.stem):
            raise ValueError(f'Invalid material key: {path.name}')
        with path.open('rb') as stream:
            header = stream.read(12)
        if len(header) != 12 or header[:8] != b'DKHDv001':
            raise ValueError(f'Invalid material header: {path.name}')
        width, height = struct.unpack('<HH', header[8:])
        size = width * height * 4
        if not (0 < width <= 1368 and 0 < height <= 896) or width % 4 or height % 4:
            raise ValueError(f'Invalid material dimensions: {path.name}')
        if path.stat().st_size != 12 + size:
            raise ValueError(f'Truncated or oversized material: {path.name}')
        keys.append(path.stem)
        total += size
    if not 0 < len(keys) <= 65536:
        raise ValueError('Pack must contain 1 to 65536 materials')
    target = pack / 'preload.txt'
    temporary = pack / 'preload.txt.tmp'
    temporary.write_text(f'DKHPv001 {len(keys)}\n' + ''.join(key + '\n' for key in keys), encoding='ascii')
    temporary.replace(target)
    return len(keys), total


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pack', type=Path)
    args = parser.parse_args()
    count, size = build(args.pack)
    print(f'Indexed {count} materials, {size} resident pixel bytes ({size / 1024**2:.1f} MiB)')
