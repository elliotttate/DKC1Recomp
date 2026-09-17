# Nano level coverage and visual finish plan

September 16, 2026. Implementation sequence, grounded in the current
native preview and the user's two annotated screenshots. The current pack is
not accepted as complete full-level replacement coverage.

Implementation and subsequent gray-sprite-matte repair are recorded in
[HD_NANO_POLISH_REVIEW.md](HD_NANO_POLISH_REVIEW.md). The original prototype
results below remain historical evidence. Connected main-level backgrounds,
offline contour/seam repair, spatial Metal cleanup and a graphics control are
implemented. The coverage overlay, complete bonus-room/sprite coverage and the
full presentation performance budget remain open; telemetry is available.

## Evidence and diagnosis

The Nano pack matches all 16,434 keys in the preceding captured inventory.
That proves inventory parity, not coverage of every visible level tile. The
user's second screenshot shows sharp replacement characters surrounded by
original-looking foreground terrain after a short move to the right. Recorded
HD traces independently report missing art: the wide pickup frame has 1,726
material misses, wide run has 1,551, and wide barrel-right has 4,998. These are
opaque material sample counts across layers, not unique missing keys or a count
of final-screen pixels. Their native reconstruction mismatch count can still be
zero; reconstruction correctness and replacement coverage are different gates.

The first screenshot exposes two more defects. Background assembly restores
nearest-neighbor source alpha, retaining native stair-step contours. Independent
48x48 context generations are cropped to 32x32 centers, and a two-native-pixel
border fades to original RGB. This keeps blocky outlines and can reveal a patch
grid even where Nano RGB is present. Missing coverage, contour aliasing, and
inconsistent patch content require separate remedies.

Background identities are SHA-256 hashes of the exact 48x48 RGBA context around
a 32x32 center (`runner/dkc1_hd_scene.c`, `DecodeBackground`). Scrolling and VRAM
streaming can create new surrounding contexts. Before changing lookup behavior,
classify actual misses as known exact centers in new contexts, genuinely new
source art/palettes, unsupported scenes, or reconstruction-oracle fallback.
Do not claim the precise cause of the user's pictured location until its state
or an equivalent controller route has been captured.

## 1. Close replacement coverage before polishing

- Capture a controller-only full main-level route, including backtracking,
  vertical camera excursions, animated terrain/palettes, death/re-entry, and
  level exit. Preserve immutable anchors for the two reported locations if
  available; keep the user's running process and save slots intact.
- Audit the level's ROM-backed terrain/layout inventory against those captures
  so reachable art outside the sampled route is not silently omitted.
- Add default-off coverage telemetry and a visible diagnostic overlay with
  separate colors for replaced art, missing material keys, unsupported scene,
  and reconstruction mismatch. Report visible missing pixels per BG/OBJ layer
  and unique keys; a large preload count is not the acceptance metric.
- For an encountered new context, reuse art only through a byte-verified source
  mapping. Register proven aliases offline. Never substitute a merely similar
  tile or change guest VRAM to make an art key match.
- Recreate genuinely missing assets with the selected Nano model and preserve
  their creation provenance. Provider-rejected requests remain rejected.
- Rebuild the level preload index, audit the memory total and material limit,
  and verify zero gameplay texture reads. Test bonus-room coverage and scene
  eligibility separately; enable a room only after its mappings and checks pass.

Acceptance: no missing visible background replacements on the complete declared
main-level route; every residual fallback has an explicit classified reason.
Do not label the whole level complete from a single start-screen comparison.

## 2. Repair asset edges and joins offline

- Build connected terrain strips or complete repeating layers with overlap,
  then crop them into runtime chunks. Share the same art on both sides of a
  boundary. Prefer this over independently inventing each small tile.
- Check internal feature alignment as well as alpha: a leaf vein, trunk, ground
  contour, or shadow must meet its neighbor. Misregistered features require
  registration correction or regeneration; blending cannot restore lost shapes.
- Reconstruct high-resolution coverage masks from the source geometry and
  generated contours. Keep an explicit displacement limit and preserve thin
  leaves, gaps and holes. Remove the nearest-neighbor alpha lock that causes
  the black stepped outlines in the first screenshot.
