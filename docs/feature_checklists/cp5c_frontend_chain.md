# CP5c — Front-end FLOW chain: boot Logos -> play logos -> auto-advance to Title

Task #270. Wire the REAL front-end flow: the build boots the Logos (SEGA)
splash scene, plays the logos, then auto-advances to the Title screen --
both rendered on the verbatim RSDKv5 engine, in ONE run.

CP4/CP4b proved a P6_FRONTEND_LOGOS build boots + renders the Logos splash.
CP5a/CP5b proved a P6_FRONTEND_TITLE build boots DIRECTLY into Title +
renders the full SONIC MANIA title (logo + Sonic + finger-wave). CP5c
connects them with a robust EXPLICIT-SELECT advance (NOT scene-list
adjacency).

## Decomp / source counterpart

- Engine scene-transition machine: `RetroEngine.cpp` ProcessEngine
  ENGINESTATE_LOAD case (LoadSceneFolder -> LoadSceneAssets -> InitObjects)
  -> Saturn-side `p6_scene_load_and_arm` (p6_io_main.cpp:4525).
- The advance trigger in the decomp front-end: LogoSetup_State_NextLogos ->
  `RSDK.LoadScene()` (++listPos) once all logos play. On Saturn the lean
  `p6_frontend_frame` currently SWALLOWS that ENGINESTATE_LOAD (line 4936,
  CP4 placeholder). CP5c lifts the swallow for Logos->Title.

## Files modified

| File | Change |
|---|---|
| `Makefile` | `ifeq ($(P6_FRONTEND_CHAIN),1)` -> add `-DP6_FRONTEND_CHAIN -DP6_FRONTEND_TITLE -DP6_FRONTEND_LOGOS` (self-imply, mirror the TITLE block at line ~217). |
| `tools/_portspike/_p6/build_shipping.sh` | `if [ -n "$P6_FRONTEND_CHAIN" ]; then export P6_FRONTEND_TITLE=1; fi` at top (which then self-implies LOGOS); add `${P6_FRONTEND_CHAIN:+P6_FRONTEND_CHAIN=1}` to both `make` invocations + a `[5d]` symbol grep. |
| `tools/_portspike/_p6/build_p6scene_objs.sh` | Same self-imply at top; add `${P6_FRONTEND_CHAIN:+-DP6_FRONTEND_CHAIN}` to the io_main + ovl_ghz compiles + the `-u` witness root for `p6_w_chain_fired`. |
| `tools/_portspike/_p6/p6_io_main.cpp` | (1) Boot dispatch (line ~5231): `#if defined(P6_FRONTEND_CHAIN)` branch (PRECEDENCE over TITLE) -> `p6_logos_reload()`. (2) `p6_frontend_frame` ENGINESTATE_LOAD (line ~4936): under chain, folder=="Logos" -> `p6_title_reload()` ONCE (static guard) + reset the VDP1 bind table for the folder change; folder=="Title" -> keep swallowing. (3) witnesses `p6_w_chain_fired`, `p6_w_chain_folder_pre`. |
| `tools/_portspike/qa_title_chain.py` | NEW RED-first gate. V1 early Logos/SEGA blue px; V2 later Title logo + Sonic-blue px; V3 transition fired. Saves `_chain_logos.png` + `_chain_title.png`. |

## src/rsdk APIs required

All EXIST (additive flow wiring, no new engine API):
- `p6_logos_reload()` (p6_io_main.cpp:4841) -- boot the Logos scene.
- `p6_title_reload()` (p6_io_main.cpp:4876) -- select + load+arm the Title scene.
- `p6_scene_load_and_arm()` (4525) -- the generic LoadScene chain (LoadSceneFolder/Assets/InitObjects) + VDP1 re-bind via `p6_ghz_arm_env` (4702).

## RESIDENCY ANALYSIS (the LIKELY failure point -- MEASURE, don't assume)

MEASURED from the code (before building):
- Sheets LOGOS.SHT + TLOGO.SHT + TSONIC.SHT are staged ONCE at boot in
  `p6_scene_run` (p6_io_main.cpp:3293-3380), ALL THREE under
  P6_FRONTEND_TITLE (which CHAIN implies). The 896 KB front-end band store
  (SaturnSheet.cpp:100, base 0x22720000) holds all 12 slots simultaneously
  -- so the second (Title) load does NOT re-stage; the slots persist.
