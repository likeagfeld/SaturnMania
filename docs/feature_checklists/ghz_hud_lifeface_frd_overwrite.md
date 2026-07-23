# GHZ HUD lives-face garbage-square — FRD-store overwrite fix (task #326 follow-up)

## Symptom (user-confirmed, live screenshot _shots/game-260723-090405.png)
Bottom-left GHZ HUD lives display: the Sonic-FACE icon renders as a pixelated
red/magenta garbage square, and the lives "x" multiplier also garbles, while the
"3" lives digit and the SCORE/TIME/RINGS labels render CLEAN. NEW with the pink
fix commit 9e692ee (author flagged it as a known follow-up).

## Root cause (MEASURED, arithmetic-proven + live-confirmed — NOT guessed)
The lives face (HUD.c:299, lifeIconAnimator anim 12 / "Life Icons" anim2 frame0 =
Global/Display.gif rect **111,112,18,17**) and the "x" (HUD.c:337, hudElements
anim0 frame14 = rect **110,130,9,13**) are served from the pre-cut frame directory
DISPLAY.FRD, staged into the shared cart resident store (SaturnSheet_ResAlloc,
RES_BASE 0x22400000).

At the GHZCutscene->GHZ handoff (p6_io_main.cpp:8600+) the 9 GHZ FRDs stage from
0x22400000 in order SONIC1/2/3, ITEMS, DISPLAY, ...:
  SONIC1 259316 + SONIC2 231908 + SONIC3 167644 + ITEMS 30724
  => DISPLAY.FRD blob @0x224A85B8..0x224B7E44 (63628 B, live-confirmed frames=110).
  Face pattern @ +31420 = **0x224B0074**;  "x" pattern @ +36484 = **0x224B143C**.
  Digit pattern @ +9388 = 0x224AAAC4  (BELOW the stomp).

The pink fix (9e692ee) then one-shot-loads the 4bpp GHZ FG char **AGHFG.CHR
(131072 B) into 0x224B0000..0x224D0000** (live-confirmed p6_w_ghzfg_chr=131072)
AND the sky **AGHFS.CHR (64K) into 0x22480000** — BOTH inside the resident FRD
store. The AGHFG load overlaps [0x224B0000,0x224B7E44) = the DISPLAY.FRD tail,
overwriting the FACE (0x224B0074) and "x" (0x224B143C) patterns with FG char
bytes. Low-offset patterns (digits @<0x224B0000, labels at sy=1..16) survive —
EXACTLY the digits-clean / face+x-garbage symptom. AGHFS at 0x22480000 also
clips SONIC3/ITEMS/DISPLAY FRD heads.

This is the SAME bug class the AIZ leg already hit + fixed with
SaturnSheet_ResFloor (p6_io_main.cpp:7569-7582). The pink-fix comment said the
scratch was "below the layout resident 0x22600000" (true) but MISSED that the FRD
store ALSO grows up from 0x22400000 through 0x224Bxxxx.

### Proofs
- DISPLAY.FRD face/x/digit patterns are byte-EXACT vs the Global/Display.gif crop
  (offline diff) -> the FRD content is correct; corruption is post-stage.
- Live FRD identity witness: DISPLAY bytes=63628 frames=110 (matches file) ->
  staged intact, THEN overwritten.
- Live p6_w_ghzfg_chr=131072 -> AGHFG.CHR did load 128K into 0x224B0000.
- Exact byte overlap [0x224B0000..0x224B7E44] contains face@0x224B0074 +
  x@0x224B143C; digit@0x224AAAC4 is below -> matches the on-screen symptom.

## Fix (minimal, cart-map-cited)
Relocate the GHZ-gameplay AGHFG.* and AGHFS.* one-shot scratch loads OUT of the
resident FRD store into its FREE TAIL. Front-end chain cart map (memory
frontend-cart-map-recarve): RES 0x22400000-0x225A0000 | OBJ pak 0x225A0000 |
PLAYER pak 0x225E0000 | SaturnLayout 0x22600000+. The 9 GHZ FRDs (+WATER) end at
0x22570CC0, so [0x22578000,0x225A0000) (~162 KB) is dead — above every GHZ FRD,
below the OBJ pak. New scratch:
  AGHFG.CHR 0x22578000 (128K), BNK 0x22598000, CMP 0x22598400.
  AGHFS.CHR 0x22578000 (64K, REUSES the window after AGHFG is uploaded to VDP2),
            MAP 0x22588000, PAL 0x2258C000.
GHZ-only (useA1 / currentSceneFolder=="GHZ"); the GHZCutscene AGHCBG path keeps
0x22480000 (byte-identical — cutscene FRDs are the small set, never reach it).
Files: tools/_portspike/_p6/p6_io_main.cpp (2 edits ~:9545-9600).

## Gate
tools/qa_ghz_lifeface.py — the pre-fix build already shows the garble on-screen;
the primary confirmation is the framebuffer face-corner (clean blue Sonic head vs
red garbage) + the FRD-store-integrity that AGHFG.CHR no longer overlaps DISPLAY.
verify_done is NOT run (chain build — clobber rule). Do-not-regress gates:
qa_ghz_pink.py (pink stays gone), qa_ghz_badnik_vis.py + qa_iso_dirtbl_ceiling
(badniks stay bound), qa_p6_ghz_regression whole-level, other-HUD intact, intro
scenes render, _end < 0x060c8000.

## Not touched (out of scope)
- The pre-existing GHZ-start transparent-white/sky flash (separate, needs a VDP2
  dump; predates this change).
