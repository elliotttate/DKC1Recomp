#!/usr/bin/env python3
"""Measure replacement-sprite geometry without modifying either input image.

Canvas coordinates are authoritative: this never auto-crops or auto-centers an
image to hide a displaced pose. Landmarks are manually identified source/native
pixel coordinates and candidate/image pixel coordinates, respectively.
"""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.ndimage import binary_erosion, distance_transform_edt


def check(source, candidate, landmarks=None):
    src = Image.open(source).convert('RGBA')
    dst = Image.open(candidate).convert('RGBA')
    sx, sy = dst.width / src.width, dst.height / src.height
    if abs(sx - sy) > 1e-6:
        raise ValueError('Canvas aspect differs; do not stretch or auto-crop the pose')
    if sx < 4:
        raise ValueError('Candidate must contain at least four texels per native pixel')
    if min(dst.getchannel('A').getextrema()) == 255:
        raise ValueError('Candidate has no transparent background')
    a = np.asarray(src.getchannel('A').resize(dst.size, Image.Resampling.NEAREST)) >= 128
    b = np.asarray(dst.getchannel('A')) >= 128
    if not a.any() or not b.any():
        raise ValueError('Source and candidate must both have a visible silhouette')
    ae = a ^ binary_erosion(a)
    be = b ^ binary_erosion(b)
    distances = np.concatenate((distance_transform_edt(~ae)[be],
                                distance_transform_edt(~be)[ae])) / sx
    iou = float(np.count_nonzero(a & b) / np.count_nonzero(a | b))

    def bounds(mask):
        y, x = np.nonzero(mask)
        return [float(x.min()/sx), float(y.min()/sy),
                float((x.max()+1)/sx), float((y.max()+1)/sy)]

    ab, bb = bounds(a), bounds(b)
    bound_error = max(abs(x-y) for x, y in zip(ab, bb))
    landmark_results = []
    for item in landmarks or []:
        source_point = np.asarray(item['source'], dtype=float)
        candidate_point = np.asarray(item['candidate'], dtype=float) / [sx, sy]
        if source_point.shape != (2,) or candidate_point.shape != (2,):
            raise ValueError('Each landmark must contain two source and candidate coordinates')
        if not np.all(np.isfinite(np.r_[source_point, candidate_point])):
            raise ValueError('Landmarks must be finite')
        error = float(np.linalg.norm(source_point-candidate_point))
        limit = float(item.get('tolerance_native_pixels', .5))
        if not np.isfinite(limit) or not 0 <= limit <= 1:
            raise ValueError('Landmark tolerance must be between zero and one native pixel')
        landmark_results.append({'name': item['name'], 'error_native_pixels': error,
                                 'tolerance_native_pixels': limit, 'pass': error <= limit})
    contour_p95 = float(np.quantile(distances, .95))
    silhouette_pass = iou >= .93 and contour_p95 <= .75 and bound_error <= .5
    return {
        'source': str(source), 'candidate': str(candidate),
        'source_size': list(src.size), 'candidate_size': list(dst.size),
        'silhouette_iou': iou, 'contour_p95_native_pixels': contour_p95,
        'contour_max_native_pixels': float(distances.max()),
        'source_bounds_native': ab, 'candidate_bounds_native': bb,
        'maximum_bounds_error_native_pixels': bound_error,
        'silhouette_pass': silhouette_pass, 'landmarks': landmark_results,
        'landmark_review_complete': bool(landmark_results),
        'geometry_pass': silhouette_pass and bool(landmark_results) and
                         all(x['pass'] for x in landmark_results),
        'art_quality_requires_visual_review': True,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('--landmarks', type=Path,
                        help='JSON array of named source and candidate pixel positions')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    marks = json.loads(args.landmarks.read_text()) if args.landmarks else None
    result = check(args.source, args.candidate, marks)
    output = json.dumps(result, indent=2)+'\n'
    if args.output:
        args.output.write_text(output)
    print(output, end='')
    return 0 if result['geometry_pass'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
