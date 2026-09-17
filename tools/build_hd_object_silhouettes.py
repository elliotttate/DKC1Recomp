#!/usr/bin/env python3
"""Build a fail-closed object silhouette index from registered native rasters.

Live CGRAM animation and night fades change an object's BGRA hash while the
authored mask and size stay identical. The compositor looks up this exact
mask. The first registered key for a mask wins; later duplicates are skipped.
Background 32x32 centers stay isolated.
Optional object-bases.bin stores the registered native colors so HD art can
keep the live palette (night) after a silhouette hit.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from PIL import Image

SPRITE_GROUPS = {'families', 'dk', 'supplement'}


def silhouette_key_bytes(raw, width, height):
    if len(raw) != width * height * 4:
        raise ValueError('silhouette raster size differs')
    packed = bytearray(4 + (width * height + 7) // 8)
    packed[0] = width & 255
    packed[1] = (width >> 8) & 255
    packed[2] = height & 255
    packed[3] = (height >> 8) & 255
    for i in range(width * height):
        if struct.unpack_from('<I', raw, i * 4)[0]:
            packed[4 + i // 8] |= 1 << (i & 7)
    return hashlib.sha256(packed).hexdigest()


def silhouette_key(image):
    converted = image.convert('RGBA')
    return silhouette_key_bytes(converted.tobytes('raw', 'BGRA'), *converted.size)


def is_sprite(row, families):
    if row.get('kind') == 'background':
        return False
    if families:
        return row.get('family') in families
    group = (row.get('group') or '').split(' / ', 1)[0]
    return bool(row.get('family')) or group in SPRITE_GROUPS


def add_frame(rows, collisions, bases, name, image, key, pack):
    if not (1 <= image.width <= 128 and 1 <= image.height <= 128):
        raise ValueError(f'{name}: invalid dimensions')
    raw = image.tobytes('raw', 'BGRA')
    if hashlib.sha256(raw).hexdigest() != key:
        raise ValueError(f'{name}: original content hash differs')
    if pack and not (pack / f'{key}.dkhd').is_file():
        return False
    token = silhouette_key(image)
    previous = rows.get(token)
    if previous and previous != key:
        collisions.setdefault(token, {previous}).add(key)
        return False
    if token not in rows:
        rows[token] = key
        bases[key] = image
        return True
    return False


def merge_index(rows, collisions, bases, path, pack):
    if not path or not path.is_file():
        return 0
    lines = path.read_text().splitlines()
    if not lines:
        return 0
    header = lines[0].split()
    if len(header) != 2 or header[0] != 'DKHOs001':
        raise ValueError(f'{path}: invalid silhouette header')
    added = 0
    for line in lines[1:]:
        parts = line.split()
        if len(parts) != 2:
            continue
        token, key = parts
        if pack and not (pack / f'{key}.dkhd').is_file():
            continue
        previous = rows.get(token)
        if previous and previous != key:
            collisions.setdefault(token, {previous}).add(key)
            continue
        if token not in rows:
            rows[token] = key
            added += 1
    return added


def write_index(rows, output):
    lines = [f'DKHOs001 {len(rows)}\n']
    lines.extend(f'{token} {key}\n' for token, key in sorted(rows.items()))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(''.join(lines))


def write_bases(rows, bases, output):
    unique = {key: bases[key] for key in rows.values() if key in bases}
    items = sorted(unique.items())
    blob = bytearray(b'DKHDb001' + struct.pack('<I', len(items)))
    for key, image in items:
        converted = image.convert('RGBA')
        blob += key.encode() + struct.pack('<HH', *converted.size)
        blob += converted.tobytes('raw', 'BGRA')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(blob)
    return len(items)


def build(registration, originals, output, families=None, pack=None,
          merge=None, bases_output=None):
    records = json.loads(registration.read_text())['frames']
    selected = [r for r in records if is_sprite(r, families)]
    if not selected:
        raise ValueError('No registered frames for the requested families')
    rows = {}
    collisions = {}
    bases = {}
    added = []
    for row in selected:
        original = Image.open(originals / (row['name'] + '.png')).convert('RGBA')
        if original.size != (row['width'], row['height']):
            raise ValueError(f"{row['name']}: original dimensions differ")
        source_key = row['key']
        if add_frame(rows, collisions, bases, row['name'], original, source_key, pack):
            added.append({'name': row['name'], 'facing': 'source', 'key': source_key})
        mirror = original.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
        mirror_key = hashlib.sha256(mirror.tobytes('raw', 'BGRA')).hexdigest()
        if add_frame(rows, collisions, bases, row['name'] + ':mirror', mirror, mirror_key, pack):
            added.append({'name': row['name'], 'facing': 'mirrored', 'key': mirror_key})
    merged = merge_index(rows, collisions, bases, merge, pack)
    if not rows:
        raise ValueError('No object silhouettes were accepted')
    write_index(rows, output)
    base_count = 0
    if bases_output:
        base_count = write_bases(rows, bases, bases_output)
    return {
        'entries': len(rows),
        'frames': added,
        'merged': merged,
        'collisions': len(collisions),
        'bases': base_count,
        'output': str(output),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--registration', type=Path, required=True)
    parser.add_argument('--originals', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--pack', type=Path, help='Require each key to exist in this pack')
    parser.add_argument('--families', nargs='+', help='Limit to these families; default is all sprites')
    parser.add_argument('--merge', type=Path, help='Keep extra tokens from an existing index')
    parser.add_argument('--bases', type=Path, help='Write native color bases (DKHDb001)')
    args = parser.parse_args()
    families = set(args.families) if args.families else None
    bases = args.bases or (args.output.parent / 'object-bases.bin')
    print(json.dumps(build(args.registration, args.originals, args.output,
                           families, args.pack, args.merge, bases), indent=2))


if __name__ == '__main__':
    main()
