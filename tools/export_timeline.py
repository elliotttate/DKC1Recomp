#!/usr/bin/env python3
"""Render lifecycle + widescreen traces into one self-contained HTML timeline.

Inputs: any mix of DKC1_LIFECYCLE_TRACE and DKC1_WS_TRACE JSONL files.
Output: a single HTML file (no network, no CDN) with:
  - camera X/Y paths over frames;
  - one row per source record with allocation episodes as bands and
    state-change ticks;
  - scanner/section transition markers;
  - widescreen decision markers (resets, cold starts, centered fallbacks,
    calibration failures) so margin events line up with object events.

Multi-thousand-frame lifecycle streams are unreadable as JSONL; the
SuperZSNES effort built its timeline viewer last and wished it had been
first. Zoom with the mouse wheel, drag to pan.
"""
from __future__ import annotations

import argparse
import html
import json
from collections import defaultdict
from pathlib import Path


def load_jsonl(paths: list[Path]) -> list[dict]:
    events = []
    for path in paths:
        for line in path.read_text(errors="replace").splitlines():
            line = line.strip()
            if not line:
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(record, dict):
                events.append(record)
    return events


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("traces", nargs="+", type=Path)
    parser.add_argument("--out", type=Path,
                        default=Path("timeline.html"))
    parser.add_argument("--max-events", type=int, default=50000)
    args = parser.parse_args()

    events = load_jsonl(args.traces)[:args.max_events]

    camera = []
    episodes = defaultdict(list)   # source -> [(start, end|None, id)]
    open_eps = {}
    ticks = defaultdict(list)      # source -> [(frame, kind)]
    markers = []                   # (frame, label, css class)

    for event in events:
        frame = event.get("frame")
        if frame is None:
            continue
        kind = event.get("event")
        if kind is None and "decision" in event:
            decision = event["decision"]
            if decision.get("source_reset"):
                markers.append((frame, "ws source reset", "ws"))
            if decision.get("cold_start"):
                markers.append((frame, "ws cold start", "ws"))
            if decision.get("centered_fallback"):
                markers.append((frame, "ws centered fallback", "wsbad"))
            calibration = event.get("calibration", {})
            if calibration.get("selected") == 0 and \
                    event.get("ppu", {}).get("wide_mask"):
                markers.append((frame, "calibration unknown", "wsbad"))
            camera_obj = event.get("camera")
            if camera_obj:
                camera.append((frame, camera_obj.get("x", 0),
                               camera_obj.get("y", 0)))
            continue
        if kind in ("actor_alloc", "slot_alloc", "actor_retype",
                    "slot_retype"):
            source = event.get("source")
            index = event.get("actor_index", event.get("slot"))
            previous = open_eps.pop(index, None)
            if previous:
                episodes[previous[0]].append(
                    (previous[1], frame, previous[2]))
            open_eps[index] = (source, frame, event.get("id"))
            cam = event.get("camera")
            if cam:
                camera.append((frame, cam[0], cam[1]))
        elif kind in ("actor_free", "slot_free"):
            index = event.get("actor_index", event.get("slot"))
            previous = open_eps.pop(index, None)
            if previous:
                episodes[previous[0]].append(
                    (previous[1], frame, previous[2]))
        elif kind in ("actor_state", "slot_state"):
            ticks[event.get("source")].append((frame, "state"))
        elif kind == "scanner":
            markers.append((frame, "scanner", "scan"))
        elif kind == "section":
            markers.append((frame, "section", "section"))
        elif kind in ("gameplay_enter", "gameplay_exit"):
            markers.append((frame, kind, "gate"))
        elif kind == "keyframe":
            cam = event.get("camera")
            if cam:
                camera.append((frame, cam[0], cam[1]))
    for source, start, actor_id in open_eps.values():
        episodes[source].append((start, None, actor_id))

    payload = {
        "camera": camera,
        "episodes": {str(k): v for k, v in episodes.items()},
        "ticks": {str(k): v for k, v in ticks.items()},
        "markers": markers,
    }

    page = """<!doctype html><html><head><meta charset="utf-8">
<title>DKC1 timeline</title><style>
body{font:12px monospace;background:#111;color:#ddd;margin:0}
#c{display:block;width:100vw;height:96vh;cursor:grab}
#hud{position:fixed;top:4px;left:8px;background:#000a;padding:4px 8px}
</style></head><body>
<div id="hud">wheel = zoom, drag = pan</div>
<canvas id="c"></canvas>
<script>
const D=__DATA__;
const cv=document.getElementById('c'),cx=cv.getContext('2d');
let f0=1e9,f1=0;
for(const [f] of D.camera){f0=Math.min(f0,f);f1=Math.max(f1,f);}
for(const s in D.episodes)for(const [a,b] of D.episodes[s]){
 f0=Math.min(f0,a);f1=Math.max(f1,b||a);}
for(const [f] of D.markers){f0=Math.min(f0,f);f1=Math.max(f1,f);}
if(f1<=f0){f0=0;f1=1;}
let vx0=f0,vx1=f1,drag=null;
const rows=Object.keys(D.episodes).sort((a,b)=>a-b);
function X(f){return (f-vx0)/(vx1-vx0)*cv.width;}
function draw(){
 cv.width=innerWidth;cv.height=innerHeight*0.96;
 cx.fillStyle='#111';cx.fillRect(0,0,cv.width,cv.height);
 const camH=120;
 let maxX=1;for(const [,x] of D.camera)maxX=Math.max(maxX,x);
 cx.strokeStyle='#4af';cx.beginPath();
 for(const [f,x] of D.camera){const px=X(f),py=camH-(x/maxX)*camH+10;
  px>=0&&px<=cv.width?cx.lineTo(px,py):cx.moveTo(px,py);}
 cx.stroke();
 cx.fillStyle='#4af';cx.fillText('camera X',4,12);
 for(const [f,label,cls] of D.markers){
  const px=X(f);if(px<0||px>cv.width)continue;
  cx.strokeStyle=cls==='wsbad'?'#f44':cls==='ws'?'#fa4':
    cls==='gate'?'#4f4':'#666';
  cx.beginPath();cx.moveTo(px,camH+16);cx.lineTo(px,cv.height);cx.stroke();
 }
 const top=camH+30,rh=Math.max(10,(cv.height-top)/Math.max(rows.length,1));
 rows.forEach((s,i)=>{
  const y=top+i*rh;
  cx.fillStyle='#888';cx.fillText('rec '+(+s).toString(16),4,y+9);
  for(const [a,b,id] of D.episodes[s]){
   const x1=X(a),x2=X(b??vx1);
   cx.fillStyle='#2a6';cx.fillRect(Math.max(x1,40),y+2,
     Math.max(2,x2-Math.max(x1,40)),rh-5);
   if(x1>=40&&x1<=cv.width){cx.fillStyle='#dfd';
     cx.fillText((id??0).toString(16),x1+2,y+9);}
  }
  for(const [f] of (D.ticks[s]||[])){
   const px=X(f);if(px<40||px>cv.width)continue;
   cx.fillStyle='#ff6';cx.fillRect(px,y+2,1,rh-5);
  }
 });
}
cv.onwheel=e=>{e.preventDefault();
 const f=vx0+(e.offsetX/cv.width)*(vx1-vx0);
 const z=e.deltaY<0?0.8:1.25;
 vx0=f-(f-vx0)*z;vx1=f+(vx1-f)*z;draw();};
cv.onmousedown=e=>drag={x:e.clientX,a:vx0,b:vx1};
onmousemove=e=>{if(!drag)return;
 const df=(drag.x-e.clientX)/cv.width*(drag.b-drag.a);
 vx0=drag.a+df;vx1=drag.b+df;draw();};
onmouseup=()=>drag=null;onresize=draw;draw();
</script></body></html>"""
    page = page.replace("__DATA__", json.dumps(payload))
    args.out.write_text(page, encoding="utf-8")
    print(f"wrote {args.out} ({len(events)} events, "
          f"{len(episodes)} records, {len(markers)} markers)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
