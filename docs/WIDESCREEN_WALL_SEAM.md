# Coral Capers left-margin wall seam — September 6, 2026

The supplied save shows a vertical join 11 source pixels from the left edge at 16:10. The local playtest app now continues the adjoining wall with its own authored pattern, confined to that offscreen strip. This is a narrow, default-off presentation capability, not a general repair of populated map art.

## Reproduction and source evidence

Private evidence is under `build/repros/water-seam-20260906/`. The screenshot and original quicksave were copied before any run; the diagnostic work never overwrote the normal slot. A later observed version of the slot was preserved separately as `inputs/quicksave-latest.state`. The original visible window and executable bundle are preserved there.

- Supported ROM SHA-256: `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`.
- Supplied state SHA-256: `82d7d898aa8ebd0d5a971e538db900f10f20406a210afb1922a9d5acc6ca88c5`.
- Baseline executable SHA-256: `e4c8551cfe6bdf020cd9e4c47e3daed1dedcf803802d09342756238550108bad`.
- First observed frame: **34978**, mode `$0003`, level `$0061`, entrance `$00BF`, camera **943,10113**, 308×224 source, Glide policy with zero current bias. The cartridge is paused in the supplied state. This is the first observation, not a claim about when the defect began.
- Map source `$E9`, definition source `$D0`, both published bases zero. The native terrain decoder matches **224/224** calibration samples. Map records and the RainbowZ level catalog identify entrance `$BF` as **Coral Capers**; older records that called this tuple Croctopus Chase should not be used to name this reproduction.

At world X **928**, the rendered strip crosses from column 28 to column 29 of the original 32×32 metatile map. Column 28's upper cells belong to a neighboring corridor. They meet the repeating rock wall in column 29 with an abrupt art discontinuity. Both columns contain real ROM tiles: this is neither a stale cache nor a missed VRAM upload. The existing empty-cell wall continuation correctly does nothing. Cold rendering reproduces the same seam, and Flat/Raw pixels show it before Reconstruct.

The same wall continues down to rows **325–327**, where its west-side cells join correctly. Their three-row phase matches column 29 at rows **316–322**. `map-original.png`, `map-continued.png`, raw VRAM/CGRAM, isolated planes, and the private source-map probe record that relationship. Atlas lookup was attempted but the local disassembly instruction index is absent; no cartridge-routine semantics are inferred from its names.

## Narrow correction

`DKC1_WS_WALL_SEAMS=1` opts into a verified authored junction capability in `runner/dkc1_wall_seams.h`, used only by host prefill in `runner/dkc1_game.c`.

The capability requires the accepted vertical layout, exact map/definition banks and bases, and all **24 metatile cells** of the two-column, twelve-row junction—including the donor strip—to match. A mismatch fails closed to the existing output. The target must be column 28, rows 316–322, wholly beyond a native left edge in column 29. It reads the corresponding source cell from rows 325–327; it does not generate pixels, change the ROM, or substitute art inside the native viewport. Already-correct lower rows and other columns are untouched. No scene tuple grants broader permission to alter populated terrain.

The correction uses the wall's complete three-row pattern, preserving the vertical phase. Copying the nearest individual edge tile would repeat distinctive fragments; softening the join in the graphics shader would hide the source mismatch and affect unrelated pixels. Global adjacency replacement was rejected because populated offscreen cells can contain legitimate terrain. The explicit source capability is deliberately limited to this verified junction, pending broader evidence.

Trace feature bit **16** and `wall_seam_tiles` record activation. The flag is excluded from serialized guest state, is off when absent, and is independent of the four earlier aquatic switches. No initializer, streamer, activation, collision, gameplay bounds, engine, or generated-source changes were made in this fix.

## Validation and limits

