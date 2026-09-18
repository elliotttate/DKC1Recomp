#!/usr/bin/env python3
"""Measure displayed BG translation from exact pixel correspondences.

Use a framegen dump and an independent same-frame layer_capture directory.
The layer masks seed visible, textured pixels; matching then follows only
surviving exact correspondences. This measures spatial steps, not scanout
timing. Insufficient or ambiguous texture is reported instead of guessed.
Requires NumPy and Pillow. No runtime or save-state writes.
"""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


def read_image(path):
    return np.asarray(Image.open(path).convert('RGB'))


def match(a, b, ys, xs, radius=6):
    height, width = a.shape[:2]
    safe = (xs >= radius) & (xs < width-radius) & (ys >= 2) & (ys < height-2)
    ys, xs = ys[safe], xs[safe]
    if len(xs) < 64:
        return {'accepted': False, 'reason': 'insufficient texture', 'points': len(xs)}, ys, xs
    values = a[ys, xs]
    scores = []
    for dy in range(-2, 3):
        for dx in range(-radius, radius+1):
            equal = np.all(values == b[ys+dy, xs+dx], axis=1)
            scores.append((int(equal.sum()), dx, dy))
    scores.sort(reverse=True)
    score, dx, dy = scores[0]
    fraction = score / len(xs)
    accepted = fraction >= .85 and scores[1][0] < score * .95
    result = {'accepted': accepted, 'points': len(xs), 'matched': score,
              'fraction': fraction, 'runner_up': scores[1][0]}
    if not accepted:
        result['reason'] = 'ambiguous or changed artwork'
        return result, ys, xs
    result.update(dx=dx, dy=dy)
    keep = np.all(values == b[ys+dy, xs+dx], axis=1)
    return result, ys[keep]+dy, xs[keep]+dx


def analyze(capture, layers, start, count, cadence=120):
    metadata = [json.loads(p.read_text()) for p in sorted(capture.glob('frame*.json'))]
    source = {m['pose_source_frame']: m['host_frame'] for m in metadata
              if m.get('pose_source_frame', 0) > 0}
    images = []
    for frame in range(start, start+count):
        host = source[frame]
        phases = [('display', 0), ('mid', .5)] if cadence == 120 else [('display', 0)]
        for kind, phase in phases:
            path = capture / f'frame{host:06}_{kind}.ppm'
            images.append((frame+phase, read_image(path), path.name))
    isolated = [read_image(layers / f'bg{i}.ppm') for i in (1, 2, 3)]
    backdrop = read_image(layers / 'backdrop.ppm')
    composite = read_image(layers / 'composite.ppm')
    result = {'capture': str(capture), 'layers': str(layers), 'source_start': start,
              'native_frames': count, 'sample_cadence': cadence,
              'units': 'source pixels per presentation',
              'physical_scanout_measured': False, 'layers_measured': {}}
    for index, plane in enumerate(isolated):
        occupied = np.any(plane != backdrop, axis=2)
        visible = np.all(plane == composite, axis=2)
        textured = np.any(plane != np.roll(plane, 1, axis=1), axis=2)
        # Exclude sprite pixels and capture/keyframe differences.
        visible &= np.all(plane == images[0][1], axis=2)
        ys, xs = np.where(occupied & visible & textured)
        records = []
        for (ta, a, pa), (tb, b, pb) in zip(images, images[1:]):
            step, ys, xs = match(a, b, ys, xs)
            step.update(source_from=ta, source_to=tb, image_from=pa, image_to=pb)
            records.append(step)
            if not step['accepted']:
                break
        accepted = [r for r in records if r['accepted']]
        result['layers_measured'][f'BG{index+1}'] = {
            'steps': records, 'accepted_steps': len(accepted),
            'horizontal_steps': [r['dx'] for r in accepted],
            'stationary_steps': sum(r['dx'] == 0 for r in accepted)}
    return result


