#!/usr/bin/env python3
"""Private HD art pipeline: export PAM references, sheet, and validated RGBA pack."""
import argparse, hashlib, json, re, struct
from pathlib import Path
from PIL import Image, ImageDraw

def read_pam(path):
    header, data = path.read_bytes().split(b'ENDHDR\n', 1)
    fields = dict(line.split(maxsplit=1) for line in header.splitlines()[1:])
    return Image.frombytes('RGBA', (int(fields[b'WIDTH']), int(fields[b'HEIGHT'])), data)

def sheet(src, out):
    entries = sorted((json.loads(p.read_text()) for p in src.glob('*.json')), key=lambda e:(e['animation'],e['pose'],e['key']))
    cols=4; cell=256; rows=(len(entries)+cols-1)//cols
    canvas=Image.new('RGBA',(cols*cell,rows*cell),(0,0,0,0))
    for i,e in enumerate(entries):
        im=read_pam(src/(e['key']+'.pam'))
        im.save(src/(e['key']+'.png'))
        w,h=im.size
        scale=min(4,(cell-32)//max(w,h))
        im=im.resize((w*scale,h*scale),Image.Resampling.NEAREST)
        x=(i%cols)*cell+(cell-im.width)//2;y=(i//cols)*cell+(cell-im.height)//2
        canvas.alpha_composite(im,(x,y))
        e['cell']=[i%cols*cell,i//cols*cell,cell,cell]
        e['reference_box']=[x,y,im.width,im.height]
    canvas.save(out)
    out.with_suffix('.json').write_text(json.dumps(entries,indent=2)+'\n')
    print(f'{len(entries)} frames: {out}')

def pack(src, out):
    out.mkdir(parents=True,exist_ok=True)
    count=0
    for p in src.glob('*.png'):
        meta=p.with_suffix('.json')
        if not meta.exists():continue
        e=json.loads(meta.read_text())
        if not re.fullmatch(r'[0-9a-f]{64}', e['key']): raise ValueError(f'{meta}: invalid content key')
        if not 1 <= e['width'] <= 128 or not 1 <= e['height'] <= 128: raise ValueError(f'{meta}: invalid dimensions')
        im=Image.open(p).convert('RGBA')
        expected=(e['width']*4,e['height']*4)
        if im.size!=expected:raise ValueError(f'{p}: expected {expected}, got {im.size}')
        if not im.getchannel('A').getextrema()[0]<255:raise ValueError(f'{p}: alpha required')
        if not im.getbbox():raise ValueError(f'{p}: empty sprite')
        data=b'DKHDv001'+struct.pack('<HH',*im.size)+im.tobytes('raw','BGRA')
        (out/(e['key']+'.dkhd')).write_bytes(data);count+=1
    if not count: raise ValueError('No PNGs with matching source metadata')
    print(f'{count} frames packed in {out}')

def pack_registered(registration, originals, out, groups):
    """Build both facing keys from reviewed frames and byte-verified originals.

    The scene compositor hashes the raster after OAM flips. A canonical-only
    update leaves the opposite direction using an older material, or no art.
    Always derive its key from original pixels and mirror the selected HD
    texels exactly; never guess a pose from its name or nearest silhouette.
    """
    root = registration.parent
    records = json.loads(registration.read_text())['frames']
    selected = [r for r in records if r['group'] in groups]
    missing = set(groups) - {r['group'] for r in selected}
    if missing or not selected:
        raise ValueError(f'Unknown or empty groups: {sorted(missing)}')
    outputs, manifest = {}, []
    for r in selected:
        key = r['key']
        if not re.fullmatch(r'[0-9a-f]{64}', key):
            raise ValueError('Invalid original content key')
        if Path(r['name']).name != r['name']:
            raise ValueError('Invalid original frame name')
        original = Image.open(originals / (r['name'] + '.png')).convert('RGBA')
        if original.size != (r['width'], r['height']):
            raise ValueError(f"{r['name']}: original dimensions differ")
        if not all(1 <= d <= 128 for d in original.size):
            raise ValueError('Invalid original dimensions')
        if hashlib.sha256(original.tobytes('raw', 'BGRA')).hexdigest() != key:
            raise ValueError(f"{r['name']}: original content hash differs")
        path = (root / r['path']).resolve()
        if not path.is_relative_to(root.resolve()):
            raise ValueError('Candidate path escapes registration directory')
        candidate = Image.open(path).convert('RGBA')
        if candidate.size != tuple(d * 4 for d in original.size):
            raise ValueError(f"{r['name']}: expected 4x candidate")
        if not candidate.getbbox() or (candidate.getchannel('A').getextrema()[0] == 255
                                       and original.getchannel('A').getextrema()[0] < 255):
            raise ValueError(f"{r['name']}: nonempty transparent art required")
        header = b'DKHDv001' + struct.pack('<HH', *candidate.size)
        row = {'name': r['name'], 'group': r['group'], 'keys': {}}
        for facing in ('source', 'mirrored'):
            source = original if facing == 'source' else original.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
            art = candidate if facing == 'source' else candidate.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
            facing_key = hashlib.sha256(source.tobytes('raw', 'BGRA')).hexdigest()
            data = header + art.tobytes('raw', 'BGRA')
            if facing_key in outputs and outputs[facing_key] != data:
                raise ValueError(f"{r['name']}: conflicting art for identical original raster")
            outputs[facing_key] = data
            row['keys'][facing] = facing_key
        manifest.append(row)
    # Validate the complete selection before overwriting any destination files.
    out.mkdir(parents=True, exist_ok=True)
    for key, data in outputs.items():
        (out / (key + '.dkhd')).write_bytes(data)
    report = {'source_frames': len(selected), 'materials_written': len(outputs),
              'groups': sorted(groups), 'frames': manifest}
    (out / 'registered-directions.json').write_text(json.dumps(report, indent=2) + '\n')
    return report

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('command',choices=['sheet','pack','registered'])
    p.add_argument('source',type=Path);p.add_argument('output',type=Path)
    p.add_argument('--originals',type=Path)
    p.add_argument('--groups',nargs='+')
    a=p.parse_args()
    if a.command == 'registered':
        if not a.originals or not a.groups:p.error('registered requires --originals and --groups')
        report=pack_registered(a.source,a.originals,a.output,set(a.groups))
        print(f"{report['source_frames']} frames, {report['materials_written']} facing materials: {a.output}")
    else:(sheet if a.command=='sheet' else pack)(a.source,a.output)
if __name__=='__main__':main()
