# Feature checklist: GFS directory-table ceiling (badnik/HUD/Water bind fix)

Regression from commit 9e692ee (GHZ 4bpp pink-flash root fix). That commit is
GOOD and must stay -- the pink fix is not touched here. It introduced two
downstream regressions by a purely accidental side effect: it added 9 new root
files to the ISO, overflowing the GFS directory table.

## Measured root cause (source-cited, not hypothesized)

- The shipping/chain build boots via jo HAL (`p6_jo_boot.c` -> `jo_core_init`
  -> `jo_fs_init`), so the ACTIVE GFS directory table is jo-engine's
  `__jo_fs_dirtbl` (`jo-engine/jo_engine/fs.c:91,114`), sized `JO_FS_MAX_FILES`
  (`jo/conf.h`). `p6_gfs.c`'s `P6_GFS_MAX_DIR` (22) is DEAD in this build --
  `p6_gfs_init()` is never called (`p6_io_main.cpp:10537`).
- `gfdr_setupDirNameTbl` (SBL `GFS_DIR.C:438-470`) reads AT MOST
  `NDIR = JO_FS_MAX_FILES` root directory records, in on-disc (ISO9660
  alphabetical) order, and stops at NDIR even if more entries exist on disc.
- Any file at ISO root position >= NDIR gets NO dirtbl slot ->
  `GFS_NameToId` (`GFS.C:340` -> `GFDR_NameToId`, `GFS_DIR.C:244-257`) returns
  -1 -> `rsdk_storage_load_to_lwram` / `Saturn_fOpen` fail -> the sheet never
  loads. A failed sheet load takes `sheetID 0xFF`; the `gfxSurface[0xFF]` deref
  corrupts the shared VDP1 sheet-bind table (memory rule
  `ghz-sprite-present-but-invisible-sheet-binding.md`), which is why the damage
  spreads to OTHER sprites (badniks vanish) and the HUD lives-face sheet
  renders garbage.

## Measurement (RED gate)

`tools/qa_iso_dirtbl_ceiling.py game.iso` parses the ISO9660 root directory and
asserts (root entries incl. `.`/`..`) <= `JO_FS_MAX_FILES`.

- BEFORE (pre-fix, `JO_FS_MAX_FILES=280`): **RED** -- 283 root entries > 280.
  Dropped (unresolvable): `UFO7TIL.BIN`, `WATER.FRD`, `WATER.SHT` (the 3
  alphabetically-last files). WATER.SHT/FRD are GHZ-gameplay Water object
  sheets; their failed bind is the badnik/HUD cascade.
- The 4bpp pink fix added 9 files (`AGHCBG.{CHR,MAP,PAL}`,
  `AGHFG.{CHR,BNK,CMP}`, `AGHFS.{CHR,MAP,PAL}`) near the front of the alphabet,
  pushing the root from ~274 to 283 entries -> crossed the 280 ceiling.

## Fix (minimal, data-driven)

