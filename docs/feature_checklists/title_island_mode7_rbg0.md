# Feature checklist — Title island Mode-7 rotation via VDP2 RBG0 + per-line coefficient table

Task #276 (CP5b.6). Front-end-gated (`P6_FRONTEND_TITLE`). The GHZ flavor must
stay byte-identical.

## 1. Decomp source being ported (authoritative math)

`tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c`:
- `TitleBG_StaticUpdate` (:34-46): `++TitleBG->angle; angle &= 0x3FF;` (10-bit
  full rotation, +1/frame) + `TitleBG->timer += 0x8000`.
- `TitleBG_Scanline_Island` (:159-178): the Mode-7 affine. Per the engine's OWN
  texel-walk consumer `RSDK::DrawLayer` (Drawing.cpp:3905-3921, INK_NONE):
    - `lx = position.x; ly = position.y` (16.16) = texture coord at screen x=0.
    - `dx = deform.x; dy = deform.y` (16.16) = per-screen-pixel texture step.
    - `texel = pixels[((ly>>16)&height)<<lineSize + ((lx>>16)&width)]`; mask =
      texture WRAP. `lx+=dx; ly+=dy` across the line.
  So `deform` = the rotation/scale *direction & magnitude* (matrix), `position`
  = the per-line texture START. This is exactly the VDP2 rotation-scroll model.
- Island = engine TileLayer 3 (`TitleBG_SetupFX`:102-103, drawGroup=1).
- Decomp clips the island to screen y[168,240] (`SetClipBounds(0,0,168,...)`).

The per-line stream (verbatim, i in 16..88 maps to screen line 168+(i-16)):
```
sine   = Sin1024(-angle) >> 2
cosine = Cos1024(-angle) >> 2
id   = 0xA00000 / (8*i)
sin  = sine   * id
cos  = cosine * id
deform.y   = sin >> 7
deform.x   = -cos >> 7
position.y = cos - center.x*deform.y - 0xA000*cosine + 0x2000000
position.x = sin - center.x*deform.x - 0xA000*sine   + 0x2000000
```
`center.x` = ScreenInfo->center.x = SCREEN_XSIZE/2 = 424/2... NO: the engine TV
is 320x240 -> center.x = 160 (confirm from ScreenInfo at runtime; the gate uses
the engine value, see below).

## 2. Saturn mapping (decomp -> VDP2 RBG0)

RBG0 rotation-scroll with Rotation Parameter A + a per-line coefficient table:
- Coefficient Data Mode 0 (kx=ky=coeff[line]); coeff read PER LINE from VRAM
  (`K_LINE`, not K_DOT). The per-line coeff scales the matrix step by `id[line]`.
- The matrix A/B/C/D carries the *rotation direction* (-cos, sin); the coeff
  carries the per-line *perspective scale* (id). Product reproduces deform.
- Per-line `Xst/Yst` (RPRCTL RAXSTRE/RAYSTRE=1 each line) = position.x/position.y.
- Plane sized + over-mode SINGLE so the single island image maps ONCE (no tile
  repeat) -- see Audit-seam below.
- RBG0 cells/map/CRAM written DIRECTLY (same as the proven NBG1 present), NOT via
  jo's `jo_vdp2_set_rbg0_plane_a_8bits_image` wrapper (that wrapper is the source
  of BOTH prior failures -- see Audit-CRAM + Audit-seam).

## 3. Authoritative docs read (catalogue-driven)

- ST-058-R2 VDP2 manual (`sega_saturn_docs/VDP2_Manual.txt`):
  - RPT layout Fig 6.3 (:6699-6779): size 0x60. KAst integer @ +0x54, frac @ +0x56;
    dKAst @ +0x58/+0x5A; KAx @ +0x5C/+0x5E. Matrix A..F @ +0x2C..+0x53.
  - Coefficient table §6.4 (:7033-7211): modes 0-3; 1-word vs 2-word; per-line vs
    per-dot; MSB=transparent. Coeff table lead addr = KAst_int * (2H or 4H).
  - RPRCTL read-enable (1800B2H, :6807-6843): RAXSTRE/RAYSTRE/RAKASTRE per line.
  - RPTA (1800BC/BEH, :6845-6879): RPT base = RPTA<<1.
  - KTCTL (1800B8?), KTAOF (1800BAH) coeff-table control/offset (:7283-7397).
  - RAOVR screen-over (18003AH, :3861-3895): mode 3 = inside-plane only.
