# Sonic Mania (Saturn) - Jo Engine project Makefile
#
# Built inside the joengine-saturn Docker image with this repo bind-mounted at
# /work. Jo Engine lives at jo-engine/ (cloned), so the engine source and bundled
# toolchain are referenced relative to this project root.
#
#   docker run --rm -v "D:/sonicmaniasaturn":/work -w /work joengine-saturn make

# --- Jo Engine module toggles (1 = compile in, 0 = leave out) ---
JO_COMPILE_WITH_VIDEO_MODULE        = 0
# Note: do NOT add inline `#` comments after a `= 1` value -- GNU make's
# `ifeq (1, ${VAR})` check fails when VAR is `1<spaces>` from the trailing
# whitespace before the comment. Phase 3 wasted a build cycle debugging
# this. Comments go on their own line above the variable.
# Backup RAM module: save data for Phase 3 tally + future menu Continue.
JO_COMPILE_WITH_BACKUP_MODULE       = 1
# TGA loader for converted sprites.
JO_COMPILE_WITH_TGA_MODULE          = 1
# PCM SFX (Phase 4): re-enabled after diagnosing the VDP2 conflict. SGL 2.0A
# release notes flagged the exact pattern -- SGL's sound subsystem uses CPU
# DMA for internal data transfers and that conflicts with scroll-screen CPU
# DMA when V-blank fires. Fix is to put our vblank page DMA on SCU DMA
# (slDMAXCopy with the cache-through source alias) instead of CPU DMA
# (slDMACopy). See fg_vblank() in src/main.c.
JO_COMPILE_WITH_AUDIO_MODULE        = 1
JO_COMPILE_WITH_3D_MODULE           = 0   # jo_sprite_draw3D lives in sprites.h
JO_COMPILE_WITH_PSEUDO_MODE7_MODULE = 0
JO_COMPILE_WITH_EFFECTS_MODULE      = 0
JO_DEBUG                            = 1   # engine error checks
JO_COMPILE_WITH_PRINTF_MODULE       = 0   # off: frees VDP2 bank B1 for the NBG2
                                          # parallax sky (printf console owns B1)

