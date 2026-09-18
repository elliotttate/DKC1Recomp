# HD animation review — September 6, 2026

> Subsequent cache repair: see [HD_MATERIAL_CACHE_REVIEW.md](HD_MATERIAL_CACHE_REVIEW.md) for the installed renderer fix that prevents existing walking art from being refused after 4,096 material identities. The 208-pose selection is unchanged.


> Latest bounce/idle repair: see [HD_BOUNCE_IDLE_REVIEW.md](HD_BOUNCE_IDLE_REVIEW.md). The installed private pack is `scene-pack-nano-bounce-idle`: **208 reviewed DK poses / 416 facing materials**. All 16 enemy-bounce and 24 chest-beating poses are covered in both directions; the left-facing chest/idle fallback is repaired. 60 deterministic replay legs and two 6,000-frame idle probes pass. Earlier pack/process identities below are historical; broader art and full-game coverage remain open. The user resumed the installed preview after its clean-root launch; it is running. **F7** pauses/resumes.

Local, uncommitted continuation of the HD experiment. This pass inspected the complete **700 Donkey Kong candidates / 53 groups** as source-versus-HD contact sheets. It did **not** play every animation or review every other character/enemy in DKC1. Runtime, artwork and geometry acceptance remain separate.

## Fixed: stale and missing art when facing left

The scene compositor hashes complete original rasters after OAM flips. The earlier motion pack builder updated only 83 canonical keys. Its inherited pack already contained 52 opposite-facing keys using older restoration artwork; other opposite-facing keys were missing. Walking/turning could change the visual style, and running left reverted to original pixels.

`tools/hd_sprite_pack.py registered` now validates each original BGRA raster hash and writes both direction keys from the **same** selected candidate, mirroring all HD texels exactly. It validates the entire selection before writes and rejects conflicting art for an identical source raster. No guest code, renderer, animation clock, OAM placement, scene guard, collision or widescreen policy changed.

The new private `build/hd-slice/scene-pack-nano-reviewed` pack contains 110 selected source poses / 220 direction materials: the prior 83 plus Hurt (18) and LookUp (9). Relative to the baseline, 83 materials are unchanged, 52 stale materials are replaced and 85 are new. Hurt is naturally exercised both ways. LookUp remains a static-review candidate without natural runtime evidence. This is an experimental pack, not a final art certification. The 83-frame baseline pack is preserved.

## Exact visual evidence and playback

The initial saved-window comparison at frame 312 still shows original art because that pose belongs to the uninstalled Roll group. The useful comparison is **relative frame 326 of `hd-actions.dks`**: the baseline displays an original pixel left-running DK and the candidate displays the selected Nano pose. The two gracefully saved machine states are byte-identical:

```text
37b551a538ef8515571d3bbc4a838bed0623e620ee9b459d051477707b9d046c
```

Evidence root: `build/hd-slice/animation-review-20260906/`. `live-left-before/window.png` and `live-left-after/window.png` are actual macOS window captures; each directory also contains launch environment, trace/audit and `final.state`. Raw WRAM/VRAM/OAM and image sequences from the initial directional/action probes are retained in `directions/` and `actions/`.

The later live left-facing hurt capture follows the controller-only `hd-hurt-left.dks` route. It waits for enemy contact after turning away; hurt starts at relative frame 213, and all 18 mirrored hurt keys were exported. No state edits were used to manufacture an animation.

## Validation and limits

- **108 passing replays**: native/wide × HD off/on × three repeats across fresh, idle, walk, directions, actions, run, hurt, hurt-left and extended idle.
- Final native framebuffer, WRAM, VRAM, CGRAM, PPU OAM, OAM shadow and audio match paired baselines and repeats. HD output is deterministic.
- Exact-root routes have zero low-resolution reconstruction mismatches. Fresh entry retains the prior 184 original-pixel fallbacks per replay.
- Nine pack contract tests pass, including exact mirrored alpha/texel placement, stale-art replacement, wrong source hash, unknown groups and path escape rejection.
- **97 distinct selected source poses** were naturally encountered; **79 were encountered in both directions**. These are material exports, not a claim of 700 live poses.
- The host binary and scene implementation are unchanged. The app resource pack was updated and the bundle re-signed/verified. No new renderer scope was promoted; the 40-entrance widescreen floor is not claimed.

| Installed group | Source poses reached | Mirrored poses reached | Total source poses |
| --- | ---: | ---: | ---: |
| Hurt | 18 | 18 | 18 |
| Idle | 21 | 11 | 21 |
| Jump | 11 | 11 | 11 |
| Land | 9 | 9 | 9 |
| LookUp | 0 | 0 | 9 |
| Run | 16 | 8 | 20 |
| Turn | 2 | 2 | 2 |
| Walk | 20 | 20 | 20 |

Results: `validation/results.json` (72), `validation-extra/results.json` (24), `validation-hurt-left/results.json` (12). `runtime-coverage.json` preserves per-pose direction and route evidence.

## Preserved native-looking live state

