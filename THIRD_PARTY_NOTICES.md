# Third-party notices

DKC1Recomp does not contain or distribute a Donkey Kong Country ROM, generated
ROM-derived source, save states, extracted game assets, screenshots, or audio.
Users must provide the supported ROM themselves.

- `snesrecomp` is pinned as a Git submodule. Its source is distributed under
  the PolyForm Noncommercial License 1.0.0 and carries its own third-party
  notices. The project fork exists to retain the host-only widescreen runtime
  changes required by this repository; upstream provenance remains in its Git
  history.
- Structural metadata in `recomp/*.cfg` derives from the GPL-3
  Yoshifanatic1 Donkey Kong Country 1 disassembly. Each generated metadata file
  records its provenance. The optional Baby Kong animation map also uses its
  numeric Donkey animation identifiers and semantic names, cross-checked at
  revision `c2080f40469c716923f550706509a0d354229841`. The disassembly itself,
  its comments, and ROM-derived assembly are not copied into this repository.
- Portions of the host integration follow the MIT-licensed DKC2Recomp project.
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