# jo's malloc pool is core.c's static global_memory[] BSS array, sized by this
# value. It must satisfy TWO constraints (verified from game.map, not guessed):
#  (a) hold the resident asset peak. With nothing freed, jo_fs_read_file keeps
#      GHZFG PAL+CEL+PAT+TMP (357KB) + GHZSKY (90KB) + Sonic SPRs (40KB) = ~477KB
#      resident; an undersized pool makes jo_fs_read_file return NULL and the
#      NBG1 page/cells are then built from garbage (the "FG scramble" bug --
#      it was a failed allocation, NOT an SGL work-area overrun).
#  (b) keep _end (top of .bss) below 0x060C0000, where SGL's work area begins
#      (SortList/SpriteBuf/Zbuffer; jo sega_saturn.h JO_WORK_RAM_SORT_LIST).
#      .bss base ~0x0601F110 -> max safe pool ~= 0x060C0000-0x0601F110 = 643KB.
# 544KB: holds the 477KB peak (~67KB margin) and _end=0x060ACA60 (77KB below SGL).
# NOTE: changing this REQUIRES deleting jo-engine/jo_engine/*.o before `make` --
# make won't recompile jo's prebuilt core.o on a Makefile-variable change, so a
# stale (default 384KB) pool silently persists otherwise.
#
# Phase 2.1 (2026-05-27): kept 384KB pool (BSS budget already at the
# 76KB-margin watermark per Phase 1.19 close). GHZ Act 1's peak resident
# = FG.CEL(90KB)+FG.PAT(5KB)+FG.TMP(262KB)+FG.PAL(0.5KB)+SKY.DAT(90KB)+
# SKY.PAL(0.5KB) = ~448KB total but loaded SEQUENTIALLY — the title
# backdrop's TITLE.DAT (112KB) is jo_free'd at the title->GHZ transition
# (see src/main.c::mania_free_title_bg_buffers) BEFORE the GHZ load
# fires, recovering enough pool space to fit the GHZ residents. Bumping
# the pool past 384KB pushes _end above the 0x060C0000 SGL work-area
# boundary (verified: 458752 -> _end=0x060C0600, +1.5KB INTO SGL).
#
# Phase 3.2.b-post BSS RESCUE (2026-05-28) — VALUE FLIPPED 393216 -> 262144
# after Phase 2.4c.2 independently confirmed the same diagnosis + landed
# entity_atlas MAX_FRAMES 72->34 trim (~36 KB recovered) + Phase 3.2.b
# SNGL typo fixed to No_Texture in drawing.c:500.
#
# Symptom: user reported "black after Sega splash" 2026-05-28 18:50.
# Root cause: game.map showed __bend=0x060C27E0, exceeding the
# 0x060C0000 SGL boundary by 10 KB. SGL SortList + SpriteBuf at
# 0x060C0000+ get corrupted by BSS overlap; SGL hangs on first use.
# Contributors: Phase 2.4e v2 entity_atlas BSS + Phase 3.2.a
# UIControl/UIBackground Object+Entity arrays + Phase 3.2.b UIWidgets
# Object+Entity arrays.
#
# Rescue (pending): reduce 393216 -> 262144 (384KB -> 256KB). Feasible
# because Phase 2.3l moved FG.TMP (262KB) + SKY.DAT (90KB) to LWRAM.
# Actual jo-pool peak post-LWRAM-moves:
#   GHZ residents (FG.PAL+CEL+PAT) ~96 KB
#   Transient SP2 loader buffer (largest entity ~50 KB) ~50 KB
#   Title transient TITLE.DAT (freed before GHZ load) peak 112 KB
# Realistic peak = max(96+50, 112) = ~146 KB. 256KB gives 110KB margin
# over realistic peak + recovers 128KB BSS (100+ KB safety to 0x060C0000).
#
# RECURRENCE RULE: any future BSS growth must check game.map
# __bend < 0x060C0000 before claim-done. New hard gate in verify_done.ps1
# (V-BSS) lands alongside the value flip.
JO_GLOBAL_MEMORY_SIZE_FOR_MALLOC = 262144
# P6.7 W12b root-cause REVISION (Task #227, 2026-06-12): the W11b "8 KB
# P6SCENE pool trim = MEASURED FATAL" conclusion is FALSIFIED. That crash
# (PC 0x06000956, PR in gfs_mngSetErrCode) was the pack-orphan-.bss-overlap
# class (qa_p6_mapoverlap.py; fixed by the p6_pack_merge.ld pass in
# build_p6scene_objs.sh) -- the trim merely re-rolled the overlap layout.
# With the overlap fixed, the merged map accounts the TRUE diag .bss and
# overshoots the 0x060C0000 overlay floor by ~255 KB, most of it this pool.
# The diag flavor therefore trims the pool to 32 KB (P6 loads run through
# GFS windows/LWRAM/fixed windows, not jo_malloc); the SHIPPING hand-port
# build keeps the full 256 KB. Builders rm jo core.o + p6_vdp1.o/p6_snd.o
# per build (stale-object rules).
ifeq ($(P6SCENE),1)
JO_GLOBAL_MEMORY_SIZE_FOR_MALLOC = 32768
endif
# P6.8 Step B (Task #211): the lean engine SHIPPING flavor uses the SAME 32 KB
# engine-flavor pool as P6SCENE (the P6 load path runs through GFS windows /
# LWRAM / fixed windows, not jo_malloc).
ifeq ($(P6_ENGINE_SHIPPING),1)
JO_GLOBAL_MEMORY_SIZE_FOR_MALLOC = 32768
endif

# --- QA build flag ---
# `make QA_MODE=1` compiles main.c with -DQA_MODE so the title screen holds
# indefinitely (deterministic capture for qa_refdiff). Default (unset) builds
# the release/demo binary that auto-advances the title and runs the demo.
ifeq ($(QA_MODE),1)
CCFLAGS += -DQA_MODE
endif

# Phase 2.4g.1: `make QA_INVBLOCK_PROBE=1` places the GHZ player on the
# nearest InvisibleBlock (world (1396,920)) at spawn so the savestate gate
# (tools/qa_phase2_4g1_gate.py --with-savestate) can observe collisionFlagV
# go nonzero. The canonical Mania spawn (108,947) is 1288 px away and Phase
# 2.4f removed autorun, so a resting release player never overlaps a block.
# Compiled out of release ISOs (QA-instrument precedent: QA_MODE title-hold).
ifeq ($(QA_INVBLOCK_PROBE),1)
CCFLAGS += -DQA_INVBLOCK_PROBE
endif