The user session was paused and saved before controlled testing. Its previous quicksave was copied aside and restored. `preserved/live-native-looking.state` is at boot frame 40227 and entrance **$005F**, whereas the scene compositor deliberately accepts only **$0016**. A one-frame replay emits no eligible scene audit or material export. This explains the newly preserved state's full original-art fallback; it does not prove every historical report had the same cause. The renderer guard was not broadened, and the saved machine was not repaired. The clean fresh-entry route still passes at $0016. A fresh-entry branch at $005F has not been validated.

`atlas.py wram:003e` could not complete because the checkout lacks the referenced disassembly instruction index. The entrance value above is a raw WRAM observation and direct comparison with the existing scene guard, not an inferred symbolic entrance name.

## Complete contact-sheet review

This is a visual triage pass of every candidate in the existing boards. It is not an internal landmark acceptance pass or cartridge-timed playback of every group. Retained groups can still need art correction. A later built-in image-generation attempt targeted the existing Turn2 pose. The service rejected the output before returning an asset; the old frame is retained. The exact prompt and error request ID are preserved in `turn2-generation-attempt.json`. No external paid provider jobs were started.

| Group | Poses | Observed result / next correction |
| --- | ---: | --- |
| AngledInMinecart | 20 | Reject: partial torsos were completed with legs; frames 4-16 also invent rotations. |
| BeatChest | 24 | Hold: frames 1-20 are plausible but mouth/hand timing drifts; 21-24 shrink and change stance. |
| Bounce | 16 | Reject: tucked somersaults become standing or sitting poses, especially 1-14. |
| ClimbUpSingleVerticalRope | 6 | Reject: 1-3 become front-facing raised arms; grip positions and foot curl drift throughout. |
| ClockDiddyOnHead | 19 | Hold: hand reach, body height and contact points differ throughout; pair with Diddy before promotion. |
| Crawling | 20 | Reject: repeated tall poses and scale jumps; 1-3, 5-6, 9, 13-14, 17-20 break ground contact. |
| Dancing | 26 | Hold: 1-8 and 12-20 are promising; 9-11 invent idle/chin poses; 21-26 are much too small. |
| DiddyTakesLead | 5 | Hold: 1/5 head angle and 3/4 body lean need correction; paired Kong transition untested. |
| DonkeyTakesLead | 10 | Hold: coherent hand raise; changing tie logo and arm/face registration still need review in the paired transition. |
| DropFromAbove | 18 | Reject: jumping/tucked legs and front-facing arm motion become planted standing poses. |
| Duck | 22 | Reject: 1-8 are promising; 9-22 substitute fists, head scratching or idle for raised arms. |
| EnterCrawlSpace | 4 | Hold: body height and palm-to-ground placement drift; needs crawling continuity first. |
| Failure | 13 | Reject: frames 7-13 substitute smiles/fists for the hanging-arm disappointed pose. |
| FingerBitten | 13 | Hold: 5-7 abruptly shrink and point the wrong way; preserve the mouth/finger contact sequence. |
| FootStomped | 10 | Reject: missing foot grasp and pain expression; 3/4 shrink, and later turns use the wrong facing. |
| FreakOut | 22 | Reject: mouth and scale change sharply after frame 10; nose/face exaggeration differs from the source. |
| GroundSlap | 30 | Hold: 1-20 broadly follow the slap; 21-30 change camera angle, contact hand and recovery pose. |
| HangOnVerticalRope | 4 | Reject: grip is replaced by a hand on the mouth; legs plant on the ground. |
| HangOntoStabbingEnguarde | 7 | Hold: 2-5 change torso tilt, grip and leg position; rider integration untested. |
| HangOntoWinky | 3 | Hold: plausible silhouette but grip and rear foot positions differ; no rider contact test. |
| HeyWatchThis | 3 | Reject: mouth/expression and body proportions differ; loses the intended small facial sequence. |
| HoldIdle | 3 | Reject: palms should reach directly overhead; candidate arms reach backward with bent elbows. |
| HoldWalk | 13 | Reject: all 13 omit the overhead carrying arms; 9-11 also shrink. |
| Hurt | 18 | Installed candidate: coherent 18-frame tumble/recovery; all 18 encountered facing both ways. Face and palm landmark certification remains pending. |
| Idle | 21 | Retained candidate: all 21 source poses observed, 11 opposite-facing; small muzzle/knuckle drift and tie detail changes remain. |
| Jump | 11 | Retained candidate: all 11 observed in both directions; inspect stretched hand tips and toe placement before final art acceptance. |
| Kick | 30 | Hold: 1-20 and 25-30 plausible; 21-24 turn the torso/back and shift the extended foot. |
| Land | 9 | Retained candidate: all 9 observed both ways; 8 is the weakest face/hand transition. |
| LookUp | 9 | Installed static-review candidate only: all 9 form a coherent arm/face arc; no natural runtime key encountered in these routes. |
| Pickup | 7 | Reject: first two become idle; 3/4 reach forward instead of upward; 5-7 overhead contacts drift. |
| PrankDiddy | 26 | Reject: body scale, camera angle and pointing/leg poses differ throughout the interaction. |
| Pushing | 3 | Hold: palms and face direction differ despite strong outline scores; needs object-contact landmarks. |
| RideAnimalBuddy | 9 | Reject: standing/running substitutions, scale changes and rear-foot drift replace riding posture. |
| RideSteelKeg | 12 | Reject: lifted leg and arm silhouette are compressed; inconsistent scale and balance. |
| Roll | 14 | Reject: 1-5 and 9-12 contain invented standing/back/headstand poses; 6-8 and 13/14 alone cannot make a complete cycle. |
| Run | 20 | Retained candidate: source Run4-Run19 observed; 8 opposite-facing poses observed. Complete mirrored pack now uses identical chosen art. 1-3/20 not naturally reached. |
| SitDown | 7 | Reject: sitting/hand-to-head motion becomes standing/chin poses and then a planted front view. |
| Swimming | 15 | Hold: coherent 15-pose stroke, but toothy face changes and hand drift remain; underwater HD renderer is unsupported. |
| SwingIntoIntro | 26 | Reject: grip is missing, 9-20 shrink dramatically, and 22-25 substitute unrelated poses. |
| SwingOnRope | 31 | Hold: most rotation is plausible; 28 is visibly clipped at the head; 29-31 shrink/change orientation. Grip continuity untested. |
| ThrowPose | 19 | Reject: 1-11 mostly invent idle/backbend poses and omit the throwing arm; later recovery is uneven. |
| TooCloseToEdge | 22 | Hold: frame 3 changes stance; 21/22 lose the outward arm. Toe support and hand positions need edge-contact review. |
| Turn | 2 | Retained experimental candidate: both poses observed both ways; Turn2 is too narrow/upright and remains an explicit art defect. |
| TurnOnAnimalBuddy | 2 | Reject: both poses substitute grounded knuckle stance for the rider torso. |
| TurnOnVerticalRope | 2 | Reject: wrapped/holding poses do not preserve the source hands or hanging legs. |
| TurnWhileSwimming | 2 | Reject: both candidates have incompatible dark rim lighting and wrong arm/facing geometry. |
| Unknown1_Pose | 1 | Reject: upright reaching pose becomes quadruped stance; usage is unproven. |
| Unknown2Pose | 2 | Reject: frame 1 is tiny/wrong arms; frame 2 replaces extended palms with fists; usage unproven. |
| Unknown3Pose | 3 | Reject: all 3 omit the leg/arm reach; usage unproven. |
| Unknown4Pose | 2 | Reject: overhead reaching arms collapse onto the head; usage unproven. |
| UnusedVictory | 12 | Hold: arm span and body proportions drift, especially 3/4; name is a navigation label, not proven runtime usage. |
| Victory | 32 | Hold: 2-20 show a plausible facial routine; 21-32 shrink and substitute poses. Do not install this whole group. |
| Walk | 20 | Retained candidate: all 20 observed in both directions; opposite-facing materials now use the chosen walking sheet instead of old restoration art. |

