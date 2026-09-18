#!/usr/bin/env python3
from pathlib import Path
import hashlib,json,struct,sys
from functools import lru_cache
import numpy as np
from PIL import Image
import argparse
parser=argparse.ArgumentParser(description='Audit the complete Jungle Hijinxs terrain against an exact source capture and private HD pack.')
parser.add_argument('--work-dir',type=Path,required=True)
parser.add_argument('--rom',type=Path,required=True)
a=parser.parse_args();P=a.work_dir.resolve();P.mkdir(parents=True,exist_ok=True);R=Path(__file__).resolve().parents[1];N=P.parent
sys.path.insert(0,str(R/'tools'));from hd_sprite_pack import read_pam
s=P/'map-source';scene=json.loads((s/'scene.json').read_text());trace=json.loads((s/'ws.jsonl').read_text().splitlines()[0])
assert trace['scene']['entrance']==22 and trace['source']['bank']==217 and trace['source']['map']==0 and trace['source']['metatiles']==41920
assert scene['bgsc'][0]==109 and scene['bgTileAdr']&15==3
rom=a.rom.read_bytes();assert hashlib.sha256(rom).hexdigest()=='fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15'
v=np.frombuffer((s/'source-vram.bin').read_bytes(),dtype='<u2');c=np.frombuffer((s/'source-cgram.bin').read_bytes(),dtype='<u2')
def rw(bank,a):return struct.unpack_from('<H',rom,(bank-0xc0)*65536+(a&65535))[0]
def entry(x,y):
 cell=rw(0xd9,(x//4)*32+((y//4)&15)*2);fx=cell&0xc000;sx=x&3;sy=y&3
 if fx&0x4000:sx=3-sx
 if fx&0x8000:sy=3-sy
 return rw(0xd9,((cell<<5)+0xa3c0+sx*2+sy*8)&65535)^fx
@lru_cache(None)
def tile(e):
 a=np.zeros((8,8,4),dtype=np.uint8)
 for y in range(8):
  sy=7-y if e&0x8000 else y;addr=(0x3000+(e&1023)*16+sy)&32767;bits=int(v[addr])|(int(v[(addr+8)&32767])<<16)
  for x in range(8):
   shift=x if e&0x4000 else 7-x;b=bits>>shift;ix=(b&1)|((b>>7)&2)|((b>>14)&4)|((b>>21)&8)
   if ix:
    color=int(c[((e&0x1c00)>>6)+ix]);a[y,x]=[((color>>(ch*5))&31)*255//31 for ch in range(3)]+[255]
    # Host expands 5 bits by replication, not rounded scale.
    for ch in range(3):t=(color>>(ch*5))&31;a[y,x,ch]=(t<<3)|(t>>2)
 return a
# Match every fully visible native tile to actual ring VRAM before extracting.
checks=[]
for y in range(23,49):
 for x in range(11,42):
  ix=0x6c00+(y&31)*32+(x&31)+(0x400 if x&32 else 0)
  checks.append(entry(x,y)==int(v[ix&32767]))
print('visible tile entry matches',sum(checks),'/',len(checks));assert all(checks)
w=5376;h=512;a=np.zeros((h,w,4),np.uint8)
for y in range(h//8):
 for x in range(w//8):a[y*8:y*8+8,x*8:x*8+8]=tile(entry(x,y))
Image.fromarray(a).save(P/'world-original.png')
def key(im):return hashlib.sha256(im.tobytes('raw','BGRA')).hexdigest()
known={line.split()[0] for line in (N/'background-fix/materials/background-centers.txt').read_text().splitlines()[1:]}
unique={};missing={}
for y in range(0,h,32):
 for x in range(0,w,32):
  im=Image.fromarray(a[y:y+32,x:x+32]);k=key(im);unique.setdefault(k,[x,y]);
  if k not in known:missing.setdefault(k,[x,y])
report={'rom_sha256':hashlib.sha256(rom).hexdigest(),'source_frame':trace['frame'],'world_size':[w,h],'visible_tile_matches':[sum(checks),len(checks)],'unique_centers':len(unique),'missing_centers':len(missing),'centers':unique,'missing':missing}
(P/'map-inventory.json').write_text(json.dumps(report,indent=2)+'\n');print('unique',len(unique),'missing',len(missing))
# Compact visible inspection across world.
bg=Image.new('RGBA',(w,h),(80,80,80,255));bg.alpha_composite(Image.fromarray(a));bg.convert('RGB').resize((2688,256)).save(P/'world-overview.jpg')
