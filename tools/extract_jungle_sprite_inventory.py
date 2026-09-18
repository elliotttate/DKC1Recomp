#!/usr/bin/env python3
"""Extract a private Jungle sprite-family superset from the checksum-locked ROM.

The external asset index supplies addresses/names only. Pixels and palettes
always come from the clean ROM. Family coverage is not runtime coverage.
"""
import argparse
import collections
import hashlib
import json
import re
import struct
from pathlib import Path

import numpy as np
from PIL import Image

ROM_SHA = 'fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15'

# Palette pointers are grounded in the source initializer scripts recorded in
# the experiment's source-index. Include full families to cover spawned states.
PALETTES = {
    'DonkeyKong': [0x84B8],  # active already processed separately
    'Diddy': [0x8422, 0x8440], 'DiddysHat': [0x8422, 0x8440],
    'DiddyStars': [0x8422, 0x8440], 'Rambi': [0x8404],
    'Gnawty': [0x8A1C], 'Kritter': [0x8512], 'Klump': [0x85C6],
    'Necky': [0x86D4], 'NeckyNut': [0x86D4], 'NeckyFeather': [0x86D4],
    'Barrel': [0x872E], 'DKBarrel': [0x872E],
    'BarrelPiece': [0x872E], 'SmallBarrelPiece': [0x872E],
    'CheckpointBarrel': [0x872E], 'CheckpointStars': [0x8F62],
    'BarrelCannon': [0x86F2], 'Tire': [0x881E], 'HalfTire': [0x881E],
    'SteelKeg': [0x8404], 'BananaBunch': [0x86F2],
    'MovingSingleBanana': [0x86F2], 'GoldenLetters': [0x86F2],
    'AnimalBuddyBox': [0x8710], 'AnimalBuddyToken': [0x89A4],
    'LifeBalloon': [0x8878, 0x8896, 0x88B4], 'HUDBalloon': [0x8878],
    'Butterfly': [0x894A], 'Sign': [0x8350],
    'GroundCover': [0x8226], 'BreakableWall': [0x8226], 'JunglePlant': [0x8244],
    'BurstEffect': [0x86F2], 'SmokePuff': [0x86F2], 'Sparkle': [0x86F2],
    'Explosion': [0x86F2],
}


def decode(rom, g, end):
    candidates = []
    for pc in range(1, 65):
        off = g - 8 - pc * 2
        big, small, ss, extra, es, d1, d2s, d2 = rom[off:off + 8]
        if big + small + extra != pc or big * 4 + small + extra != d1 + d2 or (d1 + d2) * 32 != end - g:
            continue
        xy = np.frombuffer(rom[off + 8:g], dtype=np.uint8).reshape(-1, 2).astype(int) - 128
        sizes = np.array([16] * big + [8] * (small + extra))
        lo = xy.min(axis=0); hi = (xy + sizes[:, None]).max(axis=0)
        w, h = hi - lo
        if min(w, h) < 1 or max(w, h) > 128:
            continue
        out = np.zeros((h, w), np.uint8); valid = True
        for i in range(pc - 1, -1, -1):
            for t in range(4 if i < big else 1):
                vt = ((i // 8) * 32 + (i % 8) * 2 + (t // 2) * 16 + t % 2
                      if i < big else (ss + i - big if i < big + small else es + i - big - small))
                st = vt if vt < d1 else d1 + vt - d2s if d2s <= vt < d2s + d2 else -1
                if st < 0:
                    valid = False; break
                src = rom[g + st * 32:g + (st + 1) * 32]
                x, y = xy[i] - lo + np.array([t % 2 * 8, t // 2 * 8])
                for yy in range(8):
                    for xx in range(8):
                        k = 7 - xx
                        v = sum(((src[yy * 2 + (p % 2) + (p // 2) * 16] >> k) & 1) << p for p in range(4))
                        if v:
                            out[y + yy, x + xx] = v
            if not valid:
                break
        if valid and out.any():
            ys, xs = np.where(out); a, b, c, d = int(xs.min()), int(ys.min()), int(xs.max() + 1), int(ys.max() + 1)
            candidates.append((off, out[b:d, a:c], [int(lo[0] + a), int(lo[1] + b)]))
    if len(candidates) != 1:
        raise ValueError(f'{g:06x}: {len(candidates)} possible headers')
    return candidates[0]


def palette(rom, address):
    result = np.zeros((16, 4), np.uint8)
    for i, c in enumerate(struct.unpack_from('<15H', rom, 0x3C0000 + address), 1):
        result[i] = [((c >> shift) & 31) * 8 + (((c >> shift) & 31) >> 2) for shift in (0, 5, 10)] + [255]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom', type=Path); parser.add_argument('index', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    rom = args.rom.read_bytes()
    if hashlib.sha256(rom).hexdigest() != ROM_SHA:
        raise ValueError('Unsupported ROM')
    if args.output.exists():
        raise ValueError('Use a new output directory')
    entries = re.findall(r'dl \$([A-F0-9]+),\$([A-F0-9]+),GFX_(\w+),', args.index.read_text())
    out = args.output / 'original-frames'; out.mkdir(parents=True)
    records = []; skipped = []; aliases = []; keys = set()
    for start, end, name in entries:
        family = name.split('_')[0]
        if family not in PALETTES:
            continue
        if family == 'GroundCover' and name != 'GroundCover_JungleLevel':
            continue
        if family == 'BreakableWall' and 'JungleWall' not in name:
            continue
        if family == 'AnimalBuddyBox' and name not in ('AnimalBuddyBox_Rambi', 'AnimalBuddyBox_Broken'):
            continue
        if family == 'AnimalBuddyToken' and name != 'AnimalBuddyToken_Expresso':
            continue
        if family == 'GoldenLetters' and name.rsplit('_', 1)[-1] not in 'KONG':
            continue
        g, end = int(start, 16) - 0xC00000, int(end, 16) - 0xC00000
        try:
            header, indices, anchor = decode(rom, g, end)
        except ValueError as error:
            skipped.append({'name': name, 'reason': str(error)}); continue
        for pal in ([0x86F2] if name.startswith('DKBarrel_Letters') else PALETTES[family]):
            image = Image.fromarray(palette(rom, pal)[indices])
            key = hashlib.sha256(image.tobytes('raw', 'BGRA')).hexdigest()
            variant = f'{name}_pal{pal:04x}'
            if key in keys:
                aliases.append({'name': variant, 'key': key}); continue
            keys.add(key); image.save(out / (variant + '.png'))
            suffix = re.search(r'(\d+)$', name)
            records.append({'name': variant, 'group': re.sub(r'\d+$', '', name) + f'_pal{pal:04x}',
                            'family': family, 'number': int(suffix[1]) if suffix else 0,
                            'header': header, 'graphics': g, 'graphics_end': end,
                            'palette_address': 0xFC0000 + pal, 'anchor': anchor,
                            'key': key, 'width': image.width, 'height': image.height})
    report = {'rom_sha256': ROM_SHA, 'asset_map': str(args.index.resolve()),
              'asset_map_sha256': hashlib.sha256(args.index.read_bytes()).hexdigest(),
              'scope': 'Jungle main-level sprite-family superset, including inactive Kongs and spawned effects',
              'frames': records, 'aliases': aliases, 'skipped': skipped,
              'counts': dict(collections.Counter(f['family'] for f in records))}
    (args.output / 'inventory.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'frames': len(records), 'counts': report['counts'], 'skipped': skipped}, indent=2))


if __name__ == '__main__':
    main()
