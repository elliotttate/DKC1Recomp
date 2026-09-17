# HD idle continuity and gaze review — September 7, 2026

This local candidate corrects the normal idle loop in the supported Jungle
slice. Idle21 no longer substitutes the conspicuous tie/logo and narrow body
from a different generated model. Idle1–20 use forward-looking eyes with
consistent visible pupils. The candidate keeps the original animation keys,
timing, facing selection, canvas dimensions and cartridge anchors.

## Symptoms and selected art

The user's first comparison identifies Idle20 → Idle21: the old Idle21
showed a bright tie tip, changed the face/model and shifted the apparent
stance. Its old 160×160 canvas had alpha bounds `(16,0,144,160)`. A Nano
Banana 2 edit using the adjacent Idle20 and original Idle21 produced a more
continuous body with the tie occluded. A first alternative moved the near
knuckle and was rejected. The selected replacement retains the original
40×40 source canvas at 4× and anchor `[-17,-38]`; silhouette IoU is 0.891.
This remains a provisional art judgment, not strict internal-landmark approval.

The first displayed Idle21 is frame 391 of the right idle route and 105 of
the left route. It initially holds for 15 game frames, so an outlier is more
noticeable than a single display flash. Per-frame mappings are retained in
the private audit.

The subsequent gaze report was broader: many HD irises sat high under the
brow, looking upward instead of ahead. Close-up Nano Banana 2 edits move the
gaze forward. Only a fixed region around each eye is copied back into its
original sprite canvas; all alpha bytes and RGB pixels outside that region
remain unchanged. No body rescaling, recentering or animation substitution
is performed by the gaze patcher.

The first gaze candidate retained Idle17/18 because they already looked
forward. The user's next screenshots correctly exposed a remaining pop:
their pupils were tiny and almost hidden behind the muzzle, unlike adjacent
frames. The final selection also replaces those two eye regions with the
matching edits from the same four-face Nano sheet as Idle19/20. The latest
additional candidate had oversized, detached-looking eyes and was rejected.
Idle21's forward gaze is retained from its body-continuity edit.

Generic masked retouch attempts were rejected for missing/black pupils and
distorted brows. Four two-reference Nano jobs copied the wrong head poses;
none were packed. The selected edits come from a single-reference 16-face
sheet and a single-reference four-face sheet. Source crops, exact prompts,
provider identifiers, failed alternatives and patch masks remain private.

## Pack and checks

Private evidence root: `build/hd-slice/ground-slap-20260907` (E).
Gaze evidence is under `E/idle-gaze` (G).

- Final pack: `build/hd-slice/scene-pack-nano-idle-gaze`, 1,514 materials,
  260 selected DK source poses / 520 facing materials.
- Pack SHA-256: `fe6292d561452da3e7a01b7a069c5c430c4cf72859193c3b5791ee9deee1a5a7`.
- `E/build_gaze_candidate.py` writes both directions through the maintained
  registered packer. Forty materials change relative to the body-continuity
  candidate. Including Idle21, 42 change relative to slap/Duck v2; all 1,472
  other materials, including the selected environment, remain byte-identical.
- `G/patch-audit.json` proves unchanged alpha and outside-region bytes for
  all 20 eye edits. `G/alignment.json` records geometry comparisons; these
  cannot certify appearance or internal landmarks.
- `G/all-idle-runtime.jpg` compares original, previous HD and final HD for
  all 21 poses at their actual displayed positions. Every original raster
  matches the native framebuffer's opaque source pixels exactly.
- `G/selected-eyes-review.png` audits each eye pair; `G/transition-16-21.png`
  isolates the formerly inconsistent tail of the loop.
- `G/idle-comparison-right.mp4` and `idle-comparison-left.mp4` use the logged
  cartridge raster timeline for an isolated source/previous/final sprite
  comparison. They are pose-sheet playback, not screen recordings.

The unchanged guest/headless executable, supported ROM and immutable root
identities are in [HD_GROUND_SLAP_REVIEW.md](HD_GROUND_SLAP_REVIEW.md).
The final-candidate replay and native-window evidence is recorded below
after installation. Earlier `verification-gaze` and `exact-user-gaze` results
belong to the superseded candidate that retained small Idle17/18 pupils.

## Scope and provenance

No renderer, scene guard, guest logic, gameplay timing or engine code changed
in this art correction. The existing `$0016` HD guard remains. This is a
normal-idle improvement, not completion of every animation or full-game art.
Strict geometry, Turn2, other provisional groups and environment gaps remain
open in the issue register. No old saved-state repair is needed or applied.

Idle21 generation/cutout work used 306 authorized Magnific credits. Gaze work
used 1,130: seven Nano Banana 2 generations at 150 plus eight retouch attempts
at 10, including all rejected alternatives. Slap/Duck used 1,224 separately;
the combined continuation used 2,660 credits. No additional credits were
purchased. Art, ROMs and states remain private; no commits or publication.