# SFX-confirmation probe (2026-05-29): `make QA_SFX_PROBE=1` suppresses all
# CD-DA BGM (Music_PlayTrack + the GHZ track-2 start) and fires the jump PCM
# SFX on a fixed cadence in the GHZ-active tick, so a -soundrecord WAV capture
# is SFX-only and a savestate can peek g_qa_sfx_fire_count / g_qa_sfx_ready_flag
# (Entities.c). Default period 30 ticks; override with QA_SFX_PROBE_PERIOD=N.
# Compiled out of release ISOs (QA-instrument precedent: QA_MODE title-hold).
ifeq ($(QA_SFX_PROBE),1)
CCFLAGS += -DQA_SFX_PROBE
ifneq ($(QA_SFX_PROBE_PERIOD),)
CCFLAGS += -DQA_SFX_PROBE_PERIOD=$(QA_SFX_PROBE_PERIOD)
endif
endif

# Phase 2.3i: per-build override of the title->GHZ auto-advance timer (ticks
# at 60Hz). Pass GHZ_AUTOADVANCE_TICKS=480 on the make CLI to force an
# 8-second title hold so the savestate harness can race-free park itself in
# GHZ-active state. Default (unset) inherits the Game.c-level fallback (0,
# release/demo builds) so production ISOs remain unaffected by this knob.
ifneq ($(GHZ_AUTOADVANCE_TICKS),)
CCFLAGS += -DGHZ_AUTOADVANCE_TICKS=$(GHZ_AUTOADVANCE_TICKS)
endif

# P6.2 Path A (Task #206): host the engine file-I/O proof inside this PROVEN
# jo build. `make P6IO=1` defines P6IO_HOOK (main.c calls p6_io_run() once,
# right after jo_core_init, on jo's live GFS) and links three PRE-BUILT proof
# objects: the engine's UNMODIFIED RSDK/Core/Reader.cpp (Core_Reader.o), the
# Saturn GFS FileIO backend under test (p6_gfs.o), and the lean proof driver +
# RSDK stubs (p6_io_main.o). These are plain .o (always fully linked) and are
# appended to LIBS BEFORE the `include` below so they PRECEDE the archives the
# include appends (SEGA_SYS.A/LIBCD.A/LIBSGL.A at jo_engine_makefile:231, -lgcc
# at :293, and the gcc-driver-appended -lc) -- the proof objects pull GFS_* from
# LIBCD.A and snprintf/str*/mem* from newlib -lc, so they must come first.
# Unset (the shipping build) skips the block entirely: byte-identical ISO.
ifeq ($(P6IO),1)
CCFLAGS += -DP6IO_HOOK
LIBS += tools/_portspike/_p6/p6_io_main.o tools/_portspike/_p6/p6_gfs.o tools/_portspike/_p6/Core_Reader.o
endif

# P6.3 Path A (Task #207): host the engine LoadScene proof inside this PROVEN
# jo build. `make P6SCENE=1` defines P6SCENE_HOOK (main.c calls p6_scene_run()
# once, right after jo_core_init, on jo's live GFS) and links ONE pre-built,
# pre-gc'd pack object: `ld -r --gc-sections` of {p6_io_main, p6_gfs,
# Core_Reader, Scene_Scene, Storage_Storage, miniz} rooted at p6_scene_run
# (tools/_portspike/_p6/build_p6scene_objs.sh). The gc-pack exists because the
# six objects compiled whole add ~107 KB while this image's WRAM-H headroom to
# the 0x060C0000 SGL floor is 41,936 B (game.map _end 0x060B5C30, 2026-06-06);
# the pack keeps only the LoadSceneAssets closure and parks every big engine
# array in WRAM-L (see p6_io_main.cpp P6_SCENE_TEST block). Same position as
# the P6IO block so the pack PRECEDES LIBCD.A (GFS_*), -lgcc, and -lc. Unset
# (the shipping build) skips the block entirely: byte-identical ISO. Build
# P6IO=1 and P6SCENE=1 are mutually exclusive (shared intermediate .o paths).
# Gate: tools/_portspike/qa_p6_scene.py.
ifeq ($(P6SCENE),1)
CCFLAGS += -DP6SCENE_HOOK
LIBS += tools/_portspike/_p6/p6_scene_pack.o
endif

