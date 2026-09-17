# Aquatic widescreen backports — 2026-09-06 candidate

The DKC2/DKC3 comparison led to three independently selectable presentation changes. They are implemented and available in the local **DKC1 Widescreen Candidate.app**, but remain **off by default** in ordinary builds. The complete 40-entrance promotion floor has not passed. This is a tested aquatic candidate, not a claim that every underwater wall or every level is fixed.

## Changes and ownership

| Switch (exact value `1`) | Change | Current evidence |
| --- | --- | --- |
| `DKC1_WS_PIXEL_BOUNDARIES` | The normal 8x8/4bpp PPU renderer keeps both cartridge and shadow tiles for chunks crossing the native viewport. It selects each pixel with its own palette, priority, flips, and transparency. Native insets are respected. Provenance marks only shadow pixels. Existing repeat bands bypass world-shadow lookup when rendering their native source. | Model covers all eight fine-scroll phases, both biased boundaries, tile attributes, transparent pixels, and provenance; aquatic composite and isolated planes preserve the native image. |
| `DKC1_WS_LIVE_SCROLL` | Shadow X includes the signed, wrapping 10-bit difference between the live scanline scroll and its capture anchor. DKC1's host view bias is subtracted once because world X already includes it. The unbiased capture scroll and scene-local origin stay intact. | Model covers wrap, positive/negative deltas, both view biases, and snapshot compatibility; exact and fresh aquatic replay checks pass. |
| `DKC1_WS_WALL_ADJACENCY` | For a structurally proven vertical wall, use directional neighbors found in the current source map instead of copying a distinctive edge block. Require populated source/behind cells on corroborating rows and an unused side region. Preserve partial openings, short doorways, thin decorations, tied successors, and unknown chains. | Pure topology and adjacency models pass. Available real-state routes never reach a continuation: runtime wall-art acceptance remains outstanding. |

The engine adaptations are limited to `snesrecomp/runner/src/snes/ppu.c` and `ws_shadow.[ch]`; the DKC1 wall model is in `runner/dkc1_terrain.h`, with bounded ROM readers in `dkc1_video.[ch]` and host-only prefill in `dkc1_game.c`. No cartridge initializer, streamer, object scanner, collision, or generated source was changed. Host policy flags are excluded from serialized cache state. The engine's sprite model received the required API stubs and a correction to its existing premature free/failure check so its final cases actually execute safely.

Adjacency is rebuilt from this frame's complete visible source rows within the published horizontal camera span. It never scans an arbitrary ROM bank for reusable patterns. Cell orientation belongs to the key; a successor must have a unique highest observed frequency. A failed chain retains the original decoded empty tile, rather than reverting to the old copied wall. This remains constrained extrapolation and needs a real wall reproduction before promotion.

The review's fourth recommendation was conditional secondary-layer classification. The affected BG1 uses world terrain; BG2 already uses a repeat policy, and BG2/BG3/OBJ native captures match. The backport now respects the existing scanline repeat-band classification when selecting a shadow source. No DKC3-specific waterfall/reflection decoder or unsupported layer classification was introduced.

## Reproduction and A/B evidence

Private evidence root: `build/repros/aquatic-backports-20260906/`. The original review's ROM, immutable inputs, state hashes, and baseline executable are preserved separately under `build/reviews/aquatic-widescreen-20260906/` and `baseline/`. Save slots were not overwritten.

- Supported ROM SHA-256: `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`.
- Parent source: `ee6d662b75021acfdd0592324aab0acf01344f57`, engine base `088bb05cf982617727e5c76151add4d8ed35732d`, plus the recorded uncommitted patch. Final binary/source/state hashes and the launch identity are in `final-manifest.json`.
- Exact branch: `exact-underwater-live.state`, SHA-256 `8b3e37878023a9167d7684cfd878b59c48d6ba83a7b8877a219261e0e7841019`; mode 3, level `$25`, entrance `$3F`, camera 274/5184. The defect is present at the first observed frame, 312762; this is not a claim about when it originally began.
- Fresh branch: controller-only entry from `map-after-checkpoint-exit.state`, SHA-256 `ce478c757b5c6486c8edcdf0871ec8e920912a181c70fa636a1ed54b38345807`, using the preserved `enter-croctopus.inputs` schedule. The route filename is retained for reproducibility; raw scene IDs are the source of identity.
- Exact schedule: Start for 1 frame, neutral 29, `$081` 210, `$041` 210, `$091` 120, `$0A1` 120, neutral 30. Fresh schedule: `$001` 6, neutral 714. Both are 720 frames.
- Native oracle uses `DKC1_WIDESCREEN_EDGE=shift` to remove intentional glide bias. `shift-ab/runtime-ab.json` records commands and inputs. `runtime-ab.json` is the earlier **glide** pass; its unused `DKC1_WS_EDGE_POLICY` spelling did not override the policy, as its trace confirms. Its unaligned center counts must not be read as defects.

