# DKC1Recomp regression dashboard

Generated 2026-08-16 12:20 UTC at commit `73bd0b8-dirty`. Regenerate with `python tools/make_dashboard.py` after a regression/sweep cycle.

## Contracts

| contract | last result | legs | evidence |
|---|---|---|---|
| jungle-death-transition (`jungle-death-transition.json`) | NOT RUN in latest cycle | - | - |
| jungle-entry (`jungle-entry.json`) | PASS | entry+quickload | `C:\Users\ellio\Documents\GitHub\DKC1Recomp\build\regression\jungle-entry` |

## Route sweep

_9 routes swept; levels without a route are NOT covered — absence here is not a pass_

| route | rc | cache oob (r/w) | rebases | oam wrap | scene flags |
|---|---|---|---|---|---|
| capture_jungle_route_snapshots.dks | 22 | 0/0 | 0 | 0 | clean |
| capture_jungle_snapshots.dks | 22 | 0/0 | 0 | 0 | clean |
| jungle_snapshot_scroll.dks | 0 | 0/0 | 0 | 0 | clean |
| rope_to_left_margin.dks | 0 | 0/0 | 0 | 0 | clean |
| rope_to_right_margin.dks | 0 | 0/0 | 0 | 0 | clean |
| route_death.dks | 0 | 0/0 | 0 | 2 | (0, 22, 217, 0): BLANK(4032),PILLARBOX; (0, 22, 0, 0): PILLARBOX |
| route_jungle.dks | 0 | 0/0 | 0 | 0 | (0, 22, 217, 0): BLANK(4032),PILLARBOX |
| route_jungle_quickload.dks | skipped | - | - | - | dependent leg (needs a seeded state) |
| snapshot_smoke.dks | 0 | 0/0 | 0 | 0 | clean |

## Known issues

| id | status | summary | repro |
|---|---|---|---|
| no-contact-damage | open | Contact damage never lands: DK overlaps the first Gnawty for 3800+ frames unharmed in BOTH native and wide modes, and identically under forced LLE (not a dispatch/widescreen defect). | `recipes/route_death.dks` |
| entry-wide-centered-flap | open | WIDE<->CENTERED presentation flap during level entry (frames 7304-7331 of the jungle route). | `recipes/route_jungle.dks with DKC1_WS_TRACE` |
| wide-world-key-unwrap | open | Wide world key camX=$FFF0 (-16) unwrap artifact near level start. | `recipes/route_jungle.dks with DKC1_WS_TRACE` |
| margin-decode-attribute-mismatch | fixed | Stream retrodiction proves 1690 served-margin entries on the jungle entry route disagree with the game's own later stream, overwhelmingly in the ATTRIBUTE byte (palette/priority/flip: xor 0x1c00/0x5c00/0x4c00 clusters; whole 29-row columns as they scroll into view). The ROM level-map decoder's attribute derivation does not match streamed truth for some metatiles - margins can show wrong-palette art. | `SNESRECOMP_WS_RETRODICT=out.jsonl + recipes/route_jungle.dks; analyze with tools/analyze_retrodiction.py` |
| entry-oam-xhigh-loss | open | At level entry (~frame 7488) an OAM sprite drops its X high bit: (317,158) -> (69,158) with the low byte continuous - right-margin art teleporting to the left. Caught by level_sweep's wrap detector; likely tied to the entry-time presentation-bias window (see entry-wide-centered-flap / wide-world-key-unwrap). | `recipes/route_death.dks or route_jungle.dks via tools/level_sweep.py (oam_wrap.first_suspect)` |
| standstill-pillarbox | open | Standing still at level start pillarboxes for 4000+ frames (layout never selected, source bank 0) until the camera moves; seen on route_death.dks stand-still segment. | `recipes/route_death.dks via tools/level_sweep.py` |
| post-bonus-bg1-transition-contamination | fixed | Leaving a bonus room could seed BG1 widescreen margins from a transitional VRAM ring, producing repeated vertical strips on both outer edges while the native center and cartridge state remained valid. | `build/bonus-safe-control-repros-20260816/capture-f00000464-20260816-075932-p28476` |

Issue lifecycle: edit `docs/KNOWN_ISSUES.json` (set status `fixed` with the fixing commit) and regenerate. A fixed issue regressing shows up here as its contract/sweep line failing.