# P6.8 Step B (Task #211): the verbatim engine becomes the SHIPPING boot.
# `make P6_ENGINE_SHIPPING=1` defines P6_ENGINE_SHIPPING (main.c boots
# p6_engine_boot_and_run() instead of parking at the diag proof or the
# hand-port) and links the SAME pre-gc'd engine pack p6_scene_pack.o as P6SCENE
# -- the pack now exports the lean boot entry (build_p6scene_objs.sh -u root).
# The lean boot runs the masked load core (InitStorage..LoadGameConfig + staged
# sheets/bands/anim-pack + audio) then drives GHZ continuously via p6_scene_tick;
# no diagnostic burst / Title reload / legacy Ring. Same position as the P6SCENE
# block so the pack PRECEDES LIBCD.A/-lgcc/-lc. P6_ENGINE_SHIPPING / P6SCENE /
# P6IO are mutually exclusive (shared intermediate .o paths). Unset (the default
# `make` hand-port build) skips this entirely.
# Gate: tools/_portspike/qa_p6_shipping.py.
ifeq ($(P6_ENGINE_SHIPPING),1)
CCFLAGS += -DP6_ENGINE_SHIPPING
LIBS += tools/_portspike/_p6/p6_scene_pack.o
endif

# CP4c BLUE-SCREEN FIX: the FRONT-END Logos flavor (-DP6_FRONTEND_LOGOS, passed to
# the pack via build_p6scene_objs.sh) must ALSO reach the jo-make compile of the
# jo-side TU p6_vdp1.c -- that TU's VDP1 slot-box size (P6_SPR_MAXW/H) and the
# off-.bss staging-buffer relocation are FRONT-END-conditional. Without this the
# Logos frames (up to 187x89) hit the 64x64 oversize-drop guard and never blit
# (MEASURED: p6_w_vdp1_dropreason==1, lastwh==0x00BB003A == 187x58). Picked up
# from the environment (docker -e P6_FRONTEND_LOGOS=1). The DEFAULT (GHZ) build
# leaves it empty so p6_vdp1.c compiles byte-identical (64x64x40, .bss buffers).
ifeq ($(P6_FRONTEND_LOGOS),1)
CCFLAGS += -DP6_FRONTEND_LOGOS
endif

# CP5a (Task #267): the TITLE front-end flavor. P6_FRONTEND_TITLE IMPLIES
# P6_FRONTEND_LOGOS -- it reuses the shared #if defined(P6_FRONTEND_LOGOS) machinery
# (the VDP1 oversize box in the jo-side p6_vdp1.c, p6_vdp2_arm_sprites_only, the
# frontend_frame). Both flags reach the jo-make compile of p6_vdp1.c here so the
# Title logo/electricity frames (up to 187x89) clear the 64x64 oversize-drop guard.
# build_shipping.sh passes P6_FRONTEND_TITLE=1 (which also sets LOGOS) for the Title
# flavor. The DEFAULT (GHZ) build leaves both unset -> p6_vdp1.c byte-identical.
ifeq ($(P6_FRONTEND_TITLE),1)
CCFLAGS += -DP6_FRONTEND_TITLE -DP6_FRONTEND_LOGOS
endif

# CP5c (Task #270): the front-end FLOW CHAIN flavor (boot Logos -> play -> auto-
# advance to Title). P6_FRONTEND_CHAIN IMPLIES P6_FRONTEND_TITLE which IMPLIES
# P6_FRONTEND_LOGOS -- the chain reuses every shared front-end machinery (Logos +
# Title registrations, the VDP1 box, the SaturnSheet 12-slot store, the
# frontend_frame). build_shipping.sh passes P6_FRONTEND_CHAIN=1 (which self-implies
# TITLE+LOGOS). The DEFAULT (GHZ) build leaves all three unset -> p6_vdp1.c
# byte-identical. (The jo-side TUs compiled here only need the LOGOS/TITLE box; the
# -DP6_FRONTEND_CHAIN advance logic lives in the pack TU p6_io_main.cpp, set by
# build_p6scene_objs.sh -- defining it here too is harmless + keeps the implication
# self-evident.)
ifeq ($(P6_FRONTEND_CHAIN),1)
CCFLAGS += -DP6_FRONTEND_CHAIN -DP6_FRONTEND_TITLE -DP6_FRONTEND_LOGOS
endif

