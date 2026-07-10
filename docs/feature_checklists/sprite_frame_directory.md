# Sprite frame-directory pipeline (pre-cut frames + 4bpp LUT + store codec)

Status: OFFLINE feasibility + converter + stage-1 loader diff DONE in this
worktree (commits: census 3639993, converter, codec eval, loader). Live
build/verify is the parent's integration step. All numbers below are
MEASURED by the tools in `tools/` (re-runnable), not estimated.

## Sources read (methodology step 1-4)

- `sega_saturn_docs/VDP1_Manual.txt` (ST-013-R3): sec 5.1 character
  pattern tables (32-byte boundary, width 8..504 in 8-px units, height
  1..255; 4bpp nibble order "upper 4 bits represent the left pixel");
  sec 5.2 color lookup tables (32 B, 16 x u16, entries = color bank code
  OR RGB code; CMDCOLR = address/8; RGB prohibited in 8bpp-framebuffer
  modes); sec 5.3 Gouraud ("only effective on RGB color codes... cannot
  be guaranteed... for color bank color codes"); sec 6.3 CMDPMOD (color
  mode bits 5-3: 0=16-col bank, 1=16-col LUT, 4=256-col bank; ECD bit 7,
  SPD bit 6; transparent code = 0 in all indexed modes).
- `SBL601/SBL6/SEGALIB/INCLUDE/SEGA_CMP.H` + `SEGALIB/CMP/` (SBL
  compression library survey).
- flamewing/mdcomp (Sonic Retro reference codecs): kosinski.cc:157-207,
  kosplus.cc:151-206, comper.cc:127-155, saxman.cc:150-205.
- Local code end-to-end: `platform/Saturn/SaturnSheet.cpp` (whole file),
  `tools/_portspike/_p6/p6_vdp1.c` (bind table 339-353, slot pools
  1474-1624 + 1636-1739, restage 635-, direct-VDP1 dl 898-995, CRAM
  contract note 1901-1924), `tools/build_anim_pack.py`,
  `tools/build_sheet_bands.py`, `tools/build_heavy_atlas.py`.

## 1. FRD1 format (tools/build_frame_dir.py; big-endian, parsed in place)

```
+0   'FRD1'
+4   u16 frameCount | u16 lutCount
+8   u16 sheetW | u16 sheetH
+12  directory: frameCount x 16 B, SORTED ascending by
     key = (sy<<48 | sx<<32 | w<<16 | h):
       u16 sx, sy, w, h    -- the anim rect (== today's slot-cache key)
       u32 offset          -- pattern bytes from blob start, 4-aligned
       u8  mode            -- 0 = 8bpp (VDP1 color mode 4)
                              1 = 4bpp LUT (VDP1 color mode 1)
       u8  pw8             -- padded width / 8 (== CMDSIZE Hsize field)
       u16 lutIdx          -- 4bpp: LUT table index; 0xFFFF for 8bpp
+12+16*frameCount: lutCount x 16 B LUT SOURCE tables -- original 8-bit
     palette index per nibble value (nibble 0 = index 0 = transparent).
     Deduped across frames.
then (4-aligned) patterns: h rows of pw8*8 B (8bpp) / pw8*4 B (4bpp),
     pad pixels = 0 (transparent code, ST-013-R3 sec 6.3).
```

Every frame round-trips byte-exact vs the source GIF crop (self-tests
S1/S2/S3 run on every build; a failure aborts emission). `--all8`
builds stage-1 blobs with no 4bpp entries.

## 2. Loader stage 1 (LANDED in this worktree, P6_FRAMEDIR)

- `platform/Saturn/SaturnFrameDir.cpp` (new): `SaturnFrameDir_Stage`
  (copies the GFS-loaded blob into the shared cart resident store via
  `SaturnSheet_ResAlloc`), `_SetHash/_FindSlot` (SaturnSheet
  conventions), `_Lookup` (binary search; fills `P6FrameInfo{pattern,
  lutSrc, pw, mode, lutIdx}`).
- `p6_vdp1.c`: `p6_vdp1_set_frd()` runtime function pointer (the W12b
  LTO contract, p6_vdp1.c:326-338 -- no static pack refs from the
  jo-side TU); `s_sheets[].frdSlot` + `p6_vdp1_sheet_set_frd(handle,
  frdSlot)`; FRD dispatch at BOTH miss sites (p6_pool_for +
  p6_title_pool_for) ahead of the resident/banded paths, fallback
  preserved. All additions compile away without `-DP6_FRAMEDIR`
  (default GHZ build byte-identical).