The first exact zero-frame A/B changes **475** native composite pixels with the old renderer; pixel-boundary mode alone reduces that to **zero**. Live-scroll mode alone also resolves that exact frame. Wall adjacency alone has no effect there. The exact layer-capture frame originally had **483** BG1 differences, and the fresh-entry layer frame had **520**; both now have **zero**. BG2, BG3, OBJ, and the final native composite also match pixel-exactly.

`validation-summary.json` records:

- Three independent 720-frame replays of each branch at both **342x224 (16:9)** and **308x224 (macOS 16:10)**. Per-frame trace, final frame, guest memories, and audio fingerprints repeat exactly.
- All five per-frame guest memory hashes match the disabled candidate at the **same aspect**, as does audio. Cross-aspect OAM/WRAM equality is not assumed; existing widescreen object presentation differs with side extent.
- All **48** sampled native centers (every 60 frames, both branches/aspects) are exact. Terrain misses remain zero on extended frames. The candidate with switches disabled matches the preserved old executable's frame, memory, and audio results on both 342-pixel routes.
- Fresh-entry subset: map entrances `$003E`, `$00A7`, `$006D`, `$0024`, 360-frame entry settle plus 720-frame neutral continuation, three independent native and wide runs. All four widescreen grades pass and both sides repeat. The suite's overall `investigate` status is retained: its native/wide branches differ in machine state. The preserved baseline has the same status and identical corresponding guest hashes; this candidate does not resolve that pre-existing comparison limitation.
- `check_widescreen_capability_floor.py` fails **only for 36 missing entrances**. It does not constitute a whole-game pass. Boss, bonus, title/menu, and additional aquatic-layout acceptance remains outside this evidence.
- The existing flight recorder exported an actual visible map-to-aquatic run (405 frames). `verify_flight_bundle.py` validates it; `transition_contamination_sentinel.py` passes all **16 samples over four boundaries** at offsets 0/1/8/32, comparing retained/cold memories and five separate surfaces. See `transition-sentinel-final/`.
- Eight additional 480-frame directional swimming probes, plus preserved cave and alternate aquatic states, record zero wall continuations. They cannot establish wall-adjacency visual acceptance.
- The actual macOS window was inspected on fresh entry, then the final candidate was restored to the documented fresh-entry root and paused with no input schedule. `live-transition/window.png` and `candidate-window.png` are application-window captures, not substituted internal renders.

The snapshots are historical evidence; no blanket repair of serialized VRAM/OAM was added. The same native-edge correction also works after fresh entry.

## Validation and use

```sh
cmake --build build/macos --target dkc1_snesrecomp_headless dkc1_macos --parallel 4
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -v
git diff --check
git -C snesrecomp diff --check
```

The Python suite runs 235 tests with one skip; the two new model tests and the engine sprite-limit model pass. The final macOS bundle is ad-hoc signed and verified. Existing layer capture was linked against the same headless objects; its exact build command is retained in `layer-build-command.json`. Headless/layer capture now accept `DKC1_ASPECT=16:10` when widescreen is enabled, matching the macOS frontend.

For a controlled headless A/B, set any one switch to `1`, use `DKC1_WIDESCREEN_EDGE=shift`, and retain all other inputs and flags. The trace adds optional `presentation_features` bits 1/2/4 and `boundary_adjacency_tiles`; absence of the switch is the rollback. Complete commands are in each run's `run.json`, the stress reports, and the final manifest.

For local review, open `build/macos/DKC1 Widescreen Candidate.app`. Its private `LSEnvironment` enables the three switches, loads the preserved fresh-entry candidate state, and starts paused; **F7** resumes. It uses a separate bundle identifier and contains no ROM. The ordinary app's release defaults are unchanged. Source changes remain uncommitted, including the scoped engine worktree changes.

## Subsequent traversal and host work

The newer host ports and longer aquatic traversal are documented in [HOST_ADOPTION_IMPLEMENTATION.md](HOST_ADOPTION_IMPLEMENTATION.md) and [WIDESCREEN_WATER_FLASH.md](WIDESCREEN_WATER_FLASH.md). The updated candidate adds the fourth default-off switch, `DKC1_WS_SCROLL_REBASE=1`, to repair verified upward cache-boundary flashes.
