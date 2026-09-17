# Croctopus Chase upward-scroll flash — September 6, 2026

The longer swim reproduced brief black margins at two upward cache boundaries. The opt-in candidate rebuilds the calibrated host cache on the same frame, eliminating those flashes without changing the accepted native center or cartridge state. Whole-game promotion remains pending.

## Reproduction and cause

The live world map identifies the level as **Croctopus Chase**. This checkout reports the scene as mode `$0003`, level `$0025`, checkpoint entrance `$003F`; the map/pre-entry entrance is `$003E`. Older reports using `$0061/$00BF` are not the IDs observed here.

The exact turn root is `build/repros/host-adoption-20260906/water/turnroot/final.state`, SHA-256 `5586fc30fe762278fe0165a4506cd4e0e2ad354911a756877be568a138527ccb`. It was obtained with controller input from the preserved fresh-entry root. From this turn root, the continuous 420-frame route first flashes at relative frame **61**, absolute **314683** (camera 1109, 5115), and again at relative **182**, absolute **314804**. Exact schedule: Up+Right with B on for 6 frames and off for 12, repeated; the archived input files retain every transition.

The initial cache origin Y is 5120. At the first bad frame the unwrapped terrain Y is 5117. `Dkc1PrepareWidescreenShadow` rejects this value because it is below the finite cache window, even though **224/224** native terrain samples still match the ROM decoder. The rejection clears history and blacks both 43-pixel margins. The following frame establishes a lower origin and resumes widescreen. The same failure recurs below origin 4864. It is a **host presentation/cache failure**, not a missing cartridge upload or an object-lifecycle failure. Both the actual application window and raw frame sequence show it.

Reference atlas lookup was attempted, but this checkout lacks `reference/disassembly/DKC1/Pseudocode/instruction_index.csv`. The diagnosis rests on current host code, exact runtime bytes, and repeatable A/B evidence; no new symbolic claim about a cartridge routine is inferred from missing disassembly.

## Candidate correction

`DKC1_WS_SCROLL_REBASE=1` is a separate, default-off switch. Only a currently calibrated frame may rebuild an exhausted cache. The host selects an aligned replacement origin, clears old projected keys, captures current live tiles, and performs the existing verified margin prefill on that same frame. Soft calibration grace, stream-only bootstrap, unsupported scenes, and hard identity changes retain their fail-closed rules. No ROM or generated-code changes are involved.

The second crossing also exposed a one-pixel miss in the cold cache. With fine Y=7, the water HDMA moves VOFS by one pixel during the visible frame, so the final scanline touches the row after the generic 29-row capture. The rebuild additionally captures the native columns on that guard row directly from live VRAM. It does not extrapolate side art. An engine-backed model reproduces that missing partial edge tile and verifies the live guard-row capture.

Presentation readiness previously also controlled cartridge culling. On the rescued frame the implementation preserves the old one-frame cartridge readiness gate, while displaying the repaired pixels. This avoids silently changing activation while fixing presentation. All five per-frame guest-memory hashes verify that separation.

Trace additions: `presentation_features` bit 8 and `decision.cache_rebase`. `verify_shadow_localization.py` accepts an origin change only with an explicit calibrated cold commit and still rejects out-of-range keys, unmarked origin drift, or any terrain miss. Neither new policy nor diagnostics alter serialized guest state.

## Evidence and tested scope

Root directory: `build/repros/host-adoption-20260906/`. The supported ROM SHA-256 remains `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`. `validation-final/*/manifest.json` records exact executable, state, ROM, input, trace, final-state, and image hashes plus commands/environment. This final run includes separate disabled controls for both aspects.

- **Exact tester-state branch:** the original paused underwater state is preserved outside normal slots. Unpause for one frame, wait 29, then traverse for 1,860 frames. Both cache flashes are removed. No blanket repair of old serialized VRAM/OAM is added.
- **Fresh-entry branch:** enter from the immutable world-map state (SHA-256 `ce478c757b5c6486c8edcdf0871ec8e920912a181c70fa636a1ed54b38345807`), settle for 720 frames, then traverse for 1,860. This removes the cache crossing actually exercised by that retained-history branch. Legitimate death/map/entry fallback remains.
- **Turn branch:** 420 uninterrupted frames exercise both upward boundaries. Black-margin fallback goes from **2 to 0**. Every extended frame has zero terrain misses after the native guard-row correction; localization verifies 1,175,166 terrain-margin hits.
- Each branch was replayed three times at **342x224 and 308x224**. All final states/images and per-frame traces are deterministic. Candidate versus disabled at the same aspect changes **zero** native-center hashes and **zero** WRAM/VRAM/CGRAM/PPU-OAM/WRAM-OAM hashes across the turn, supplied-state, and fresh-entry branches.
- Independently running the whole route in native width diverges in guest state because of existing widescreen activation behavior; some later native-center hashes consequently differ (157 turn, 958 fresh, 843 exact frames). This is not new damage from the cache fix. The accepted center oracle here is the identical-state, same-aspect disabled build. Do not describe this as whole-route native-versus-wide gameplay parity.
- The actual live app reproduced the first black frame (`flash-before/window.png`) and rendered its repaired sides (`flash-after/window.png`). A subsequent **1,980-frame live fresh-entry run** reached the lower passage via the first corridor, upward turn, upper passage, and descending shaft. It ended paused, cleared its input schedule, and exported a verified bundle under `water-live/flights/`. `lower-turn-window.png` shows the later passage. The automated batch continuation also exercises death and re-entry.
- Final transition sentinels pass **16 samples over four entry boundaries** and **20 samples over five boundaries** of the longer live route, comparing raw memories and isolated layers under retained/cold rendering. No failure bundle was produced.
- The four available clean entrance anchors (`003E`, `00A7`, `006D`, `0024`) run three repeats with no hard failure and unchanged results versus the previous aquatic candidate (`fresh-matrix-previous-candidate.json`). The sweep retains its pre-existing native/wide machine-divergence classification. The complete 40-entrance floor fails for **36 missing anchors** (`capability-floor-final.txt`); this is not a whole-game pass. Other aquatic levels, bosses, and unsupported layouts remain unvalidated for promotion.

## Reusable route and local review

`recipes/croctopus-cache-crossing.json` enters from the documented immutable map root, records clean-entry and first-turn checkpoints, then crosses the cache boundary and continues. Run it with `tools/run_route_recipe.py`, the supported ROM, `--snapshot-input` pointing to that copied root, and all four candidate switches set to 1. `recipe-validation/` records the compiled/run result. The longer private input schedules and exact root identities remain with the evidence rather than being committed as game data.

The candidate app enables `DKC1_WS_PIXEL_BOUNDARIES`, `DKC1_WS_LIVE_SCROLL`, `DKC1_WS_WALL_ADJACENCY`, and `DKC1_WS_SCROLL_REBASE`, loads the same preserved clean-entry state, and starts paused. **F7** resumes. The earlier wall-adjacency experiment remains model-only for these scenes: these traversals do not establish visual acceptance of that path. Removing a switch is its rollback. No full-matrix default promotion or commits were made.
