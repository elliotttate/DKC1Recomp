#!/usr/bin/env python3
"""Serial visible 60 Hz pacing runs, immutable roots, and independent traces.

The elevated trace-only helper is optional. No live controller injection or
frame dumping occurs during these runs. Each run restores its root and closes
normally; display failures are retained, never replaced by a later clean run.
"""
import argparse
import json
from pathlib import Path
import time
import threading
from verify_framegen import run, sha, ROM_SHA
from analyze_pacing import load_log
from analyze_presentmon import analyze as analyze_etw


def trace_request(evidence, action, name, kernel=False):
    request = evidence/'trace-request.json'
    if request.exists():
        raise RuntimeError('Trace helper has an unconsumed request')
    status_path = evidence/'trace-response.json'
    previous = status_path.stat().st_mtime_ns if status_path.exists() else 0
    temporary = request.with_suffix('.tmp')
    temporary.write_text(json.dumps({'action':action, 'name':name, 'kernel':kernel}))
    temporary.replace(request)
    expected = 'recording' if action == 'start' else 'stopped'
    deadline = time.monotonic()+900  # WPR merges may be slow; caller remains interruptible.
    while time.monotonic() < deadline:
        try:
            status = json.loads(status_path.read_text(encoding='utf-8-sig'))
            changed = status_path.stat().st_mtime_ns != previous
        except (ValueError, FileNotFoundError):
            status,changed = {},False
        if changed and status.get('status') == 'error':
            raise RuntimeError(status)
        if changed and status.get('status') == expected and status.get('name') == name:
            return status
        time.sleep(.2)
    raise TimeoutError('Trace helper has not completed; inspect it without killing the game')


def passed(result):
    analysis = result.get('timing_analysis',{})
    scan = analysis.get('scanout',{})
    return (analysis.get('pacing') == 'waitable' and scan.get('presents',0)>0
            and scan.get('missing_statistics_rows',1)==0
            and scan.get('presents',0)>=analysis.get('steady_frames',0)-scan.get('stat_lag_presents_max',0)-1
            and not any(analysis.get('frame_order',{}).values())
            and not any(scan.get(k,1) for k in ('repeated_refreshes','early_refreshes','disjoint'))
            and not any(analysis.get(k,1) for k in ('steady_overruns','steady_wait_timeouts',
                'steady_audio_starvations','steady_audio_drops','steady_audio_internal_underflows')))


