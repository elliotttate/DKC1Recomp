# DKC1Recomp regression dashboard

Generated 2026-08-30 14:24 UTC at commit `d38d7ea-dirty`. Regenerate with `python tools/make_dashboard.py` after a regression/sweep cycle.

## Contracts

| contract | last result | legs | evidence |
|---|---|---|---|
| jungle-death-transition (`jungle-death-transition.json`) | NOT RUN in latest cycle | - | - |
| jungle-entry (`jungle-entry.json`) | NOT RUN in latest cycle | - | - |

## Route sweep

_No sweep report; run `python tools/level_sweep.py`._

## Known issues

| id | status | summary | repro |
|---|---|---|---|
| no-contact-damage | fixed | Contact damage never lands: DK overlaps the first Gnawty for 3800+ frames unharmed in BOTH native and wide modes, and identically under forced LLE (not a dispatch/widescreen defect). | `recipes/route_death.dks` |
| entry-wide-centered-flap | open | WIDE<->CENTERED presentation flap during level entry (frames 7304-7331 of the jungle route). | `recipes/route_jungle.dks with DKC1_WS_TRACE` |
| wide-world-key-unwrap | open | Wide world key camX=$FFF0 (-16) unwrap artifact near level start. | `recipes/route_jungle.dks with DKC1_WS_TRACE` |
| margin-decode-attribute-mismatch | fixed | Stream retrodiction proves 1690 served-margin entries on the jungle entry route disagree with the game's own later stream, overwhelmingly in the ATTRIBUTE byte (palette/priority/flip: xor 0x1c00/0x5c00/0x4c00 clusters; whole 29-row columns as they scroll into view). The ROM level-map decoder's attribute derivation does not match streamed truth for some metatiles - margins can show wrong-palette art. | `SNESRECOMP_WS_RETRODICT=out.jsonl + recipes/route_jungle.dks; analyze with tools/analyze_retrodiction.py` |
| entry-oam-xhigh-loss | open | At level entry (~frame 7488) an OAM sprite drops its X high bit: (317,158) -> (69,158) with the low byte continuous - right-margin art teleporting to the left. Caught by level_sweep's wrap detector; likely tied to the entry-time presentation-bias window (see entry-wide-centered-flap / wide-world-key-unwrap). | `recipes/route_death.dks or route_jungle.dks via tools/level_sweep.py (oam_wrap.first_suspect)` |
| standstill-pillarbox | open | Standing still at level start pillarboxes for 4000+ frames (layout never selected, source bank 0) until the camera moves; seen on route_death.dks stand-still segment. | `recipes/route_death.dks via tools/level_sweep.py` |
| post-bonus-bg1-transition-contamination | fixed | Leaving a bonus room could seed BG1 widescreen margins from a transitional VRAM ring, producing repeated vertical strips on both outer edges while the native center and cartridge state remained valid. | `build/bonus-safe-control-repros-20260816/capture-f00000464-20260816-075932-p28476` |
| wide-vertical-row-staging-corruption | fixed | Wide simultaneous horizontal/vertical motion could expose black or mixed-color rectangular blocks in BG1 because the stock vertical row builder refreshed only 36 entries before publishing a full 64-entry ring row. | `build/visible-flight-recorder-20260816/capture-f00004159-20260816-083801-p60144` |
| wide-seven-tile-stream-guard-gap | fixed | The host renderer sampled seven complete margin tiles at arbitrary sub-tile scroll phases, but the cartridge initializer and moving-row sweep prepared only six. The uninitialized seventh physical BG1 ring column could later rotate into the native center as one 8-pixel vertical strip of stale terrain. | `build/visible-rowfix-flight-20260816/capture-f00005014-20260816-084158-p69336` |
| jungle-bonus1-widened-initializer-corruption | fixed | Jungle Hijinxs Bonus 1 lost its purple floor and showed checkerboard cave columns because a shared fixed-layout initializer was incorrectly treated as a rolling-terrain widescreen capability and wrote widened columns into the native VRAM ring. | `build/visible-margin7-flight-20260816/capture-f00158989-20260816-094934-p70536` |
| ropey-rampage-widened-initializer-corruption | fixed | Ropey Rampage could start with terrain sliced into displaced horizontal and vertical bands because the shared widened cartridge initializer generated invalid native-ring data for this layout. | `build/visible-bonusguard-flight-20260816/capture-f00009082-20260816-101211-p61340` |
| underwater-split-map-metatile-bank-pillarbox | fixed | Underwater level $0061/$80BF stayed entirely 4:3 because the host decoded both level-map cells and metatile definitions from the map bank. This scene publishes map bank $E9 in $D5 and metatile-definition bank $D0 in $D6, so the one-bank decode failed calibration and correctly fell back to pillarbox. | `build/current-level-live/level0061-entrance80BF-20260816-120545.state (external evidence; not committed)` |
| croctopus-authored-right-boundary-gap | fixed | Croctopus Chase level $0061/$00BF widened correctly, but its lower-right rock wall stopped at the native edge and exposed BG2 water in the right margin. The ROM map itself switches from a fully populated wall metatile at world X $0620-$063F to wholly transparent metatiles at X $0640 because those cells were never visible on stock hardware. | `build/current-underwater-rightgap-20260816/level0061-rightgap.state (external evidence; not committed)` |
| croctopus-nearest-right-wall-source | fixed | At Croctopus Chase level $0061/$00C0 near camera X $007F, BG1's first offscreen right metatile was a complete wall but the following unused metatile was empty. The continuation rule skipped the nearer complete block and tested only the native-edge block, leaving alternating water and rock fragments in the outermost right margin. | `build/repros/underwater-right-20260816-1823/underwater-right.state (external evidence; not committed)` |
| croctopus-authored-left-boundary-gap | fixed | At world X $0241 in Croctopus Chase level $0061/$00BF, BG1's rock wall stopped 11 pixels inside the left widescreen margin and exposed BG2 water. The ROM map has a fully populated wall metatile at the stock left edge (metatile X $12) but a wholly transparent offscreen metatile at X $10. | `build/current-underwater-leftgap-20260816/level0061-leftgap.state (external evidence; not committed)` |
| slip-slide-ride-moving-margin-calibration-flicker | fixed | Slip-Slide Ride mode $0009, level $0051, entrance $006D could replace most of both widescreen margins with adjacent ROM tile columns while the camera moved. Independent floor-division first exposed 1-3 pixel camera smoothing; a later uphill/downhill repro showed that rounding an exact four-pixel half-tile tie away from zero still selected an adjacent column. | `build/repros/ice-cave-flicker-20260830/exact-user.state plus traverse-right.inputs for the original traversal; build/repros/ice-cave-flicker-20260830/vertical-left-right/exact-user-new.state plus left-right.inputs for the four-pixel uphill/downhill tie` |

Issue lifecycle: edit `docs/KNOWN_ISSUES.json` (set status `fixed` with the fixing commit) and regenerate. A fixed issue regressing shows up here as its contract/sweep line failing.