def fractional_fit(reference, image, ys, xs, guess=0, radius=4):
    """Fit actual output pixels to a translated, bilinear native-layer image.

    The candidate motion comes from pixels only, never the runtime motion log.
    Sixteenth-pixel candidates distinguish the bounded smoothing phases. A half
    channel-value tolerance allows only final 8-bit rounding, not a loose
    optical-flow similarity score.
    """
    h, w = reference.shape[:2]
    safe = (xs > radius+abs(guess)+2) & (xs < w-radius-abs(guess)-2) & (ys > 2) & (ys < h-3)
    yy, xx = ys[safe], xs[safe]
    if len(xx) < 64:
        return {'accepted': False, 'reason': 'insufficient texture', 'points': len(xx)}
    # Bound cost without biasing the candidate motion or choosing a patch by eye.
    stride = max(1, len(xx)//6000)
    yy, xx = yy[::stride], xx[::stride]
    source = reference.astype(np.float32)
    scores = []
    for dy in range(-1, 2):
        for tick in range(round(16*(guess-radius)), round(16*(guess+radius))+1):
            dx = tick/16
            integer = int(np.floor(dx)); phase = dx-integer
            expected = source[yy, xx]*(1-phase) + source[yy, xx-1]*phase
            observed = image[yy+dy, xx+integer].astype(np.float32)
            errors = np.max(np.abs(expected-observed),axis=1)
            count = int(np.count_nonzero(errors <= .51))
            scores.append((count, -float(np.mean(errors[errors <= .51])) if count else -255., dx, dy))
    scores.sort(reverse=True)
    best = scores[0]
    # Adjacent subpixel hypotheses can differ by less than final RGB rounding;
    # ambiguity requires a genuinely distinct translation, at least 1/4 pixel.
    runner = next(s for s in scores[1:] if abs(s[2]-best[2])>=.25 or s[3]!=best[3])
    fraction = best[0]/len(xx)
    accepted = fraction >= .85 and runner[0] < .95*best[0]
    result = {'accepted': accepted, 'points': len(xx), 'matched': best[0],
              'fraction': fraction, 'runner_up': runner[0], 'mean_inlier_error': -best[1]}
    if accepted:result.update(dx=best[2],dy=best[3])
    else:result['reason']='ambiguous or changed artwork'
    return result


def analyze_fractional(capture, layers, start, count, cadence=120):
    metadata = [json.loads(p.read_text()) for p in sorted(capture.glob('frame*.json'))]
    source = {m['pose_source_frame']: m['host_frame'] for m in metadata if m.get('pose_source_frame',0)>0}
    composite, backdrop = [read_image(layers/f'{n}.ppm') for n in ['composite','backdrop']]
    result = {'capture':str(capture),'layers':str(layers),'source_start':start,
              'native_frames':count,'sample_cadence':cadence,'method':'bilinear native-layer registration',
              'units':'source pixels per presentation','physical_scanout_measured':False,'layers_measured':{}}
    for index in (1,2,3):
        plane = read_image(layers/f'bg{index}.ppm')
        native = read_image(capture/f'frame{start:06}_cur.ppm')
        mask = np.any(plane != backdrop,axis=2) & np.all(plane==composite,axis=2) & np.all(plane==native,axis=2)
        # Require neighboring texels to belong to this visible native plane;
        # blends with another parallax plane do not define a single translation.
        core = mask.copy()
        for dy in range(-1,2):
            for dx in range(-2,3):core &= np.roll(mask,(dy,dx),(0,1))
        gradient = np.max(np.abs(plane.astype(np.int16)-np.roll(plane,1,axis=1)),axis=2)>12
        ys,xs = np.where(core & gradient)
        records=[]; guess=0; previous=None
        for frame in range(start,start+count):
            for kind,phase in ([('display',0),('mid',.5)] if cadence==120 else [('display',0)]):
                path=capture/f'frame{source[frame]:06}_{kind}.ppm'
                image=read_image(path)
                fit=fractional_fit(plane,image,ys,xs,guess)
                fit.update(source=frame+phase,image=path.name)
                records.append(fit)
                if not fit['accepted']:break
                guess=fit['dx']
                if previous is not None:fit['step']=guess-previous
                previous=guess
                integer=int(np.floor(guess)); phase=guess-integer; dy=fit['dy']
                h,w=plane.shape[:2]
                safe=(xs>0)&(xs+integer>=0)&(xs+integer<w)&(ys+dy>=0)&(ys+dy<h)
                ys,xs=ys[safe],xs[safe]
                expected=plane[ys,xs].astype(np.float32)*(1-phase)+plane[ys,xs-1].astype(np.float32)*phase
                keep=np.max(np.abs(expected-image[ys+dy,xs+integer]),axis=1)<=.51
                ys,xs=ys[keep],xs[keep]
            if not records[-1]['accepted']:break
        steps=[r['step'] for r in records if 'step' in r]
        result['layers_measured'][f'BG{index}']={'fits':records,'horizontal_steps':steps,
            'accepted_steps':len(steps),'stationary_steps':sum(x==0 for x in steps)}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--layers', type=Path, required=True)
    parser.add_argument('--start', type=int, required=True)
    parser.add_argument('--count', type=int, default=8)
    parser.add_argument('--cadence', type=int, choices=(60, 120), default=120)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fractional',action='store_true',help='fit bilinear subpixel motion against native isolated layers')
    args = parser.parse_args()
    method = analyze_fractional if args.fractional else analyze
    result = method(args.capture, args.layers, args.start, args.count, args.cadence)
    args.output.write_text(json.dumps(result, indent=2)+'\n')
    for layer, data in result['layers_measured'].items():
        print(layer, data['horizontal_steps'], 'accepted:', data['accepted_steps'])


if __name__ == '__main__':
    main()