# --- Our sources ---
#
# Phase 0.5 foundation alignment (2026-05-26): the hand-rolled
# game-logic code (main.c v0.1, player.c, save.c, title_sonic.c,
# intro_video.c) has been archived under src/_archived/ pending the
# decomp-port effort. The Phase 0.5 build is intentionally MINIMAL —
# just the engine-compat layer (src/rsdk/) + a boot stub (src/main.c)
# that queues the Title scene. The actual SonicMania/Objects/*.c
# files will land under src/mania/Objects/ during Phase 1+ per
# docs/COMPREHENSIVE_PLAN.md.
SRCS = src/main.c \
       src/rsdk/storage.c src/rsdk/collision.c src/rsdk/colwindow.c \
       src/rsdk/puff.c \
       src/rsdk/spc.c \
       src/rsdk/object.c \
       src/rsdk/animation.c src/rsdk/save.c src/rsdk/input.c \
       src/rsdk/drawing.c src/rsdk/audio.c src/rsdk/scene.c \
       src/rsdk/math.c src/rsdk/palette.c src/rsdk/string.c \
       src/rsdk/tilelayer.c src/rsdk/api.c \
       src/rsdk/scene_ghz.c \
       src/rsdk/entity_atlas.c \
       src/rsdk/player_atlas.c \
       src/rsdk/breadcrumb.c \
       src/mania/Game.c \
       src/mania/Objects/Title/TitleSetup.c \
       src/mania/Objects/Title/TitleLogo.c \
       src/mania/Objects/Title/TitleSonic.c \
       src/mania/Objects/Title/TitleBG.c \
       src/mania/Objects/Title/Title3DSprite.c \
       src/mania/Objects/Title/TitleAssets.c \
       src/mania/Objects/GHZ/GHZSetup.c \
       src/mania/Objects/GHZ/BuzzBomber.c \
       src/mania/Objects/GHZ/SpikeLog.c \
       src/mania/Objects/GHZ/Newtron.c \
       src/mania/Objects/GHZ/Chopper.c \
       src/mania/Objects/GHZ/Crabmeat.c \
       src/mania/Objects/GHZ/Batbrain.c \
       src/mania/Objects/GHZ/Bridge.c \
       src/mania/Objects/Global/Player.c \
       src/mania/Objects/Global/Spikes.c \
       src/mania/Objects/Global/InvisibleBlock.c \
       src/mania/Objects/Global/Zone.c \
       src/mania/Objects/Global/BoundsMarker.c \
       src/mania/Objects/Global/PlaneSwitch.c \
       src/mania/Objects/Global/TitleCard.c \
       src/mania/Objects/Common/Entities.c \
       src/mania/Objects/Common/Platform.c \
       src/mania/Objects/Common/CollapsingPlatform.c \
       src/mania/Objects/Common/ForceSpin.c \
       src/mania/Objects/Common/BreakableWall.c \
       src/mania/Objects/Common/SpinBooster.c \
       src/mania/Objects/Menu/UIControl.c \
       src/mania/Objects/Menu/UIBackground.c \
       src/mania/Objects/Menu/UIWidgets.c \
       src/mania/Objects/Menu/UIButton.c \
       src/mania/Objects/Menu/UIButtonPrompt.c \
       src/mania/Objects/Menu/UISubHeading.c

# P6.7 Player wave (Task #227, 2026-06-12): the DIAG flavor drops the parked
# hand-port game sources entirely. The P6SCENE jo_main parks at p6_scene_run +
# jo_core_run BEFORE any hand-port call (src/main.c:1227-1241), LTO was
# already sweeping their unreachable .text, but their __attribute__((used))
# atlas globals + statics survived in the ltrans .bss and the W12b honest
# accounting (post-overlap-fix) left only 4,284 B under the 0x060C0000 floor
# -- nowhere near the ~102 KB Player closure. The pack's jo-side import
# surface is EXACTLY ONE src/ symbol (rsdk_storage_load_to_lwram, measured
# from p6_scene_pack.o's ELF symtab, 52 undefineds total) -- so the diag
# needs only main.c + storage.c here. The SHIPPING `make` keeps the full
# hand-port list above.
ifeq ($(P6SCENE),1)
SRCS = src/main.c src/rsdk/storage.c \
       tools/_portspike/_p6/p6_handport_stubs.c
