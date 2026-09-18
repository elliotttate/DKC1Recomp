#!/usr/bin/env python3
from pathlib import Path
from functools import lru_cache
import hashlib,json,os,struct,sys
import numpy as np
from PIL import Image
from scipy.ndimage import distance_transform_edt,gaussian_filter
import argparse
parser=argparse.ArgumentParser(description='Assemble registered connected Nano scenery from a private work directory.')
parser.add_argument('--work-dir',type=Path,required=True)
a=parser.parse_args();P=a.work_dir.resolve();D=P/'connected';N=P.parent;R=Path(__file__).resolve().parents[1]
from hd_sprite_pack import read_pam
from hd_background_contours import contour
pack=D/'materials';pack.mkdir(exist_ok=True)
for src in (N/'background-fix/materials').glob('*.dkhd'):
 dst=pack/src.name
 if not dst.exists():os.link(src,dst)
manifest=json.loads((D/'manifest.json').read_text())

def normalize(row):
 return np.asarray(Image.open(D/'outputs'/(row['name']+'.png')).convert('RGB').resize((row['size']*4,row['size']*4),Image.Resampling.LANCZOS),dtype=np.float32)

def finish(rgb,source,periodic=False):
 # Exact source contour is reconstructed once for the connected surface.
 mask=np.pad(source[:,:,3],4,mode='wrap' if periodic else 'edge');alpha=contour(mask)[16:-16,16:-16];old=np.repeat(np.repeat(source[:,:,3],4,0),4,1)>0
 trustworthy=old & (np.max(np.abs(rgb-80),axis=2)>18)
 if trustworthy.any():
  inds=distance_transform_edt(~trustworthy,return_distances=False,return_indices=True)
  edge=(alpha>0)&(~trustworthy);rgb[edge]=rgb[inds[0][edge],inds[1][edge]]
 rgba=np.dstack((np.clip(np.rint(rgb),0,255).astype(np.uint8),alpha));rgba[alpha==0,:3]=0
 return rgba

world=np.asarray(Image.open(P/'world-original.png').convert('RGBA'));h,w=world.shape[:2]
# Stitch the 128-native-pixel common regions, not individual runtime cells.
rgb=np.zeros((h*4,w*4,3),np.float32);weights=np.zeros((1,w*4,1),np.float32)
for row in manifest[:7]:
 im=normalize(row)[192*4:(192+h)*4];x=row['x'];start=max(0,x);end=min(w,x+896);im=im[:,(start-x)*4:(end-x)*4]
 weight=np.clip(np.minimum(np.arange(896*4)+.5,896*4-.5-np.arange(896*4))/(128*4),0,1)[(start-x)*4:(end-x)*4].astype(np.float32)[None,:,None]
 # A bounded overlap color correction joins independent generations without
 # returning to a pixel-art border. Adjustment is constant per connected strip.
 if start and weights[:,start*4:end*4].max()>0:
  overlap=(weights[:,start*4:end*4,0]>0)[0]
  prev=rgb[:,start*4:end*4][:,overlap]/weights[:,start*4:end*4][:,overlap]
  valid=world[:,start:end][:,np.flatnonzero(overlap)[::4]//4,3]>0
  # Robust per-channel correction uses generated overlap pixels only.
  delta=np.median((prev-im[:,overlap]).reshape(-1,3),axis=0);delta=np.clip(delta,-12,12)
  im=np.clip(im+delta,0,255)
 rgb[:,start*4:end*4]+=im*weight;weights[:,start*4:end*4]+=weight
 print(row['name'],'stitched',flush=True)
rgb/=weights;world_hd=finish(rgb,world);Image.fromarray(world_hd).save(D/'world.png');del rgb,weights
sources=[world];outputs=[world_hd]
for row in manifest[7:]:
 src=np.asarray(read_pam(Path(row['source'])));h,w=src.shape[:2];gen=normalize(row)
 rgb=np.zeros((h*4,w*4,3),np.float32);weight=np.zeros((h*4,w*4,1),np.float32)
 # Fold the model's shared 64px context onto the periodic surface.
 for dy in (-h,0,h):
  for dx in (-w,0,w):
   left=max(0,dx-64);right=min(w,dx+w+64);top=max(0,dy-64);bottom=min(h,dy+h+64)
   if left>=right or top>=bottom:continue
   sx=(left-dx+64)*4;sy=(top-dy+64)*4
   gy,gx=np.mgrid[sy:sy+(bottom-top)*4,sx:sx+(right-left)*4]
   taper=np.minimum(np.minimum(gx+.5,(w+128)*4-.5-gx),np.minimum(gy+.5,(h+128)*4-.5-gy));taper=np.clip(taper/256,0,1)[:,:,None]
   rgb[top*4:bottom*4,left*4:right*4]+=gen[sy:sy+(bottom-top)*4,sx:sx+(right-left)*4]*taper;weight[top*4:bottom*4,left*4:right*4]+=taper
 rgb/=weight;hd=finish(rgb,src,True);Image.fromarray(hd).save(D/(row['name']+'.png'));sources.append(src);outputs.append(hd)

centers=dict(line.split() for line in (N/'background-fix/materials/background-centers.txt').read_text().splitlines()[1:]);added=[]
with (pack/'connected-world.bin').open('wb') as f:
 f.write(b'DKHWv002');f.write(struct.pack('<6I',*[n for src in sources for n in [src.shape[1]//32,src.shape[0]//32]]))
 palette=np.frombuffer((P/'map-source/source-cgram.bin').read_bytes(),dtype='<u2')
 for c in palette:
  channels=[((int(c)>>(i*5))&31) for i in range(3)];r,g,b=[(v<<3)|(v>>2) for v in channels];f.write(struct.pack('<I',0xff000000|(r<<16)|(g<<8)|b))
 for layer,(src,hd) in enumerate(zip(sources,outputs)):
  for y in range(0,src.shape[0],32):
   for x in range(0,src.shape[1],32):
    original=Image.fromarray(src[y:y+32,x:x+32]).tobytes('raw','BGRA');art=Image.fromarray(hd[y*4:(y+32)*4,x*4:(x+32)*4]).tobytes('raw','BGRA')
    key=hashlib.sha256(b'connected-nano-v1'+art).hexdigest();target=pack/(key+'.dkhd')
    if not target.exists():target.write_bytes(b'DKHDv001'+struct.pack('<HH',128,128)+art)
    f.write(key.encode());f.write(original);center=hashlib.sha256(original).hexdigest();centers[center]=key;added.append(key)
(pack/'background-centers.txt').write_text(f'DKHCv001 {len(centers)}\n'+''.join(f'{a} {b}\n' for a,b in sorted(centers.items())))
report={'world_pixels':[sources[0].shape[1],sources[0].shape[0]],'grid_cells':len(added),'unique_connected_materials':len(set(added)),'exact_centers':len(centers),'assembly':'connected overlap, source contour, exact RGBA registration; sprites unchanged'}
(D/'assembly.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