- The per-scene VDP1 BIND runs in `p6_ghz_arm_env` (4702), called every
  `p6_scene_load_and_arm`. After Logos->Title it re-runs and binds Title's
  `Title/Logo.gif` + `Title/Sonic.gif` surfaces (loaded fresh by Title's
  LoadSceneAssets) to VDP1 by hash against the persisted slots.
- **HAZARD (chain-specific, new):** `p6_vdp1HandleBySurface[]` is
  init-once (`p6_vdp1HandlesInit` static, line 2218) and NEVER reset on a
  folder change. The bind loop SKIPS surfaces with handle>=0 (line 2252).
  In the DIRECT title boot Title is the first load (table all -1, binds
  clean). In the CHAIN, Logos binds its UIPicture surface(s) FIRST; when
  the folder changes to Title, `LoadSceneFolder` runs ClearGfxSurfaces and
  Title surfaces may reuse those surface indices -> the stale "bound"
  entry blocks the re-bind -> Title renders blank. **Plan: MEASURE the
  tlogo/tsonic handle witnesses on the post-transition capture; if <0,
  the fix is to reset p6_vdp1HandleBySurface[] (+p6_vdp1HandlesInit=false)
  on the Logos->Title fire (front-end-only).**

## TIMING (measure, don't assume)

Lean boot + Logos play + Title load + settle is DEEP. CP5b.2 title alone
settled ~70s wall-clock; the chain runs Logos FIRST then loads Title, so
it is longer. The gate burst window is calibrated by MEASURING which frame
shows Logos and which later frame shows Title (not a fixed guess).

## Scene-list order (measure, report -- design does NOT depend on it)

Report Logos listPos vs Title listPos from the engine scene list. The
explicit-select design (`p6_title_reload` scans every category for folder
"Title") is order-independent; the report is evidence the advance is
explicit, not adjacency.

## Acceptance (qa_title_chain.py, RED-first; thresholds MEASURED)

- V1: an EARLY frame shows the Logos/SEGA splash -- saturated SEGA-blue px
  in the central splash region > MEASURED threshold (CP4b `_logos_fixed.png`
  ground truth: large blue SEGA on black).
- V2: a LATER frame shows the Title -- SONIC MANIA logo mass + Sonic-blue
  head px > MEASURED thresholds (reuse qa_title_logo / qa_title_sonic).
- V3: the transition FIRED -- `p6_w_chain_fired==1` AND
  `p6_w_frontend_folder_tag==0x5469` ('Ti') from the same run.
- BOTH V1 and V2 from the SAME run prove the chain.

## Do-not-regress

- P6_FRONTEND_TITLE gates (qa_engine_title / qa_title_logo / qa_title_sonic)
  stay GREEN on a P6_FRONTEND_TITLE build (chain is additive).
- Default GHZ build byte-identical: all CP5c code under
  `#if defined(P6_FRONTEND_CHAIN)`; `_end == 0x060B6BA0`; R0-R16 GREEN.

## PROACTIVE-DETECTION-CHECKLIST (no NEW draw calls land in this port)

CP5c adds NO new sprite/draw call to Game.c or an entity Draw callback --
it only RE-DRIVES the already-ported Logos + Title scenes through the
existing `p6_scene_load_and_arm` + `p6_frontend_frame` path. So audits
1-3 (Z-order / cadence / pivot-flip) were resolved in CP4b (Logos) and
CP5b.1/.2 (Title logo + Sonic) and are unchanged.

- Audit 1 (Z-order): unchanged -- same UIPicture (Logos) + TitleLogo/
  TitleSonic (Title) draw order CP4b/CP5b already validated.
- Audit 2 (cadence): unchanged -- TitleSonic anim walker per CP5b.2.
- Audit 3 (pivot+flip): unchanged -- TitleSonic head/finger per CP5b.2.
- Audit 4 (boot-delay budget): CP5c adds NO new synchronous asset to the
  boot path. All 3 front-end sheets (LOGOS/TLOGO/TSONIC) were already in
  the P6_FRONTEND_TITLE boot path (CP5b.2). The Title scene's
  LoadSceneFolder/Assets at the Logos->Title transition runs at RUNTIME
  (post-logos-play), NOT in mania_engine_init -- it is the same CD read
  CP5a/CP5b already pay, just triggered by the advance instead of at boot.
  Net new boot-path asset bytes = 0.