endif

# P6.8 Step B (Task #211): the lean SHIPPING flavor links the SAME lean SRCS as
# the diag (main.c + storage.c + the hand-port stub TU) -- the pack's only src/
# import is rsdk_storage_load_to_lwram, and the hand-port game sources stay out
# (their used-attribute atlas globals would breach the WRAM-H floor). The full
# hand-port SRCS list above ships only in the default `make`.
ifeq ($(P6_ENGINE_SHIPPING),1)
SRCS = src/main.c src/rsdk/storage.c \
       tools/_portspike/_p6/p6_handport_stubs.c
endif

# P6.5b2 (Task #208): the VDP1 sprite half of the engine proof compiles
# INSIDE this make (appended AFTER the SRCS = assignment above -- an earlier
# `SRCS +=` would be wiped) so it sees the project's exact jo configuration
# flags; jo/jo.h struct layouts depend on them, and a flag mismatch between
# TUs corrupts the sprite tables silently (the FR-2 #189 clobber class).
ifeq ($(P6SCENE),1)
SRCS += tools/_portspike/_p6/p6_vdp1.c
# P6.6b (Task #209): the SCSP playback half -- routes the engine-converted
# S16 buffer through jo_audio_play_sound (jo.h-dependent, same rule as above).
SRCS += tools/_portspike/_p6/p6_snd.c
# Perf Phase 1 (Task #211): jo-side FRT read + true-60Hz vblank counter (uses
# SGL SEGA_TIM.H + jo_core_add_vblank_callback -- jo-config-dependent, same rule).
SRCS += tools/_portspike/_p6/p6_perf.c
endif

# P6.8 Step B (Task #211): the lean SHIPPING flavor compiles the SAME jo-config-
# dependent VDP1 sprite + SCSP halves inside the jo make as P6SCENE (jo/jo.h
# struct layouts depend on the project's flags -- the FR-2 #189 clobber rule).
ifeq ($(P6_ENGINE_SHIPPING),1)
SRCS += tools/_portspike/_p6/p6_vdp1.c
SRCS += tools/_portspike/_p6/p6_snd.c
# Perf Phase 1 (Task #211): jo-side FRT read + true-60Hz vblank counter.
SRCS += tools/_portspike/_p6/p6_perf.c
endif

# --- Cinepak Video (auto-linked when JO_COMPILE_WITH_VIDEO_MODULE=1) ---
# Per docs/cinepak_integration_spec.md: setting JO_COMPILE_WITH_VIDEO_MODULE=1
# is the FIRST step but NOT sufficient on its own. Verified 2026-05-26:
# bumping the flag alone makes jo_core_init call jo_video_init() which
# calls CPK_Init() + adds CPK_VblIn callback -- but Saturn still locks to
# black at boot. CPK_Init() needs additional pre-init (likely LWRAM
# malloc zone for its ~520 KB transient allocations per spec §4) that
# isn't covered by jo's default heap. Investigation paused; the manual
# LIBCPK.A linkage is restored below as the bisect-1 baseline (LIBCPK
# linked but no CPK_* refs = empty-stub intro_video.c = clean boot).
LIBS += jo-engine/Compiler/COMMON/SGL_302j/LIB_ELF/LIBCPK.A

# --- Engine + bundled toolchain locations (relative to this project root) ---
JO_ENGINE_SRC_DIR = jo-engine/jo_engine
COMPILER_DIR      = jo-engine/Compiler

include $(COMPILER_DIR)/COMMON/jo_engine_makefile

