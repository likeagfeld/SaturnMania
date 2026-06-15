# Plan — FG foreground: vblank SCU-DMA page upload (kill the grass-hole tearing)

Task #242 (render). Methodology run 2026-06-15 (sega-saturn-developer skill).

## Root cause (measured + cited)
The engine present `p6_vdp2_present_ghz_camera` writes the VDP2 NBG1 pattern-name
map with **CPU stores during active display**. Per the hand-port that WORKED
(`src/_archived/main_streaming_WORKING.c.bak` lines 9-12) and
`memory/saturn-vdp2-streaming-solved.md`: CPU stores to the VDP2 map during
active display **do not reliably reach the screen — they tear / land partially**.
That is the user's "chunks of grass missing while moving." (A separate already-
fixed hole class: the stolen-blank-char collision, fixed a530c43 -> tile 0.)

## Docs/samples cited
- ST-210-110194 (SCU precautions), HTML lines 228/230/234/244-249:
  - "Write to the A-Bus by SCU-DMA is prohibited" (we READ A-Bus -> OK).
  - "Read from the VDP2 area by SC-DMA is prohibited" (we WRITE VDP2 -> OK).
  - "Do not use SCU-DMA to WORK RAM-L" (buffer must NOT be WRAM-L).
  - A-Bus->B-Bus DMA is supported; only CPU access to A/B bus DURING the DMA is
    prohibited (the vblank callback does only the DMA + slScrPos, no bus touch).
- ST-058-R2 (VDP2): pattern-name map = the NBG1 plane data we upload.
- SGL302/SAMPLE2/SEGA2D_1/MAIN.C: slDMACopy(src_RAM -> VDP2_VRAM_B0 map) is the
  canonical map-to-VRAM mechanism.
- Hand-port fg_vblank: build page in RAM each frame, DMA in the vblank callback,
  slScrPosNbg1 in the same callback.
- memory/sgl-audio-vs-scroll-cpu-dma-conflict: use slDMAXCopy (cache-through
  SCU-DMA alias), NOT slDMACopy, so audio DMA does not corrupt the playfield.
- memory/saturn-vdp2-streaming-solved: the original discovery of this fix.

## Design
1. Page buffer in the 4MB cart free tail (A-Bus): 64x64 cells x 4 B (2-word PND,
   charno=tile*8 needs >12 bits so 1-word PND is impossible) = 16,384 B. Place at
   a fixed cart address inside the existing sheet-store region's free tail (store
   uses 264,865 of 384 KB). `#define P6_FG_PAGE 0x227F0000u` (16 KB, ends
   0x227F4000; sheet store ends ~0x227E0A00 -> no overlap). CPU writes via the
   cache-through alias = coherent; the DMA reads the same alias.
2. `p6_vdp2_present_ghz_camera`: on rebuild (dirty OR camera tile-origin changed),
   build the WHOLE 64x64 PND page into P6_FG_PAGE (the camera-anchored wrapping
   map, same PND formula) instead of CPU-storing into VDP2 VRAM. Set
   `p6_fg_dma_pending = 1`. Keep CRAM upload + the self-check witness.
3. New jo-side vblank callback `p6_fg_vblank()` (p6_vdp2.c or p6_perf.c, has SGL
   headers): if `p6_fg_dma_pending`, `slDMAXCopy((void*)P6_FG_PAGE,
   (void*)P6_VDP2_MAP, 16384)` + `slDMAXWait`/`slDMAWait` then clear pending;
   ALWAYS `slScrPosNbg1(toFIXED(scroll_x), toFIXED(scroll_y))` from the last
   camera. (scroll cached in a witness int updated by the present.)
4. main.c (P6_ENGINE_SHIPPING + P6SCENE branches): register p6_fg_vblank via
   `jo_core_add_vblank_callback` before jo_core_run (next to p6_perf_vblank).
5. The present's `slScrAutoDisp` + NBG1 config stays; remove the per-present
   `slScrPosNbg1` (moves to the vblank) and the CPU map stores.

## RED-first gate (qa_p6_fgdma.py)
- Witness `p6_w_fg_dma` (count of vblank page DMAs) + reuse `p6_w_fg_visok_far`.
- RED on the current build: `_p6_w_fg_dma` symbol ABSENT (present still CPU-stores).
- GREEN: symbol present AND p6_w_fg_dma > 0 (DMA path live) AND, on an in-motion
  capture past the 1024px wrap, visok_far == 1.
- FINAL visual confirmation (the real proof): dense ~60ms in-motion capture
  (tools/_portspike/qa_motion_shots.ps1) shows NO black tile holes in the grass
  strip across consecutive scrolling frames. (visok checks VRAM CONTENT and can
  pass while the DISPLAY tears -- that is why it passed before; the dense pixel
  capture is the authoritative gate for tearing.)

## BLOCKER DISCOVERED (Step 4 code inspection -- the real reason it tears)
`tools/_portspike/_p6/p6_dma_stubs.c` lines 24-26 DEFINE slDMACopy/slDMAXCopy as
a CPU `memcpy` (not SCU DMA). The stub exists because the bare P6.2 IO-proof boot
did NOT link LIBSGL.A. BUT the shipping/diag builds DO link LIBSGL.A (in the link
line). The pack's stub OVERRIDES LIBSGL's real slDMACopy -> the engine render
path has NEVER had a real DMA; every "DMA" is a CPU memcpy = the active-display
tearing. So the FG fix has a prerequisite:

PREREQ -- restore the real SCU DMA for the shipping/diag builds. Lowest-risk:
make the stub's slDMACopy/slDMAXCopy `__attribute__((weak))` so LIBSGL.A's strong
SCU-DMA defs win when linked, while the bare P6IO build (no LIBSGL) still falls
back to the weak memcpy. (GFS forces GFS_TMODE_CPU and never calls slDMA*, so the
CD/GFS path is unaffected -- stub comment lines 13-22.) Verify the real
slDMACopy resolves from LIBSGL in game.map after the change. Alternative if weak
linking misbehaves: a dedicated register-level SCU-DMA level-1 routine per
ST-097-R5 (read/write addr at 0x25FE0030/0034, count 0x25FE0038, add 0x25FE003C,
enable+trigger 0x25FE0040/0044; ST-210 bus rules above) -- more code, fully
self-owned, no link-order risk.

## Files
- tools/_portspike/_p6/p6_vdp2.c (build page to cart, p6_fg_vblank, witnesses)
- src/main.c (register the vblank callback in both engine branches)
- tools/_portspike/qa_p6_fgdma.py (RED-first gate)
- Makefile / build_*: no new TU (p6_fg_vblank lives in p6_vdp2.c, already jo-side)

## Acceptance
_end stays < 0x060b8000; build exit 0; qa_p6_fgdma RED->GREEN; dense in-motion
capture shows continuous grass (no holes) across >=8 consecutive scroll frames;
diag sweep + verify_done unaffected. Sprite blink is a SEPARATE follow-on
(resident frames, hand-port load_sonic_sprites pattern).
