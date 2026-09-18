#!/usr/bin/env python3
"""Replay the private HD scene against the original guest outputs, three times."""
import argparse, concurrent.futures, hashlib, json, os, re, subprocess
from pathlib import Path

ANIMATION_CASES = {
    'idle-cycle-right': (600, 'source', {'Idle': 21, 'BeatChest': 24}),
    'idle-cycle-left': (660, 'mirrored', {'Idle': 21, 'BeatChest': 24}),
    'bounce-right': (340, 'source', {'Bounce': 16}),
    'bounce-left': (340, 'mirrored', {'Bounce': 16}),
    'ground-slap-right': (330, 'source', {'GroundSlap': 30, 'Duck': 22}),
    'ground-slap-left': (436, 'mirrored', {'GroundSlap': 30, 'Duck': 22}),
    'slap-transitions-right': (787, 'source', {'GroundSlap': 30, 'Duck': 22}),
    'slap-transitions-left': (893, 'mirrored', {'GroundSlap': 30, 'Duck': 22}),
}

def dk_raster_index(originals):
    """Index the complete private source corpus, including uninstalled groups."""
    from PIL import Image
    result = {}
    for path in sorted(originals.glob('*.png')):
        source = Image.open(path).convert('RGBA')
        for facing, image in [('source', source),
                              ('mirrored', source.transpose(Image.Transpose.FLIP_LEFT_RIGHT))]:
            key = hashlib.sha256(image.tobytes('raw', 'BGRA')).hexdigest()
            result.setdefault(key, []).append(f'{path.stem}:{facing}')
    if not result:
        raise ValueError(f'No private DK original frames in {originals}')
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('rom',type=Path);p.add_argument('output',type=Path)
    p.add_argument('--jobs',type=int,default=3)
    p.add_argument('--pack',type=Path,help='Private candidate material pack; defaults to the baseline scene-pack')
    p.add_argument('--preload',action='store_true',help='Preload the indexed level pack and require zero gameplay texture reads')
    p.add_argument('--metal',action='store_true',help='On macOS, compare every eligible Metal frame with the CPU pixel oracle; requires --preload')
    p.add_argument('--exact-centers',action='store_true',help='Enable byte-exact center aliases from the indexed candidate pack')
    p.add_argument('--connected-world',action='store_true',help='Enable source-verified connected scenery; requires the private world index (optional connected-cave.bin for Jungle Bonus 1)')
    p.add_argument('--polish',type=int,choices=range(101),default=0,metavar='0..100',help='Exercise spatial Metal cleanup while validating the raw compositor separately')
    p.add_argument('--state',type=Path,help='Immutable root for non-fresh cases; defaults to entry.state')
    p.add_argument('--input-play',type=Path,help='Exact controller input file for the replay case')
    p.add_argument('--frames',type=int,help='Positive frame count for the replay case')
    p.add_argument('--coverage',action='store_true',help='Record visible replacement misses separately from render correctness')
    p.add_argument('--cases',nargs='+',choices=['replay','fresh','idle','walk','directions','actions','run','hurt','hurt-left','idle-extended','barrel-right','barrel-left','barrel-jump','cache-pressure',*ANIMATION_CASES],default=['fresh','idle','walk'])
    p.add_argument('--export-materials',action='store_true',help='Export exact encountered rasters on the first enabled repeat')
    p.add_argument('--dk-originals',type=Path,help='Complete private DK PNG corpus for slam transition coverage; defaults to the all-animations original-frames directory')
    a=p.parse_args();repo=Path(__file__).resolve().parents[1];root=repo/'build/hd-slice'
    pack=a.pack.resolve() if a.pack else root/'scene-pack'
    if a.metal and not a.preload:p.error('--metal requires --preload')
    if not pack.is_dir():p.error('Material pack directory does not exist')
    preload_expected=len(list(pack.glob('*.dkhd'))) if a.preload else 0
    if a.preload and (not preload_expected or not (pack/'preload.txt').is_file()):
        p.error('--preload requires a nonempty pack indexed by build_hd_preload_manifest.py')
    exe=repo/'build/macos/dkc1_snesrecomp_headless';out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    cases=list(dict.fromkeys(a.cases))
    if 'replay' in cases and (not a.input_play or not a.input_play.is_file() or not a.frames or a.frames<1):
        p.error('replay requires an existing --input-play file and positive --frames')
    state=a.state.resolve() if a.state else root/'entry.state'
    transition_cases={case for case in cases if case.startswith(('ground-slap-', 'slap-transitions-'))}
    dk_keys=dk_raster_index(a.dk_originals or root/'reimagined/all-animations/original-frames') if transition_cases else {}
    required={}
    for case in cases:
        if case not in ANIMATION_CASES:continue
        _,facing,groups=ANIMATION_CASES[case]
        manifest=json.loads((pack/'registered-directions.json').read_text())
        required[case]={}
        for group,count in groups.items():
            poses=[f for f in manifest['frames'] if f['group']==group]
            if len(poses)!=count:p.error(f'{case}: expected {count} registered {group} poses, got {len(poses)}')
            for f in poses:
                key=f['keys'][facing]
                if not (pack/(key+'.dkhd')).is_file():p.error(f'{case}: missing {f["name"]} {facing} material')
                required[case][key]=f['name']
    tasks=[(aspect,case,on,repeat) for aspect in ('native','wide') for case in cases for on in (0,1) for repeat in range(3)]
    def run(task):
        aspect,case,on,repeat=task;name=f'{aspect}-{case}-{on}-{repeat+1}';dest=out/name;dest.mkdir(exist_ok=True)
        env={k:v for k,v in os.environ.items() if not k.startswith(('DKC1_','SNESRECOMP_'))}
        env.update(DKC1_WIDESCREEN=str(int(aspect=='wide')),DKC1_HD_SCENE=str(on),DKC1_HD_SPRITES=str(on),
                   DKC1_HD_SCENE_PACK=str(pack),DKC1_FRAME_PPM=str(dest/'native.ppm'),
                   DKC1_HD_FRAME_PPM=str(dest/'hd.ppm'),DKC1_WRAM_OUTPUT=str(dest/'wram.bin'),DKC1_VRAM_OUTPUT=str(dest/'vram.bin'))
        if on:env.update(DKC1_HD_SCENE_AUDIT=str(dest/'audit.jsonl'),DKC1_HD_SCENE_TRACE=str(dest/'hd.jsonl'),DKC1_HD_RENDER_EVERY_FRAME='1')
        if on and a.preload:env['DKC1_HD_SCENE_PRELOAD']='1'
        if on and a.exact_centers:env['DKC1_HD_EXACT_CENTERS']='1'
        if on and a.connected_world:env['DKC1_HD_CONNECTED_WORLD']='1'
        if on and a.polish:env['DKC1_HD_POLISH']=str(a.polish)
        if on and a.coverage:env['DKC1_HD_COVERAGE_TRACE']=str(dest/'coverage.jsonl')
        if on and a.metal:
            env.update(DKC1_HD_METAL_VALIDATE='1',DKC1_HD_METAL_TRACE=str(dest/'metal.jsonl'),
                       DKC1_HD_METAL_SHADER=str(repo/'runner/macos_hd_scene.metal'))
        frames=120
        if case=='fresh':env['DKC1_SCRIPT']=str(repo/'recipes/hd-jungle-entry.dks');frames=10000
        else:env['DKC1_SAVESTATE_INPUT']=str(state)
        if case=='replay':env['SNESRECOMP_INPUT_PLAY']=str(a.input_play.resolve());frames=a.frames
        if case=='walk':env['SNESRECOMP_INPUT_PLAY']=str(root/'walk.inputs');frames=315
        if case in ('directions','actions','run'):
            env['DKC1_SCRIPT']=str(repo/f'recipes/hd-{case}.dks')
            frames={'directions':366,'actions':450,'run':330}[case]
        if case in ('hurt','hurt-left'):env['DKC1_SCRIPT']=str(repo/f'recipes/hd-{case}.dks');frames=600
        if case in ('barrel-right','barrel-left','barrel-jump'):
            env['DKC1_SCRIPT']=str(repo/f'recipes/hd-{case}.dks')
            frames={'barrel-right':500,'barrel-left':940,'barrel-jump':870}[case]
        if case=='idle-extended':frames=1800
        if case=='cache-pressure':
            frames=3347
            env['DKC1_SCRIPT']=str(repo/'recipes/hd-cache-pressure.dks')
        if case in ANIMATION_CASES:
            frames=ANIMATION_CASES[case][0]
            env['DKC1_SCRIPT']=str(repo/f'recipes/hd-{case}.dks')
        if on and repeat==0 and (a.export_materials or case in required):
            (dest/'materials').mkdir(exist_ok=True)
            env['DKC1_HD_SCENE_EXPORT']=str(dest/'materials')
        r=subprocess.run([str(exe),str(a.rom.resolve()),str(frames)],env=env,cwd=repo,capture_output=True,text=True)
        (dest/'stdout.txt').write_text(r.stdout);(dest/'stderr.txt').write_text(r.stderr)
        if r.returncode:raise RuntimeError(name+' failed')
        keys=dict(re.findall(r'(\w+_sha256|audio_fnv1a)=([0-9a-f]+)',r.stdout+r.stderr))
        if len(keys)!=7:raise RuntimeError(name+' incomplete guest hashes')
        result={'guest':keys,'hd_sha256':hashlib.sha256((dest/'hd.ppm').read_bytes()).hexdigest()}
        if on:
            audits=[json.loads(s) for s in (dest/'audit.jsonl').read_text().splitlines()]
            result['audited_frames']=len(audits);result['fallback_pixels']=sum(x['mismatch_pixels'] for x in audits)
            result['final_hd']=json.loads((dest/'hd.jsonl').read_text().splitlines()[-1])
            if a.coverage:
                coverage=[json.loads(s) for s in (dest/'coverage.jsonl').read_text().splitlines()]
                supported=[x for x in coverage if x['supported']]
                result['coverage']={'supported_frames':len(supported),
                    'visible_missing_bg':sum(sum(x['visible_missing'][:3]) for x in supported),
                    'visible_missing_obj':sum(x['visible_missing'][3] for x in supported),
                    'missing_bg_frames':sum(bool(sum(x['visible_missing'][:3])) for x in supported)}
            if a.preload:
                trace=[json.loads(s) for s in (dest/'hd.jsonl').read_text().splitlines()]
                if any(x['resident_materials']!=preload_expected or x['material_file_reads']!=preload_expected for x in trace):
                    raise RuntimeError(name+' incomplete preload or gameplay texture reads')
            if a.metal:
                metal=[json.loads(s) for s in (dest/'metal.jsonl').read_text().splitlines()]
                validated=[x for x in metal if 'validated_pixels' in x]
                if len(validated)!=len(audits) or not validated or any(x.get('mismatch_pixels',0) or x.get('gpu_error',0) for x in metal):
                    raise RuntimeError(name+' Metal image oracle failed or incomplete')
                result['metal']={'validated_frames':len(validated),'mismatch_pixels':0}
            if case in transition_cases and (len(audits)!=frames or result['fallback_pixels']):
                raise RuntimeError(f'{name}: slam route left the audited HD scene or reconstruction fell back')
            if case=='cache-pressure':
                cache=result['final_hd']
                if not a.connected_world and (cache.get('cache_entries')!=4096 or not cache.get('cache_evictions')):
                    raise RuntimeError(name+': cache pressure was not reached')
                if cache['cache_failures'] or len(audits)<966 or any(x['mismatch_pixels'] for x in audits[-966:]):
                    raise RuntimeError(name+': cache failed or post-reload reconstruction differs')
                result['cache_pressure']={'post_reload_audited_frames':966,'mismatch_pixels':0,
                    'cache_entries':cache.get('cache_entries'),'cache_evictions':cache.get('cache_evictions'),
                    'resident_world_bypasses_cache':a.connected_world}
            if repeat==0 and case in required:
                seen={f.stem for f in (dest/'materials').glob('*.pam')}
                missing=[name for key,name in required[case].items() if key not in seen]
                if missing:raise RuntimeError(f'{name}: required animation poses not encountered: {missing}')
                result['animation_coverage']={'facing':ANIMATION_CASES[case][1],
                    'matched_poses':list(required[case].values()),'missing':missing}
                if case in transition_cases:
                    encountered=sorted(seen & dk_keys.keys())
                    uncovered={key:dk_keys[key] for key in encountered if not (pack/(key+'.dkhd')).is_file()}
                    if uncovered:
                        raise RuntimeError(f'{name}: encountered DK transition rasters lack HD art: {uncovered}')
                    result['dk_transition_coverage']={'encountered_rasters':len(encountered),
                        'poses':[pose for key in encountered for pose in dk_keys[key]], 'missing':uncovered}
        print(name,flush=True);return name,result
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:results=dict(pool.map(run,tasks))
    for aspect in ('native','wide'):
        for case in cases:
            baseline=results[f'{aspect}-{case}-0-1']['guest']
            for on in (0,1):
                for repeat in range(1,4):
                    r=results[f'{aspect}-{case}-{on}-{repeat}']
                    assert r['guest']==baseline,(aspect,case,on,repeat,'guest differs')
                    assert r['hd_sha256']==results[f'{aspect}-{case}-{on}-1']['hd_sha256'],'HD nondeterminism'
    pack_hash=hashlib.sha256()
    for material in sorted(pack.glob('*.dkhd')):
        pack_hash.update(material.name.encode());pack_hash.update(material.read_bytes())
    summary={'passed':True,'replays':len(results),'pack':str(pack),'pack_sha256':pack_hash.hexdigest(),'executable_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
        'rom_sha256':hashlib.sha256(a.rom.read_bytes()).hexdigest(),
        'state':str(state),'state_sha256':hashlib.sha256(state.read_bytes()).hexdigest(),'runs':results}
    if 'replay' in cases:
        summary['input_play']=str(a.input_play.resolve());summary['input_sha256']=hashlib.sha256(a.input_play.read_bytes()).hexdigest();summary['input_frames']=a.frames
    summary['pack_indices']={name:hashlib.sha256((pack/name).read_bytes()).hexdigest() for name in ('preload.txt','background-centers.txt','object-silhouettes.txt','object-bases.bin','connected-world.bin','connected-cave.bin') if (pack/name).exists()}
    (out/'results.json').write_text(json.dumps(summary,indent=2)+'\n');print(f'PASS: {len(results)} deterministic replays')
if __name__=='__main__':main()
