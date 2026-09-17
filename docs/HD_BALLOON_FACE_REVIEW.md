# Floating balloon face restoration

September 16, 2026. This corrects private replacement art only. The user reported
that the floating red life balloon was a blank oval. Source comparison confirms
that the earlier Nano result omitted DK's sculpted eyes, brow, muzzle, mouth and
hair tuft. The source sprite contains those features; the compositor was not
removing the face.

Evidence root `P` is
`build/hd-slice/nano-level-20260916/polish/balloon-face-20260916`.
`P/comparison.png` compares original, previously installed and corrected art.
The exact source atlas, Nano Banana Pro prompt, provider result and registration
are retained in `source-sheet.png`, `job.json`, `provider-result.png` and
`registration.json`. One 4K generation cost 150 credits; no other upscale model
or second enhancement pass was applied. The prompt explicitly preserves the
same-color sculpted gorilla face, exact cell layout, silhouette, string, knot,
color and animation orientation.

The installed selection is **27 LifeBalloon source frames / 54 facing keys**:
eight idle poses plus the existing special pose in each of red, green and blue.
The generated sheet also contains HUD reference frames, but those are not
installed by this change; the user identified the floating level balloon.
Original source hashes, dimensions and anchors are preserved. The maintained
`hd_sprite_pack.pack_registered` builds exact source and mirrored keys. Existing
source-constrained matte cleanup avoids reintroducing gray contour pixels.
All 27 silhouette checks pass; interior face quality is reviewed visually,
not inferred from the silhouette metric.

`P/scope-check.json` confirms that exactly those 54 materials change. Every
other material is byte-identical to the accepted scrolling preview pack.
Hardlinked files are unlinked before replacement, so the prior pack and app
remain intact. No runtime code, gameplay state, scene guards or defaults change.

## Runtime evidence

A controller-only route from the immutable Jungle entry reaches a visible red
balloon. `P/canopy-47/route.inputs` records the complete schedule. Saving after
551 frames creates `P/balloon.state`; the following frame visibly displays
`LifeBalloon_Idle6_pal8878`, whose exact installed material is confirmed in
`P/encountered.json`. `P/before` and `P/after` retain original memory companions,
source rasters, framebuffer outputs and matching guest hashes for that state.
No supplied save placed the player directly at the balloon; this review anchor
was derived from clean entry without memory edits.

- 22 sprite pack/alignment/matte tests pass (`P/pack-tests.log`).
- 12 fresh-entry replay legs: 600 frames, native/wide, HD off/on, three repeats.
  Guest hashes and HD repeat hashes are identical; **3,600 raw CPU/Metal frames
  match exactly**, with complete preload and zero gameplay texture reads.
- Native Metal preview visibly shows the restored red face in the level.
  The remaining color variants and facings were reviewed in the registered
  atlas; live green/blue pickup encounters are not claimed.
- The new canopy route still exposes unrelated coverage limitations: wide BG
  missing samples total 3,198 across 38 frames; original reconstruction fallback
  totals are 239 native / 575 wide. Missing enemy/composite art also remains.
  Passing the art-change replay is not a claim that these existing limitations
  or balloon pickup behavior were fixed. No gameplay changes were made.

## Identities and commands

- ROM: `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`.
- Clean entry: `7aeaa4e44e714db99af7cbeb8e79e68ee0c072bf9b4e84ba35f5c927e3cc05da`.
- Balloon anchor: `66dd2c7c0826f5e4ce2c6ec900312f10480882c9758c078bfe8acf7fd0620682`.
- Headless executable: `86cb6a5ea7bf67f2681475d89669d7626700253d2285d34665dfee3a8c263a6f`.
- New pack: `405cb0829a27648a9b16c238460a688428259512ba08a230e9314333627a764c`.
- Signed native executable: `e4f79e89789b7da9a28221e6330bc35e44dc02f8f201a43a98b78e07ad42bf0b`.

The pack still has 23,253 materials / 1,676,725,888 resident pixel bytes.
Full per-run memory/image hashes are in `P/fresh-verification/results.json`.
Reproduce with a new output directory:

```sh
P=build/hd-slice/nano-level-20260916/polish/balloon-face-20260916
python tools/verify_hd_scene.py "$ROM" "$P/verification-new" \
  --pack "$P/materials" --preload --exact-centers --connected-world \
  --metal --polish 65 --coverage --cases replay \
  --input-play "$P/canopy-47/route.inputs" --frames 600 --jobs 1
```

`build/macos/DKC1 Jungle Balloon Faces.app` contains the corrected pack and
starts paused at the balloon anchor, using `P/user` for its private save. F7
resumes and F12 reloads its review anchor. There is no scheduled controller
playback. Earlier tester saves and preview bundles are preserved. No commits
or private assets were staged. Bundle settings and final UI state are recorded
in `P/app-build.json`.
