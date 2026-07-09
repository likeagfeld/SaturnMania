# FEATURE CHECKLIST -- TitleCard zone-name glyphs + strip saturation (GL1 finish)

Owner iteration: 2026-07-06. Extends the DONE colored-card (GL1 primary gate
GREEN, docs/feature_checklists/frontend_full_chain_parity.md:162-241). Closes the
TWO remaining GL1 gaps measured on `_shots/game-260706-214050.png`:

1. NO ZONE-NAME TEXT -- "GREEN HILL" / "ZONE" / "ACT 1" glyphs are not drawn.
2. WASHED COLORS -- the card composites through the GHZ-arrival VDP2 colour
   offset (FXRuby white wash), so strips render pastel not source-saturated.

## Decomp source (authoritative)

- `tools/_decomp_raw/SonicMania_Objects_Global_TitleCard.c`
  - Glyph LAYOUT: `TitleCard_Draw_ShowTitleCard` L790-835:
    - ZONE word (L795-799): 4 sprites, anim 2 (zoneLetterAnimator), frameID=i
      (0..3 = Z,O,N,E), drawPos.x = `zoneXPos` (const), drawPos.y = `TO_FIXED(186)
      + zoneCharPos[i]`. The 4 frames' pivotX (-117,-90,-64,-37) spread them.
    - Name word (L810-813): `DrawText(nameLetterAnimator[anim1], word2XPos-20,
      154, zoneName, 0..0, ALIGN_CENTER, spacing=1, charPos)`.
    - Act number (L818-832): actNumbersAnimator[anim3] frameID=actID at actNumPos,
      plus a decoration box (anim0 frame0) behind it (FX_SCALE grow-in).
  - `TitleCard_SetupTitleWords` L251-292: charPos[c].y bounce init, zoneCharPos[i]
    slide init, zoneXPos/word2XPos anchors. (Already ported in p6_wave1_reg.c
    tc_setup_title_words.)
- `rsdkv5-src/RSDKv5/RSDK/Graphics/Drawing.cpp`
  - `DrawString` L4312-4391 ALIGN_CENTER (no-charOffsets + charOffsets arms):
    letters laid RIGHT->LEFT from anchor x; per char `draw at x - frame->width
    (+ charOffset.x)`, y = `y + pivotY (+ charOffset.y)`, then `x = (x -
    frame->width) - spacing`.
  - `DrawSprite`/`DrawSpriteFlipped` L2785: screen pos = `pos + pivot` (pivotX,
    pivotY). This is how every glyph's top-left is computed.
- ST-058-R2 Ch.13 (VDP2 Color Offset CLOFEN/CLOFSL/COAR/COAG/COAB) -- the
  saturation fix mechanism. ST-013-R3 sec 6.4 + ST-058-R2 sec 10.1 (VDP1 8bpp
  sprite CRAM addr = colno + pixel; Type-3 full-11-bit DC) -- the glyph palette
  block routing.

## Assets (MEASURED -- no new sheet/pack build needed)

- Glyph frames live on `Global/Display.gif` (NOT a TitleCard.gif; TitleCard.bin
  header @off 0x0A references "Global/Display.gif"). Parsed frame rects
  (tools parse of extracted/Data/Sprites/Global/TitleCard.bin):
  - anim1 "Name Letters" 27 frames: A@(1,46,16,32) piv(-8,-16) ... Z@(216,79)
    ... space@(1,46,12,0). Letter frame index = ASCII - 'A' (space = 26).
  - anim2 "Zone Letters" 4 frames: Z@(1,112,26,18) piv(-117,-10), O@(28,112,26,
    18) piv(-90,-10), N@(55,112,26,18) piv(-64,-10), E@(82,112,28,18) piv(-37,
    -10).
  - anim3 "Act Numbers" 3 frames: "1"@(173,112,16,48) piv(-8,-24),
    "2"@(190,112,32,48) piv(-16,-24), "3"@(223,112,32,48) piv(-16,-24).
  - anim0 "Decorations" f0@(191,161,64,64) piv(-32,-32) (act-num backing box).
- `cd/DISPLAY.SHT` (11,416 B) already built + ALREADY STAGED at the GHZ landing
  seam (p6_io_main.cpp:7301 sheet list "DISPLAY.SHT"). Its slot re-resolved via
  SaturnSheet_FindSlot(hash("Global/Display.gif")).
- Glyph palette (MEASURED via Display.gif GCT): indices used {0=transparent
  magenta,1=black,16/18=dark-red,32..41=blue-gray->near-white gradient,255=white}.
  Max index 255 -> needs a full 256-entry CRAM block.

## Saturation gap -- ROOT CAUSE (data-driven)

`p6_io_main.cpp:7882` applies `p6_vdp2_fade_apply(fade_w,fade_b)` EVERY frame
from `s_ovl.fade_fn` (the live FXRuby fade reader, p6_ovl_ghz.c:788). While a
residual FXRuby white fade (fadeWhite>0) is live at the landing, `slColOffsetA(+aw
...) + slColOffsetAUse(...SPRON...)` (p6_vdp2.c:2307-2311) adds a white bias to
the SPR layer -> the card's VDP1 strips render pastel. VISUAL PROOF
(_shots/214050): the BLACK decor bar (0x000000) renders GRAY (~+64 white offset);
strips pale.

FIX (doc-cited, decomp-faithful, checklist option 1): while the card is up, force
the VDP2 colour offset OFF (clear CLOFEN via `p6_vdp2_fade_apply(0,0)`). In Mania
the FXRuby arrival flash COMPLETES before the card slides in (card shows on a
clean screen). Any residual offset at card-time is a Saturn artifact of the fade
not clearing post-handoff. Gated by `p6_titlecard_is_active()` so only the card
window clears it; the arrival flash itself (before the card spawns) is untouched.
RED-first witness: `p6_w_ghzcut_fade` (fade_w<<16|fade_b) read live at the landing.