- SGL header `jo-engine/Compiler/COMMON/SGL_302j/INC/SL_DEF.H`: ROTSCROLL struct
  (:480-512), K-table API slMakeKtable/slKtableRA/slRparaInitSet/slCurRpara/
  slRparaMode/slCharRbg0/slPlaneRA/sl1MapRA/slOverRA (:1004-1009,982,992,1000,994);
  K_LINE/K_DOT/K_2WORD/K_ON/K_MODE0/K_FIX consts (:559-571); RBG0ON (:546).
- DEMOCOEF `NOV96_DTS/EXAMPLES/DEVCON96/DEMOCOEF/MAIN.C`: the per-line/per-pixel
  coeff write template (CurvedCoeff:229-290 = per-line kx scale; the RPT KAst/
  dKAst/dKAx setup :279-289; KTCTL/KTAOF register writes :240-252).
- jo canonical RBG0 sequence `jo-engine/jo_engine/vdp2.c:294-359` (sl1MapRA ->
  slOverRA -> slPageRbg0 -> slRparaInitSet -> slMakeKtable -> slKtableRA(K_FIX|
  K_DOT|K_2WORD|K_ON|K_LINECOL) -> slCharRbg0 -> slRparaMode(K_CHANGE) -> slPlaneRA
  -> RBG0ON). NOTE: jo uses K_DOT; we use K_LINE (per-line coeff, 1 entry/line).

## 4. Prior-failure avoidance (do NOT repeat)

