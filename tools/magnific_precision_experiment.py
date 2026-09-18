#!/usr/bin/env python3
"""Prepare exact-grid Precision V2 inputs and assemble private 4x sprite candidates.

Cloud submission is deliberately separate: use Magnific MCP, preview the cost,
record every request/result, and never resubmit an ambiguous or pending job.
No ROM, extracted art, or provider output belongs in Git.
"""
import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageChops

from hd_sprite_pack import pack_registered


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def prepare(source, output, tight=False):
    if output.exists():
        raise ValueError('Use a new experiment directory; existing evidence is immutable')
    inventory = json.loads((source / 'inventory.json').read_text())
    priority = ['Idle', 'Walk', 'Run', 'Jump', 'Land', 'Turn']
    frames = sorted(inventory['frames'], key=lambda f: (
        priority.index(f['group']) if f['group'] in priority else len(priority),
        f['group'], f['number']))
    if len({f['key'] for f in frames}) != len(frames):
        raise ValueError('Duplicate source keys')
    # Read and verify the complete source set before emitting any output.
    images = {}
    for f in frames:
        im = Image.open(source / 'original-frames' / (f['name'] + '.png')).convert('RGBA')
        if im.size != (f['width'], f['height']):
            raise ValueError(f"Invalid dimensions: {f['name']}")
        if hashlib.sha256(im.tobytes('raw', 'BGRA')).hexdigest() != f['key']:
            raise ValueError(f"Invalid source raster: {f['name']}")
        if max(im.size) > (128 if tight else 80):
            raise ValueError('Sprite exceeds padded cell capacity')
        images[f['key']] = im
    for name in ('inputs', 'raw-results', 'frames', 'originals'):
        (output / name).mkdir(parents=True, exist_ok=True)
    layouts = []
    if tight:
        # Fixed, deterministic shelf packing; six untouched native pixels of
        # context on every side. AI never determines the output crop.
        current = []; x = y = row_height = 0
        for f in sorted(frames, key=lambda f: (-f['height'], -f['width'], f['name'])):
            width, height = f['width'] + 12, f['height'] + 12
            if x + width > 768:
                x = 0; y += row_height; row_height = 0
            if y + height > 768:
                layouts.append(current); current = []; x = y = row_height = 0
            current.append((f, x + 6, y + 6))
            x += width; row_height = max(row_height, height)
        if current:
            layouts.append(current)
    else:
        for start in range(0, len(frames), 64):
            layouts.append([(f, i % 8 * 96 + (96 - f['width']) // 2,
                             i // 8 * 96 + (96 - f['height']) // 2)
                            for i, f in enumerate(frames[start:start + 64])])
    batches = []
    for batch_index, layout in enumerate(layouts):
        name = f'plate-{batch_index + 1:02d}'
        canvas = Image.new('RGB', (768, 768), (80, 80, 80))
        rows = []
        for f, x, y in layout:
            im = images[f['key']]
            canvas.paste(im, (x, y), im.getchannel('A'))
            im.save(output / 'originals' / (f['name'] + '.png'))
            rows.append({**f, 'box': [x, y, im.width, im.height]})
        path = output / 'inputs' / (name + '.png')
        canvas.save(path)
        batches.append({'name': name, 'input': str(path.relative_to(output)),
                        'sha256': digest(path), 'frames': rows})
    manifest = {'schema': 1, 'rom_sha256': inventory['rom_sha256'],
                'inventory_sha256': digest(source / 'inventory.json'),
                'source': str(source.resolve()), 'frames': len(frames),
                'groups': sorted({f['group'] for f in frames}),
                'settings': {'mode': 'ultra-sublime', 'scale': '4x',
                             'sharpness': 7, 'grain': 0},
                'scope': inventory.get('scope', 'Donkey Kong character sprites'),
                'packing': 'shelf, 6px context' if tight else '96px cells',
                'alpha': 'bicubic source alpha; exact crop and anchor; no pose fitting',
                'batches': batches}
    save_json(output / 'manifest.json', manifest)
    print(f"Verified {len(frames)} unique source rasters; prepared {len(batches)} plates")


def assemble(root):
    manifest = json.loads((root / 'manifest.json').read_text())
    records = []
    for batch in manifest['batches']:
        source_path = root / batch['input']
        if digest(source_path) != batch['sha256']:
            raise ValueError('Input plate changed')
        raw = root / 'raw-results' / (batch['name'] + '.png')
        with Image.open(raw) as image:
            if image.size != (3072, 3072):
                raise ValueError(f'{raw}: expected exact 4x output, got {image.size}')
            result = image.convert('RGB')
        for f in batch['frames']:
            x, y, w, h = f['box']
            source = Image.open(root / 'originals' / (f['name'] + '.png')).convert('RGBA')
            if hashlib.sha256(source.tobytes('raw', 'BGRA')).hexdigest() != f['key']:
                raise ValueError('Original changed')
            art = result.crop((x * 4, y * 4, (x + w) * 4, (y + h) * 4)).convert('RGBA')
            alpha = source.getchannel('A').resize(art.size, Image.Resampling.BICUBIC)
            exact = source.getchannel('A').resize(art.size, Image.Resampling.NEAREST)
            original_mask = exact.point(lambda a: 255 if a >= 128 else 0, mode='1')
            smooth_mask = alpha.point(lambda a: 255 if a >= 128 else 0, mode='1')
            intersection = ImageChops.logical_and(original_mask, smooth_mask).histogram()[255]
            union = ImageChops.logical_or(original_mask, smooth_mask).histogram()[255]
            alpha_method = 'bicubic'
            if intersection / max(1, union) < .93:
                alpha = exact
                alpha_method = 'nearest to preserve sparse source silhouette'
            art.putalpha(alpha)
            path = root / 'frames' / (f['name'] + '.png')
            art.save(path)
            records.append({**f, 'path': str(path.relative_to(root)),
                            'alpha_method': alpha_method,
                            'plate': batch['name'], 'raw_sha256': digest(raw),
                            'sha256': digest(path)})
    if len(records) != manifest['frames'] or len({r['key'] for r in records}) != len(records):
        raise ValueError('Incomplete or duplicate registration')
    # An exact source raster must map to one exact result in either facing.
    # Some effects are symmetric; other source poses are exact mirror pairs.
    resolved = {}
    for f in records:
        source = Image.open(root / 'originals' / (f['name'] + '.png')).convert('RGBA')
        mirror_key = hashlib.sha256(source.transpose(Image.Transpose.FLIP_LEFT_RIGHT).tobytes('raw', 'BGRA')).hexdigest()
        path = root / f['path']; art = Image.open(path).convert('RGBA')
        if f['key'] in resolved:
            art = resolved[f['key']].copy()
            f['normalization'] = 'reuse exact source-facing equivalent'
        elif f['key'] == mirror_key:
            art = ImageChops.add(art, art.transpose(Image.Transpose.FLIP_LEFT_RIGHT), scale=2)
            f['normalization'] = 'horizontal symmetry required by identical original facing keys'
        art.save(path); f['sha256'] = digest(path)
        resolved[f['key']] = art
        resolved[mirror_key] = art.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
    save_json(root / 'registration.json', {'frames': records})
    report = pack_registered(root / 'registration.json', root / 'originals',
                             root / 'materials', set(manifest['groups']))
    save_json(root / 'coverage.json', {'source_frames': len(records),
                                     'groups': len(manifest['groups']),
                                     'facing_materials': report['materials_written'],
                                     'status': 'experimental candidates; full-game visual acceptance pending'})
    gallery(root, records)
    print(f"Assembled {len(records)} candidates and {report['materials_written']} facing materials")


def gallery(root, records):
    # Viewer plays source numeric pose order, not cartridge animation timing.
    data = json.dumps(records).replace('</', '<\\/')
    page = '''<!doctype html><meta charset="utf-8"><title>DKC · Precision V2 experiment</title>
<style>body{margin:0;background:#11150f;color:#eee;font:16px system-ui}main{max-width:1100px;margin:auto;padding:36px}h1{font-size:34px;margin-bottom:8px}p{color:#adb5a8}select,button,input{font:inherit;padding:8px;margin:8px 8px 8px 0}section{display:grid;grid-template-columns:1fr 1fr;gap:20px}.panel{background:#272b26;border-radius:16px;padding:20px}canvas{width:100%;height:auto;image-rendering:pixelated}label{display:inline-block}#status{font:14px monospace}</style>
<main><p>PRIVATE ASSET EXPERIMENT · 4×</p><h1>DKC sprites / Magnific Precision V2</h1>
<p>COUNT source rasters · GROUPS groups · exact source anchors · both facings. Sublime, sharpness 7, grain 0.</p>
<select id="group"></select><button id="play">Pause</button><button id="flip">Mirror</button>
<label>Pose rate <input id="fps" type="range" min="1" max="30" value="10"></label><input id="pose" type="range" min="0" value="0">
<section><div class="panel">Original · nearest 4×<canvas id="a" width="512" height="400"></canvas></div><div class="panel">Precision V2 · source alpha<canvas id="b" width="512" height="400"></canvas></div></section>
<p id="status"></p><p>Pose-order review only; cartridge holds, loops, and transitions are tested in the separate game preview. RGB is AI-upscaled. Alpha is interpolated from the original to preserve silhouettes. Existing runtime coverage is limited to Jungle Hijinxs.</p></main>
<script>const records=DATA,groups=[...new Set(records.map(r=>r.group))],g=document.querySelector('#group'),slider=document.querySelector('#pose');let bounds=[-64,-80,64,20],frames=[],idx=0,playing=true,mirror=false,last=0;const cache={};for(const name of groups)g.add(new Option(name,name));function getImage(prefix,r){const key=prefix+r.name;if(!cache[key]){let im=new Image();im.src=`${prefix}/${r.name}.png`;cache[key]=im;}return cache[key];}function choose(){frames=records.filter(r=>r.group===g.value).sort((a,b)=>a.number-b.number);bounds=[Math.min(...frames.map(r=>r.anchor[0])),Math.min(...frames.map(r=>r.anchor[1])),Math.max(...frames.map(r=>r.anchor[0]+r.width)),Math.max(...frames.map(r=>r.anchor[1]+r.height))];idx=0;slider.max=frames.length-1;draw()}function draw(){const r=frames[idx];if(!r)return;slider.value=idx;for(const [id,prefix,scale] of [['a','originals',4],['b','frames',1]]){const c=document.querySelector('#'+id),ctx=c.getContext('2d');c.width=Math.max(512,(bounds[2]-bounds[0]+16)*4);c.height=Math.max(400,(bounds[3]-bounds[1]+16)*4);ctx.clearRect(0,0,c.width,c.height);ctx.imageSmoothingEnabled=false;ctx.save();if(mirror){ctx.translate(c.width,0);ctx.scale(-1,1)}const im=getImage(prefix,r);if(im.complete&&im.naturalWidth)ctx.drawImage(im,(c.width-(bounds[2]-bounds[0])*4)/2+(r.anchor[0]-bounds[0])*4,(c.height-(bounds[3]-bounds[1])*4)/2+(r.anchor[1]-bounds[1])*4,im.width*scale,im.height*scale);ctx.restore()}document.querySelector('#status').textContent=`${r.name} · ${idx+1}/${frames.length} · ${r.width}×${r.height} → ${r.width*4}×${r.height*4} · anchor ${r.anchor.join(', ')}`;}g.onchange=choose;slider.oninput=()=>{idx=+slider.value;draw()};document.querySelector('#play').onclick=e=>{playing=!playing;e.target.textContent=playing?'Pause':'Play'};document.querySelector('#flip').onclick=()=>{mirror=!mirror;draw()};function tick(t){if(playing&&t-last>1000/+document.querySelector('#fps').value){idx=(idx+1)%frames.length;last=t}draw();requestAnimationFrame(tick)}g.value=groups.find(g=>g==='Walk'||g==='DK / Walk')||groups[0];choose();requestAnimationFrame(tick);</script>'''
    (root / 'index.html').write_text(page.replace('COUNT', str(len(records))).replace('GROUPS', str(len({r['group'] for r in records}))).replace('DATA', data))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['prepare', 'assemble'])
    parser.add_argument('output', type=Path)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--tight', action='store_true', help='Pack variable-size sprites with six-pixel context')
    args = parser.parse_args()
    if args.command == 'prepare':
        if not args.source:
            parser.error('prepare requires --source')
        prepare(args.source, args.output, args.tight)
    else:
        assemble(args.output)


if __name__ == '__main__':
    main()
