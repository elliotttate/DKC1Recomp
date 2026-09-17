#!/usr/bin/env python3
"""Assemble captured OBJ composites only when existing source pixels cover them exactly."""
import argparse,hashlib,json,struct
from pathlib import Path
import numpy as np
from PIL import Image
from hd_sprite_pack import read_pam
from build_hd_object_silhouettes import silhouette_key_bytes

def composite_reconstructed_cells(out,source,cells):
    """Blend a source-proven remainder without exposing its native cell grid.

    The object capture is still the color/alpha oracle, but the ownership mask
    is reconstructed as one continuous high-resolution contour.  Pasting each
    4x cell independently leaves square seams on small impact/smoke effects.
    """
    if not np.any(cells):return out
    size=(source.width*4,source.height*4)
    reconstructed=source.resize(size,Image.Resampling.LANCZOS)
    mask=Image.fromarray(np.where(cells,255,0).astype(np.uint8),'L').resize(
        size,Image.Resampling.LANCZOS)
    pixels=np.asarray(reconstructed).copy()
    pixels[:,:,3]=((pixels[:,:,3].astype(np.uint16)*np.asarray(mask,dtype=np.uint16)+127)//255).astype(np.uint8)
    out.alpha_composite(Image.fromarray(pixels,'RGBA'))
    return out

def silhouette_art(pack):
    index=pack/'object-silhouettes.txt';bases_path=pack/'object-bases.bin'
    if not index.is_file() or not bases_path.is_file():return {},{}
    lines=index.read_text().splitlines();header=lines[0].split() if lines else []
    if len(header)!=2 or header[0]!='DKHOs001' or int(header[1])!=len(lines)-1:
        raise ValueError('Invalid object silhouette index')
    aliases={row.split()[0]:row.split()[1] for row in lines[1:]}
    data=bases_path.read_bytes()
    if data[:8]!=b'DKHDb001' or len(data)<12:raise ValueError('Invalid object base index')
    count=struct.unpack_from('<I',data,8)[0];offset=12;bases={}
    for _ in range(count):
        if offset+68>len(data):raise ValueError('Truncated object base index')
        key=data[offset:offset+64].decode();w,h=struct.unpack_from('<HH',data,offset+64);offset+=68
        size=w*h*4
        if not w or not h or offset+size>len(data):raise ValueError('Invalid object base raster')
        bases[key]=Image.frombytes('RGBA',(w,h),data[offset:offset+size],'raw','BGRA');offset+=size
    if offset!=len(data):raise ValueError('Trailing object base data')
    return aliases,bases

def recolor_alias(source,base,hd):
    if source.size!=base.size or hd.size!=(source.width*4,source.height*4):
        raise ValueError('Silhouette alias dimensions differ')
    current=np.asarray(source).astype(np.int16);registered=np.asarray(base).astype(np.int16)
    result=np.asarray(hd).copy()
    for y in range(source.height):
        for x in range(source.width):
            if not current[y,x,3]:continue
            delta=current[y,x,:3]-registered[y,x,:3]
            block=result[y*4:y*4+4,x*4:x*4+4,:3].astype(np.int16)
            result[y*4:y*4+4,x*4:x*4+4,:3]=np.clip(block+delta,0,255).astype(np.uint8)
    return Image.fromarray(result,'RGBA')

def assemble(pack,captures,libraries,reconstruct_unowned=False):
    primitives={};primitive_art={};aliases,bases=silhouette_art(pack)
    for folder in libraries:
        for file in sorted(folder.glob('*.png')):
            im=Image.open(file).convert('RGBA')
            for flip in (False,True):
                src=im.transpose(Image.Transpose.FLIP_LEFT_RIGHT) if flip else im
                raw=src.tobytes('raw','BGRA');key=hashlib.sha256(raw).hexdigest()
                art_key=key if (pack/(key+'.dkhd')).is_file() else aliases.get(silhouette_key_bytes(raw,*src.size))
                art=pack/((art_key or '')+'.dkhd')
                if not art_key or not art.exists() or key in primitives:continue
                data=art.read_bytes();size=struct.unpack_from('<HH',data,8)
                if data[:8]!=b'DKHDv001' or size!=(src.width*4,src.height*4):continue
                a=np.asarray(src);mask=a[:,:,3]>0
                if not mask.any():continue
                ys,xs=np.where(mask);pixels=a[ys,xs];colors,counts=np.unique(pixels,axis=0,return_counts=True);rare=colors[counts.argmin()];anchor=np.where(np.all(pixels==rare,axis=1))[0][0]
                hd=Image.frombytes('RGBA',size,data[12:],'raw','BGRA')
                if art_key!=key:
                    if art_key not in bases:continue
                    hd=recolor_alias(src,bases[art_key],hd)
                primitives[key]=(a,ys,xs,int(xs[anchor]),int(ys[anchor]),rare,hd);primitive_art[key]=art_key
    def part(key,x,y):
        row={'key':key,'xy':[x,y]}
        if primitive_art[key]!=key:row['art_key']=primitive_art[key]
        return row
    report={'assembled':{},'unmatched':[],'primitives':len(primitives)}
    for capture in captures:
        for meta in sorted(capture.glob('*.json')):
            row=json.loads(meta.read_text())
            if row.get('kind')!='object' or (pack/(row['key']+'.dkhd')).exists():continue
            source=read_pam(meta.with_suffix('.pam'))
            if hashlib.sha256(source.tobytes('raw','BGRA')).hexdigest()!=row['key']:raise ValueError('Captured object identity differs')
            a=np.asarray(source);mask=a[:,:,3]>0;covered=np.zeros(mask.shape,bool);out=Image.new('RGBA',(source.width*4,source.height*4));parts=[]
            composition=None
            candidates=[]
            for key,(b,ys,xs,ax,ay,rare,hd) in primitives.items():
                for yy,xx in zip(*np.where(np.all(a==rare,axis=2))):
                    x=int(xx)-ax;y=int(yy)-ay
                    if x<0 or y<0 or x+b.shape[1]>source.width or y+b.shape[0]>source.height:continue
                    if np.array_equal(a[ys+y,xs+x],b[ys,xs]):candidates.append((len(xs),key,x,y,True))
            for _,key,x,y,_ in sorted(candidates,reverse=True):
                b,ys,xs,ax,ay,rare,hd=primitives[key]
                if np.all(covered[ys+y,xs+x]):continue
                # Accepted overlaps must also agree at native resolution.
                covered[ys+y,xs+x]=True;out.alpha_composite(hd,(x*4,y*4));parts.append(part(key,x,y))
            if not np.array_equal(covered,mask):
                # A moving banana bunch can contain two source sprites that
                # overlap.  The later sprite changes a few source pixels, so
                # requiring every primitive pixel to match rejects the whole
                # composite even though the source can be reconstructed
                # exactly.  Retry with candidates that match most of their
                # opaque pixels, then verify the composed native samples.
                relaxed=[]
                for key,(b,ys,xs,ax,ay,rare,hd) in primitives.items():
                    opaque=len(xs)
                    if b.shape[0] <= 16 and b.shape[1] <= 16:
                        placements=((x,y) for y in range(source.height-b.shape[0]+1)
                                    for x in range(source.width-b.shape[1]+1))
                    else:
                        placements=((int(xx)-ax,int(yy)-ay)
                                    for yy,xx in zip(*np.where(np.all(a==rare,axis=2))))
                    for x,y in placements:
                        if x<0 or y<0 or x+b.shape[1]>source.width or y+b.shape[0]>source.height:continue
                        primitive_mask=b[:,:,3]>0
                        source_mask=mask[y:y+b.shape[0],x:x+b.shape[1]]
                        # A source sprite may be covered by another sprite,
                        # but it cannot paint outside the captured silhouette.
                        if np.any(primitive_mask & ~source_mask):continue
                        region=a[ys+y,xs+x]
                        matches=np.all(region==b[ys,xs],axis=1)
                        count=int(matches.sum())
                        if count>=max(8,int(opaque*.35)):
                            absolute=np.zeros(mask.shape,bool)
                            py,px=np.where(primitive_mask);absolute[y+py,x+px]=True
                            relaxed.append({'key':key,'x':x,'y':y,'b':b,'ys':ys,'xs':xs,'hd':hd,'matches':count,'mask':absolute})
                # Many HUD count variants share the same silhouette. Keep the
                # best color match for each placed mask before set-covering.
                unique={}
                for c in relaxed:
                    token=c['mask'].tobytes()
                    if token not in unique or c['matches']>unique[token]['matches']:unique[token]=c
                relaxed=sorted(unique.values(),key=lambda c:(c['matches'],int(c['mask'].sum())),reverse=True)
                source_pixels=int(mask.sum())

                def native_order(chosen):
                    edges=set()
                    for i,left in enumerate(chosen):
                        for j,right in enumerate(chosen):
                            if i==j:continue
                            lx,ly=left['x'],left['y'];rx,ry=right['x'],right['y']
                            lmask=left['b'][:,:,3]>0;rmask=right['b'][:,:,3]>0
                            x0=max(lx,rx);y0=max(ly,ry);x1=min(lx+left['b'].shape[1],rx+right['b'].shape[1]);y1=min(ly+left['b'].shape[0],ry+right['b'].shape[0])
                            if x1<=x0 or y1<=y0:continue
                            for sy in range(y0,y1):
                                for sx in range(x0,x1):
                                    if not lmask[sy-ly,sx-lx] or not rmask[sy-ry,sx-rx]:continue
                                    px=a[sy,sx];li=left['b'][sy-ly,sx-lx];ri=right['b'][sy-ry,sx-rx]
                                    if np.array_equal(px,li) and not np.array_equal(px,ri):edges.add((j,i))
                                    elif np.array_equal(px,ri) and not np.array_equal(px,li):edges.add((i,j))
                    order=[];remaining=set(range(len(chosen)))
                    while remaining:
                        ready=sorted((i for i in remaining if not any((u,i) in edges for u in remaining)),key=lambda i:(chosen[i]['y'],chosen[i]['x'],i))
                        if not ready:ready=[min(remaining)]
                        for i in ready:order.append(i);remaining.remove(i)
                    native=Image.new('RGBA',(source.width,source.height))
                    for i in order:
                        c=chosen[i];native.alpha_composite(Image.fromarray(c['b'],'RGBA'),(c['x'],c['y']))
                    ordered=[chosen[i] for i in order]
                    if np.array_equal(np.asarray(native),a):return ordered,None,0
                    # OAM can interleave pieces from two logical objects. A
                    # single whole-layer ordering then cannot represent the
                    # capture, even though every visible source pixel still
                    # has one exact primitive owner. Preserve that ownership
                    # per native cell and blend the approved HD cells in the
                    # same front/back relation. Cells without an exact owner
                    # reject by default; the explicit bounded reconstruction
                    # mode below can rebuild that known source remainder.
                    unowned=0
                    for sy,sx in zip(*np.where(mask)):
                        owners=[]
                        for c in ordered:
                            lx=sx-c['x'];ly=sy-c['y']
                            if 0<=lx<c['b'].shape[1] and 0<=ly<c['b'].shape[0] and \
                               c['b'][ly,lx,3] and np.array_equal(c['b'][ly,lx],a[sy,sx]):
                                owners.append(c)
                        if not owners:unowned+=1
                    if unowned and not reconstruct_unowned:return None
                    return ordered,('cellwise-source-reconstruction' if unowned else 'cellwise-source-owner'),unowned

                def find_cover(covered_now,chosen):
                    if int(covered_now.sum())==source_pixels:
                        if not chosen:return None
                        result=native_order(chosen)
                        if result and result[2]>max(16,source_pixels//4):return None
                        return result
                    if len(chosen)>=8:return None
                    remaining=mask & ~covered_now
                    py,px=np.where(remaining)
                    # Branch from the rarest uncovered source pixel to keep
                    # the overlap search small even with all 100 HUD counts.
                    best_pixel=None;best_options=None;unsupported=[]
                    for sy,sx in zip(py,px):
                        options=[c for c in relaxed if c not in chosen and c['mask'][sy,sx]]
                        if not options:unsupported.append((sy,sx));continue
                        if best_options is None or len(options)<len(best_options):best_pixel=(sy,sx);best_options=options
                        if best_options is not None and len(best_options)==1:break
                    if not best_options:
                        if reconstruct_unowned and chosen and unsupported:
                            sy,sx=unsupported[0];new=covered_now.copy();new[sy,sx]=True
                            return find_cover(new,chosen)
                        return None
                    best_options.sort(key=lambda c:(int((c['mask'] & ~covered_now).sum()),c['matches']),reverse=True)
                    for c in best_options:
                        new=covered_now | c['mask']
                        result=find_cover(new,chosen+[c])
                        if result is not None:return result
                    return None

                solution=find_cover(np.zeros(mask.shape,bool),[])
                if solution is None:
                    report['unmatched'].append(row['key']);continue
                order,composition,reconstructed_cells=solution
                out=Image.new('RGBA',(source.width*4,source.height*4))
                for c in order:out.alpha_composite(c['hd'],(c['x']*4,c['y']*4))
                if composition:
                    reconstructed_cells_mask=np.zeros(mask.shape,bool)
                    for sy,sx in zip(*np.where(mask)):
                        contributors=[];owners=[]
                        for c in order:
                            lx=sx-c['x'];ly=sy-c['y']
                            if not (0<=lx<c['b'].shape[1] and 0<=ly<c['b'].shape[0]) or not c['b'][ly,lx,3]:continue
                            (owners if np.array_equal(c['b'][ly,lx],a[sy,sx]) else contributors).append((c,lx,ly))
                        if owners:
                            cell=Image.new('RGBA',(4,4))
                            for c,lx,ly in contributors+owners:
                                cell.alpha_composite(c['hd'].crop((lx*4,ly*4,lx*4+4,ly*4+4)))
                            out.paste(cell,(sx*4,sy*4))
                        else:reconstructed_cells_mask[sy,sx]=True
                    out=composite_reconstructed_cells(out,source,reconstructed_cells_mask)
                parts=[part(c['key'],c['x'],c['y']) for c in order]
            data=b'DKHDv001'+struct.pack('<HH',*out.size)+out.tobytes('raw','BGRA');(pack/(row['key']+'.dkhd')).write_bytes(data)
            result={'capture':str(meta),'parts':parts}
            if composition:
                result['composition']=composition
                result['reconstructed_cells']=reconstructed_cells
            report['assembled'][row['key']]=result
    (pack/'object-composite-provenance.json').write_text(json.dumps(report,indent=2)+'\n');return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--pack',type=Path,required=True);p.add_argument('--captures',type=Path,nargs='+',required=True);p.add_argument('--libraries',type=Path,nargs='+',required=True);p.add_argument('--reconstruct-unowned',action='store_true',help='Opt in to continuous Lanczos source reconstruction for the bounded remainder of interleaved cells with no exact primitive owner');a=p.parse_args();r=assemble(a.pack,a.captures,a.libraries,a.reconstruct_unowned);print('primitives',r['primitives'],'assembled',len(r['assembled']),'unmatched',len(set(r['unmatched'])))
