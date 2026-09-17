# Widescreen adoption review — 2026-09-06

Implementation follow-up: [Aquatic backports and validation](WIDESCREEN_AQUATIC_BACKPORTS.md). The observations below describe the pre-backport review.
DKC1 can adopt concrete improvements from the newer DKC2/DKC3 checkouts. The highest-priority transfer is their pixel-level separation of native tiles and widescreen tiles, followed by their handling of per-scanline horizontal scroll. The current DKC1 aquatic reproduction changes pixels inside the native viewport at both boundaries. This happens even when every terrain lookup succeeds.

This is an investigation and adoption recommendation. No rendering, gameplay, generated code, or engine revision was changed. The user identified missing/repeated scenery but did not supply a new exact state; the runtime checks below use the preserved Croctopus Chase investigation and its controller-only map re-entry.

Reviewed local revisions: DKC1 `ee6d662` with engine `088bb05`; DKC2 `24425bf` with engine `3a929cd`; DKC3 `5afdacb` with engine `3b24b9d`. All three parent checkouts contain unrelated work in progress. The comparison uses their actual working files. DKC3's waterfall decoder is currently an uncommitted change; its river alias, wall continuation, and the engine boundary handling are already in its HEAD. Full revisions, source hashes, commands, input hashes, and existing working-tree status are recorded in the private [manifest](/Users/briantate/Documents/GitHub/DKC1Recomp/build/reviews/aquatic-widescreen-20260906/manifest.json).

## 1. Adopt native/margin selection per pixel first

DKC1's [WsShadowTileDebug](/Users/briantate/Documents/GitHub/DKC1Recomp/snesrecomp/runner/src/snes/ws_shadow.c:2163) deliberately returns a shadow tile for a whole 8-pixel chunk crossing a native boundary. Its [4bpp renderer](/Users/briantate/Documents/GitHub/DKC1Recomp/snesrecomp/runner/src/snes/ppu.c:900) then draws that tile across the entire chunk. A cache hit does not prove that the returned tile equals the cartridge tile at the native pixels.

DKC2's [PpuDrawMixed4bppTile](/Users/briantate/Documents/GitHub/DKC2Recomp/snesrecomp/runner/src/snes/ppu.c:713), also present in DKC3, retains both tiles and selects the cartridge tile for native pixels and the shadow tile for margin pixels. It handles the presentation inset when the host view shifts. This directly addresses the boundary failure mechanism found here. DKC2's [journal](/Users/briantate/Documents/GitHub/DKC2Recomp/docs/IMPLEMENTATION_JOURNAL.md:4240) records the same bug class during camera reversal.

Current DKC1 evidence, using `DKC1_WIDESCREEN_EDGE=shift` with zero actual bias so the native comparison is aligned:

| Branch | Frame | Changed composite center pixels | Changed BG1 center pixels | BG2/BG3/OBJ center differences |
| --- | ---: | ---: | ---: | --- |
| Preserved state, zero-frame render | 312762 | 475 | Not isolated at this frame | Not isolated at this frame |
| Preserved state, existing layer tool advances one frame | 312763 | 475 | 483 | 0 / 0 / 0 |
| Fresh map re-entry, then layer tool advances one frame | 314203 | 519 | 520 | 0 / 0 / 0 |

Every changed BG1 pixel lies in native columns **0–4 or 253–255**. Exact zero-frame native/wide WRAM, VRAM, CGRAM, and both OAM hashes match. The fresh-entry recurrence means this is not solely old serialized margin corruption. These observations identify a current rendering defect; a backported candidate has not yet been built or validated.

The default `glide` policy applies a 9-pixel bias at the preserved position. Comparing its unaligned centered crop to native produces many intentional positional differences. Those are recorded separately and are not counted as the boundary defect.

## 2. Adopt live horizontal scroll in the shadow world key

DKC1 computes the shadow lookup X as `layer->worldX + screenX`, although it receives the live `hScroll`. DKC2/DKC3 add the signed, wrapped difference between that scanline's scroll and the frame anchor through [WsShadowPresentWorldX](/Users/briantate/Documents/GitHub/DKC2Recomp/snesrecomp/runner/src/snes/ws_shadow.c:1352).

This matters when HDMA distorts an aquatic background: the PPU can sample one tile while the shadow addresses its neighbor. A wrong but populated tile still reports a successful lookup. Port the coordinate calculation together with consistent source-pixel phase where required, preserving DKC1's scene-local origins. This is a confirmed code difference and a strong aquatic candidate; this review did not independently measure the failing scanline's register delta or test a candidate fix.

## 3. Adopt map-based wall continuation for repeated rock blocks