## Palette plan (MEASURED -- collision-free)

CRAM (mode 1, RAMCTL=0x1327, 2048 entries) at the GHZ LANDING:
- block0 [0..255] FG bank0; block1 [256..511] general VDP1 sprite bank;
  blocks2-6 [512..1663] = the 5 GHZCutscene Heavies -- **UNUSED at the landing**
  (Heavies exit in the GHZCutscene ExitHBH beat; p6_heavy_palblock stays 1, no
  landing draw routes to 512+); block7 [1792..2047] merged players.
- Upload Display.gif's 256-color GCT to **block 2 (CRAM[512..767])** at the seam
  (mirrors p6_vdp2_player_pal_upload / p6_vdp2_hbh_pal_upload EXACTLY, raw CRAM
  write, no jo +1 shift). Route glyph blits to colno=512. Zero contention.
- Palette blob: cd/DISPCARD.BIN = Display.gif GCT -> 256 BGR555 u16 (0x8000|bgr),
  built offline (mirror how PLRPAL.BIN is built). Loaded via
  rsdk_storage_load_to_lwram at the seam.

## Files touched

- `tools/build_titlecard_pal.py` (NEW) -- emit cd/DISPCARD.BIN from Display.gif.
- `tools/_portspike/_p6/p6_vdp2.c` -- add `p6_vdp2_titlecard_pal_upload()`
  (256 u16 -> CRAM[512]). GHZCUT-gated.
- `tools/_portspike/_p6/p6_vdp1.c` -- add `p6_dl_glyph(sheet,x,y,w,h,sx,sy,
  palblk)` cross-TU entry: a p6_vdp1_blit variant that lets the caller pass the
  CRAM block (glyphs need block 2, not the default 1). GHZCUT-gated.
- `tools/_portspike/_p6/p6_wave1_reg.c` -- add the glyph draw to tc_draw_showcard
  + tc_draw_slideaway (ZONE via anim2 pivots, name via DrawText-ALIGN_CENTER
  port, act digit via anim3). Extend s_tc with zoneName char->frame indices.
- `tools/_portspike/_p6/p6_io_main.cpp` -- (a) stage DISPCARD.BIN + upload to
  block 2 at the landing seam (next to PLRPAL); (b) clear the VDP2 offset while
  the card is active (the saturation fix, at the fade-apply site 7879).
- `tools/qa_chain_titlecard.py` -- add a TEXT-presence check (dark-glyph mass in
  the name box) + a SATURATION check (strips match SOURCE not washed refs).

## PROACTIVE-DETECTION AUDITS (CLAUDE.md 4.5.1)

- **Audit 1 Z-order:** glyphs draw LAST in tc_draw_showcard (after BG/strips/
  decor plates), inside the same direct list (painter order = later in front).
  So: yellow BG -> strips -> black word plate -> white zone plate -> ZONE glyphs
  + name glyphs + act digit ON TOP. Matches decomp draw order (L753-832: plates
  then glyphs). GHZ scenery is VDP2 = always behind VDP1. PASS.
- **Audit 2 cadence:** glyphs are STATIC frames (speed=0 for all 4 anims, MEASURED
  from TitleCard.bin) -- no per-frame animation walk. Their MOTION is the slide
  (charPos.y bounce + zoneCharPos slide), already ticked by the decomp state math
  in tc_state_enter_title -> tc_handle_zone_char_movement (60Hz-paced by
  P6_TICK_CAP). No cadence table needed (no multi-frame loop). PASS.
- **Audit 3 pivot/flip:** all glyphs FLIP_NONE (DrawSprite/DrawText emit FLIP_NONE,
  Drawing.cpp:2785/4340). Screen top-left = pos + pivot (per glyph). ZONE letters:
  x = zoneXPos>>16 + pivotX (spreads Z/O/N/E via the -117/-90/-64/-37 pivots),
  y = 186 + (zoneCharPos[i]>>16) + pivotY(-10). Name letters (ALIGN_CENTER):
  right-to-left from word2XPos-20, each x -= (width+spacing), y = 154 + pivotY(-16)
  + charPos[c].y. Documented per-glyph below in code. No parent/child flip pair.
  PASS.
- **Audit 4 boot-delay:** DISPLAY.SHT already staged (0 new). DISPCARD.BIN = 512 B
  (256 u16), 1 GFS read at the landing seam. Budget: 512/150000 + 0.1 = ~0.10 s,
  on a seam that already loads the whole GHZ scene. Cumulative landing boot far
  under 5 s. PASS.

## #228 budget

`_end` must stay < 0x060C8000. Colored-card build was 0x060BD210 (44,528 B
headroom). Additions are CODE (glyph draw fns + pal upload) + ~512 B of .rodata
(the glyph frame-rect table) + no new .bss of note (glyph draw uses the existing
s_tc). Re-measure `_end` from game.map after build; if over, report the measured
budget (do NOT overflow-brick).

## Gate (RED-first)

- `tools/qa_chain_titlecard.py --shot <landing PNGs>`:
  - EXISTING: strip mass > min (card present). Keep.
  - NEW `--require-source-sat`: strips must match the SOURCE refs (0x4060B0 etc.)
    within tol -- RED on the washed render, GREEN after the offset-clear.
  - NEW `--require-text`: dark-glyph pixel mass (near-black + near-white letter
    pixels) inside the name/zone box region > threshold -- RED today (no text),
    GREEN after the glyph draw.
- RED baseline = _shots/game-260706-214050.png (washed + no text).
- GREEN = a landing PNG with readable "GREEN HILL ZONE"+"ACT 1" and SATURATED
  strips (source refs).