- Effect at a miss: srcPx = pre-cut pattern, srcStride = pw. Rows are
  4-aligned at the padded stride, so `p6_title_restage_content`'s
  aligned-u32 fast path always runs and the shift-merge branch is dead;
  the banded path's miniz inflate + 16 KB scratch round trip is gone
  for FRD-backed sheets. p6_io_main wiring (parent): GFS-load each
  .FRD, `SaturnFrameDir_Stage`, `p6_vdp1_set_frd(SaturnFrameDir_Lookup)`,
  `p6_vdp1_sheet_set_frd(handle, frdSlot)` next to the existing
  bind loop; keep .SHT staging for any sheet that still takes non-anim
  rects (defensive fallback -- witness p6_w_frd_misses shows whether
  any fire; if 0 for a sheet, drop its .SHT from the CD).

## 3. Stage 2 spec: 4bpp LUT draw path (P6_FRAMEDIR_4BPP -- NOT coded)

Exact values (ST-013-R3):

- CMDPMOD (sec 6.3): bits 5-3 = 001B (16-color lookup table mode).
  Today's 8bpp value is 0x00A0 (ECD | mode 4, p6_vdp1.c:915/960); the
  4bpp value is 0x0088 (ECD bit7=1 | bits5-3=001). Keep SPD=0
  (transparent nibble 0). +0x0003 CL_Half for fades, unchanged.
- CMDCOLR (sec 5.2): LUT address / 8. LUT = 32 B at a 32-byte boundary
  in VDP1 VRAM (not address 0).
- CMDSIZE: UNCHANGED -- Hsize is in 8-pixel units regardless of color
  mode (pw8 in the directory is the field value).
- Slot VRAM: a 4bpp pattern is pw/2 * h bytes -- half the slot bytes.
  jo `__jo_sprite_def` TEXDEF assumes 8bpp sizing; the 4bpp path is
  DIRECT-VDP1-only at first (p6_dl_sprite builds CMDSRCA/CMDSIZE by
  hand): give each bucket a per-slot `mode` and pack 4bpp patterns
  into the same reserved boxes (upper half unused in stage 2a; a
  4bpp-box re-carve is stage 2b, below).
- LUT build at bind time (per sheet): for each of the FRD's lutCount
  tables, write 16 u16 entries = (palblk << 8) | lutSrc[k] -- COLOR
  BANK CODE entries (sec 5.2 allows them). This preserves the CRAM
  palette pipeline bit-exact: SPCTL Type-3 full-11-bit DC (the
  contract at p6_vdp1.c:1918-1924) resolves the same CRAM entry the
  8bpp pixel would have -> palette cycles, per-Heavy blocks and the
  VDP2 color-offset fades all keep working. Zero color loss.
- Gouraud (sec 5.3): guaranteed ONLY for RGB-code LUT entries. So:
  color-bank LUTs (default) = no Gouraud but full palette animation;
  an opt-in RGB LUT for a specific effect enables Gouraud + shading
  at the cost of freezing that sprite out of CRAM animation. Per-use
  choice; both are 4bpp.
