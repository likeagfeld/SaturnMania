# CP5b.6 -- Title Green Hill island Mode-7 ROTATION via VDP2 RBG0 + coefficient table

Status: PLANNED (2026-06-22). Gated behind `P6_TITLE_RBG0_ON` so the stable CP5b.5
title (clean FG + static VDP2 NBG1 island) remains the DEFAULT until RBG0 is proven.
This implements the "Part-4b live Mode-7" path the decomp comment (TitleBG.c:116-118)
always pointed at.

## Decomp contract (the rotation + perspective to mirror)

- `TitleBG_StaticUpdate` (TitleBG.c:34-46): `TitleBG->angle += 1; angle &= 0x3FF`. Full
  revolution = 1024 frames (~17 s at 60 Hz), Sin1024 space.
- `TitleBG_Scanline_Island` (TitleBG.c:159-178): the island occupies screen rows
  **168..239** (72 lines); per line `i=16..88`: perspective scale `id = 0xA00000/(8*i)`
  (1/depth, grows with row), rotated by `sine/cosine = Sin1024(-angle)>>2`. deform.x/y =
  per-line affine; this is exactly a VDP2 RBG0 rotation matrix (A..F from sine/cosine) +
  a per-line coefficient table (the 1/depth scale ramp). Above row 168 = sky/cloud (NBG).

## SGL RBG0 call sequence (Agent A contract, VERIFIED against SGL302 DEMO_D MAIN.C:70-100,165-194)

INIT (once): `slColRAMMode` -> `slRparaInitSet(RA_table)` -> `slMakeKtable(KTBL)` ->
`slCharRbg0(COL_TYPE_256, CHAR_SIZE_1x1)` -> `slPageRbg0(cel, 0, PNB_1WORD|CN_12BIT)` ->
`slPlaneRA(PL_SIZE_*)` -> `sl1MapRA(map)` -> `slOverRA(2)` (OUTSIDE TRANSPARENT, finite
island) -> `slKtableRA(KTBL, K_FIX|K_DOT|K_2WORD|K_ON)` -> [upload cel/map/pal] ->
`slRparaMode(RA-only)` -> `slScrAutoDisp(RBG0ON|NBG1ON|SPRON)`. All symbols verified in
SL_DEF.H:982-1008,530-571.

PER FRAME: `angle += 1; slCurRpara(RA); slUnitMatrix(CURRENT); slTranslate(viewX,viewY,
toFIXED(H)); slRotX(DEGtoANG(-90)); slRotZ(angle_in_SGL_units); slScrMatSet();` -- the
coefficient table is built ONCE (K_FIX); only the matrix changes. Re-apply the RBG0
enable/priority/CRAM-offset each frame (see prior-art symptom 3).

## Prior-art failure modes + the mitigations (Agent B post-mortem of src/main.c:229-1100, Phase 1.26-1.31)

| Symptom (MEASURED) | Root cause | Mitigation in CP5b.6 |
|---|---|---|
| 224px tile-repeat seams | over-mode REPEAT + non-plane-sized bitmap (jo baked wrap) | `slOverRA(2)` SINGLE/transparent + a real plane-sized island map; engine tile data not a hand-baked bitmap |
| Neon-green flood ~95% | (a) R0CAOS=0 but palette at CRAM[257+]; (b) back-screen word = neon 0x83E0 shared w/ jo K-table VRAM | (a) island palette in CRAM bank 1 + `slColRAMOffsetRbg0(1)` re-applied per frame; (b) back-screen stays sky `0xF180` (p6 already owns it) + KTBL placed in B1, NOT near the back-screen word |
| Register clobber (BGON/PRIR/matrix not reaching VDP2) | SGL slSynch re-applies cached scroll state every vblank | re-apply `slScrAutoDisp(...RBG0ON)` + `slPriorityRbg0` + `slColRAMOffsetRbg0` + `slScrMatSet` EVERY frame (the p6 title already re-applies its NBG1 state per frame -- extend it) |
| W0 clip collapses plane to a sliver | W0 is the wrong tool for the horizon | NO W0 clip; the row-168..239 perspective comes from the coefficient table (per-line scale) |
| Used jo `jo_enable_background_3d_plane` (opaque CRAM/K-table routing) | jo wrapper | use SGL directly (slCharRbg0...) + place every table myself; the p6 present already writes VDP2/CRAM raw |

