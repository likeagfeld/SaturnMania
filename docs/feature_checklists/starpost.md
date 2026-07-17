# StarPost -- global checkpoint lampposts (source-only port batch 2026-07-17)

## Decomp source (ported verbatim; TWO cited Saturn deltas, both bonus-stage)
- `tools/_decomp_raw/SonicMania_Objects_Global_StarPost.c` (whole file)
- `tools/_decomp_raw/SonicMania_Objects_Global_StarPost.h` (72 L)
- Sister reads (the checkpoint state graph): SaveGame.c:96-158 (recallEntities
  restore + new-act reset), Zone.c:879-894 (Zone_State_ReloadScene stores the
  clock into StarPost->stored*), GameOver.c:319, PauseMenu.c:476-501,
  Player.c:2224, ActClear.c:766/790 (StarPost_ResetStarPosts).

## Placement census (MEASURED, tools/_parse_ghz_scene.py)
- GHZ1: 4 lampposts -- slot 15 (6528,872), slot 44 (4580,544),
  slot 365 (7784,1748), slot 399 (14860,1648). First checkpoint by x = 4580.
- GHZ2: 7 lampposts. AIZ/GHZCutscene: 0 placed (global class still StageLoads).

## Entity/static size (computed from the census include tree)
- EntityStarPost = 88 + state 4 + 9x int32 + ballPos 8 + 3x Animator(24) +
  hitboxStars 8 + interactedPlayers 1 -> 224 B <= 344 narrow stride. No shrink.
- ObjectStarPost ~72 B <= 592 RegisterObject static cap (PLAYER_COUNT=4 arrays).

## Saturn deltas (the ONLY non-mechanical translations; in-file citations)
1. Bonus-stage ARMING gated out (`#if !defined(SATURN_GLOBALS_RETARGET)` around
   the rings>=quota block in StarPost_CheckCollisions): bonusStageID>0 leads to
   the floating-stars circle + warp into "Blue Spheres" -- a scene NOT in the
   Saturn scene set. Checkpoint touch/spin/save is verbatim.
2. Bonus WARP block in StarPost_CheckBonusStageEntry gated out (same flag):
   (a) SetScene("Blue Spheres")/("Pinball") destinations unported;
   (b) LINK CLOSURE: SaveGame_SaveGameState / SaveGame_GetSaveRAM are NOT
   exported by game.elf (grep game.map 2026-07-17: 0 hits) -- the verbatim ref
   would fail the ovl_ring -R link. Restore both blocks when Blue Spheres ports.
- Achievement path verbatim: MANIA_USE_PLUS=0 -> API_UnlockAchievement ==
  APICallback_UnlockAchievement (pack closure-edge stub, ord 62 inert);
  ACH_STARPOST=4 < achievementList[6] (p6_closure_edge.c:179). Same as SignPost.

## Static-state sharing (the critical seam -- pack TUs write StarPost->*)
- The class is OVERLAY-resident but SaveGame/Zone/GameOver/PauseMenu/Player
  (all PACK TUs, verified in the pack .o list) read/write StarPost->postIDs/
  playerPositions/stored* on the death-respawn chain. Pre-port the pack
  `StarPost` was NULL (writes landed in ROM space = silent no-op = checkpoints
  lost) or the M3.1 zeroed instance (P6_AIZ_TEST).
- FIX: `api->starpost_slot` rewire (the #235 Ring-seam pattern, same as
  animals_slot/itembox_slot) -- p6_io_main repoints the pack global at the
  overlay's registered instance every frame at BOTH tick sites (p6_ghz_frame +
  p6_frontend_frame). One shared ObjectStarPost instance.
- ActClear's StarPost_ResetStarPosts call: pack stub (ord 55) now FORWARDS via
  `p6_ovl_starpost_reset_raw` (the #258b p6_ovl_loserings_raw pattern).
  qa_p6_edge_audit.py PORTED set += StarPost (a stub hit = unwired forward =
  HARD RED).
- TMZ2Setup (`if (!TMZ2Setup)` guards, StarPost.c:97/244): NULL placeholder
  added to p6_closure_edge.c + -u root (linear-zone arm taken; TMZ2 unported).
- AIZ note: the M3.1 zeroed pack instance (#if P6_AIZ_TEST, p6_closure_edge.c)
  is retained as the pre-overlay-load fallback; post-load the rewire supersedes
  it. AIZTornado/AIZTornadoPath now bind intra-overlay to the REAL StarPost --
  fresh-boot postIDs[0]==0 preserves the fly-in-arm semantics exactly.

## Assets
- Anim `Global/StarPost.bin` (parsed: sheet `Global/Objects.gif` == GLOBJ.SHT
  staged 8th sheet; 6 anims: Post 2f, Bulb Unused 1f, Bulb Used 2f speed 32,
  Stars 1-3 4f). NOTE: the task brief said Items.gif/ITEMS.SHT -- MEASURED
  wrong; the .bin references Global/Objects.gif. ADDED to build_anim_pack.py
  OBJ_BINS (GHZ pack; already in AIZ_OBJ_BINS via the #302 seam batch).
  GHZCutscene leg: not in HBHOBJ.PAK -> StageLoad slow-paths one GFS seek at
  that seam (~135-200 ms one-shot) -- flagged for the primary's seam budget.
- Sfx `Global/StarPost.wav` + `Global/SpecialWarp.wav`: GetSfx -1 -> PlaySfx
  no-op if unstaged (declared gap class; HUD already GetSfx's StarPost.wav).

## Wiring
- w4 compile: `Global_StarPost:Game_StarPost`; overlay link `Game_StarPost.o`
  (build_shipping.sh); register_object_full after Splats (all flavors --
  global class); witnesses p6_w_starpost_classid / p6_w_starpost_aniframes;
  -u roots: witnesses + _TMZ2Setup + _p6_ovl_starpost_reset_raw.

## Audits (CLAUDE.md 4.5.1)
- Audit 1 layering: drawGroup = Zone->objectDrawGroup[0] (Create, verbatim);
  pole under ball (Draw order, verbatim decomp).
- Audit 2 cadence: ballAnimator ProcessAnimation verbatim; Bulb Used speed 64
  after interaction (Create :67), speed 0 during spin (:315) -- decomp values.
- Audit 3 pivot/flip: ball position = Cos1024/Sin1024 orbit (Draw :29-31),
  verbatim; no flipped composites.
- Audit 4 boot budget: Global/StarPost.bin ~300 B inside the existing
  GHZOBJ.PAK read; TU ~5 KB inside OVLRING.BIN. Zero new synchronous opens on
  the GHZ path; ONE new slow-path seek at the GHZCutscene seam (noted above).

## Gates
- RED (verified 2026-07-17 pre-fix): `grep StarPost_StageLoad game.map
  tools/_portspike/_p6/ovl_ring.map` -> 0 hits in both.
- GREEN required (post primary batch build): StarPost_StageLoad in
  ovl_ring.map; live GHZ capture p6_w_starpost_classid > 0 +
  p6_w_starpost_aniframes >= 0; lamppost visible at x=4580 (GHZ1 slot 44) and
  x=6528 (slot 15); checkpoint chain: touch post (sfx optional) -> die past it
  -> respawn at post x/y+16 (StarPost_StageLoad :119-121); edge audit ord 55
  zero after an act-clear run; chain _end < 0x060C8000.

## Memory walls
- Pack .bss: +16 B (2 witnesses + forward ptr + api fields starpost_slot/
  reset_fn grow s_ovl by 8). Overlay: ~5 KB TU in the 0x30000 window.
- Entity pool: +4 narrow entities in GHZ1 (+7 in GHZ2).
