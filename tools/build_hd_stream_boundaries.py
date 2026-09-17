#!/usr/bin/env python3
"""Assemble only byte-exact 8x8 Nano subregions for observed mixed stream cells."""
from pathlib import Path
import hashlib,json,struct,sys
from functools import lru_cache
from PIL import Image
import numpy as np
from scipy.ndimage import distance_transform_edt
import argparse
parser=argparse.ArgumentParser(description='Fill captured streaming cells only with byte-exact registered Nano subregions.')
parser.add_argument('--work-dir',type=Path,required=True)
parser.add_argument('--pack',type=Path,required=True)
parser.add_argument('--captures',type=Path,nargs='+',required=True)
a=parser.parse_args();P=a.work_dir.resolve();D=P/'connected';N=P.parent;R=Path(__file__).resolve().parents[1]
from hd_sprite_pack import read_pam
from hd_background_contours import contour
pack=a.pack.resolve();library={}
def add(src,hd,label):
 a=np.asarray(src);b=np.asarray(hd)
 for y in range(0,src.height,8):
  for x in range(0,src.width,8):
   cell=a[y:y+8,x:x+8]
   if cell.shape!=(8,8,4):continue
   key=cell.tobytes()
   library.setdefault(key,(b[y*4:(y+8)*4,x*4:(x+8)*4].copy(),label,[x,y]))
add(Image.open(P/'world-original.png').convert('RGBA'),Image.open(D/'world.png').convert('RGBA'),'connected-world')
for l in [1,2]:
 add(read_pam(next((P/'map-source').glob(f'atlas-{l}-*.pam'))),Image.open(D/f'layer-{l}.png').convert('RGBA'),f'connected-layer-{l}')
# Previously accepted Nano contexts cover a few authored fragments beyond the
# main map. Keep the exact source identity attached to each reused subregion.
for src in sorted((N/'backgrounds/originals').glob('*.png')):
 hp=N/'background-fix/frames'/src.name
 if hp.exists():add(Image.open(src).convert('RGBA'),Image.open(hp).convert('RGBA'),'captured-'+src.stem)
rows={};unknown={};changed=[]
for capture in a.captures:
 for meta in capture.glob('*.json'):
  row=json.loads(meta.read_text())
  if row['kind']!='background':continue
  source=read_pam(meta.with_suffix('.pam'));raw=source.tobytes('raw','BGRA')
  if hashlib.sha256(b'DKCWv002'+raw).hexdigest()!=row['key']:continue
  rows[row['key']]=(source,meta)
centers=dict(line.split() for line in (pack/'background-centers.txt').read_text().splitlines()[1:]);report={'assembled':{},'unmatched':{}}
for key,(im,path) in rows.items():
 a=np.asarray(im);hd=np.zeros((128,128,4),np.uint8);provenance=[];missing=[]
 for y in range(0,32,8):
  for x in range(0,32,8):
   cell=a[y:y+8,x:x+8]
   if not cell[:,:,3].any():continue
   found=library.get(cell.tobytes())
   if found is None:missing.append([x,y]);continue
   hd[y*4:(y+8)*4,x*4:(x+8)*4]=found[0];provenance.append({'dst':[x,y],'source':found[1],'xy':found[2]})
 if missing:report['unmatched'][key]=missing;continue
 # Contour donor coverage may include its neighbor. Restore this exact cell's
 # reconstructed mask instead, retaining generated foreground RGB.
 alpha=contour(np.pad(a[:,:,3],4,mode='edge'))[16:-16,16:-16];old=hd[:,:,3]>0
 if old.any():
  inds=distance_transform_edt(~old,return_distances=False,return_indices=True);edge=(alpha>0)&~old;hd[edge,:3]=hd[inds[0][edge],inds[1][edge],:3]
 hd[:,:,3]=alpha;hd[alpha==0,:3]=0
 target=pack/(key+'.dkhd');target.unlink(missing_ok=True);target.write_bytes(b'DKHDv001'+struct.pack('<HH',128,128)+Image.fromarray(hd).tobytes('raw','BGRA'))
 centers[hashlib.sha256(im.tobytes('raw','BGRA')).hexdigest()]=key;report['assembled'][key]={'capture':str(path),'regions':provenance};changed.append(key)
(pack/'background-centers.txt').write_text(f'DKHCv001 {len(centers)}\n'+''.join(f'{a} {b}\n' for a,b in sorted(centers.items())))
(pack/'boundary-provenance.json').write_text(json.dumps(report,indent=2)+'\n');print('exact source tiles',len(library),'canonical centers',len(rows),'assembled',len(changed),'unmatched',len(report['unmatched']))