- Exact-state A/B changes **2,105 composite pixels**, all within left source columns **0–10** at 16:10. The entire 256-pixel native center and right margin match exactly. WRAM, VRAM, CGRAM, and both OAM hashes are unchanged. At 16:9, 5,468 composite pixels change only in the left margin.
- Isolated BG1 changes only on the left; BG2, BG3, and OBJ are exact. Native 4:3 remains byte-identical across all five surfaces. Three independent candidate layer captures at each aspect are identical (`layer-comparison.json`). Layer capture advances one canonical frame, so these are frame 34979; the zero-frame A/B separately covers 34978.
- Stay, Down, Up, and Right routes pass at both **308×224 and 342×224**, with three independent candidate replays each and a disabled oracle at the same aspect. Every per-frame center, right margin, and guest-memory hash matches; final states, frames, and full traces repeat exactly. Down and Right leave the patch region, ending at cameras 880/10513 and 1344/10309 respectively. Complete input schedules and hashes are in `validation-summary.json`.
- Exact/cold state loads agree. The transition sentinel passes **34 samples over five boundaries** of the prior controller-only aquatic entry/traversal flight bundle, with no failure bundle. This cross-layout bundle covers Croctopus, not the new Coral Capers junction.
- The four available authentic entrance anchors run three repeats through 360-frame entry settle plus 360-frame continuation. They retain the pre-existing native/wide machine-divergence investigation status; there are zero hard failures. None exercises this Coral Capers source capability. The complete 40-entrance floor remains unavailable and must not be claimed as passed.
- Unit/model suite: **241 tests, no failures, one skip**. The new model rejects every changed source cell, missing reads, wrong orientation, wrong target/edge columns, native overlap, and rows outside the patch. Host/headless builds and whitespace checks pass.
- Actual non-headless before/after windows were inspected at the preserved save. The candidate uses the user's Reconstruct mode 3, strength 100, softness 86, shading 99; `live/corrected-window.jpg` shows the seamless join.

**Fresh-entry limitation:** a clean pre-entry anchor for Coral Capers was not available. Controller-only exit/death probes from the supplied state did not reach a map transition. Those probes are not labeled fresh-entry proof, and the four other entrance anchors do not substitute for it. The fix addresses the ROM-authored junction rather than rewriting serialized corruption, but a fresh Coral Capers entry remains an outstanding promotion gate. All source defaults stay off; the normal local app enables this narrowly guarded fix for the user's requested playtest.

## Commands and local use

```sh
cmake --build build/macos --target dkc1_snesrecomp_headless dkc1_macos --parallel
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -v
git diff --check
# Retain the other four aquatic switches when comparing this playtest build.
DKC1_WS_WALL_SEAMS=1 DKC1_ASPECT=16:10 DKC1_WIDESCREEN=1 \
  DKC1_SAVESTATE_INPUT=/absolute/preserved/quicksave.state \
  DKC1_FRAME_PPM=/absolute/output/frame.ppm \
  build/macos/dkc1_snesrecomp_headless /absolute/verified-rom.sfc 0
```

`baseline-manifest.json`, per-run environment/input files, `layer-comparison.json`, `validation-summary.json`, and `final-manifest.json` record identities and commands. The normal app preserves all graphics/control preferences and the original save slot. Source remains uncommitted for user verification. Disabling only `DKC1_WS_WALL_SEAMS` restores the prior junction behavior.

The rebuilt normal bundle passed strict signature verification and was relaunched. Final live inspection showed the loaded quicksave in 16:10 composite, with the player already beyond the original junction; play was left undisturbed. `final-manifest.json` records the executable and both preserved save identities. The saved corrected-window capture is from the exact-state visible QA run.


## Right-facing junction follow-up — September 6, 2026

A later save exposes the **opposite face of the same authored junction**. The native camera is now left of the wall, so extending the west-facing pattern would be incorrect. Private evidence is in `build/repros/water-right-seam-20260906/`; both the supplied image and current slot were copied before testing.

- State SHA-256: `18eeac13727416f8fd442f931cd933a068ec8c858c56695f29a8cfa20c4b2ea0`.
- Baseline app executable SHA-256: `e9de05b7aa7c0df2de7f5e9ea57973e38ba18dd4ad8b1189f96258ed36fb5a73`.
- Candidate normal app executable SHA-256: `558ae6f03814df929ddb506df5afadf52327c57746159dfae369d40e0cf25863`.
- Same supported ROM hash as above; first observed frame **78551**, mode 3, level `$0061`, entrance `$00BF`, camera **671/10048**, Glide bias 0, 16:10 (308×224). Exact neutral zero-frame replay reproduces the supplied seam. Cold-load output is identical.
- The native image ends at world X926. The source boundary at X928 joins column 28 to columns 29–30. Rows 313–315 contain unauthored empty margin cells, producing the blue gap; rows 316–322 contain the neighboring passage's mismatched wall. The vertical decoder still matches 224/224 native samples. Raw VRAM/CGRAM, isolated BG1, and the actual window establish a presentation defect, with no evidence of guest corruption.