DKC1's [vertical-boundary rule](/Users/briantate/Documents/GitHub/DKC1Recomp/runner/dkc1_game.c:1232) checks an empty target and a full source on corroborating rows, then [copies the source metatile](/Users/briantate/Documents/GitHub/DKC1Recomp/runner/dkc1_game.c:1831), retaining its local 8x8 position. This can repeat distinctive scenery.

DKC2/DKC3 instead consult the map's actual neighboring metatile placements, choose a populated outward successor, and follow that sequence across the margin. See [Dkc2MetatileSuccessor](/Users/briantate/Documents/GitHub/DKC2Recomp/runner/dkc2_game.c:740) and [Dkc3MetatileSuccessor](/Users/briantate/Documents/GitHub/DKC3Recomp/runner/dkc3_game.c:1138). Their structural checks additionally require a wall two metatiles thick and distinguish long sealed voids from openings. DKC2's [documented example](/Users/briantate/Documents/GitHub/DKC2Recomp/docs/IMPLEMENTATION_JOURNAL.md:5062) was a copied lamp panel appearing repeatedly where rock should continue.

Adapt the principle to DKC1's ROM map and independent definition bank; do not import DKC2/3's WRAM layouts or constants. Adjacency is a constrained extrapolation, not proof of the original offscreen artist's intent. Keep partial openings protected and validate each affected boundary visually. The route tested here records **zero boundary-continuation tiles**, so this recommendation addresses the reported repeated-scenery class rather than explaining the measured native-edge defect.

## 4. Add richer background classification only where needed

DKC1 mostly selects layer policy from frame-start enables and physical tilemap width. DKC2's [HDMA scanner](/Users/briantate/Documents/GitHub/DKC2Recomp/runner/dkc2_hdma.h:8) reads the upcoming scanline bands without changing guest state. Its [band classifier](/Users/briantate/Documents/GitHub/DKC2Recomp/runner/dkc2_game.c:1990) distinguishes world terrain, authored static planes, and repeating effects. Physical width alone does not establish which one a layer is.

DKC3 goes further: its [row alias verification](/Users/briantate/Documents/GitHub/DKC3Recomp/runner/dkc3_game.c:346) checks that reflection rows really correspond to decoded terrain before sharing the terrain store. Its [waterfall verifier](/Users/briantate/Documents/GitHub/DKC3Recomp/runner/dkc3_video.c:699) validates a dedicated ROM stream against complete native columns before extending it. These are useful designs for a proven secondary-layer problem, not evidence that DKC1 needs a DKC3 waterfall decoder.

## Validation performed and limits

The existing macOS headless build is current according to Ninja (`no work to do`). The supported ROM SHA-256 is `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`; executable SHA-256 is `582114030a4ef4d3bd6d8e1880c61ba32cfbc760b90929f7e06f6e076bd799bb`. Original states were copied into a separate review directory.

- Three 720-frame exact-state traversals: identical per-frame traces, final frame/memory hashes, and audio fingerprint; 508 extended frames per run, zero terrain misses.
- Three 720-frame controller-only fresh entries: the same determinism checks pass; 626 extended frames per run, zero terrain misses.
- Existing trace analysis reports zero policy violations and zero raw-fallback frames for both routes. Remaining centered frames are included in the evidence, not counted as extended passes.
- The existing layer-capture source was linked against the current headless objects under a private filename because the macOS CMake targets do not expose that tool. It reloads the root for every plane and advances one frame; the exact frame identities are recorded above.
- Internal composite and layer images were inspected. No live application window was opened; no live-play acceptance or full 40-entrance matrix is claimed. No candidate backport or full unit suite was run for this review. `git diff --check` passes.

Evidence: [runtime summary](/Users/briantate/Documents/GitHub/DKC1Recomp/build/reviews/aquatic-widescreen-20260906/summary.json), [layer comparison](/Users/briantate/Documents/GitHub/DKC1Recomp/build/reviews/aquatic-widescreen-20260906/layer-center-comparison.json), [native-edge difference image](/Users/briantate/Documents/GitHub/DKC1Recomp/build/reviews/aquatic-widescreen-20260906/shift-center-diff.png), and [fresh-entry composite](/Users/briantate/Documents/GitHub/DKC1Recomp/build/reviews/aquatic-widescreen-20260906/fresh-layers-shift/composite.png). Images, states, ROMs, and binaries remain private under ignored `build/` paths.

Recommended implementation order: native/margin pixel separation, live-scroll world coordinates, then a boundary-specific adjacency experiment. Preserve DKC1's existing provenance diagnostics, scene-local cache origins, state compatibility, and stock cartridge streaming. Each shared renderer change needs the native oracle, three-repeat determinism, fresh entry, transition sentinel, full 40-entrance capability floor, and visible application QA before promotion. A blanket engine revision replacement would combine substantially more changes than this finding establishes.