- **Seam (Phase 1.31 Fix #1, main.c:311-319):** a 224x512 image in PL_SIZE_1x1
  (512x512) with `slOverRA(REPEAT=0)` baked a horizontal tile-wrap into jo's
  64x64 page map -> repeating island field. FIX: `slOverRA(3)` = SINGLE =
  "display inside-plane only, outside = back-color" (ST-058 RAOVR=3). The single
  island maps once; outside the plane shows the sky back-color, not a repeat.
  Diagnose with a savestate VRAM peek of the RBG0 map (not eyeball-nudge).
- **CRAM neon-green flood (Phase 1.26b, main.c:240-273):** the jo RBG0 wrapper
  (`__jo_create_map` + `jo_img_to_vdp2_cells`) rendered pure green; the hypothesis
  was a RBG0 pattern-name `palette_id` field-width vs CRAM-bank mismatch. FIX:
  write RBG0 cells/map/CRAM directly (bypass the wrapper), and put RBG0's palette
  in a CRAM bank DISTINCT from the FG sprites + the sky. CRAM mode is CRM16_2048
  (one 2048-entry 16-bit bank for the whole title; NBG1 island used CRAM[0..255]).
  RBG0 island palette -> CRAM[256..511] (bank 1 in 256-color terms), pattern-name
  palette bits select it. Verify no collision via a savestate CRAM peek.

## 5. RED gate (added FIRST, fires RED on current flat build)

`tools/_portspike/qa_title_island_rot.py`:
- OFFLINE-computes the per-line coefficient + RPT stream for angle=0 AND angle=64,
  mirroring `TitleBG_Scanline_Island` + reading the BAKED `Sin1024/Cos1024` from
  `platform/Saturn/TrigTables_Saturn.inc` (the on-target newlib values -- recomputing
  sinf() in CPython risks a rounding mismatch).
- Captures TWO savestates at TWO times (two different TitleBG->angle values).
- Peeks the RBG0 coefficient table + RPT from VDP2 VRAM (addr via RPTA reg ->
  RPT -> KAst -> coeff base) and asserts byte-match (within rounding) to the
  computed stream FOR BOTH angles. Proves rotation is LIVE + correct, not static,
  not a wrong angle.
- On the current flat build: RED (no RBG0 RPT/coeff table exists; RPTA reads 0 or
  the RBG0ON bit is clear). Witnesses: `_p6_w_title_island_armed`,
  `_p6_w_title_island_angle`, `_p6_w_title_island_kast`, `_p6_w_title_island_coeff0`.

## 6. PROACTIVE-DETECTION audits (§4.5.1, adapted to a VDP2-layer feature)

- **Audit 1 (Z-order/layering):** RBG0 owns ONLY the island band (decomp y[168,240]).
  Priority: RBG0 < VDP1 sprites (so Sonic/logo/ribbon composite IN FRONT). The sky
  back-color + NBG cloud band stay above the island (screen y<168). Table:
  | layer | content | screen y | VDP2 pri | vs VDP1(7) |
  |---|---|---|---|---|
  | back-color | sky blue | all | n/a (lowest) | behind |
  | RBG0 | rotating island | 168..240 (clip) | 2 | behind sprites |
  | NBG1 | (cloud band, if kept) | 0..168 | 1 | behind sprites |
  | VDP1 | Sonic/logo/ribbon | per-sprite | sprite | FRONT |
  RBG0 pri (2) > NBG1 (1) so the island draws over the cloud band where they meet.
- **Audit 2 (cadence):** angle advances +1/frame (StaticUpdate), wraps at 0x400 ->
  a full island rotation every 1024 frames (~17 s at 60 Hz). The Saturn re-builds
  the coeff/RPT stream EACH front-end frame from the live angle (same +1/frame).
  No frame-table needed (it's a continuous register/VRAM rewrite, not a sprite anim).
- **Audit 3 (pivot+flip):** N/A -- RBG0 is a single rotation plane, not flipped
  composite sprites. The FG sprites are untouched (still p6_vdp1 path).
- **Audit 4 (boot-delay budget):** ZERO new asset load. The island cells already
  upload during the load gap (p6_vdp2_upload_cells, the same A0/A1 cells the flat
  present used). RBG0 reuses those cells + a NEW map at a free VRAM bank + the RPT
  + coeff table in VRAM (built on-CPU each frame, scratch in cart cache-through,
  NOT new .bss). No GFS read added -> 0 s boot delta.

## 7. WRAM-H budget (#228 trap)

- Title `_end` currently ~0x060B59F0, ceiling P6_HW_ANIMPAK 0x060B6600 (~4.6 KB).
- The coeff table (240 lines x 2 words = ~1 KB) + RPT (0x60) live in VDP2 VRAM,
  NOT .bss. Any CPU-side scratch for building the stream uses a cart cache-through
  window (0x22xxxxxx). New code is ~one function in p6_vdp2.c + ~4 witnesses.
- After build: grep `_end` < 0x060B6600 BEFORE capture (else PC=0x06000956 trap).
  Run `python tools/_portspike/qa_p6_mapoverlap.py`.

## 8. VRAM bank plan (512 KB VDP2 VRAM, 4 banks A0/A1/B0/B1 @ 128 KB each)

Existing title NBG1 present: cells @ A0 (0x25E00000, 256 KB = A0+A1), map @ B0
(0x25E40000), back-color @ B1 last word. RBG0 needs: its OWN cells (reuse the A0
island cells -- same tileset), a coeff table (per-dot/line data MUST be in VRAM),
the RPT, and a map. Plan:
- RBG0 cells: REUSE A0 island cells (charno = tile*8, already uploaded). No new VRAM.
- RBG0 map: B1 region (0x25E60000), a single PL_SIZE plane sized to hold the island
  image once (no repeat with slOverRA(3)).
- RPT: B1 high (e.g. 0x25E7FF00, 0x60 bytes, RPTA-aligned).
- Coeff table: B1 (e.g. 0x25E70000, 240 lines * 4 B 2-word = ~1 KB; or 2 B 1-word).
- The flat NBG1 present is REPLACED by RBG0 for the island; the cloud band stays
  on NBG1 (or RBG0 covers full screen with the island masked to the lower band via
  the clip / the coeff transparency). Decision recorded in the implementation.

## 9. Files touched

- NEW `tools/_portspike/qa_title_island_rot.py` (the coeff-stream byte gate).
- `tools/_portspike/_p6/p6_vdp2.c`: new `p6_vdp2_title_island_rbg0_arm()` +
  `p6_vdp2_title_island_rbg0_frame(int angle)` (front-end-gated). Replaces/augments
  `p6_vdp2_present_title_backdrop` island half + `p6_vdp2_title_backdrop_scroll`.
- `tools/_portspike/_p6/p6_io_main.cpp`: call arm at load, frame(angle) each
  front-end tick; declare the witnesses; pass the live TitleBG->angle.
- `tools/_portspike/_p6/build_p6scene_objs.sh`: already threads P6_FRONTEND_TITLE
  to p6_vdp2.c -- verify the new fn compiles into game.map.

## 10. Acceptance (RED -> GREEN)

- `_end` < 0x060B6600 (no trap).
- New fn symbol present in game.map (TU compiled, not swallowed).
- qa_title_island_rot.py: RED on flat build -> GREEN after fix, with the peeked
  RBG0 coeff+RPT stream byte-matching the offline-computed decomp stream for BOTH
  angle captures (proves LIVE + CORRECT rotation).
- Dense PNG burst: island visibly rotates/shifts in perspective frame-to-frame,
  NO tile-repeat seam, correct colors (no CRAM collision), FG (Sonic/logo/ribbon/
  PRESS ANY BUTTON) intact.
