# Feature checklist: GHZ FG 8bpp->4bpp + sky BG char relocate (task #326)

Root cause: memory `ghz-pink-flash-root-cause-4bpp-fg-relocate.md`. The GHZ
sky BG plane (NBG0) drops its VDP2 fetch intermittently under motion because
its character base is in VRAM bank B1 (SGL slScrAutoDisp cannot hold a B1-char
NBG, gotcha #7/#12). The dropped frame exposes RSDK's magenta transparent-key
(CRAM 0xFC1F). The only race-free fix is to move the sky char OUT of B1 into a
standard bank (A1). Bank A1 is currently full (256KB 8bpp FG char = A0+A1). Free
it by re-cutting the GHZ FG to 4bpp (128KB, A0 only).

## Decomp / doc citations
- ST-058-R2 (VDP2 manual) sec 3.2 (VRAM cycle / char selection limits),
  p.73 (charno unit 0x20 B), p.87 (map-plane registers).
- ST-238-R1 (SGL reference): slCharNbg0/1 COL_TYPE_16 vs COL_TYPE_256,
  slScrCycleSet, slPageNbg PNB_2WORD.
- GHZSetup.c:21-24,101-102 RotatePalette (water cycle 181-184/197-200) -> the
  FG 4bpp CRAM banks must be rebuilt from the LIVE cycled palette each frame.
- Proven 4bpp consumption model: p6_vdp2.c p6_vdp2_aiz_bg_upload:1696-1745 /
  aiz_bg_stream (per-tile PND palette-bank + .CMP live CRAM rebuild).

## Assets (built, GREEN)
- `tools/build_ghz_fg_4bpp.py` -> `cd/AGHFG.{CHR,BNK,CMP}`
  - AGHFG.CHR 131072 B (1024 tiles x 128 B, raw tile order, A0 only)
  - AGHFG.BNK 1024 B (per-tile CRAM bank index 0..29)
  - AGHFG.CMP 481 B ([u8 N=30][30*16 u8 source-palette-index])
  - 30 banks, PAL_BASE=32 -> CRAM[512..991]; lossless gate GREEN (0 non-quantised
    px mismatch, 272 px on 3 quantised tiles 885/942/954).
  - "AGHFG" naming sorts at ISO root index ~5-7 so GFS_NameToId resolves it;
    pushes the AIZBG.* block to index 10-18 -> P6_GFS_MAX_DIR bumped 16->20.

## VRAM bank map (128 KB banks)
| Bank | Addr | Before | After |
|---|---|---|---|
| A0 | 0x25E00000 | FG cells (8bpp, half) | FG cells (4bpp, all 128KB) |
| A1 | 0x25E20000 | FG cells (8bpp, half) | **GHZ sky BG char (relocated from B1)** |
| B0 | 0x25E40000 | FG PND map + sky maps | unchanged |
| B1 | 0x25E60000 | GHZ sky BG char | freed (sky char moved to A1) |

## STEP 2 (FG 8bpp->4bpp) files
- `tools/_portspike/_p6/p6_vdp2.c`:
  - NEW `p6_vdp2_upload_cells_4bpp(chr, bnk)` (GHZ-only; does NOT touch the shared
    8bpp upload used by Title/AIZ). Uploads 128KB 4bpp char to A0.
  - `p6_pnd_for` -> GHZ 4bpp variant: charno = tile*4 (was *8) + palette-bank
    field (PAL_BASE + BNK[tile]) in PND bits 22-16.
  - `p6_present_config`: slCharNbg1 COL_TYPE_16 (was COL_TYPE_256) when GHZ 4bpp.
  - `p6_present_compute` CRAM: rebuild 30x16 banks at CRAM[512..991] from the LIVE
    pal565 via the loaded .CMP composition (water cycle stays live).
- `tools/_portspike/_p6/p6_io_main.cpp`: load AGHFG.* at GHZ load; call the 4bpp
  upload instead of the 8bpp upload for GHZ.
- `tools/_portspike/_p6/p6_gfs.c`: P6_GFS_MAX_DIR 16 -> 20.
- All new paths GHZ-gated (a `p6_ghz_fg_4bpp` runtime flag) so Title/AIZ/GHZCut
  p6_vdp2.o behavior is byte-unchanged.

## STEP 3 (relocate sky BG char B1->A1) files
- `tools/build_ghzcut_bg.py`: emit a GHZ-gameplay sky variant with CHARNO_BASE =
  (0x25E20000-0x25E00000)/0x20 = 0x1000 (was 0x3000 for B1) -> map charno lands
  in A1. Upload target P6_GHCBG_CHR_* = A1.
- `tools/_portspike/_p6/p6_vdp2.c`: GHZ-gameplay sky char base A1; slScrCycleSet
  A1 (bank A) instead of B1, so SGL auto-allocates it (no drop). GHZCutscene keeps
  B1 (4-plane).

## Gates (all must pass)
- G1: `python tools/build_ghz_fg_4bpp.py --census` GREEN (DONE).
- G2 (STEP 2): FG colour parity vs 8bpp golden GHZ capture (SSIM/pixel).
- G3 (STEP 2): sprite/HUD/player/ring spot-check unbroken (CRAM[256..511] untouched).
- G4 (STEP 3): `python tools/qa_ghz_pink.py` 8/45 -> 0/N under AUTORUN motion.
- G5: `python tools/qa_p6_ghz_regression.py` whole-level GREEN.
- G6: map `_end` < 0x060c8000.
- G7: AIZ/Title/GHZCutscene still render (shared-path spot-check).

## Audits
- Audit 1 (Z-order): FG NBG1 pri stays 3 (GHZ 4-plane) / 1 (pre-BG); sky NBG0
  pri 2. Sprites (Sonic/Tails/HUD) VDP1 pri 7 = front. Unchanged by 4bpp.
- Audit 4 (boot budget): AGHFG.CHR 128KB / 150KB/s + 1 seek ~= 0.95s at GHZ load
  (one-shot, GFS idle). Under the 5s boot budget; not on the front-end boot path.
