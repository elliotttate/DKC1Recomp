#!/usr/bin/env python3
"""Package the built native Windows host from an explicit file allowlist.

Run build_host.bat from a clean committed checkout first. No recursive asset,
ROM, generated-source, or save-state discovery is used for a player package.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT/'build/dkc1_desktop.exe')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if git('status', '--porcelain'):
        parser.error('Commit source changes before packaging a release')
    version = re.search(r'\bVERSION\s+(\d+\.\d+\.\d+)',
                        (ROOT/'CMakeLists.txt').read_text()).group(1)
    commit = git('rev-parse', 'HEAD')
    exe = args.exe.resolve()
    files = {
        'DKC1Recomp.exe': exe,
        'README.txt': ROOT/'packaging/windows/README.txt',
        'RELEASE_NOTES.md': ROOT/f'docs/RELEASE_v{version}.md',
        'LICENSE.txt': ROOT/'LICENSE',
        'licenses/snesrecomp.txt': ROOT/'snesrecomp/LICENSE',
    }
    for license in sorted((ROOT/'snesrecomp/third_party/psxrecomp_color_lut').glob('LICENSE*')):
        files['licenses/color-lut/'+license.name] = license
    contents = {name: path.read_bytes() for name, path in files.items()}
    # build_host.bat embeds the Git identity in the visible window title.
    identity = git('rev-parse', '--short', 'HEAD').encode('ascii')
    if identity+b'-dirty' in contents['DKC1Recomp.exe'] or identity+b'\0' not in contents['DKC1Recomp.exe']:
        parser.error('Executable does not embed this clean commit; rebuild it')
    manifest = {
        'version': version, 'platform': 'windows-x64', 'frontend': 'native-win32',
        'commit': commit, 'engine_commit': git('-C', 'snesrecomp', 'rev-parse', 'HEAD'),
        'files': {name: {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
                  for name, data in contents.items()},
    }
    contents['BUILD.json'] = (json.dumps(manifest, indent=2)+'\n').encode()
    args.output.mkdir(parents=True, exist_ok=True)
    archive = args.output/f'DKC1Recomp-v{version}-Windows-x64.zip'
    if archive.exists():
        parser.error(f'Output already exists: {archive}')
    prefix = f'DKC1Recomp-v{version}-Windows-x64/'
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, data in contents.items():
            z.writestr(prefix+name, data)
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        assert set(z.namelist()) == {prefix+name for name in contents}
        for name, data in contents.items():
            assert z.read(prefix+name) == data
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    Path(str(archive)+'.sha256').write_text(f'{digest}  {archive.name}\n', encoding='ascii')
    print(json.dumps({'archive': str(archive), 'sha256': digest, 'manifest': manifest}, indent=2))


if __name__ == '__main__':
    main()