`Dkc1EastWallSeamSourceMatches` requires all 36 original cells of columns 28–30, rows 312–323, plus 18 unique donor cells. The donor triples at `(50,306/307/308/310/311)` and `(32,300)` are authored within the same Coral Capers map. Each donor's first cell must exactly equal the target's native edge cell, including flip bits. The two following cells supply its matching rock interior. This repairs both empty and populated side art without treating other populated cells as disposable. `map-junction.png`, `donor-wall.png`, and `layer-comparison.json` preserve the visual and byte evidence.

`Dkc1EastWallSeamDonor` accepts only offscreen columns 29–30, rows 313–322, with native right edge in column 28. Every native pixel remains owned by the original renderer. The previous west-facing capability remains independent and unchanged. The existing default-off `DKC1_WS_WALL_SEAMS` switch and trace count cover both directions; source defaults, cartridge state, streaming, activation, and collision are unchanged.

Validation on this extension:

- Exact 16:10 A/B changes **4,982 pixels**, confined to source X282–307 in the right margin. 16:9 changes **8,375** right-margin pixels. BG1 accounts for every changed pixel; BG2/BG3/OBJ are identical. The left margin and native center are byte-exact. Native 4:3 is unchanged on all five captured surfaces. Each candidate layer capture repeats identically three times.
- Stay, Down, Up, Right, and Left input routes pass at both 308×224 and 342×224, each with three identical candidate repeats against the preserved pre-extension executable. All per-frame center/left and guest-memory hashes match, including WRAM, VRAM, CGRAM, PPU OAM and WRAM OAM. Up reaches camera 671/9897; Left reaches 212/10048 and leaves the repair region. Down and Right remain against the floor/wall at 671/10048 and are not claimed as camera traversal. Exact schedules and final hashes are in `validate.py` and `validation-summary.json`.
- The original left-seam save's four routes at both aspects remain pixel-exact against the previous implementation on all regions and guest-memory hashes (`previous-seam-regression.json`).
- Transition sentinel: **34 samples, five boundaries, passed**. Four available clean entrances retain zero hard failures with their pre-existing investigation status. The full floor check still reports 36 missing entrances; none of these runs supplies a clean Coral Capers entry.
- Unit/model suite: **241 tests, zero failures, one skip**. The new checks reject each changed or unavailable target/donor cell and every unsupported row, column, native edge or output pointer. Host/headless builds and strict normal-app signature verification pass.
- The rebuilt **normal app** was reopened at this exact immutable save with the user's existing Reconstruct preferences. `candidate/normal-app-window.jpg` confirms the right wall and blue-gap correction in the actual window. The original normal quicksave remains unchanged. No scheduled controller input was installed, and the app was left at the user's save for playtesting.

This remains a local opt-in fix with exact-state and nearby-scrolling validation. A fresh Coral Capers entry is still an outstanding promotion gate. No commit or release was made. `manifest.json`, `final-manifest.json`, `gate-commands.json`, and the layer/route reports contain identities and repeatable commands.


## Remaining fine lines: original raster effect

The user's subsequent `remaining-lines.jpg` marks finer lines inside the native image, after the large margin seam and blue gap were corrected. This is distinct from the donor-junction repair. A private reference-renderer capture at the same immutable save is byte-identical to the current native output on composite, BG1, BG2, BG3 and OBJ. An independent ROM tile decode, using the actual per-scanline scroll registers, matches all **6,294 nonblack source pixels** checked around the marked wall (allowing only SNES color-expansion rounding).

The original underwater HDMA alternates BG1 scroll between 671/832 and 672/833. At this frame its transitions fall at output rows 7, 17, 38, 60, 71, 81, 102, 124, 135, 145, 166, 188, 199 and 209. Those discrete one-pixel steps can make hard texture joins visible under Reconstruct. This is not evidence of another native streaming failure, nor justification for replacing more map cells.

`raster-probe/analysis.json` and its raw scanline log retain the reference comparison. A **private preview only** holds terrain scroll steady during each render call, then restores live scroll before the next HDMA operation. Other layers retain their original animation, and all five guest-memory hashes match. The preview was rendered with the user's Reconstruct mode 3, strength 100, softness 86 and shading 99. It removes the discrete terrain ripple; it does not claim to remove every edge in the original texture. The normal app still retains the cartridge effect. Applying a deliberate native-image cosmetic change is awaiting the user's choice under the repository's native-center rule.