- Extend foreground color into transparent edge texels and blend in
  premultiplied-alpha form during filtering, converting back to the runtime's
  straight-alpha format when writing DKHD. This prevents gray/black fringes.
- Blend exact overlapping Nano content and reconcile low-frequency exposure
  and color differences while retaining leaf/fur detail. Remove the fade back
  to original pixel-art RGB when a valid shared overlap is available.
- Anchor these corrections in source/texture coordinates so the appearance
  stays stable as the camera moves. Freeze character and HUD assets while
  validating this background-only stage.

Acceptance: same-coordinate before/after crops show smooth leaf/sky contours,
no new holes or halos, and no obvious 32x32 texture grid while scrolling.

## 3. Add a restrained Metal edge pass

Suggested order: verified assets -> existing layer compositor -> edge detection
and selective antialiasing -> existing display/color presentation.

- Preserve enough material/layer metadata to distinguish world art, characters,
  HUD, and original fallback. HUD classification should use verified material
  metadata rather than a blanket screen rectangle.
- Detect luminance/color and silhouette edges on the composed 4x image. Blend
  only across confirmed edge directions, with short bounded sample spans.
- Start with background silhouettes and residual diagonal edges. Keep texture
  interiors sharp and protect HUD digits. Character edge strength can be tuned
  separately after sprite motion review.
- Start with spatial processing. Avoid frame-history blending in the first
  version, which would need reliable motion and disocclusion handling for
  sprites, parallax, fades, and scene transitions.
- Keep intermediate targets on the GPU, allocate them on size changes, and
  avoid CPU readback or per-frame resource allocation in production.
- Optional mild detail recovery comes only after AA quality is accepted; avoid
  ringing, aggressive sharpening, or grain that conceals unresolved defects.

Initial performance target: at most 1 ms additional GPU time on the current
test Mac at 1368x896, measured rather than assumed. Retain the existing original
fallback if resources or shader setup fail. Do not change simulation cadence.

## 4. Expose comparison controls and validate in motion

- Keep F10's existing original/HD comparison. Add independent graphics controls
  for edge cleanup and overall polish strength, with a way to inspect raw Nano
  art. Keep experimental processing default-off outside this private preview.
- Review paused aligned crops and running clips at the reported locations,
  dense foliage, ground transitions, distant tree line, DK/Diddy silhouettes,
  banana pickup/counts, and parallax movement in both directions.
- Check shimmering, ghosting, edge crawling, color seams, thin foliage and HUD
  legibility at native/wide supported geometry and different window sizes.
- Run three-repeat guest-state equivalence from immutable roots plus fresh
  entry, transition and cache-pressure/reload cases. The native center oracle
  before the optional HD/postprocess path must remain unchanged.
- Compare raw compositor CPU/Metal pixels independently of the intentional
  postprocess differences; give the new edge pass its own reference fixtures.
- Measure CPU/GPU frame times, p99, scanout cadence, texture reads, allocations,
  and memory under an identical input schedule with processing off/on. Visible
  native QA is required before installing the polished candidate.

## Current prototype and limits

`build/hd-slice/nano-level-20260916/background-fix/build.py` is an isolated,
offline prototype, not an installed renderer change. It reconstructs background
alpha using a scalar form of the existing host slope filter, extends edge RGB,
and blends neighboring Nano contexts only where original RGBA overlaps match.
It updates 7,588 background keys in a separate pack; sprites and HUD are held
constant. The baseline pack and user's live app remain untouched.

One 125-frame pickup replay preserves all seven guest hashes and has zero
CPU/Metal pixel differences across 125 validated frames. Its comparison image
is `background-fix/preview/comparison.png`. The leaf outline improves, but broad
patch seams remain visibly unacceptable and replacement coverage is unchanged.
This is evidence for the staged plan, not a completed fix or accepted candidate.
One preliminary replay ran before its preload index existed; it is explicitly
retained under `discarded-missing-preload-index/` and excluded from validation.

The initial Nano pack passed 72 deterministic replay legs and 31,485 eligible
CPU/Metal frame comparisons. Those tests establish simulation/render agreement,
not full art coverage or visual acceptance. The user's screenshots override any
premature appearance of completion based on the inventory count.
