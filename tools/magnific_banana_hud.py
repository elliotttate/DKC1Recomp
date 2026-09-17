#!/usr/bin/env python3
"""Build every normal banana HUD count from exact ROM layouts and HD primitives.

The normal HUD uses the global $86F2 palette, a 16x16 banana at x=16,
and 8x16 digit columns at x=32 onward. CODE_80A3D2..80A49A provides
the layout; captured source hashes independently verify this interpretation.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

import numpy as np
from PIL import Image

from extract_jungle_sprite_inventory import ROM_SHA, palette


def prepare(rom_path, output):
    rom = rom_path.read_bytes()
    if hashlib.sha256(rom).hexdigest() != ROM_SHA:
        raise ValueError('Unsupported ROM')
    if output.exists():
        raise ValueError('Use a new output directory')
    (output / 'original-frames').mkdir(parents=True)
    colors = palette(rom, 0x86F2)
    def tile(base, number):
        data = rom[base + number * 32:base + (number + 1) * 32]
        pixels = np.zeros((8, 8), np.uint8)
        for y in range(8):
            for x in range(8):
                pixels[y, x] = sum(((data[y * 2 + p % 2 + (p // 2) * 16] >> (7 - x)) & 1) << p for p in range(4))
        return Image.fromarray(colors[pixels])
    rows = []
    for family, count, base, width in [('BananaStatic', 8, 0x39EB65, 16), ('HUDDigit', 10, 0x39E765, 8)]:
        for n in range(count):
            image = Image.new('RGBA', (width, 16)); offset = n * 2 if width == 16 else n + 6
            for y in range(2):
                for x in range(width // 8):
                    image.paste(tile(base, offset + x + y * 16), (x * 8, y * 8))
            box = image.getbbox(); cropped = image.crop(box); name = f'{family}_{n}'
            image.save(output / (name + '-full.png'))
            cropped.save(output / 'original-frames' / (name + '.png'))
            rows.append({'name': name, 'group': family, 'number': n,
                         'key': hashlib.sha256(cropped.tobytes('raw', 'BGRA')).hexdigest(),
                         'width': cropped.width, 'height': cropped.height,
                         'anchor': list(box[:2]), 'full_size': list(image.size),
                         'box_full': list(box), 'rom_graphics': base})
    (output / 'inventory.json').write_text(json.dumps({'rom_sha256': ROM_SHA, 'frames': rows}, indent=2))


def assemble(source, pack, output):
    if output.exists():
        raise ValueError('Use a new output directory')
    (output / 'materials').mkdir(parents=True)
    (output / 'frames').mkdir(); (output / 'originals').mkdir()
    inventory = json.loads((source / 'inventory.json').read_text())
    primitives = {}
    for row in inventory['frames']:
        original = Image.open(source / 'original-frames' / (row['name'] + '.png')).convert('RGBA')
        if hashlib.sha256(original.tobytes('raw', 'BGRA')).hexdigest() != row['key']:
            raise ValueError('Original hash differs')
        data = (pack / (row['key'] + '.dkhd')).read_bytes()
        size = struct.unpack_from('<HH', data, 8)
        if data[:8] != b'DKHDv001' or size != tuple(d * 4 for d in original.size) or len(data) != 12 + size[0] * size[1] * 4:
            raise ValueError('Invalid HD primitive')
        image = Image.frombytes('RGBA', size, data[12:], 'raw', 'BGRA')
        canvas = Image.new('RGBA', tuple(d * 4 for d in row['full_size']))
        canvas.paste(image, tuple(d * 4 for d in row['box_full'][:2]))
        primitives[row['name']] = (Image.open(source / (row['name'] + '-full.png')).convert('RGBA'), canvas)
    records = []
    for phase in range(8):
        for count in range(100):
            digits = str(count); source_image = Image.new('RGBA', (16 + len(digits) * 8, 16))
            hd_image = Image.new('RGBA', (source_image.width * 4, 64))
            for dest, which, scale in [(source_image, 0, 1), (hd_image, 1, 4)]:
                dest.paste(primitives[f'BananaStatic_{phase}'][which], (0, 0))
                for i, digit in enumerate(digits):
                    dest.paste(primitives[f'HUDDigit_{digit}'][which], ((16 + i * 8) * scale, 0))
            box = source_image.getbbox(); source_image = source_image.crop(box)
            hd_image = hd_image.crop(tuple(d * 4 for d in box))
            identity = hashlib.sha256(source_image.tobytes('raw', 'BGRA')).hexdigest()
            name = f'BananaHUD_{count:02d}_phase{phase}'
            source_image.save(output / 'originals' / (name + '.png'))
            hd_image.save(output / 'frames' / (name + '.png'))
            (output / 'materials' / (identity + '.dkhd')).write_bytes(b'DKHDv001' + struct.pack('<HH', *hd_image.size) + hd_image.tobytes('raw', 'BGRA'))
            records.append({'name': name, 'group': f'Banana HUD {count:02d}', 'number': phase,
                            'key': identity, 'width': source_image.width, 'height': source_image.height,
                            'anchor': [0, -source_image.height], 'path': 'frames/' + name + '.png'})
    (output / 'registration.json').write_text(json.dumps({'frames': records}, indent=2))
    print('Assembled', len(records), 'normal HUD count/phase combinations')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['prepare', 'assemble'])
    parser.add_argument('source', type=Path); parser.add_argument('output', type=Path)
    parser.add_argument('--pack', type=Path)
    args = parser.parse_args()
    if args.command == 'prepare':
        prepare(args.source, args.output)
    else:
        if not args.pack:
            parser.error('assemble requires --pack')
        assemble(args.source, args.pack, args.output)
