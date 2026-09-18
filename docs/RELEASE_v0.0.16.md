# DKC1Recomp v0.0.16 - Windows frame generation

This release packages the native Windows Direct3D 11 host and its optional
animation smoothing, plus the unchanged latest macOS HD Preview.

## Windows changes

- Direct3D 11 flip-model presentation with display-driven pacing, per-monitor
  DPI awareness, sharp scaling and SNES pixel aspect controls.
- Optional held-pose interpolation at 60 Hz and generated intermediate images
  for 120 Hz output. Enable **View > Smooth Animation / Frame Generation** or
  press **F10**. Smoothing adds approximately 67 ms of display latency.
- Corrected terrain/KONG-letter registration: terrain and world objects now
  share camera endpoints, preventing the measured relative wobble and the
  alternating softening of fine ground texture. Parallax and character-pose
  smoothing remain active.
- Asynchronous diagnostic logging and expanded frame/order/audio/ETW analysis.

The new Windows archive is the **native Win32 frontend**, with keyboard
controls documented in its README. Its features differ from the older SDL
Windows frontend; controller/Dixie options and the Mac HD preview are not
provided in this native package. The previous SDL Windows package remains
available in [v0.0.15](https://github.com/elliotttate/DKC1Recomp/releases/tag/v0.0.15).

## Packages

- `DKC1Recomp-v0.0.16-Windows-x64.zip`: newly built Windows native host.
- `DKC1Recomp-v0.0.15-macOS-arm64-HD-Preview.zip`: **unchanged** archive from
  v0.0.15, retaining its original filename and SHA-256. It includes that
  release's first-level HD preview and Dixie support; it has not been rebuilt
  with the Windows changes above. It requires macOS 26 on Apple Silicon.

SHA-256 sidecars accompany both archives. No ROM or save state is included.
Supply the verified clean US v1.0 DKC1 ROM described in each package.

## Validation and known limits

The interpolation change passed three identical 60 Hz replays, three identical
software 120-frame replays, native-width and fresh-entry checks, and serial
versus worker comparison. Raw frames and recorded game state remain unchanged.
The measured K-letter/terrain drift falls from 0.875 native pixels to zero in
the tested Jungle sequence. Primary/tools builds and 17 focused tests pass.

This is **not a zero-hitch or full-game interpolation certification**. A separate
short timing sample still recorded Windows message-pump/audio stalls. Physical
120 Hz delivery, the complete cross-level matrix, and repeated 30-minute
windowed/fullscreen soaks remain unverified. The full Python suite also has
existing environment errors from missing POSIX `cc`; the focused MSVC/model
tests pass. Smoothing remains optional and unsupported cases retain original
artwork.

See [terrain evidence](https://github.com/elliotttate/DKC1Recomp/blob/v0.0.16/docs/FRAMEGEN_TERRAIN_STABILITY_REVIEW.md)
and [pacing investigation](https://github.com/elliotttate/DKC1Recomp/blob/v0.0.16/docs/PACING_HARDENING_REVIEW.md).
