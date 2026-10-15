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
        'docs/COOP.md': ROOT/'docs/COOP.md',
        'docs/INGAME_SAVES.md': ROOT/'docs/INGAME_SAVES.md',
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
dkc1_dixie_desktop.exe and SDL2.dll beside it. Select your own headerless
Donkey Kong Country USA v1.0 ROM (4 MiB, SHA-256
fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15).

Choose Mods > Dixie Kong Country to enable Dixie. This restarts into the
Dixie executable using the same clean ROM. The embedded IPS patch is applied
in memory; no separate patched ROM is needed. Switch the menu item off to
return to stock. Use View > 4:3 for the tested Dixie presentation.

Choose Mods > Simultaneous co-op for two independently controlled Kongs
with stacked views. Configure Player 2 in Controls; see docs/COOP.md.

Game > Controller rumble (enemy stomps) turns the existing stomp feedback on
or off and remembers your preference. Game > Test controller rumble sends a
short test pulse to a supported controller. Feedback currently covers enemy
stomps; it is enabled by default.

Dixie includes the map sprite-addressing fix. A save made while graphics were
corrupt can retain old graphics until leaving and re-entering the map normally.
Back up existing saves before replacing your installation. In-game saves from
Candy persist in %APPDATA%/Flat2VR/DKC1Recomp/saves/save.srm; host save states
are separate. This build retains the v0.0.12 save-persistence fix.

Windows 10/11 x64 and OpenGL 3.3 are required. No ROM or private save is bundled.
Full-game and exhaustive widescreen/controller coverage remain unverified.
The separately supplied Mac archive is unchanged v0.0.9 and has neither Dixie
nor the v0.0.12 in-game save fix. See the included docs for details.
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
