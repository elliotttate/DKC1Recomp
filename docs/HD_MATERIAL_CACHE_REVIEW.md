# HD material-cache exhaustion repair

September 7, 2026. Private, uncommitted Jungle Hijinxs experiment.

The reported return to old sprites while moving left was a second defect:
the renderer permanently stopped accepting new material identities after
4,096 slots. The art files existed. The corrected preview reuses inactive
slots and retains the current frame's material pointers.

## Evidence and cause

The actual tester process (PID 3922) was paused and its save preserved before
any replay. Read-only LLDB inspection, followed by verified detach, found
4,096 occupied entries, only 674 with HD art. Of those entries, 3,007 were
32×32 background chunks without HD files. Only six of twenty left-facing
Walk poses were cached. Walk7–Walk20 were installed but absent from the cache.

The append-only `GetMaterialKey` returned NULL whenever a new identity arrived
at capacity. Missing object pointers caused the original-pixel fallback;
missing background pointers also broke the low-resolution reconstruction.
The exact tester state replayed cold with all twenty Walk poses in each
direction, no missing installed DK materials, and no reconstruction mismatch.
This was host cache history, not corrupt serialized guest memory.

A controller traversal reproduces the exhaustion. In the wide old/new A/B,
**eligible HD frame 637** is the first difference: 2,617 mismatched source
pixels in the old build versus zero in the candidate. After warming and a
normal scripted reload of the exact tester state, the next 360 left/right
frames accumulate **1,983,782 mismatched pixels in the old build, zero in all
three candidate repeats**. The 22 paired motion samples visibly show original
walking sprites in the old build and HD poses in both directions in the fix.

Private evidence root: `build/hd-slice/left-fallback-20260907/`.
`live-cache.json` contains the original cache inspection; `warm-exact-results.json`,
`warm-walking-comparison.jpg`, and the paired `warm-exact-*/motion-*.ppm`
files contain the A/B evidence. All raw saves, images and material data remain
private build artifacts.

## Change

`runner/dkc1_hd_scene.c` retains the fixed 4,096-entry limit and evicts the
least recently used unpinned entry. Before decoding a frame, it pins all
retained background chunks: an unchanged atlas can skip decoding and still
hold those pointers. Every cache hit/new material is then pinned for the
current frame. Previous object pointers are rebuilt before presentation.
Both old and new background chunks plus all objects fit below capacity.
Eviction frees both native and HD rasters. Allocation occurs before replacing
a valid entry, so failure cannot leave a cached key with a NULL native raster.

Increasing the limit would only postpone the failure. Clearing the whole
cache would repeatedly reload active assets and invalidate retained pointers.
The bounded reuse policy addresses the lifetime problem directly. Rendering
workers still read immutable completed frames. No cartridge, guest-memory,
OAM grouping, widescreen policy, source art or material-file change was made.
The HD compositor remains default-off outside the private preview resources,
with the unchanged `$0016` scene guard.

## Validation

- 108 replay legs pass: fresh entry, both idle/chest cycles, both bounce spins,
  directions, barrel pickup/carry/throw both ways and carrying jumps. Each case
  uses native/wide, HD off/on and three repeats. All seven guest hashes match;
  enabled output is deterministic. All 60 outputs comparable to the previous
  bounce/idle build have identical guest and HD hashes.
- The new 3,347-frame `cache-pressure` case adds 12 passing legs. The cache
  genuinely fills in both widths. Final cumulative evictions are 3,512 native
  and 3,859 wide, with zero cache failures. All 966 post-reload idle/movement
  frames reconstruct exactly. The earlier traversal still has 799/720 fallback
  pixels respectively, including guarded transitions/partial sprites; these
  are not claimed repaired. The fresh-entry case retains its known 184 pixels.
- Exact-state warm A/B: one old-build replay and three candidate repeats.
  Guest frame, WRAM, VRAM, CGRAM, OAM, source OAM and audio hashes all match.
  Left margin, native 256-pixel center and right margin independently match
  (`native-regions.json`). Candidate HD outputs repeat identically.
- The actual cache implementation passes address/undefined-behavior sanitizer
  checks for full capacity, safe refusal when all entries are in use, repeated
  turnover, retained background pointers and current-frame object/cache-hit
  protection. Full Python suite: **258 tests, one skipped** (absent private
  SuperZSNES state5 evidence). The repository's upscale venv supplies NumPy;
  system Python lacked it on the first attempt. CTest has no registered tests
  in this build tree. Native/headless builds and `git diff --check` pass.
- Native-window QA used a real traversal to fill the cache, then normal F12
  loads of four controller-derived walking snapshots (two per direction).
  All four display HD with zero reconstruction mismatch. See
  `live-warm/{left-a,left-b,right-a,right-b}.png` and `pose-check.json`.
  Direct CUA key taps did not reliably become held SDL input; movement evidence
  comes from the recorded controller replays, not those taps.

This is a material lifetime fix within the supported Jungle slice. The
700-pose/53-group art review and full-game animation work remain open;
ropes, swimming, minecarts, scripted sequences and other entrances are not
promoted by this result. No shared widescreen capability or release default
changed, so this is not a claim of passing the 40-entrance promotion matrix.

## Identities and replay

| Item | SHA-256 |
| --- | --- |
| Supported USA ROM | `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15` |
| Immutable entry.state | `7aeaa4e44e714db99af7cbeb8e79e68ee0c072bf9b4e84ba35f5c927e3cc05da` |
| Exact tester state | `fddce5a8ea8f0c49485d03201ee2c60b70d4c2958236f9dd509e457971af1c91` |
| Old headless | `63ba41a4b2334190cde1f791df86b3b55c4aad5908687ddf007b8301e5b4b22f` |
| Candidate headless | `eda806fed104bc00064e7a9c43b77f5eed701b7dc83e447b6b4fa027e48d4ea2` |
| Installed signed preview executable | `e1fe4b1879b02ee998cbdc70d73878072b66ff72f51a94fc6d74f6438fa16281` |
| Unchanged 1,319-file HD pack | `f797992a1106b871efbc54e00f936b5777903854f1e5297cbce6582ef341d876` |

```sh
build/hd-slice/upscale-venv/bin/python -m unittest discover -s tests -v
python3 tools/verify_hd_scene.py "$DKC1_ROM" build/hd-slice/cache-check \
  --pack build/hd-slice/scene-pack-nano-bounce-idle \
  --cases cache-pressure --jobs 3
```

`warm_compare.py` and `warm-exact.dks` in the evidence root preserve the exact
private-state A/B command and schedule. `verification/results.json`,
`cache-verification/results.json`, `prior-build-comparison.json`, `install.json`
and `evidence-hashes.json` record the results and identities.

Final installed artifact: **DKC1 HD Nano Preview.app**, same bundle ID.
PID **7863** is paused at the preserved tester position after normal F12
restore, with all input schedules cleared. The cache remains full: 3,564
reuses and zero failures at final observation. The private quicksave is the
original preserved tester state. F7 resumes play. See
`live-warm/final-observation.json`; live state can change if the user resumes.
Nothing was committed or published. The reviewed art selection remains
208 DK poses / 416 facing materials from the preceding bounce/idle repair.