# --- Phase 2.3j (2026-05-28): -fno-lto rule for scene_ghz.c REMOVED ---
#
# HISTORICAL CONTEXT (kept as a doc record for future investigators):
#
# Phase 2.3h identified that with LTO + whole-program analysis, the
# volatile readiness flags in src/rsdk/scene_ghz.c (g_ghz_fg_ready /
# g_ghz_sky_ready / canary apparatus) got internalized as `*.lto_priv.NNN`
# and CSE'd / dead-store-eliminated despite the `volatile` qualifier.
# Phase 2.3h savestate at SaveFrame=30 measured all four canary bytes +
# g_ghz_fg_ready + g_ghz_fg_mirror reading 0x00, despite the commit
# block having clearly executed (NBG1ON in BGON + slDMACopy-populated
# VDP2 VRAM were visible in the same state).
#
# Phase 2.3h added per-file -fno-lto on scene_ghz.c. That helped the
# LOCAL TU but the cross-TU reader in mania/Game.c still went through
# whole-program LTO and read 0x00 from the volatile flag.
#
# Phase 2.3j eliminates the bug class entirely by retiring the async-
# readiness-gate architecture in favor of a synchronous decomp-style
# scene-load (mirroring rsdkv5-src/RSDKv5/RSDK/Core/RetroEngine.cpp:
# 345-384 ENGINESTATE_LOAD: LoadSceneFolder -> LoadSceneAssets ->
# InitObjects). With no cross-TU volatile flag in the gating path, the
# LTO workaround is no longer needed.
#
# Full LTO is restored on scene_ghz.c.
#
# RECURRENCE RULE: if a future change reintroduces a cross-TU volatile
# flag in the GHZ load path, do NOT immediately add back -fno-lto.
# Instead, audit whether the flag is actually necessary; the sync-load
# pattern can usually accomplish the same thing without a flag.

# --- Phase 1.21 B1: two-binary build pipeline (Path B intro chain) ---
#
# Default `make` (= jo's `all`) is unchanged: builds game.elf, writes
# cd/0.bin from game.raw, and produces game.iso/game.cue. This preserves
# the pre-Phase-1.21-B1 baseline so verify_done.ps1 still exits 0.
#
# `make intro` recurses into Makefile.intro to build the Cinepak-only
# binary under the JO_COMPILE_WITH_VIDEO_MODULE=1/AUDIO=0 profile.
# Output: cd/INTRO.BIN (separate from cd/0.bin -- the game ISO's first-
# read file is untouched).
#
# `make all_phase121b1` runs both. Default `all` REMAINS jo's `all`
# (game profile only), so build.bat and other existing callers see no
# behavioural change.
#
# See docs/COMPREHENSIVE_PLAN.md sec.13.8 for the bounded scope.

.PHONY: intro all_phase121b1 intro_clean

intro:
	$(MAKE) -f Makefile.intro intro

intro_clean:
	$(MAKE) -f Makefile.intro intro_clean

# Note: order is intro FIRST, then game. The intro build clobbers
# project-root game.elf/game.raw as intermediates (renamed to
# intro.elf/intro.raw in the post step); running the default `make`
# AFTER intro restores game.elf/game.raw/cd/0.bin to the game profile.
all_phase121b1: intro all
	@echo "[phase121b1] Both binaries built:"
	@echo "  cd/0.bin    -- GAME.BIN (default ISO first-read; unchanged)"
	@echo "  cd/INTRO.BIN -- Cinepak intro binary (Phase 1.22 chain target)"
	@echo "  intro.elf   -- intro ELF (for symbol inspection during B2)"

# --- Engine true-port pivot (Tasks #199-#204): TRUEPORT codegen target ---
#
# `make TRUEPORT=1` (or `make trueport`) compiles the true-port object set --
# the 16 platform-independent RSDKv5 logic-core TUs + the 5 Saturn backend shim
# TUs -- into tools/_portspike/_trueport_obj/ via build_trueport.sh (the P1
# gate). This is the incremental SH-2 true-port of the RSDKv5 engine that will
# replace the jo-engine hand-rewrite in src/rsdk + src/mania. It is GATED so the
# default `make` (jo's `all`: the working shipping game) stays byte-identical --
# the trueport target is inert unless explicitly selected, exactly like the
# `intro` / `all_phase121b1` phony targets above.
#
# The compile uses C++ + RSDKv5 headers + newlib (NOT jo's C SRCS), so it lives
# in its own script rather than in jo_engine_makefile's C rules (which must not
# be modified). P1 is CODEGEN ONLY (21/21 .o); the real Saturn link is P2.
.PHONY: trueport
trueport:
	bash tools/_portspike/build_trueport.sh

# TRUEPORT=1 redirects the default goal to `trueport` so the canonical invocation
# `make TRUEPORT=1` triggers the true-port codegen. This MUST come AFTER the
# jo_engine_makefile include (which sets its own first-target default goal) and
# is guarded so an unset / != 1 TRUEPORT leaves jo's `all` as the default goal --
# i.e. plain `make` is byte-identical to before this block existed.
ifeq ($(TRUEPORT),1)
.DEFAULT_GOAL := trueport
endif
