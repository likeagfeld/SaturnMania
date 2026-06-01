# Init sequence diff — archived (working) vs current (broken)

Inputs:
- `docs/init_sequence_archived.md`
- `docs/init_sequence_current.md`
- Phase 1.4 empirical evidence: probe sprites (one .SPR atlas frame; one synthesized 16x16 opaque-red BGR1555 square) drawn from `mania_tick` via `jo_sprite_draw3D` did NOT render. Only NBG2 backdrop visible.

## Invariants confirmed identical in both builds

- `jo_core_init(JO_COLOR_RGB(96,128,224))` first
- Four `slPriorityNbg0..3(0)` immediately after jo_core_init (hide-NBGs-first)
- `jo_audio_init()` (jo-engine/jo_engine/core.c:363-365 runs it automatically inside jo_core_init when `JO_COMPILE_WITH_AUDIO_SUPPORT` is defined; archived's explicit `setup_audio()` call is redundant, not a delta)
- `setup_title_bg()` loads TITLE.PAL → `jo_create_palette_from`, TITLE.DAT → `jo_vdp2_set_nbg2_8bits_image`, then `slScrPosNbg2(0,0)`
- `slPriorityNbg2(5)` (NBG2 below sprite layer)
- `jo_audio_play_cd_track(3, 3, true)` (title BGM)
- All `jo_sprite_add` calls happen before `jo_core_run`

## DTS-cited evidence on SGL sprite-display semantics

- `D:\Claude Saturn Skill Documentation\NOV96_DTS\EXAMPLES\SGL\BIPLANE\MAIN.C:212` — `slScrAutoDisp(NBG0ON | RBG0ON)` — sprites render WITHOUT `SPRON` flag.
- `jo-engine/Compiler/COMMON/SGL_302j/INC/SL_DEF.H:548` — `SPRON = (1<<6)` is a slScrAutoDisp flag.
- `ST-013-R3-061694.pdf` §VDP1 Command Tables / §Framebuffer Mode — VDP1 sprite output to the framebuffer is enabled by `slInitSystem` and continues until `slTVOff()` is called. The VDP2 compositor reads the VDP1 framebuffer in its sprite layer.
- `ST-058-R2-060194.pdf` §6.3 (Priority Function) — sprites have 8 priority slots PRISA..PRISD; per-sprite priority-slot index comes from VDP1 CMDCOLR bits (in COL_32K) or CMDPMOD bits (in palette modes). Default SGL state has sprites visible.
- `jo-engine/jo_engine/core.c:189-298` — `__jo_init_vdp2` (SGL path) calls `slInitSystem` + `slBitMapNbg1(COL_TYPE_32768, JO_VDP2_SIZE, nbg1_bitmap)` for an NBG1 bitmap-mode layer, then `slScrAutoDisp(screen_flags = NBG0ON|NBG1ON)`. **NBG1 is left in BITMAP MODE with the full 320x224 NBG1 bitmap occupying VRAM bank A0**.

**THIS IS THE LIKELY ROOT CAUSE.** See Delta 2 below.

## Candidate deltas, ranked by hardware-level relevance to "VDP1 sprite output suppressed at compositor level"

### Delta 2 (RANKED #1 — HIGHEST CONFIDENCE)

**The current build calls `jo_vdp2_set_nbg1_8bits_image` with an 8x8 dummy image and 16-entry palette, IMMEDIATELY before calling `jo_vdp2_set_nbg2_8bits_image` on TITLE.DAT (224×512).**

`src/main.c:159-174`:
```
jo_create_palette_from(&s_nbg1_dummy_pal, 16-entry-zero-palette, 16);
jo_vdp2_set_nbg1_8bits_image(<8x8 dummy>, dummy_pal.id, false);
slPriorityNbg1(0);
```
Then immediately `setup_title_bg()` → `jo_vdp2_set_nbg2_8bits_image(TITLE.DAT 224×512, ...)`.

**The archived build never calls `jo_vdp2_set_nbg1_8bits_image` with a dummy.** It either (a) skips NBG1 entirely (Phase 1.x has no GHZ FG), or (b) calls it with the REAL 8 × (1406×8) cell bank in `setup_ghz_foreground`. Either way, no "dummy" call.

**Why this is the strongest candidate:** `jo_vdp2_set_nbg1_8bits_image` (jo-engine/jo_engine/vdp2.c:527-543) does:
1. `__jo_switch_to_8bits_mode()` — switches NBG1 from jo's default `slBitMapNbg1(COL_TYPE_32768, JO_VDP2_SIZE, nbg1_bitmap)` (line 114) to **cell mode**. This frees `nbg1_bitmap` (the 320×224 bitmap that occupied 144 KB of VRAM bank A0).
2. `slPlaneNbg1(PL_SIZE_1x1)`, `slCharNbg1(COL_TYPE_256, CHAR_SIZE_1x1)`.
3. Allocates 8×8×1 = 64 bytes of cell data in VRAM bank A0 (whatever `__jo_a0` happens to point at after the bitmap free).
4. `slMapNbg1(nbg1_map, nbg1_map, nbg1_map, nbg1_map)` — points NBG1 page-name registers at jo's allocated map buffer.
5. `slPageNbg1(nbg1_cell, 0, PNB_1WORD | CN_12BIT)`.
6. `__jo_create_map(...)` — writes pattern-name entries into nbg1_map.
7. `JO_ADD_FLAG(screen_flags, NBG1ON); slScrAutoDisp(screen_flags)`.

**Then `jo_vdp2_set_nbg2_8bits_image` runs:**
- `__jo_set_nbg2_8bits_image` — switches NBG2 from default-state to cell mode, allocates 224×512=114,688 bytes of cell data in VRAM bank B0 (jo's NBG2 cell bank).
- Programs `slPlaneNbg2`, `slCharNbg2`, `slPageNbg2` for NBG2.
- `__jo_create_map(img, nbg2_map, palette_id, JO_VDP2_CELL_TO_MAP_OFFSET(nbg2_cell))`.

**Hypothesis:** The DUMMY NBG1 init occupies VRAM cycle-pattern slots and VRAM banks A0 differently than jo's default bitmap-mode NBG1 setup. The followup NBG2 init then runs from this PARTIALLY-RECONFIGURED state. The VDP2 VRAM cycle-pattern registers (`CYCA0L`..`CYCB1U` at 0x25F80010-0x25F8001E) may end up in a configuration where:
- NBG1 (priority 0, hidden) consumes most of bank A0 cycle slots for cell fetch.
- NBG2 (priority 5, the visible backdrop) consumes most of bank B0 cycle slots.
- **VDP1 sprite framebuffer-read cycles get starved.** VDP1 framebuffer is in VDP1's OWN VRAM, BUT — the VDP2 SPCTL (0x25F800E0) reads the VDP1 framebuffer at composite-time. The READ from VDP1 by VDP2 happens at a fixed cycle each pixel; what differs is whether VDP2's *layer*-priority blender SELECTS the VDP1 read or the NBG cell-mode read for output.

Actually re-reading the VDP2 cycle-pattern: cycle slots are for VDP2's INTERNAL fetches from VDP2 VRAM (cells, pattern names, color RAM). They do NOT affect VDP1 framebuffer access — VDP1's framebuffer is read by the VDP2 composite stage at a fixed slot per pixel.

**Refined hypothesis:** The dummy NBG1 init does NOT directly affect VDP1 sprite read. BUT — `jo_vdp2_set_nbg1_8bits_image` calls `slCharNbg1(COL_TYPE_256, CHAR_SIZE_1x1)` (vdp2.c:533). `COL_TYPE_256` configures NBG1 in 8-bpp CRAM-paletted mode. The SAME call sequence for NBG2 happens inside `__jo_set_nbg2_8bits_image`. Both NBG1 and NBG2 get configured in `COL_TYPE_256`.

The **VDP2 SPCTL (Sprite Control) register at 0x25F800E0** is set to `0x23` by jo's non-SGL init path (jo-engine/jo_engine/core.c:276) but is left to SGL's `slInitSystem` default in the SGL path. SPCTL contains:
- bits 0-3: SPTYPE (sprite type 0-7 — controls how VDP1 framebuffer pixels are interpreted)
- bit 4: SPCLMD (sprite color mode — 0=palette, 1=RGB)
- bit 5: SPWINEN (sprite window enable)
- bits 8-9: SPCCCS (sprite color calculation condition)

If SGL's `slInitSystem` default SPCTL conflicts with the actual VDP1 framebuffer format that `jo_sprite_add` writes (which is RGB COL_32K direct-color BGR1555 with bit 15 = opaque), AND the subsequent `slCharNbg2(COL_TYPE_256, CHAR_SIZE_1x1)` call somehow updates a shared SPCTL field through SGL internal accounting, then VDP2 may stop interpreting VDP1 framebuffer reads as visible sprites.

**The DTS-citation path forward:** verify what `slCharNbg2(COL_TYPE_256, CHAR_SIZE_1x1)` does to VDP2 registers. Per `ST-238-R1-051795.pdf` §slCharNbg* — this should update `CHCTLB` (Character Control B) for NBG2 only, NOT SPCTL. Sprite control is independent.

**Conclusion on Delta 2:** strong code-difference signal, but the hardware path from "dummy NBG1 init" to "VDP1 sprite invisible" requires a register interaction I can't pin down from documentation alone. **Yet it remains the strongest candidate because the dummy init didn't exist before Phase 1.3** and the bug appeared in Phase 1.3+.

### Delta 1 (RANKED #2)

Phase 1.3 added `slPrioritySpr0..7(6)` (8 register writes at main.c:200-207). Archived does not write any sprite priority.

- DTS96 BIPLANE / CHROME / CDDA_SGL samples — none write sprite priority and sprites render fine. So `slPrioritySpr*` is NOT mandatory.
- Setting all 8 slots = 6 (above NBG2's 5) is STRICTLY SAFER than the SGL default (which appears to be at least 5+ since BIPLANE works).
- The 8 calls don't suppress sprite output; they should make sprites MORE visible.

**Unlikely root cause.** But worth a probe-only test: remove these 8 calls, see if probe renders.

### Delta 3 (RANKED #3)

Phase 1.3 added `fg_vblank` callback that writes to absolute CRAM offsets [0..255] when bank-0 of RSDK palette mirror is dirty.

`TitleBG_StageLoad` (src/mania/Objects/Title/TitleBG.c:135) sets `rsdk_set_palette_entry(0, 55, 0x202030u)` → marks bank 0, entry 55 dirty. fg_vblank writes ONE short to absolute CRAM[55] on the next vblank. CRAM[55] is in jo's "reserved" first-palette region (vdp2_malloc.c:60 sets `__jo_cram = JO_VDP2_CRAM + 256 + 1`).

Writing one CRAM entry should not suppress VDP1 sprite output. **Unlikely root cause** but worth verifying by stubbing fg_vblank to no-op.

### Delta 4 (RANKED #4)

Current builds calls ~80 `jo_sprite_add`'s (inside `setup_title_assets()` inside `mania_engine_init()`) BEFORE `setup_title_bg()`. Archived calls all `jo_sprite_add`'s AFTER `setup_title_bg()`. Different call ordering of VDP1-VRAM writes versus VDP2-init.

- VDP1 VRAM (0x25C00000) and VDP2 VRAM (0x25E00000) are separate physical memories — no overlap.
- `jo_sprite_add` writes VDP1 character-pattern data and sprite-descriptor entries to `__jo_sprite_def` table (which is in WORK RAM, not VDP1 VRAM).
- The Phase 1.4 probe sprite was added in `jo_main` AFTER all this, and STILL didn't render. So ordering of `jo_sprite_add` calls isn't the issue.

**Very unlikely.**

## Implementation plan for the bisect — ONE delta per turn

The strongest empirical+code signal points at Delta 2 (the dummy NBG1 init). The Phase 1.3 rationale comment at src/main.c:137-158 cites a "MANDATORY NBG1 cycle-pattern init" claim but the actual citation chain is weak — the archived build's NBG1 cell-mode init is for the REAL GHZ FG cell bank, not a dummy. The Phase 1.3 build is title-only and ARGUABLY DOES NOT NEED ANY NBG1 INIT — the archived build's title state hides NBG1 with `slPriorityNbg1(0)` anyway.

**Bisect step (this turn):** REMOVE the dummy NBG1 init at main.c:159-174. The result should be:
- jo's default `__jo_init_vdp2` leaves NBG1 in COL_TYPE_32768 bitmap mode (vdp2.c:113-114). NBG1 priority is already 0 from main.c:125 (hidden). NBG1 bitmap is non-displayed, occupying VRAM but invisible.
- `setup_title_bg` then runs `jo_vdp2_set_nbg2_8bits_image` on a clean state.
- VDP1 sprites should render again (matching archived behavior — archived's title state never touched NBG1 in cell-mode either).

**Verification:** add the Phase 1.4 probe sprite (16x16 opaque-red BGR1555 square) drawn from `mania_tick`. Capture qa_gate.png. If the probe renders, Delta 2 IS the root cause and the fix is permanent removal of the dummy NBG1 init.

If probe does NOT render after Delta-2 removal, try Delta 1 (remove the 8 `slPrioritySpr*` writes) next.

If still no render after Delta 1+2, try Delta 3 (stub fg_vblank to no-op).

## Acceptance criteria

1. Probe sprite (16x16 opaque-red BGR1555) renders at fixed screen coords.
2. Once probe renders, remove probe, capture title.
3. qa_gate.png shows title-screen VDP1 overlays (orb + wings + ribbon + SONIC + MANIA + ring + PRESS START + TitleSonic w/ finger wave).
4. `tools/qa_visual_diff.py` Gate V1 SSIM ≥ 0.45 vs `tools/refs/mania_pc/title_full_archiveorg.jpg`.
5. `pwsh tools/verify_done.ps1` exit 0.
