#!/usr/bin/env python3
"""Package both Windows frontends from an explicit runtime/license allowlist."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--version', required=True)
    args = parser.parse_args()
    version = args.version.removeprefix('v')
    if not all(part.isdigit() for part in version.split('.')):
        parser.error('version must contain decimal components separated by dots')
    if subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT).strip():
        parser.error('commit the source before packaging')
    args.out.mkdir(parents=True, exist_ok=True)
    name = f'DKC1Recomp-v{version}-Windows-x64'
    archive = args.out/(name+'.zip')
    if archive.exists():
        parser.error(f'output already exists: {archive}')
    mapping = {filename: args.build/filename for filename in
               ['DKC1Recomp.exe', 'dkc1_dixie_desktop.exe', 'SDL2.dll']}
    mapping.update({
        'LICENSE': ROOT/'LICENSE',
        'THIRD_PARTY_NOTICES.md': ROOT/'THIRD_PARTY_NOTICES.md',
        'docs/DIXIE_MOD.md': ROOT/'docs/DIXIE_MOD.md',
        'docs/DIXIE_HAPTICS.md': ROOT/'docs/DIXIE_HAPTICS.md',
        'docs/DIXIE_MAP_FIX_2026-09-14.md': ROOT/'docs/DIXIE_MAP_FIX_2026-09-14.md',
        'docs/INGAME_SAVES.md': ROOT/'docs/INGAME_SAVES.md',
        'docs/WINDOWS_RELEASE.md': ROOT/'docs/WINDOWS_RELEASE.md',
        f'docs/RELEASE_{version}.md': ROOT/f'docs/RELEASE_{version}.md',
        'licenses/SDL-LICENSE.txt': ROOT/'third_party/windows/SDL-LICENSE.txt',
        'licenses/miniz-LICENSE.txt': ROOT/'third_party/windows/miniz-LICENSE.txt',
        'licenses/snesrecomp-LICENSE.txt': ROOT/'snesrecomp/LICENSE',
        'licenses/recomp-net-LICENSE.txt': ROOT/'snesrecomp/lib/recomp-net/LICENSE',
    })
    lut = ROOT/'snesrecomp/third_party/psxrecomp_color_lut'
    for filename in ['LICENSE-APACHE-2.0.txt', 'LICENSE-MIT.txt',
                     'LICENSE-POLYFORM-NONCOMMERCIAL-1.0.0.txt', 'README.md']:
        mapping['licenses/psxrecomp_color_lut/'+filename] = lut/filename
    payload = {name: path.read_bytes() for name, path in mapping.items()}
    for filename in ['DKC1Recomp.exe','dkc1_dixie_desktop.exe','SDL2.dll']:
        if not payload[filename].startswith(b'MZ'):
            raise ValueError(f'Invalid Windows binary: {filename}')
    payload['README.md'] = f'''# DKC1Recomp v{version} for Windows x64

Extract this entire folder and open DKC1Recomp.exe. Keep
dkc1_dixie_desktop.exe and SDL2.dll beside it. On the first launch, select
your own headerless Donkey Kong Country USA v1.0 ROM (4 MiB, SHA-256
fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15). The
verified path is remembered, so later launches open the game directly;
Game > Change ROM... picks a different file. No ROM is bundled.

Press Escape (or use Game > Pause / Settings) for the settings panel:
Graphics, CRT, Settings, Controls, Assist and Mods / Music tabs. Escape
never closes the game. The View menu switches 4:3, 16:10 and 16:9,
fullscreen (Alt+Enter), the upscaler, display model, level-edge policy
and pixel aspect; View > Smooth animation / frame generation (F10)
enables the optional held-pose interpolation with about 67 ms of extra
display latency, adding generated in-between images on 120/240 Hz
displays. Game > Controller rumble covers enemy stomps and is on by
default.

Presentation uses a Direct3D 11 flip-model swap chain paced by the
display's own refresh (DXGI statistics record which refresh each image
landed on); OpenGL 3.3 is the automatic fallback. Frame generation and
the pacing modes are the same as the v0.0.16 native host, now with the
full graphics, controls, save-state, Dixie and MSU-1 feature set.

Choose Mods > Dixie Kong Country to enable Dixie. This restarts into the
Dixie executable using the same clean ROM; the embedded IPS patch is
applied in memory. Switch the item off to return to stock. Switching
restarts the game because Dixie is a separate recompiled program. Dixie
keeps your aspect-ratio choice, including 16:10 and 16:9 widescreen.

In-game saves from Candy persist in %APPDATA%/Flat2VR/DKC1Recomp/saves/
save.srm; host save states and windows.ini live beside it. Back up saves
before replacing an installation.

Windows 10/11 x64 with Direct3D 11 (feature level 10.0) or OpenGL 3.3 is
required. Full-game and exhaustive widescreen/controller coverage remain
unverified. See the included docs for details and limits.
'''.encode('utf-8')
    buildinfo = {
        'version': 'v'+version,
        'commit': subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        'engine_commit': subprocess.check_output(['git','-C','snesrecomp','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        'files': {name: {'bytes':len(data),'sha256':sha(data)} for name,data in payload.items()},
    }
    payload['BUILDINFO.json'] = (json.dumps(buildinfo,indent=2)+'\n').encode('utf-8')
    with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for filename,data in sorted(payload.items()):
            z.writestr(name+'/'+filename,data)
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        assert set(z.namelist()) == {name+'/'+filename for filename in payload}
        for filename,data in payload.items():
            assert z.read(name+'/'+filename) == data
    digest = sha(archive.read_bytes())
    archive.with_suffix('.zip.sha256').write_text(f'{digest}  {archive.name}\n',encoding='ascii')
    print(json.dumps({'archive':str(archive),'sha256':digest,'files':len(payload)},indent=2))


if __name__ == '__main__':
    main()
