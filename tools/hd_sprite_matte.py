#!/usr/bin/env python3
"""Remove generation-matte contamination from registered private HD sprites.

This is an offline, source-constrained color/alpha operation, not a global gray
color key. The source's gray paint and the interior are immutable. Ambiguous
edges stay intact. No sprite is cropped, translated, rescaled, or regenerated.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct

import numpy as np
from PIL import Image
from scipy import ndimage as ndi

from build_hd_preload_manifest import build as build_preload


def silhouette(source, alpha, scale=4):
    a, b = source, alpha >= .5
    if not a.any() or not b.any():
        return {'pass': False}
    ae, be = a ^ ndi.binary_erosion(a), b ^ ndi.binary_erosion(b)
    distances = np.concatenate((ndi.distance_transform_edt(~ae)[be],
                                ndi.distance_transform_edt(~be)[ae])) / scale
    def bounds(mask):
        y, x = np.nonzero(mask)
        return np.array([x.min(), y.min(), x.max() + 1, y.max() + 1]) / scale
    iou = float(np.count_nonzero(a & b) / np.count_nonzero(a | b))
    p95 = float(np.quantile(distances, .95))
    error = float(np.max(np.abs(bounds(a) - bounds(b))))
    return {'pass': iou >= .93 and p95 <= .75 and error <= .5,
            'iou': iou, 'contour_p95_native': p95, 'bounds_error_native': error}


def clean_matte(image, source):
    """Unmix the sampled sheet matte only within two native pixels of an edge."""
    image, source = image.convert('RGBA'), source.convert('RGBA')
    if image.size != (source.width * 4, source.height * 4):
        raise ValueError('Expected exact 4x registered canvas')
    a = np.array(image).copy()
    rgb, alpha = a[:, :, :3].astype(np.float64), a[:, :, 3] / 255.0
    orig = np.asarray(source.resize(image.size, Image.Resampling.NEAREST))
    inside = orig[:, :, 3] >= 128
    if not inside.any():
        return image.copy(), {'changed_texels': 0, 'reason': 'empty source'}
    distance = ndi.distance_transform_edt(np.pad(inside, 1))[1:-1, 1:-1]
    # Nano varies the supplied #505050 slightly. Sample only known exterior
    # pixels near that explicit matte, excluding generated foreground spill.
    exterior = (~inside) & (np.max(np.abs(rgb - 80), axis=2) < 24)
    matte = np.median(rgb[exterior], axis=0) if exterior.sum() > 16 else np.full(3, 80.)
    color_distance = np.linalg.norm(rgb - matte, axis=2)
    _, indices = ndi.distance_transform_edt(~inside, return_indices=True)
    source_color = orig[:, :, :3][tuple(indices)].astype(np.float64)
    gray = ((np.ptp(source_color, axis=2) < 24)
            & (np.mean(source_color, axis=2) > 24)
            & (np.mean(source_color, axis=2) < 220))
    eligible = (distance <= 8) & ~gray & (alpha > 0)
    trusted = (color_distance >= 60) & (alpha >= .9)
    if not trusted.any():
        return image.copy(), {'changed_texels': 0, 'reason': 'no confident foreground'}
    near_distance, near_indices = ndi.distance_transform_edt(~trusted, return_indices=True)
    foreground = rgb[tuple(near_indices)]
    direction, delta = foreground - matte, rgb - matte
    fraction = np.clip(np.sum(delta * direction, axis=2)
                       / np.maximum(np.sum(direction * direction, axis=2), 1), 0, 1)
    residual = np.linalg.norm(delta - fraction[:, :, None] * direction, axis=2)
    use = eligible & (near_distance <= 10) & (fraction < .98) & (residual < 18)
    remove = eligible & (color_distance < 14) & (near_distance <= 10)
    fraction = np.where(remove, 0, fraction)
    use |= remove
    proposed = np.where(use, np.minimum(alpha, fraction), alpha)
    # A provider can move the contour inward. Bound any alpha adjustment by the
    # existing geometry contract; retain the source contour and extend clean
    # edge color where further trimming would lose fingers/tails/pose bounds.
    alpha_weight = 1.0
    for weight in (1., .75, .5, .25, 0.):
        candidate_alpha = alpha + weight * (proposed - alpha)
        geometry = silhouette(inside, candidate_alpha)
        if geometry['pass']:
            alpha_weight = weight
            break
    else:
        candidate_alpha, alpha_weight = alpha, 0.
        geometry = silhouette(inside, alpha)
    recovered = (rgb - (1 - fraction[:, :, None]) * matte) / np.maximum(fraction[:, :, None], .15)
    new_rgb = np.where((use & (fraction > .15))[:, :, None], np.clip(recovered, 0, 255), rgb)
    new_rgb = np.where((use & (fraction <= .15))[:, :, None], foreground, new_rgb)
    a[:, :, :3] = np.rint(new_rgb).astype(np.uint8)
    a[:, :, 3] = np.rint(candidate_alpha * 255).astype(np.uint8)
    # Byte checks enforce the semantic veto and preserve unassociated RGB.
    before = np.asarray(image)
    assert np.array_equal(a[gray | (distance > 8)], before[gray | (distance > 8)])
    assert np.all(a[:, :, 3] <= before[:, :, 3])
    return Image.fromarray(a), {
        'matte_rgb': matte.tolist(), 'changed_texels': int(np.any(a != before, axis=2).sum()),
        'matte_texels': int(remove.sum()), 'gray_protected_texels': int((gray & (alpha > 0)).sum()),
        'alpha_weight': alpha_weight, 'silhouette': geometry,
    }


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_material(path):
    data = path.read_bytes()
    if data[:8] != b'DKHDv001':
        raise ValueError(f'Invalid material: {path}')
    size = struct.unpack('<HH', data[8:12])
    if len(data) != 12 + size[0] * size[1] * 4:
        raise ValueError(f'Invalid material length: {path}')
    return Image.frombytes('RGBA', size, data[12:], 'raw', 'BGRA')


def material_bytes(image):
    return b'DKHDv001' + struct.pack('<HH', *image.size) + image.tobytes('raw', 'BGRA')


def build(source, pack, output):
    if output.exists():
        raise ValueError('Output must be new; never modify a live/preloaded pack')
    rows = json.loads((source / 'registration.json').read_text())['frames']
    provenance = json.loads((source / 'provenance.json').read_text())
    output.mkdir(parents=True)
    materials, frames = output / 'materials', output / 'frames'
    materials.mkdir(); frames.mkdir()
    for path in pack.iterdir():
        if not path.is_file():
            continue
        target = materials / path.name
        if path.suffix == '.dkhd':
            os.link(path, target)
        else:
            shutil.copy2(path, target)
    handled, report, changed = set(), [], 0
    for row in rows:
        key = row['key']
        if key in handled or 'background' in provenance.get(key, ''):
            continue
        original = Image.open(source / 'originals' / (row['name'] + '.png')).convert('RGBA')
        if digest(original.tobytes('raw', 'BGRA')) != key:
            raise ValueError(f"Source identity differs: {row['name']}")
        mirror_key = digest(original.transpose(Image.Transpose.FLIP_LEFT_RIGHT).tobytes('raw', 'BGRA'))
        art = read_material(pack / (key + '.dkhd'))
        cleaned, stats = clean_matte(art, original)
        # Identical original-facing keys require identical symmetric HD pixels.
        if key == mirror_key:
            pixels = np.array(cleaned).astype(np.uint16)
            cleaned = Image.fromarray(((pixels + pixels[:, ::-1]) // 2).astype(np.uint8))
        cleaned.save(frames / (row['name'] + '.png'))
        entry = {'name': row['name'], 'group': row['group'], 'key': key,
                 'mirror_key': mirror_key, 'source_sha256': digest(original.tobytes()),
                 'before_sha256': digest(material_bytes(art)), **stats}
        for facing_key, image in ((key, cleaned), (mirror_key, cleaned.transpose(Image.Transpose.FLIP_LEFT_RIGHT))):
            if facing_key in handled:
                continue
            target = materials / (facing_key + '.dkhd')
            if not target.exists():
                if provenance.get(key) == 'composed-nano-hud' and facing_key == mirror_key:
                    continue  # The counter only registers its authored direction.
                raise ValueError(f'Missing registered facing {facing_key}')
            data = material_bytes(image)
            if data != target.read_bytes():
                target.unlink()  # Break hardlink before changing this private copy.
                target.write_bytes(data)
                changed += 1
            handled.add(facing_key)
        entry['after_sha256'] = digest(material_bytes(cleaned))
        report.append(entry)
    count, size = build_preload(materials)
    result = {'schema': 1, 'source': str(source.resolve()), 'baseline_pack': str(pack.resolve()),
              'materials': count, 'resident_pixel_bytes': size, 'registered_sources': len(report),
              'facing_materials': len(handled), 'changed_materials': changed,
              'silhouette_failures': [r['name'] for r in report if not r.get('silhouette', {}).get('pass', True)],
              'frames': report}
    (output / 'matte-report.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'frames'}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='Combined registered source directory')
    parser.add_argument('--pack', type=Path, required=True, help='Immutable baseline pack')
    parser.add_argument('--output', type=Path, required=True, help='New private result directory')
    args = parser.parse_args()
    build(args.source, args.pack, args.output)
