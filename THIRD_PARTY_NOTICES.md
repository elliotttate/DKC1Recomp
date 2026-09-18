# Third-party notices

DKC1Recomp does not contain or distribute a Donkey Kong Country ROM, generated
ROM-derived source, save states, extracted game assets, screenshots, or audio.
Users must provide the supported ROM themselves. The optional Dixie variant
embeds the user-selected Dixie Kong Country IPS patch and applies it only to
the verified clean ROM; see [the patch provenance and integration record](docs/DIXIE_MOD.md)
and `docs/DIXIE_HD_MAC.md`.

Windows adds unmodified SDL 2.30.9 (zlib) and miniz 3.0.2 (MIT).
Exact revisions, uses and complete license texts are recorded under
[`third_party/windows/`](third_party/windows/README.md).

- `snesrecomp` is pinned as a Git submodule. Its source is distributed under
  the PolyForm Noncommercial License 1.0.0 and carries its own third-party
  notices. The project fork exists to retain the host-only widescreen runtime
  changes required by this repository; upstream provenance remains in its Git
  history.
- Structural metadata in `recomp/*.cfg` derives from the GPL-3
  Yoshifanatic1 Donkey Kong Country 1 disassembly. Each generated metadata file
  records its provenance. The disassembly itself, its comments, and ROM-derived
  assembly are not copied into this repository.
- Portions of the host integration follow the MIT-licensed DKC2Recomp project.
  This includes the CRT/filter models, translated Reconstruct/CRT shaders,
  audio drift model, and input/refresh/rewind designs. The DKC2 license notice
  is reproduced below. The native AppKit/Metal integration is DKC1 host code.
- The optional Baby Kong mod's source-only Kiddy frame map records names,
  addresses, sizes, and palette location derived from the GPL-3 H4v0c21
  DKC3 disassembly at revision
  `bed96892f5e85eabd5c920306f00b361c2e1f34c`. No disassembly source,
  comments, extracted graphics, palette bytes, or other ROM-derived data are
  copied. At runtime the mod decodes the user's separately supplied, verified
  DKC3 ROM in memory.
- The DKC3 sprite-header interpretation was cross-checked against Mike
  Pavone's RainbowZ Editor at revision
  `3a8badfec278ba11c1581ea3df02463077666619`. That repository is marked
  all-rights-reserved. No RainbowZ source, comments, binaries, or assets are
  copied; this project independently expresses only the sprite format facts
  needed to address the user's verified ROM in memory.

Donkey Kong Country, Nintendo, Rare, and related names and trademarks belong
to their respective owners. This is an unofficial noncommercial fan project.

## DKC2Recomp host and graphics code

MIT License

Copyright (c) 2026 DKC2 Port contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
