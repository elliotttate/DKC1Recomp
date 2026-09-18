DKC1Recomp - Windows native frame-generation host

Extract this entire archive into a writable folder. Supply your own clean
Donkey Kong Country (USA), version 1.0 ROM. No ROM or save state is included.

To play, put your ROM beside DKC1Recomp.exe under the name dkc1.sfc, then
double-click DKC1Recomp.exe. You can also drag your ROM onto the executable
or run: DKC1Recomp.exe "C:\path\to\your\game.sfc"

Supported headerless ROM SHA-256:
fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15

This package uses the native Win32 host, with keyboard controls:
  Arrows       Move
  Z            B / jump
  X            Y / run, roll, carry
  S            A
  A            X
  Q / W        L / R
  Enter        Start
  Right Shift  Select
  Alt+Enter    Fullscreen
  F7           Pause
  F8           Single frame while paused
  F10          Smooth Animation / Frame Generation
  F11 / F12    Quick-save / quick-load

Smooth animation is optional and initially off. Enable it with F10 or
View > Smooth Animation / Frame Generation. It adds about four frames
(67 ms) of visual latency to interpolate held character poses at 60 Hz.
Higher-refresh displays can use generated intermediate images at 120 Hz.
Physical 120 Hz delivery has not been verified for this release.

Widescreen is initially enabled. Scaling and pixel aspect are in View.
Use View > Debug Panel to hide or show the diagnostics sidebar.
Direct3D 11 flip presentation is preferred; GDI is the fallback. The Windows
host uses Windows system runtime libraries and does not require bundled SDL.

This is the native Windows presentation host, not the earlier SDL Windows
frontend. Its menu/features differ: this package does not provide the Mac
HD texture preview or the earlier SDL frontend's controller/Dixie options.
Earlier Windows SDL packages remain available on the GitHub releases page.

The terrain/KONG-letter alignment fix has repeatable Jungle Hijinxs evidence.
All-game interpolation quality and universally hitch-free presentation are
not certified. Remaining Windows message-loop/audio stalls are documented
in RELEASE_NOTES.md. Disable smoothing with F10 if an unsupported scene
looks incorrect. The original game simulation and raw native image remain
the reference.

The carried macOS HD Preview is a separate, unchanged v0.0.15 archive.
It does not contain these Windows-specific frame-generation changes.

Source and releases: https://github.com/elliotttate/DKC1Recomp
BUILD.json identifies this executable's source commit and SHA-256.
See LICENSE.txt and licenses/ for the project and engine notices.