- LUT VRAM placement: lutCount for the whole 12-sheet working set =
  201 tables (sum of manifest luts) = 6,432 B. Candidate: after the
  direct-VDP1 command halves (P6_DL_B 0x2800 + 62*32 = 0x3000 ->
  0x25C03000..0x25C04920). MUST be probed the same way P6_DL_A/B was
  (SGL's reserved-command-area transfer reach) before use -- do not
  assume.

## 4. Measured tables

### 4.1 Color census (tools/frame_census.py, frame_census.json)

| sheet | frames | sheet colors | max frame colors | 4bpp-eligible |
|---|---|---|---|---|
| Players/Sonic1 | 206 | 25 | 21 | 22/206 |
| Players/Sonic2 | 189 | 23 | 19 | 16/189 |
| Players/Sonic3 | 107 | 21 | 19 | 3/107 |
| Players/Tails1 | 245 | 17 | 16 | 243/245 |
| Global/Items | 104 | 48 | 19 | 100/104 |
| Global/Display | 110 | 52 | 17 | 108/110 |
| Global/Shields | 102 | 22 | 7 | 102/102 |
| Global/Objects | 87 | 45 | 25 | 82/87 |
| Global/PhantomRuby | 34 | 8 | 5 | 29/34 |
| GHZ/Objects | 151 | 61 | 28 | 85/151 |
| AIZ/Objects | 64 | 73 | 24 | 34/64 |
| GHZCutscene/Objects | 11 | 33 | 16 | 10/11 |
| HBH sources (5 sheets) | 373 | 39-69 | 28-45 | 218/373 |

Honest headline: the Sonic body sheets are NOT 4bpp material (16-21
colors/frame); Tails, Shields, Items, Display, HUD and most object
sheets are.

### 4.2 Store sizes (12-sheet working set, bytes)

| representation | bytes | vs raw |
|---|---|---|
| raw resident sheets (today's MakeResident) | 2,162,688 | 100% |
| pre-cut 8bpp (`--all8`) | 1,562,168 | 72.2% |
| pre-cut mixed 4/8bpp | 1,173,848 | 54.3% |
| + zlib-9 (CD/store) | 290,327 | 13.4% |
| + kosinski / kosplus | 359,828 | 16.6% |
| + saxman | 390,055 | 18.0% |
| + comper | 607,720 | 28.1% |

Codec recommendation: KEEP zlib/miniz for the store. It beats the best
Genesis codec by 19% absolute on this data, is already linked, and
decompression is once-per-load (the measured 680-1340 inflates/beat
catastrophe was PER-DRAW decompression -- not this design). The
Genesis codecs' only edge is decoder speed (structurally ~8-20
cycles/byte for Kosinski/Saxman, ~5-10 for Comper vs miniz's measured
~44 cycles/byte incl. cart wait-states in this codebase) -- irrelevant
off the hot path; a whole-set load-time inflate difference is ~10s of
ms. Nemesis was not implemented: it is tile-oriented (32-byte cell
streams), its decoder is bit-serial Huffman (slowest of the family),
and its ratio on linear sprite data is bounded by the same LZ window
economics that put Kosinski 6 points behind zlib -- no path to a win.

### 4.3 Budget wins

- Cart resident store: 2,162,688 -> 1,173,848 B (-988,840 B, -45.7%)
  for the 12-sheet set at full 4bpp adoption; -600,520 B stage-1
  (--all8). Directly relieves the F-LAND-SONIC RES_END squeeze
  (SaturnSheet.cpp:125-134).
- CD: .FRD.z (zlib whole-blob) ~290 KB vs today's ~2 MB-equivalent
  banded .SHT set; fewer files if .SHTs are dropped after the
  p6_w_frd_misses witness reads 0.
- VDP1 VRAM: stage 2a = none (patterns still land in 8bpp-sized
  boxes). Stage 2b re-carve: buckets whose measured demand is
  4bpp-dominant (tiny 16x20 = HUD digits/rings, 64x80 = Tails/fan/
  claw) can halve their per-slot bytes: e.g. the #324 carve's
  8*320 + 18*5,120 = 94,720 B -> 47,360 B if all-4bpp, funding more
  slots against the measured fmax 17 thrash without touching the
  466,232 B ceiling. Requires per-bucket depth split (mode is
  per-command, so a mixed bucket cannot share slot VRAM safely).
- Runtime: the per-miss cost drops from
  {miniz inflate of 1-2 16-row bands + row repack} or {resident
  per-row repack} to one aligned linear copy of pw*h (or pw/2*h)
  bytes -- the #324-measured restage hog shrinks proportionally
  (4bpp halves the copied bytes too).

## 5. Libraries (verified, not assumed)

- SBL CMP (`SEGA_CMP.H`): run-length only (`CMP_DecRunlen{,Byte,Word,
  Dword}`) -- strictly weaker than miniz; nothing to adopt. The BGCON
  tool ships a Huffman decompressor (NOV96 PROGRAM/BGCON DEHUFF.S) --
  also no advantage over miniz.
- SGL DMA: already in use (slDMACopy/slDMAXCopy per the project memory
  rules); the FRD copy path deliberately uses the #324 synchronous
  u32 pack instead (measured faster than the DMA round trip for these
  sizes; async DMA was the #311/#312 tear class).
- SaturnMath++: C++23 (README badge, "Leverages C++23 capabilities").
  SRL: C++ SGL wrapper built on it, ships its own modern toolchain.
  Shipping compiler = sh-none-elf GCC 8.2.0 (docker/Linux tree; max
  -std=c++2a, partial); the Windows tree GCC 9.3.0 has NO cc1plus at
  all. VERDICT: incompatible without a toolchain change -- and the
  GCC 8.2 LTO behaviors are load-bearing project memory (W12b,
  sync-load rules). Do not adopt; nothing in this pipeline needs
  them (no fixed-point math in the loader path).

## 6. RED gates for the parent integration (write BEFORE flipping the flag)

1. `qa_p6_frd.py` (structural): replay N probe rects per sheet through
   `SaturnFrameDir_Lookup` + pattern djb2 vs the offline manifest
   (mirror of qa_p6_sheet.py) -- RED on a build with the FRD staged
   but the hook off by hashing the wrong bytes fixture-style.
2. Witness contract: `p6_w_frd_staged == <count>`, `p6_w_frd_misses
   == 0` across a full chain run; any nonzero miss lists the rect ->
   either a non-anim blit (keep .SHT) or a converter gap.
3. Perf: existing qa_drawcost gates (G1/G2) must not regress; expected
   direction is DOWN (restage bytes halve for 4bpp frames; inflates
   -> 0 for FRD sheets).
4. Visual: existing scene pixel gates unchanged (parity binding); the
   4bpp stage-2 flips must show byte-identical CRAM resolution by
   construction (color-bank LUT entries), verified by the pixel gates.

## 7. Stage-1 LIVE integration plan (parent, 2026-07-10) -- MEASURED budgets

Blobs: `--all8` emitted 12 cd/*.FRD, total 1,562,168 B (72.2% of raw;
converter self-tests S1/S2/S3 pass). Per-blob (B): SONIC1 259,316 /
SONIC2 231,908 / SONIC3 167,644 / TAILS1 261,644 / ITEMS 30,724 /
DISPLAY 63,628 / SHIELDS 222,252 / GLOBJ 57,876 / RUBYOBJ 14,340 /
GHZOBJ 123,324 / AIZOBJ 108,620 / GHCOBJ 20,892.

Chain-flavor cart RES store = 0x22400000..0x225A0000 = 1,703,936 B.
The FRD blobs live in this store (SaturnSheet_ResAlloc) and are
therefore KILLED by every seam `SaturnSheet_ResReset()` -- the design
stages per-leg, resetting the FRD registry at each reclaim seam:

| site (p6_io_main.cpp) | action | store math (B) |
|---|---|---|
| plain-GHZ boot loop :4268 (non-frontend) | FRD-stage all 9 GHZ sheets AFTER the .SHT loop; keep MakeResident (3.80 MB store: 1,605,632 res + ~551K layout + 1,418,316 FRD = 3.57 MB fits) | 3,574,948 < 3,801,088 |
| Menu->AIZ seam (p6_aiz_reload :6745) | FRD {AIZOBJ,SONIC1,TAILS1} = 629,580; skip their MakeResident; SONIC2/3 keep today's bounds-checked promote (all-5-FRD would overflow by ~5 KB) | 679,776 boot res + 629,580 = 1,309,356 fits |
| AIZ->GHZCut seam :7394 | after ResReset: SaturnFrameDir_Reset + p6_vdp1_frd_detach_all; promote PLR+HBH (no FRD exists); FRD {DISPLAY,ITEMS,GHCOBJ,RUBYOBJ} = 129,584, skip their promote | 286,720 + 129,584 = 416,304 fits |
| GHZCut->GHZ landing :7532 | after ResReset: FRD reset+detach; FRD-stage all 9 GHZ sheets; promote (promoteOrder) ONLY sheets whose FRD staging failed | 1,418,316 + align pad fits (285,620 headroom) |

Loader path: `p6_frd_stage_file()` GFS-loads the .FRD DIRECTLY into the
cart at the ResAlloc cursor (peek via ResAlloc(0) + cap via the new
gated SaturnSheet_ResRemain()) -- no WRAM bounce (blobs up to 262 KB >>
the 64 KB P6_LW_ENTITYLIST window, which is also the forbidden live
entity pool). `SaturnFrameDir_StageDirect` then claims the bytes and
computes a one-time djb2 read-back witness (load-phase only, never
per-frame). Attach = FindSlot-by-gif-path-MD5 inside both arm-env bind
loops + `p6_frd_attach_bound()` after each seam's load_and_arm.

CD-read cost (audit-4 style): +1.42 MB at the landing seam ~= +9.5 s
at 1x CD, +0.63 MB AIZ seam, +0.13 MB cutscene seam -- load-time only,
traded against the removed MakeResident band-inflate cycles; the fps
gates measure the per-frame effect.

Witness contract (all `__attribute__((used))` + -u rooted, WRAM .bss,
live-readable): p6_w_frd_staged (cumulative), p6_w_frd_active
(post-reset live slot count), p6_w_frd_lookups, p6_w_frd_misses,
p6_w_frd_hash/bytes/frames[16] (per-slot djb2-of-staged-cart-bytes vs
the offline blob), p6_w_frd_missrect/misswh/missslot[4] (miss ring).
Gate: tools/qa_p6_frd.py (offline structural replay + manifest djb2 +
live witness contract; RED on the pre-FRD build).

Flag threading: -DP6_FRAMEDIR via Makefile (jo side, p6_vdp1.c),
build_p6scene_objs.sh (p6_io_main.o + SaturnSheet.o + new
SaturnFrameDir.o, link + -u roots all ${P6_FRAMEDIR:+...}-gated),
build_shipping.sh make lines. Without the env var every addition
compiles/links away -- plain GHZ byte-identical.
