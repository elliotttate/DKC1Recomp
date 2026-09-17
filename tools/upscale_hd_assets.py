#!/usr/bin/env python3
"""Local neural upscale of private DKC textures with alpha and context padding.

Uses published Real-ESRGAN weights through Spandrel. No cloud credentials.
"""
import argparse, hashlib, json, time
from pathlib import Path
import numpy as np
from PIL import Image
from scipy.ndimage import distance_transform_edt
import torch
from spandrel import ModelLoader, ImageModelDescriptor

def infer(model, pixels, device, tile=128, overlap=24):
    h,w,_=pixels.shape
    out=np.zeros((h*model.scale,w*model.scale,3),dtype=np.float32)
    for y in range(0,h,tile):
        for x in range(0,w,tile):
            l=max(0,x-overlap);t=max(0,y-overlap)
            r=min(w,x+tile+overlap);b=min(h,y+tile+overlap)
            block=np.ascontiguousarray(pixels[t:b,l:r].transpose(2,0,1))
            tensor=torch.from_numpy(block).unsqueeze(0).to(device)
            with torch.inference_mode():result=model(tensor).clamp(0,1).squeeze(0).cpu().numpy().transpose(1,2,0)
            s=model.scale;hh=min(tile,h-y);ww=min(tile,w-x)
            out[y*s:(y+hh)*s,x*s:(x+ww)*s]=result[(y-t)*s:(y-t+hh)*s,(x-l)*s:(x-l+ww)*s]
    return out

def upscale(model, im, device, alpha_mode='neural', wrap=False):
    im=im.convert('RGBA');rgba=np.asarray(im,dtype=np.float32)/255
    rgb=rgba[:,:,:3];alpha=rgba[:,:,3]
    # Extend the edge material into transparent pixels before restoration.
    # This avoids black/white halos without baking a background into sprites.
    if np.any(alpha<1) and np.any(alpha>0):
        _,inds=distance_transform_edt(alpha<=0,return_indices=True)
        rgb=rgb[inds[0],inds[1]]
    pad=24
    padded=np.pad(rgb,((pad,pad),(pad,pad),(0,0)),mode='wrap' if wrap else 'edge')
    s=model.scale
    color=infer(model,padded,device)[pad*s:(pad+im.height)*s,pad*s:(pad+im.width)*s]
    if np.all(alpha==1): a=np.ones(color.shape[:2])
    elif alpha_mode=='neural':
        a3=np.repeat(np.pad(alpha,((pad,pad),(pad,pad)),mode='wrap' if wrap else 'constant')[:,:,None],3,axis=2)
        a=infer(model,a3,device)[pad*s:(pad+im.height)*s,pad*s:(pad+im.width)*s].mean(axis=2)
        a=np.clip((a-0.015)/0.97,0,1)
    else:
        a=np.asarray(im.getchannel('A').resize((im.width*s,im.height*s),Image.Resampling.BICUBIC),dtype=np.float32)/255
    result=np.dstack((color,a))
    result[a<1/255,:3]=0
    return Image.fromarray(np.clip(np.rint(result*255),0,255).astype(np.uint8),'RGBA')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('model',type=Path);p.add_argument('source',type=Path);p.add_argument('output',type=Path)
    p.add_argument('--alpha',choices=['neural','bicubic'],default='neural');p.add_argument('--limit',type=int,default=0)
    a=p.parse_args();device='mps' if torch.backends.mps.is_available() else 'cpu'
    model=ModelLoader().load_from_file(a.model);assert isinstance(model,ImageModelDescriptor)
    model.to(device).eval();a.output.mkdir(parents=True,exist_ok=True)
    files=[a.source] if a.source.is_file() else sorted(a.source.glob('*.png'))
    if a.limit:files=files[:a.limit]
    print(f'{model.architecture.id}, {model.scale}x, {device}, {len(files)} assets',flush=True)
    manifest={'model':str(a.model),'model_sha256':hashlib.sha256(a.model.read_bytes()).hexdigest(),'architecture':model.architecture.id,'scale':model.scale,'alpha':a.alpha,'device':device,'assets':[]}
    for f in files:
        t=time.monotonic();out=a.output/f.name
        if not out.exists():upscale(model,Image.open(f),device,a.alpha,wrap=f.name.startswith('atlas-')).save(out)
        if f.with_suffix('.json').exists():(a.output/f.with_suffix('.json').name).write_bytes(f.with_suffix('.json').read_bytes())
        manifest['assets'].append({'source':str(f),'source_sha256':hashlib.sha256(f.read_bytes()).hexdigest(),'output':str(out),'output_sha256':hashlib.sha256(out.read_bytes()).hexdigest()})
        print(f'{f.name}: {time.monotonic()-t:.2f}s',flush=True)
    (a.output/'upscale-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
if __name__=='__main__':main()