- `jo-engine/jo_engine/jo/conf.h`: `JO_FS_MAX_FILES` 280 -> **320** (covers 283
  + 37 headroom). Cost: +40 entries * sizeof(GfsDirName)=24 B = +960 B .bss.
  `_end` 0x060c4640 + ~960 ~= 0x060c4a08, far under the chain GLOBALS ceiling
  0x060c8000 (14,784 B headroom measured) and the plain-GHZ ANIMPAK ceiling.
  Chose the active dirtbl (jo's), NOT the dead `P6_GFS_MAX_DIR`.
- `tools/_portspike/_p6/p6_vdp2.c`: add `extern int p6_ghz_fg_4bpp;` forward
  decl before `p6_vdp2_ghzcut_bg_upload` (line ~1498). The symbol is used at
  1505/1611 but only defined at ~2108 (used-before-declared) -- a latent
  compile error in 9e692ee that surfaced on a clean `p6_vdp2.o` rebuild. The
  forward decl is compatible with the later non-static definition.
- Stale `jo-engine/jo_engine/fs.o` + `core.o` removed before build (pool-size
  change -> stale-object gotcha `jo-pool-stale-core-o-gotcha.md`; the build
  script only rm's core.o, not fs.o, and fs.o holds the `__jo_fs_dirname[]`
  array whose size just changed).

## Gates

- G1 (RED->GREEN): `tools/qa_iso_dirtbl_ceiling.py game.iso` 283<=320 GREEN.
- G2 (bind, live): `tools/qa_p6_water.py --live` -> `p6_w_water_shtslot >= 0`.
- G3 (bind, live): `tools/qa_ghz_badnik_vis.py` -> `p6_w_dropbysheet` all 0,
  `p6_w_ghzobj_surf_handle >= 0`.
- G4 (whole-level regression): `tools/qa_whole_game.py` GREEN.
- G5 (intro scenes): boot-through chain, no new unbound surfaces at AIZ/Title/
  GHZCutscene (GFS is global -- all scenes share the one dirtbl).
- G6: map `_end` < 0x060c8000.
- USER-CONFIRMED (visual, do NOT self-fake): badniks visible, HUD lives-face
  correct, pink still gone under manual motion.

## STATUS of the black-flash follow-on: REVERTED (needs savestate + user A/B)

The mirror-cycle fix below was BUILT and live-tested. Result: it restored the FG
but turned the PARKED-landing sky solid black (a regression vs 9e692ee, whose
parked sky was GREEN on both qa_ghz_pink and qa_ghz_blackflash). Live netmem
proved my intended mirror values landed (mir_bgon=0x0043, mir_cyc=the 2-plane
pattern, mir_valid=1 in steady state via a poke-and-readback test) yet N0 still
did not display -- i.e. the 2-plane A1 cycle I published does not fetch a visible
N0 on this hardware, which can only be adjudicated by reading the LIVE VDP2
VRAM/CRAM/registers from a Mednafen savestate (VDP2 regs are NOT in
READ_CORE_RAM -- confirmed READ_CORE_RAM 0x25F80020 -> -1). Per the prompt's
"if a gate stays RED, back out, keep chain buildable + pink-GREEN" rule, the
p6_vdp2.c mirror change was REVERTED to 9e692ee; only the build-fix forward decl
(extern int p6_ghz_fg_4bpp) is kept. The chain builds clean, pink stays GREEN,
and the sky renders exactly as 9e692ee. The under-motion black flash + the HUD-
face scramble remain OPEN and need the savestate CRAM/VDP2 dump described below.

## OPEN: HUD lives-face VDP1 sprite scrambled (user report)

DISPLAY.SHT stages fine (p6_w_dispsht_slot=18) and badnik-vis is GREEN, so this
is NOT a sheet-bind/GFS failure. The scramble (garbage pixels, not missing)
points to a CRAM palette overlap. The 4bpp pink fix rebuilds CRAM banks:
sky P6_GHCBG_PAL_BASE=4 -> CRAM[64..127] (VDP2, clear of sprites); the 4bpp FG
uses PAL_BASE=32 -> CRAM[512..991]. p6_vdp2.c:1465-1467 explicitly warns that
PAL_BASE=32 "stomped the Gunner HBHPAL block (Heavies claim CRAM[512..1791])".
VDP1 HUD sprites read a colno palette that may fall in that stomped range. This
is a CRAM question -> read the LIVE CRAM from a Mednafen savestate at GHZ
(tools/mcs_extract.py region dump of 0x25F00000) and compare the HUD-face
colno block against a known-good frame. Do NOT blind-fix.

## Follow-on (REVERTED design, kept for the record): flashing transparent-BLACK

After the badnik/HUD fix the user reported a NEW flashing transparent-black in
the background, "similar to how pink was flashing." Measured: qa_ghz_pink.py
0/45 GREEN (magenta gone) AND qa_ghz_blackflash.py 0/45 on a PARKED camera --
the drop is motion-only, exactly like the original pink (0% parked, 13-15%
moving), and not netmem-observable (VDP2 VRAM/SGL not in READ_CORE_RAM).

Root cause (the code's OWN documented mechanism, p6_vdp2.c:1538-1551,1642-1646,
NOT a guess): the FG present re-arms NBG1ON|SPRON every frame; SGL's auto-
allocator emits a no-N0 cycle for that; p6_vdp2_mirror_apply (per-vblank +
frame-top) overwrites it with the PUBLISHED cycle. But
p6_vdp2_ghzcut_bg_frame published the 4-plane cycle
(0x55FEEEEE,0xFFFEEEEE,0x123FEEEE,0x0467EEEE) + BGON 0x004F for ALL branches --
including the 2-plane GHZ-gameplay 4bpp path whose sky char is in A1 and whose
armed cycle is (0x5FEEEEEE,0x4FEEEEEE,0x1FEEEEEE,0x0FEEEEEE). So the per-vblank
replay drove N0 from the WRONG bank (4-plane B1 fetch) -> N0 intermittently
dropped -> top scanlines went BLACK under motion (the pink mechanism, one layer
up in the mirror-replay). The back-screen 0xF180 can't reach the active area
behind a dropped scroll layer (gotcha #6), so the drop shows black not sky-blue.

Fix (p6_vdp2.c): capture the branch's ACTUAL cycle constants (mc0..mc3) and
publish THOSE to the mirror, plus the BGON READ-BACK (0x25F80020, the real
value slScrAutoDisp wrote for the branch -- measurement, not a guessed const).
The 4-plane GHZCutscene path is byte-identical (same constants). CYC hi16=T0-T3
/ lo16=T4-T7 per ST-058-R2 p.85.

Gate: qa_ghz_blackflash.py (parked baseline GREEN); the motion black-flash is
USER-CONFIRMED (not self-fakeable, motion + non-netmem-observable).