## Reproduce the pack and checks

```sh
build/hd-slice/upscale-venv/bin/python tools/hd_sprite_pack.py registered \
  build/hd-slice/reimagined/all-animations/registration.json NEW_PRIVATE_PACK \
  --originals build/hd-slice/reimagined/all-animations/original-frames \
  --groups Idle Walk Run Jump Land Turn Hurt LookUp
python3 tools/verify_hd_scene.py "$DKC1_ROM" NEW_EVIDENCE_DIRECTORY \
  --pack NEW_PRIVATE_PACK --export-materials \
  --cases fresh idle walk directions actions run hurt hurt-left idle-extended
```

First copy the preserved baseline material directory to `NEW_PRIVATE_PACK` so scenery and non-DK material coverage remain present. Use new evidence/output paths. Rebuilding an app is unnecessary for this asset-only correction; install the pack into the separate Nano preview bundle while closed, preserve its ID/root, re-sign, verify and restart.

## Identities

| Artifact | SHA-256 |
| --- | --- |
| ROM | `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15` |
| Immutable entry.state | `7aeaa4e44e714db99af7cbeb8e79e68ee0c072bf9b4e84ba35f5c927e3cc05da` |
| Preserved live native-looking state | `43efe3bd524cb60859cf2cef1fe2d01c4efa5a4a269eeefd3de825494e5a3b33` |
| Headless executable | `63ba41a4b2334190cde1f791df86b3b55c4aad5908687ddf007b8301e5b4b22f` |
| Nano preview executable | `3d43193d21d7e3c3c14920f633154164a1de8a8b06a6dffcdbb80ec5f49d34fd` |
| Reviewed material pack | `d690d6105c1b8b529fca766e833ed8685881824e508aa867a88441058d9fdf78` |

## Final live state

The Nano preview was restored to `entry.state`, advanced one neutral frame and left **paused**, with no controller playback or script schedule. At the final September 6 check it was PID **80174**; re-identify the process before future operations. `live-final-root/launch.json`, `trace.jsonl`, `audit.jsonl` and `window.png` record the exact process/build/root and visible HD result. The original-checkout game was not operated. Press **F7** to resume the preview.