## VRAM budget (Agent B, MEASURED from p6_vdp2.c:34-37 + SaturnSheet.cpp P6_CART)

Current title: A0+A1 = NBG1 cells (256 KB), B0 first 16 KB = NBG1 map, CRAM bank 0 =
NBG1 palette. The SaturnSheet band store is in CART (P6_CART), so **bank B1
(0x25E60000-0x25E7FFFE, 128 KB) is entirely FREE** (only the 2-byte back-color at
0x25E7FFFE). Place ALL RBG0 tables in B1, flag ONLY B1 in RAMCTL RDBS:
- RBG0 char (island cells, ~256 referenced tiles, <=64 KB) at 0x25E60000
- RBG0 pattern-name map (<=16 KB) at 0x25E78000
- KTBL coefficient table (per-line 2-word, ~240*4 ~= 1 KB) at 0x25E7C000
- RA rotation-parameter table (0x60 B) at 0x25E7BF00
A0/A1 (NBG1 cells) + B0 (NBG1 map) stay pure NBG1 banks. CRAM: NBG1 in bank 0, RBG0
island palette in CRAM bank 1 (slots 256-511), R0CAOS=1. Coefficients in VRAM (not CRAM)
-> CRAM stays mode 0, no palette loss.

## Incremental steps (each its own RED gate; nothing ships until visually verified no-flood)

- **STEP 1 (de-risk): STATIC RBG0 island.** Convert the island to RBG0 with a fixed
  identity-ish matrix (no per-frame spin). Proves the hard part: RBG0 in B1 + CRAM bank 1
  + sky back-screen + over-mode-transparent + the register-clobber per-frame re-apply, with
  NO neon flood and the FG intact. Gate `qa_title_rbg0.py`: (savestate) BGON.R0ON set,
  PRIR RBG0 priority below sprites, R0CAOS=1, back-screen==0xF180; (pixel) island-green
  present in rows 168..239, NEON-GREEN pixel-mass ~= 0 across ALL settled frames, FG
  sonic_head>=5000 (reuse qa_title_fg_stable). RED on current build (no RBG0).
- **STEP 2: ROTATION.** Drive the per-frame matrix from an angle that increments 1/frame
  (mirrors TitleBG->angle). Gate: the island region's content rotates (angular change
  measured across an N-frame burst), flood still ~=0, FG intact.

## Files

- `tools/_portspike/_p6/p6_vdp2.c` -- NEW `p6_vdp2_present_title_island_rbg0()` (init) +
  `p6_vdp2_title_island_spin(angle)` (per-frame), under `#if defined(P6_TITLE_RBG0_ON)`.
- `tools/_portspike/_p6/p6_io_main.cpp` -- call them in the title load + per-frame, gated;
  RBG0 angle witness `p6_w_title_island_angle`.
- `tools/_portspike/_p6/build_shipping.sh` + `build_p6scene_objs.sh` -- thread
  `P6_TITLE_RBG0_ON` (opt-in; default OFF so CP5b.5 stays the shipped title).
- `tools/_portspike/qa_title_rbg0.py` -- NEW RED-first gate (no-flood + RBG0-on + FG-intact
  + rotation). Reuses qa_title_fg_stable scoring + the savestate harness.

## Acceptance (measurable, not impressionistic)

STEP 1 GREEN: across a 40-frame settled burst -- neon-green pixel-mass (g>=200,r<64,b<64)
<= 200 in EVERY frame; island-green present in rows 168..239; sonic_head>=5000 every
frame; savestate BGON bit4 (R0ON)=1, PRIR RBG0<sprite, CRAOFB R0CAOS=1, back-screen
0x25E7FFFE==0xF180. STEP 2 GREEN: same + the island visibly rotates (I open the frames).
