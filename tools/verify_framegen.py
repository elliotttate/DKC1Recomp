#!/usr/bin/env python3
"""Serial visible-host frame-generation verification from an immutable snapshot.

Captures every raw/display/mid image and WRAM frame. Three repeats per mode
must match byte for byte; enabled modes must preserve every off-mode raw frame
and WRAM byte. Undumped timing is a separate run. Force mode is software-path
coverage, never evidence that a physical 120 Hz monitor displayed each image.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
from analyze_pacing import analyze, load_log
from live_test_guard import LiveTestGuard
ROOT=Path(__file__).resolve().parents[1]
ROM_SHA='fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15'
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def signature(folder,suffix):
    files=sorted(folder.glob('frame*_'+suffix+'.ppm'))
    return hashlib.sha256(b''.join(bytes.fromhex(sha(p)) for p in files)).hexdigest(),len(files)
def run(exe,rom,state,output,mode,script,frames,dump,wide=1,overrides=None,capture_window=False):
    with LiveTestGuard(output) as guard:
        return _run(exe,rom,state,output,mode,script,frames,dump,wide,overrides,capture_window,guard)

def _run(exe,rom,state,output,mode,script,frames,dump,wide,overrides,capture_window,guard):
    if capture_window and not dump:raise ValueError('window capture is excluded from undumped timing runs')
    output.mkdir(parents=True,exist_ok=False)
    route=output/'input.dks'
    route.write_text(script+'\ncheckpoint framegen-final\nstate_load '+str(state)+'\n0 * 1\n')
    env={k:v for k,v in os.environ.items() if not k.startswith(('DKC1_','SNESRECOMP_'))}
    env.update(DKC1_DIXIE='0',DKC1_WIDESCREEN=str(wide),DKC1_FRAMEGEN=mode,
      DKC1_SCRIPT=str(route),DKC1_SAVESTATE_INPUT=str(state),DKC1_SESSION_DIR=str(output),
      DKC1_ROUTE_AUTOCLOSE_MS='100',DKC1_ROUTE_RESULT=str(output/'result.json'),
      DKC1_PACING_LOG=str(output/'pacing.jsonl'),
      # Present the initial frame for 2.5 s first so the compositor's one-time
      # presentation-path change (a 60-80 ms Present stall ~1.5 s after the
      # first present) lands before frame 1 instead of inside the timing gate.
      DKC1_PRESENT_WARMUP_MS='2500')
    if overrides:env.update(overrides)
    if dump:
        env.update(DKC1_FRAMEGEN_DUMP_START='1',DKC1_FRAMEGEN_DUMP_COUNT=str(frames),
          DKC1_FRAMEGEN_DUMP_DIR=str(output),DKC1_POSE_LOG=str(output/'pose.jsonl'),
          DKC1_BG_MOTION_LOG=str(output/'bg-motion.jsonl'),
          DKC1_ANIM_CADENCE_LOG=str(output/'animation.jsonl'),DKC1_OAM_LOG=str(output/'oam'),
          DKC1_WRAM_DUMP=f'1-{frames}',DKC1_WRAM_DUMP_PATH=str(output/'wram.bin'))
    with (output/'stdout.log').open('wb') as stdout,(output/'stderr.log').open('wb') as stderr:
        process=subprocess.Popen([str(exe),str(rom)],cwd=output,env=env,stdout=stdout,stderr=stderr)
        guard.track(process.pid)
        (output/'process.json').write_text(json.dumps({'pid':process.pid,'exe_sha256':sha(exe),
            'state_sha256':sha(state),'input_sha256':sha(route)},indent=2))
        if capture_window:
            time.sleep(4)
            if process.poll() is None:
                subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',
                    str(ROOT/'tools/capture_process_window.ps1'),'-ProcessId',str(process.pid),
                    '-OutputDirectory',str(output/'window')],check=True,capture_output=True)
                (output/'window/identity.json').write_text(json.dumps({'pid':process.pid,
                    'exe_sha256':sha(exe),'state_sha256':sha(state),'input_sha256':sha(route)},indent=2))
        try: code=process.wait(timeout=max(60,frames/60*5))
        except subprocess.TimeoutExpired:
            # Do not kill a healthy visible process or lose its evidence.
            raise RuntimeError(f'Visible process {process.pid} did not complete; inspect {output}')
    if code:raise RuntimeError(f'Host exit {code}: {output}')
    result=json.loads((output/'result.json').read_text())
    if result['status']!='complete':raise RuntimeError(result)
    data={'path':str(output),'mode':mode,'route':result,'input_sha256':sha(route)}
    if dump:
        data['raw'],data['raw_count']=signature(output,'cur')
        data['display'],data['display_count']=signature(output,'display')
        data['mid'],data['mid_count']=signature(output,'mid')
        data['wram']=sha(output/'wram.bin')
        data['oam']=sha(output/'oam.bin')
        data['final_machine']=json.loads((output/'checkpoints.jsonl').read_text().splitlines()[-1])
        if data['raw_count']!=frames or data['display_count']!=frames:raise RuntimeError('incomplete capture')
        meta=[json.loads(p.read_text()) for p in sorted(output.glob('frame*.json'))]
        data['composition_mismatches']=sum(x.get('pose_mismatch',0) for x in meta)
        data['frames_with_generated_poses']=sum(x.get('pose_actors',0)>0 for x in meta)
        data['frames_with_generated_pixels']=sum(x.get('pose_pixels',0)>0 for x in meta)
        data['generated_pixels']=sum(x.get('pose_pixels',0) for x in meta)
    elif env.get('DKC1_PACING_LOG'):
        header,all_rows=load_log(output/'pacing.jsonl')
        data['timing_header']=header
        before=next(x for x in all_rows if x['frame']==59)
        rows=[x for x in all_rows if 60<=x['frame']<=frames]
        def pct(key,p):
            v=sorted(x[key] for x in rows);return v[min(len(v)-1,int((len(v)-1)*p))]
        data['timing']={key:{'p50':pct(key,.5),'p99':pct(key,.99),'max':pct(key,1)} for key in ['submit_interval_ms','work_ms','interp_ms','real_to_mid_ms','mid_to_real_ms'] if key in rows[0]}
        data['timing']['mid_presented']=sum(x['mid_presented'] for x in rows)
        data['timing']['frames']=len(rows)
        for key in ['overruns','mid_skips','audio_starvations','audio_drops','audio_internal_underflows']:
            data['timing'][key]=rows[-1][key]-before[key]
        data['timing_analysis']=analyze(header,[r for r in all_rows if r['frame']<=frames],59)
    else:
        data['timing_log_disabled']=True
    print(output.name,json.dumps({k:v for k,v in data.items() if k in ['raw_count','frames_with_generated_poses','composition_mismatches','timing']}),flush=True)
    return data
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for flag in ['exe','rom','state','output']:parser.add_argument('--'+flag,type=Path,required=True)
    parser.add_argument('--wide',type=int,choices=[0,1],default=1)
    parser.add_argument('--repeats',type=int,default=3)
    parser.add_argument('--input',type=Path)
    parser.add_argument('--frames',type=int,default=210)
    parser.add_argument('--capture-window',action='store_true')
    ARGS=parser.parse_args()
    if ARGS.frames<60:parser.error('at least 60 frames are required for timing')
    if ARGS.repeats<3:parser.error('at least three repeats are required')
    exe,rom,state,out=[getattr(ARGS,x).resolve() for x in ['exe','rom','state','output']]
    if sha(rom)!=ROM_SHA:raise SystemExit('unsupported ROM')
    out.mkdir(parents=True,exist_ok=False)
    # Copy the immutable root outside the normal user-slot directory.
    root=out/'root.state';root.write_bytes(state.read_bytes())
    script=ARGS.input.read_text() if ARGS.input else '0 * 15\n80 * 90\n40 * 90\n0 * 15\n'
    if not ARGS.input and ARGS.frames!=210:parser.error('custom frame count requires --input')
    report={'exe':str(exe),'exe_sha256':sha(exe),'rom_sha256':sha(rom),'state_sha256':sha(root),'wide':ARGS.wide,'frames':ARGS.frames,'runs':[],'physical_120hz_verified':False}
    try:
        baseline=run(exe,rom,root,out/'off','0',script,ARGS.frames,True,ARGS.wide);report['runs'].append(baseline)
        for mode,name in [('1','60'),('force','120-pipeline')]:
            reference=None
            for repeat in range(ARGS.repeats):
                result=run(exe,rom,root,out/f'{name}-{repeat+1}',mode,script,ARGS.frames,True,ARGS.wide,capture_window=ARGS.capture_window and repeat==0)
                report['runs'].append(result)
                if any(result[k]!=baseline[k] for k in ['raw','wram','oam','final_machine']):raise RuntimeError('guest/native-frame divergence')
                if reference and any(result[k]!=reference[k] for k in ['raw','display','mid','wram','oam','final_machine']):raise RuntimeError('nondeterministic replay')
                if result['composition_mismatches']:raise RuntimeError('composition oracle failed')
                if result['frames_with_generated_poses']==0:raise RuntimeError('no generated poses exercised')
                reference=result
            timing=run(exe,rom,root,out/f'{name}-timing',mode,script,ARGS.frames,False,ARGS.wide)
            report['runs'].append(timing)
            t=timing['timing']
            if any(t[k] for k in ['overruns','mid_skips','audio_starvations','audio_drops','audio_internal_underflows']):
                raise RuntimeError('timing or audio gate failed')
            if mode=='force' and t['mid_presented']!=t['frames']:
                raise RuntimeError('missing half-frame submissions')
            analysis=timing['timing_analysis']
            if analysis.get('pacing')=='waitable':
                scan=analysis['scanout']
                if not scan['presents'] or any(scan[k] for k in ['repeated_refreshes','early_refreshes','disjoint']):
                    raise RuntimeError('DXGI display cadence gate failed')
        report['passed']=True
    except Exception as error:
        report['passed']=False;report['error']=str(error);raise
    finally:
        report['state_unchanged']=sha(root)==sha(state)
        (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
