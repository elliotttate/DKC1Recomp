#!/usr/bin/env python3
"""Prepare private background patches with verified context; assemble MCP results.

Repeated exact 32x32 centers share one upscale from a canonical captured 48x48
neighborhood. Context-key aliases remain explicit. This avoids charging for the
same texture repeatedly as the cartridge rewrites neighboring ring-buffer cells.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

import numpy as np
from PIL import Image

from hd_sprite_pack import read_pam


def key(image):
    return hashlib.sha256(image.tobytes('raw', 'BGRA')).hexdigest()


def prepare(captures, output):
    if output.exists():
        raise ValueError('Use a new output directory')
    materials = {}
    for capture in captures:
        for path in sorted(capture.glob('*.json')):
            row = json.loads(path.read_text())
            if row.get('kind') == 'background':
                source = read_pam(path.with_suffix('.pam'))
                if source.size != (32, 32):
                    raise ValueError('Unexpected background dimensions')
                materials[row['key']] = source
    contexts = {}
    for capture in captures:
        for path in sorted(capture.glob('atlas-*.pam')):
            atlas = read_pam(path)
            wrapped = np.pad(np.asarray(atlas), ((8, 8), (8, 8), (0, 0)), mode='wrap')
            for y in range(0, atlas.height, 32):
                for x in range(0, atlas.width, 32):
                    context = Image.fromarray(wrapped[y:y + 48, x:x + 48])
                    identity = key(context)
                    if identity in materials and identity not in contexts:
                        if context.crop((8, 8, 40, 40)).tobytes() != materials[identity].tobytes():
                            raise ValueError('Context center differs from captured source')
                        contexts[identity] = (context, str(path), [x, y])
    if materials.keys() - contexts.keys():
        raise ValueError('Missing captured context')
    for directory in ('inputs', 'raw-results', 'originals', 'contexts', 'frames', 'materials'):
        (output / directory).mkdir(parents=True)
    unique = {}
    for identity, source in sorted(materials.items()):
        center_key = key(source)
        if center_key not in unique:
            context, atlas, at = contexts[identity]
            unique[center_key] = {'name': center_key, 'aliases': [], 'atlas': atlas,
                                  'atlas_xy': at, 'empty': source.getchannel('A').getbbox() is None}
            source.save(output / 'originals' / (center_key + '.png'))
            context.save(output / 'contexts' / (center_key + '.png'))
        unique[center_key]['aliases'].append(identity)
    nonempty = [row for row in unique.values() if not row['empty']]
    batches = []
    for start in range(0, len(nonempty), 256):
        rows = nonempty[start:start + 256]
        canvas = Image.new('RGB', (768, 768), (80, 80, 80))
        for i, row in enumerate(rows):
            context = Image.open(output / 'contexts' / (row['name'] + '.png'))
            x, y = i % 16 * 48, i // 16 * 48
            canvas.paste(context, (x, y), context.getchannel('A'))
            row['box'] = [x + 8, y + 8, 32, 32]
        name = f'plate-{len(batches) + 1:02d}'
        path = output / 'inputs' / (name + '.png'); canvas.save(path)
        batches.append({'name': name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'frames': rows})
    report = {'scope': 'Captured native and wide Jungle backgrounds; canonical context per identical center',
              'captured_keys': len(materials), 'unique_centers': len(unique),
              'empty_centers': [row for row in unique.values() if row['empty']],
              'batches': batches, 'alpha': 'nearest source alpha; exact tile boundaries',
              'settings': {'mode': 'ultra-sublime', 'scale': '4x', 'sharpness': 7, 'grain': 0}}
    (output / 'manifest.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: report[k] for k in ('captured_keys', 'unique_centers')}), 'plates', len(batches))


def assemble(root, edge_feather=0):
    manifest = json.loads((root / 'manifest.json').read_text())
    coverage = []
    def emit(row, image):
        source = Image.open(root / 'originals' / (row['name'] + '.png')).convert('RGBA')
        if key(source) != row['name']:
            raise ValueError('Original changed')
        if edge_feather:
            # The AI patches are independent. Fade only the outer native pixels
            # to the verified source colors so their borders do not form a grid.
            y, x = np.mgrid[:128, :128]
            distance = np.minimum.reduce([x, y, 127 - x, 127 - y])
            weight = Image.fromarray(np.uint8(np.clip(distance / (edge_feather * 4), 0, 1) * 255))
            image = Image.composite(image, source.resize((128, 128), Image.Resampling.BICUBIC), weight)
        image.putalpha(source.getchannel('A').resize((128, 128), Image.Resampling.NEAREST))
        image.save(root / 'frames' / (row['name'] + '.png'))
        data = b'DKHDv001' + struct.pack('<HH', 128, 128) + image.tobytes('raw', 'BGRA')
        for alias in row['aliases']:
            (root / 'materials' / (alias + '.dkhd')).write_bytes(data)
            coverage.append(alias)
    for batch in manifest['batches']:
        if hashlib.sha256((root / 'inputs' / (batch['name'] + '.png')).read_bytes()).hexdigest() != batch['sha256']:
            raise ValueError('Input changed')
        image = Image.open(root / 'raw-results' / (batch['name'] + '.png')).convert('RGBA')
        if image.size != (3072, 3072):
            raise ValueError('Expected exact 4x output')
        for row in batch['frames']:
            x, y, w, h = row['box']
            emit(row, image.crop((x * 4, y * 4, (x + w) * 4, (y + h) * 4)))
    for row in manifest['empty_centers']:
        emit(row, Image.new('RGBA', (128, 128)))
    if len(set(coverage)) != manifest['captured_keys']:
        raise ValueError('Incomplete material coverage')
    (root / 'coverage.json').write_text(json.dumps({'background_materials': len(coverage),
                                                  'edge_feather_native_pixels': edge_feather}, indent=2))
    print('Assembled', len(coverage), 'background materials')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['prepare', 'assemble'])
    parser.add_argument('output', type=Path)
    parser.add_argument('--captures', type=Path, nargs='+')
    parser.add_argument('--edge-feather', type=int, choices=range(0, 5), default=0,
                        help='Fade this many native boundary pixels to original colors')
    args = parser.parse_args()
    if args.command == 'prepare':
        if not args.captures:
            parser.error('prepare requires --captures')
        prepare(args.captures, args.output)
    else:
        assemble(args.output, args.edge_feather)
