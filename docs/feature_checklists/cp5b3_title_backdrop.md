# CP5b.3 — Title backdrop (TitleBG + Title3DSprite + island/clouds)

Mission: the engine Title (P6_FRONTEND_TITLE) currently renders ONLY the
VDP1 foreground (SONIC MANIA logo + Sonic head/finger) on a BLACK VDP2
backdrop. Add the green floating island + clouds + horizon behind Sonic.

## Decomp sources (verbatim, cached)
- `tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c`
  - 5 sprite types: anim0 MOUNTAIN1, anim1 MOUNTAIN2(INK_BLEND),
    anim2 REFLECTION(INK_ADD), anim3 WATERSPARKLE(INK_ADD),
    anim4 WINGSHINE(INK_MASKED).
  - `TitleBG_SetupFX` sets layer2(Clouds).scanlineCallback=Scanline_Clouds,
    layer3(Island).scanlineCallback=Scanline_Island, drawGroup[0]=0/1.
  - `TitleBG_StaticUpdate` rotates palette 140..143 + advances angle/timer.
- `tools/_decomp_raw/SonicMania_Objects_Title_Title3DSprite.c`
  - 5 billboard frames (anim5 of Background.bin): MountainL/M/S, Tree, Bush.
  - perspective projection from TitleBG->angle.

## Asset facts (MEASURED, parse_spr on Title/Background.bin)
- Sheet: `Title/BG.gif` (256x256 indexed).
- anim0 MOUNTAIN1   : src(66,1)   176x16 pivot(-88,-16)
- anim1 MOUNTAIN2   : src(1,130)  192x16 pivot(-96,-16)  INK_BLEND
- anim2 REFLECTION  : src(66,18)  176x16 pivot(-88,0)    INK_ADD a=0x80
- anim3 WATERSPARKLE: src(1,147)  192x16 pivot(-96,0)    INK_ADD a=0x80
- anim4 WINGSHINE   : src(1,1)    64x128 pivot(-32,-64)  INK_MASKED
- anim5 billboards  : 5 frames, max 16x30
- => widest sprite frame = 192 wide, fits the front-end 248x160 VDP1 box.

## Title scene tile layers (MEASURED, Title/Scene1.bin)
- L0 Black BG : 32x15 (512x240)  nonblank=0
- L1 Horizon  : 32x15 (512x240)  nonblank=480   (sky gradient + horizon line)
- L2 Clouds   : 16x16 (256x256)  nonblank=256   (Scanline_Clouds = line scroll)
- L3 Island   : 64x64 (1024x1024) nonblank=256  (Scanline_Island = Mode-7)
  - visible bbox (384,384)-(640,640) = the 256x256 green Sonic-head island.
- Tileset: Title/16x16Tiles.gif (1024 tiles), same format p6_vdp2 expects.

## Engine scanlineCallback finding (PREREQUISITE — MEASURED)
- `Object.cpp:1198-1199` (ProcessObjectDrawLists): `if (layer->scanlineCallback)
  layer->scanlineCallback(scanlines); else ProcessParallax(layer);` — NOT
  Saturn-gated, RUNS on Saturn. The software DrawLayer* rasterizer (1203-1218)
  IS Saturn-gated off.
- BUT `scanlines` (Scene.cpp:13 `ScanlineInfo *scanlines = NULL`) is allocated
  ONLY by the desktop RenderDevice (DX11/EGL/...). There is NO Saturn
  RenderDevice -> `scanlines` stays NULL on Saturn. So registering a
  scanlineCallback today -> `callback(NULL)` -> the callback writes
  `scanlines->deform.x` -> WILD WRITE / crash.
- Conclusion: to use the callbacks at all the Saturn build must FIRST allocate
  `scanlines[SCREEN_YSIZE]` (240*16 = 3840 B). Even then, the per-line output
  drives the Saturn-stubbed framebuffer rasterizer (gated off) — nothing
  consumes scanlines into VDP2. So the decomp scanline path does NOT auto-map
  to VDP2; the Saturn backend must own the island/cloud VDP2 render.

## DECISION (data-driven, honest scope for one run)
Part A (HIGH confidence, proven path = TitleSonic/TitleLogo mirror):
  - Register verbatim TitleBG + Title3DSprite in the front-end overlay.
  - Stage `TBG.SHT` (Title/BG.gif) as the 13th SaturnSheet slot.
  - Mountains/water/billboards blit via the engine DrawSprite VDP1 path.
  - Allocate `scanlines[SCREEN_YSIZE]` at a fixed cart address so SetupFX's
    callback registration is safe; then NEUTRALISE the tile-layer drawGroup
    (drawGroup[0] = DRAWGROUP_COUNT) right after SetupFX so the engine never
    enters the Saturn-stubbed DrawLayer path for them.
  - INK effects: VDP1 half-transparency (PMOD CL_Trans) for INK_BLEND/ADD;
    measure what the Saturn DrawSprite path passes for inkEffect/alpha. If
    unsupported -> opaque + report (per task).

Part B (the big visible win — VDP2 backdrop):
  - Display the ISLAND (engine Title tileLayer 3) on NBG1 cell-mode via the
    proven p6_vdp2_present_layout path, sourced from the engine's decoded
    Title tileset (tilesetPixels) + tileLayers[3].layout + fullPalette[0].
  - Add HORIZON+CLOUDS so the sky is blue with clouds, not black. Scroll the
    cloud/island NBG to animate (line scroll = stretch goal).
  - The full Mode-7 perspective RBG0 island (derived from the decomp per-row
    affine) is the STRETCH goal; if not landed, report precisely.

## Gate (RED-first)
- `tools/_portspike/qa_title_backdrop.py`: measure green-island pixel mass in
  the region behind/below Sonic + sky/cloud-blue pixels in the upper region.
  RED on the current black-backdrop build; GREEN when backdrop pixels present.
- Screenshot: `tools/_portspike/_title_backdrop.png` (orchestrator opens it).

## Don't regress
- qa_title_sonic / qa_title_logo / qa_engine_title GREEN (FG must still render).
- default GHZ `_end=0x060B6BA0` byte-identical + R0-R16.
- Title-flavor `_end` < ANIMPAK 0x060B6C00.

## Build
MSYS_NO_PATHCONV=1 docker run --rm -v "D:/sonicmaniasaturn":/work -w /work \
  -e P6_FRONTEND_TITLE=1 joengine-saturn:latest bash tools/_portspike/_p6/build_shipping.sh
python tools/build_cdda.py cd_audio/track02.wav cd_audio/track03.wav --cue-out game.cue --iso game.iso