class HitchCapture:
    """Stop bounded ETW history promptly; never pause or steer the game."""
    def __init__(self, directory, name, log, phase, threshold):
        self.directory,self.name,self.log=directory,name,log
        self.phase,self.threshold=phase,threshold
        self.cancel=threading.Event()
        self.stopped=False
        self.error=None
        self.thread=threading.Thread(target=self.watch,daemon=True)

    def watch(self):
        try:
            while not self.log.exists():
                if self.cancel.wait(.05): return
            with self.log.open(encoding='utf-8') as stream:
                pending=''
                while not self.cancel.is_set():
                    pending+=stream.read()
                    lines=pending.split('\n')
                    pending=lines.pop()
                    for line in lines:
                        if not line: continue
                        row=json.loads(line)
                        if row.get('frame',0)>59 and row.get(self.phase,0)>=self.threshold:
                            (self.log.parent/'trace-trigger.json').write_text(json.dumps(row,indent=2))
                            print(f'TRACE HITCH {self.name}: frame {row["frame"]}, {self.phase}={row[self.phase]}',flush=True)
                            trace_request(self.directory,'stop',self.name)
                            self.stopped=True
                            return
                    self.cancel.wait(.05)
        except Exception as error:
            self.error=error

    def finish(self):
        self.cancel.set()
        self.thread.join()
        if self.error: raise self.error
        if not self.stopped: trace_request(self.directory,'stop',self.name)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('exe','rom','state','input','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--trace-directory',type=Path)
    parser.add_argument('--kernel',action='store_true',help='diagnostic capture, not a lightweight acceptance run')
    parser.add_argument('--kernel-phase',choices=('submit_interval_ms','work_ms','audio_ms','gap_ms','wait_ms'),default='submit_interval_ms')
    parser.add_argument('--kernel-trigger-ms',type=float,default=25,
                        help='stop circular trace at first steady frame reaching this threshold')
    parser.add_argument('--loop-frames',type=int,default=570)
    parser.add_argument('--loops',type=int,default=190,help='190 x 570 frames is 30 min 5 s')
    parser.add_argument('--repeats',type=int,default=2)
    parser.add_argument('--modes',nargs='+',choices=('windowed','fullscreen'),default=['windowed','fullscreen'])
    parser.add_argument('--queue',type=int,choices=(1,2),default=1)
    parser.add_argument('--wide',type=int,choices=(0,1),default=1)
    parser.add_argument('--framegen',choices=('0','1'),default='1')
    parser.add_argument('--warmup-ms',type=int,default=2500)
    parser.add_argument('--log-off',action='store_true',help='PresentMon-only observer-effect control')
    args=parser.parse_args()
    if args.loops<1 or args.repeats<1 or args.loop_frames*args.loops<60:
        parser.error('positive repeats/loops and at least 60 frames required')
    if args.log_off and not args.trace_directory:
        parser.error('--log-off needs an independent trace')
    if args.kernel and (not args.trace_directory or args.log_off):
        parser.error('--kernel needs a trace helper and the host timing log')
    exe,rom,state,schedule,out=[getattr(args,k).resolve() for k in ('exe','rom','state','input','output')]
    if sha(rom)!=ROM_SHA:
        parser.error('unsupported ROM')
    out.mkdir(parents=True,exist_ok=False)
    root=out/'root.state'
    root.write_bytes(state.read_bytes())
    frames=args.loop_frames*args.loops
    script=(schedule.read_text().rstrip()+'\n')*args.loops
    report={'schema':'dkc1.pacing-soak.v1','exe':str(exe),'exe_sha256':sha(exe),
            'rom_sha256':sha(rom),'state_sha256':sha(root),'frames_per_run':frames,
            'queue_limit':args.queue,'kernel_trace':args.kernel,'runs':[],'passed':False}
    try:
        for repeat in range(1,args.repeats+1):
            for mode in args.modes:
                name=f'{mode}-{repeat}'
                trace_name=f'{out.name}-{name}'
                overrides={'DKC1_MAX_FRAME_LATENCY':str(args.queue),
                    'DKC1_DESKTOP_DEBUG_PANEL':'0','DKC1_FULLSCREEN':str(int(mode=='fullscreen')),
                    'DKC1_PRESENT_WARMUP_MS':str(args.warmup_ms)}
                if args.log_off:
                    overrides['DKC1_PACING_LOG']=''
                (out/'progress.json').write_text(json.dumps({'run':name,'status':'running',
                    'start_unix':time.time(),'expected_seconds':frames/60,'queue':args.queue}))
                print(f'START {name}: {frames} frames, queue {args.queue}',flush=True)
                if args.trace_directory:
                    trace_request(args.trace_directory,'start',trace_name,args.kernel)
                watcher=None
                if args.kernel:
                    watcher=HitchCapture(args.trace_directory,trace_name,out/name/'pacing.jsonl',
                                         args.kernel_phase,args.kernel_trigger_ms)
                    watcher.thread.start()
                try:
                    result=run(exe,rom,root,out/name,args.framegen,script,frames,False,args.wide,overrides)
                    # Keep the gameplay result even when a collector fails to stop.
                    report['runs'].append(result)
                finally:
                    if watcher:
                        watcher.finish()
                    elif args.trace_directory:
                        trace_request(args.trace_directory,'stop',trace_name)
                result['cpu_dxgi_passed']=passed(result)
                result['mode']=mode
                if args.trace_directory:
                    result['presentmon_csv']=str(args.trace_directory/(trace_name+'-presentmon.csv'))
                    process=json.loads((out/name/'process.json').read_text())
                    logged=[] if args.log_off else load_log(out/name/'pacing.jsonl')[1]
                    result['etw']=analyze_etw(result['presentmon_csv'],process['pid'],
                        [r for r in logged if 60<=r['frame']<=frames])
                result['passed']=(result['cpu_dxgi_passed'] and result.get('etw',{}).get('passed',False)
                                  and not args.kernel)
                (out/'report.json').write_text(json.dumps(report,indent=2))
                print('RESULT',name,result['cpu_dxgi_passed'],result.get('timing_analysis',{}).get('scanout'),flush=True)
        report['passed']=all(r['passed'] for r in report['runs'])
    except Exception as error:
        report['error']=str(error)
        raise
    finally:
        report['state_unchanged']=sha(root)==sha(state)
        (out/'report.json').write_text(json.dumps(report,indent=2))
        (out/'progress.json').write_text(json.dumps({'status':'finished','passed':report['passed']}))


if __name__=='__main__':
    main()
