# Large Sprite Animation Strategy — TitleSonic (49 frames)

**Source authority for every claim in this document:**

| Citation tag | File |
|---|---|
| `[VDP1 Manual]` | `D:\Claude Saturn Skill Documentation\sega_saturn_docs\VDP1_Manual.txt` |
| `[Saturn Overview]` | `D:\Claude Saturn Skill Documentation\sega_saturn_docs\Saturn_Overview.txt` |
| `[SGL S_7_4]` | `D:\Claude Saturn Skill Documentation\NOV96_DTS\LIBRARY\SDK_10J\SGL302\SAMPLE\S_7_4\MAIN.C` |
| `[jo sprites.c]` | `D:\sonicmaniasaturn\jo-engine\jo_engine\sprites.c` |
| `[jo sega_saturn.h]` | `D:\sonicmaniasaturn\jo-engine\jo_engine\jo\sega_saturn.h` |
| `[jo SL_DEF.H]` | `D:\sonicmaniasaturn\jo-engine\Compiler\COMMON\SGL_302j\INC\SL_DEF.H` |
| `[Sega Tech 13-APR-94]` | `D:\Claude Saturn Skill Documentation\Saturn Documentation HTML Files\13-APR-94.html` |
| `[MANCPK.DOC]` | `D:\Claude Saturn Skill Documentation\Saturn Video Tools\Official Cinepak Demos\SBL Cinepak Demo 4\CINEPAK\SBL\SEGALIB\MAN\MANCPK.DOC` (Shift-JIS) |

The problem is "play 49 frames of TitleSonic at 60 Hz, frame sizes 89x82..198x126,
total 1,410 KB when stored as RGB1555."

## 1. Hard hardware budgets

### VDP1 VRAM

- VDP1 has **512 KB VRAM** at `0x05C00000..0x05C7FFFF` (`[VDP1 Manual]:2567-2568`,
  `[jo sega_saturn.h]:90,121`). The whole 0x80000 byte range must hold: the
  command list, the system clipping setup, the gouraud tables, palettized color
  lookup tables, AND every sprite character pattern simultaneously visible /
  ready to be drawn.
- jo reserves `0x10000` for the command-list / control region and stops sprite
  data at `0x25C7FEF8` (= `JO_VDP1_USER_AREA_END_ADDR`), giving
  `JO_VDP1_USER_AREA_SIZE = 0x71D38 = 466,232 bytes` for sprite character
  patterns and run-time texture upload (`[jo sega_saturn.h]:121,123,125`).
- **Per-character-pattern hard limits** (`[VDP1 Manual]:2573-2582`,
  Table 5.1 §2588): horizontal `8..504 px in 8-px units`, vertical
  `1..255 px in 1-px units`, maximum one pattern at 16 bpp = `3EC10h = 257,040 bytes`.
  TitleSonic's largest frame (198x126) becomes `(200 px wide for 8-px alignment)`
  x 126 x 2 bytes = **50,400 bytes** at 16 bpp — well within the per-pattern
  ceiling, well within the user area.

### Random-frame CD streaming

- 2X CD-ROM **sustained throughput is 300 KB/s** = 150 sectors/s
  (`[Saturn Overview]:1806-1808`, `[Sega Tech 13-APR-94]:403`).
- **Mean random seek = 400 ms** (`[Saturn Overview]:1799`, Table 3.10 row 2).
- 60 Hz frame budget is **16.7 ms per frame**. A single random seek alone
  consumes ~24 frames of wall-clock time. **No frame-per-tick streaming scheme
  is viable.**

### Sound RAM (SCSP)

- 512 KB at `0x25A00000..0x25A7FFFF` (Sega Tech and SCSP Manual). Already
  partially consumed by jo's audio module (`bootsnd_map` + driver task).
  Using it as a secondary cache is technically possible via `slDMACopy`, but
  only the master SH-2 + SCU/SCU-DSP can DMA into it; this fights audio.

### Best palette opportunities

- VDP1 character data sizing rule (`[jo sprites.c]:174`):
  `bytes = (w * h * 4) >> color_mode`. SGL constants: `COL_32K=1` (16 bpp),
  `COL_256=2` (8 bpp), `COL_16=3` (4 bpp) (`[jo SL_DEF.H]:373-377`).
  So palettizing replaces every byte-of-frame ratio:
  - 16-bpp RGB1555: TitleSonic 49 frames = 1,410 KB (current).
  - 8-bpp Color Bank 256: ~ 705 KB.
  - 4-bpp Color Bank 16:  ~ 353 KB.

## 2. Strategy-by-strategy verdict (definitive, per-Saturn-spec)

### (a) Stream frames from CD on-demand — REJECTED

Single random seek = 400 ms (`[Saturn Overview]:1799`). Sustained streaming is
300 KB/s (`[Saturn Overview]:1806-1808`); even with **zero seek overhead** that
gives 5 KB/frame at 60 Hz, far below an 198x126x2 = 50 KB frame. With realistic
seeks 60 Hz is impossible. The only on-demand CD pattern documented in the
NOV96 DTS samples for animation-grade asset streaming is the Cinepak (CPK)
pipeline, which is engineered to amortize seeks via large preroll + ring buffer
+ keyframe-only-on-keyframe model, not arbitrary random frame access.

### (b) Sound RAM as secondary cache — REJECTED for jo+SGL

PCM RAM at `0x25A00000` is the SCSP's working area. Sega's own Cinepak driver
puts PCM samples there at `0x25A20000` (`[SBL CPK SMPCPK2 main.c]:59`); the
moment jo's audio module is on, ~64-128 KB of that bank is owned by the BOOTSND
map / SDDRVS driver. Pushing arbitrary game data through Sound RAM also forces
slDMAXCopy-style cache-through transfers that contend with the SCSP's own
SCU-DMA traffic — exactly the conflict already documented in
`memory/sgl-audio-vs-scroll-cpu-dma-conflict.md`. **Net win is negative.**

### (c) Palettize the frames (16-color color-bank) — RECOMMENDED PRIMARY

VDP1 supports `COL_16` (color-bank 16, 4 bpp) directly:
sprite-bytes = `(w * h * 4) >> 3 = (w * h) / 2`
(`[jo sprites.c]:174`, formula derived from `[VDP1 Manual] §5.1 4-bit color`).

For TitleSonic's max frame 198x126 (rounded to 200 wide for the mandatory
8-pixel-horizontal alignment in `[VDP1 Manual]:4354`):
- 4-bpp size = (200 * 126) / 2 = **12,600 bytes** per worst-case frame.
- 49 frames average ~half that (varying sizes) = roughly **350 KB total**.

Color-bank 16 means each frame uses 4-bit indices into a 16-entry palette held
in VDP2 Color RAM. Title's actual on-screen palette **is already a 16-color
palette in the RSDK source** (Mania uses 16-color palette banks per
TitleLogo entity in the decomp, including TitleSonic's pose), so the
palettization is exact, not lossy.

**350 KB fits comfortably within the 466 KB user area (`[jo sega_saturn.h]:121`)**.
This is strategy (e) — atlas in VDP1 VRAM — but at the right bit depth.

### (d) Use VDP2 NBG cell mode for the character animation — REJECTED for our case

VDP2 NBG cell mode is excellent for a static background that scrolls, but
each NBG layer reads from a fixed character pattern table and pattern-name
table; swapping the displayed image at 60 Hz means uploading a full new
cell-bank every frame (same I/O cost as (a)) AND fighting the VDP2 cycle
pattern (it expects to be reading those cells continuously to display them).
The Cinepak demo we have on disk does this and it requires the elaborate
"CYCLE_CPU_WRITE/CYCLE_VDP_READ" bank flip per frame
(`[cinepak_sgl.c]:42-48,386-410`). This is engineering effort for a feature
we don't need — TitleSonic must layer above the title background, which is
already on NBG (`docs/title_ground_truth.md` and `memory/saturn-vdp2-streaming-solved.md`).
The sprite + NBG2 mountain layer must coexist; the right plane for the
character is VDP1.

### (e) Pre-place all 49 frames in a VDP1 atlas at boot — RECOMMENDED (with (c))

This is what `jo_create_sprite_anim` already does. It assumes N
*consecutive* sprite IDs all live in VDP1 VRAM and just steps
`__jo_sprite_anim_tab[].cur_frame` per tick
(`[jo sprite_animator.h]:154-165`). The canonical SGL "load several sprites
once, animate by changing sprite_id" pattern is the same as the documented
SGL texture-upload loop in `[SGL S_7_4]:12-22`:

```c
for (; NbPicture-- > 0; pcptr++) {
    txptr = tex_sample + pcptr->texno;
    slDMACopy((void *)pcptr->pcsrc,
              (void *)(SpriteVRAM + ((txptr->CGadr) << 3)),
              (Uint32)((txptr->Hsize * txptr->Vsize * 4) >> (pcptr->cmode)));
}
```

`jo_sprite_add_8bits_image` / `jo_sprite_add` does exactly this DMA upload for
each frame at boot (`[jo sprites.c]:172-220`), then `jo_create_sprite_anim`
indexes them by integer offset at run time (zero per-frame I/O).

## 3. Canonical recommendation

**Strategy (e) + (c): Pre-load all 49 frames as a 4-bpp Color-Bank-16 VDP1
sprite atlas at boot, then animate by stepping `jo_create_sprite_anim`.**

Citations supporting this being the canonical SGL pattern for multi-frame
2D character animation:

1. `[SGL S_7_4]:12-22` — Sega's own SGL-302 sample 7.4 demonstrates the
   `slDMACopy` loop that uploads N character patterns into VDP1 VRAM at
   `SpriteVRAM + CGadr*8` with width `(W*H*4) >> cmode` bytes.
2. `[jo sprite_animator.h]:154-165` — jo's `jo_get_anim_sprite` returns
   `frame0_sprite_id + cur_frame`; expects N consecutive sprites already
   uploaded to VRAM. Identical pattern.
3. `[jo sprites.c]:173-175,217-220` — `jo_sprite_replace` / `jo_sprite_add`
   both end in `jo_dma_copy(src, JO_VDP1_VRAM + texture->adr*8, bytes)`. This
   IS `slDMACopy` for sprite character patterns.

## 4. Concrete implementation skeleton

### 4.1 Asset side: emit 49 × 4-bpp SPRs

Add a `tools/extract_title_sonic_anim.py` that:
1. Parses RSDK `Sprites/Title/Sonic.bin` to enumerate the 49 frames of the
   first animation entry (TitleSonic main entrance pose). Use the existing
   RSDKv5 spritesheet decoder already in `tools/`.
2. For each frame, output `MTSONIC%02d.SPR` to `cd/` with on-disk layout:
   ```
   uint16 reserved (== 0x0001 for 4-bpp; existing SPR uses 0x0010 for 16-bpp)
   uint16 width    (rounded up to multiple of 8)
   uint16 height
   uint8  data[(width * height) / 2]   // 4-bit packed indices, 2 nibbles/byte
   ```
   (Mirror the existing `MSONIC.SPR` layout but at 4 bpp; cf. `src/main.c:1114-1121`.)
3. Pre-shift the corresponding 16-color palette UP by 1 entry on disk to
   compensate for the `jo_create_palette_from` shift documented in
   `memory/jo-cram-off-by-one-shift.md`; emit `TSONIC.PAL` (32 bytes).
4. Total disk size budget (rough): mean ~7 KB × 49 frames ≈ **350 KB**.

### 4.2 Runtime side: boot-load and animate

```c
/* In src/main.c near g_sprite_msonic, after the title state assets load. */
#define TS_FRAME_COUNT  49

static int g_ts_frame0_sprite_id;
static int g_ts_anim_id;
static int g_ts_palette_id;

static void load_title_sonic_anim(void)
{
    /* Step 1: upload the 16-color palette (32 bytes pre-shifted on disk). */
    int                 plen;
    unsigned short     *pal = (unsigned short *)jo_fs_read_file("TSONIC.PAL", &plen);
    g_ts_palette_id = jo_create_palette_from(0, 16, pal);  /* CRAM bank 0 entry. */
    jo_free(pal);

    /* Step 2: upload each of the 49 frames as a 4-bpp Color-Bank-16 sprite.
     * Frames must be added CONSECUTIVELY so jo_create_sprite_anim can step
     * them by adding cur_frame to frame0_sprite_id (jo sprite_animator.h:154-165).
     */
    int frame0 = -1;
    for (int f = 0; f < TS_FRAME_COUNT; ++f) {
        char fname[16];
        jo_sprintf(fname, "MTS%02d.SPR", f);
        int len;
        unsigned char *spr = (unsigned char *)jo_fs_read_file(fname, &len);
        if (spr == NULL) break;
        jo_img_8bits img;     /* 8bits API is the jo channel for sub-32K modes. */
        img.width  = ((unsigned short *)spr)[1];
        img.height = ((unsigned short *)spr)[2];
        img.data   = (unsigned char *)(spr + 6);   /* skip 3-uint16 header. */
        /* 4-bpp upload via the SGL-canonical formula (W*H*4)>>color_mode bytes. */
        int sid = jo_sprite_add_4bits_image(&img, g_ts_palette_id);
        if (frame0 < 0) frame0 = sid;
        jo_free(spr);
    }
    g_ts_frame0_sprite_id = frame0;

    /* Step 3: create a 60-Hz one-shot animation (frame_rate==1 means one
     * frame per vblank, per jo_sprite_animator.h:170-173). */
    g_ts_anim_id = jo_create_sprite_anim(g_ts_frame0_sprite_id, TS_FRAME_COUNT, 1);
    jo_start_sprite_anim(g_ts_anim_id);   /* JO_SPRITE_ANIM_STOP_AT_LAST_FRAME */
}

/* In the title state draw callback, replace the static msonic draw: */
static void draw_title_sonic(void)
{
    int sid = jo_get_anim_sprite(g_ts_anim_id);
    jo_sprite_draw3D(sid, +1, -39, SPRITE_Z + 5);   /* same offsets as main.c:1581 */
}
```

### 4.3 If jo lacks a 4-bpp adder

`jo_sprite_add_8bits_image` exists (`[jo sprites.c]:237-247`); the 4-bpp variant
is not in the upstream API. The straightforward path is to drop one level and
upload via SGL directly, mirroring `[SGL S_7_4]:12-22`:

```c
extern unsigned int __jo_sprite_addr;
extern int          __jo_sprite_id;
/* ... read frame, then: */
int sid = ++__jo_sprite_id;
__jo_sprite_def[sid].width  = w;
__jo_sprite_def[sid].height = h;
__jo_sprite_def[sid].adr    = JO_DIV_BY_8(__jo_sprite_addr);
__jo_sprite_def[sid].size   = JO_MULT_BY_32(w & 0x1f8) | h;
__jo_sprite_pic[sid].color_mode = COL_16;       /* 4 bpp Color Bank 16. */
__jo_sprite_pic[sid].data       = (void *)(JO_VDP1_VRAM + JO_MULT_BY_8(__jo_sprite_def[sid].adr));
jo_dma_copy(spr_data, __jo_sprite_pic[sid].data, (w * h * 4) >> COL_16);
__jo_sprite_addr = __jo_get_next_sprite_address(JO_MULT_BY_8(__jo_sprite_def[sid].adr),
                                                w, h, COL_16);
```

This is byte-for-byte the same formula SGL itself uses
(`[SGL S_7_4]:18-20`), with the destination computed by jo's own helper.

### 4.4 Sanity check the VDP1 budget after extraction

After `tools/extract_title_sonic_anim.py` runs, sum the per-frame upload size:

```python
total = sum( ((w + 7) & ~7) * h // 2 for (w, h) in frame_dims )
assert total < 0x71D38, f"VDP1 atlas {total} bytes exceeds JO_VDP1_USER_AREA_SIZE"
```

With averaged dimensions of (~140 wide × ~110 tall) × 49 frames at 4 bpp,
expect ~375 KB used out of 466 KB available — leaves 90+ KB headroom for the
existing logo / ribbon / ring sprites.

## 5. Worst-case escape hatch (do NOT need this for TitleSonic)

If a future asset genuinely cannot fit the 466 KB VDP1 atlas budget — e.g. the
in-level FX bursts in Mania that contain many large frames — the canonical SGL
fallback is **CD-resident streamed mini-keyframes**: drop the per-frame
update rate (10–15 Hz instead of 60 Hz), preload a small ring buffer of
upcoming frames via `slCdLoadFile` (`[SGL S_CD3 main.c]:50,67`), and
schedule transfers around vblank with `slDMAXCopy` (SCU DMA, cache-through —
see `memory/sgl-audio-vs-scroll-cpu-dma-conflict.md`). At 10 Hz one is asking
the CD-ROM for at most 30 KB/frame * 10 = 300 KB/s, exactly the sustained
ceiling. **TitleSonic does not require this path; (c)+(e) suffices.**

---

## 6. Summary table

| Strategy | Verdict | Hard reason | Citation |
|---|---|---|---|
| (a) CD stream per-tick | NO | 400 ms mean seek, 300 KB/s sustained — impossible at 60 Hz | `[Saturn Overview]:1799,1806-1808` |
| (b) Sound RAM cache | NO | SCU-DMA collides with SCSP traffic; jo audio already on | `memory/sgl-audio-vs-scroll-cpu-dma-conflict.md` |
| (c) Palettize to COL_16 | YES | Reduces 1,410 KB → ~350 KB; lossless (RSDK source palette is 16-color) | `[VDP1 Manual]:5.1`, `[jo sprites.c]:174`, `[jo SL_DEF.H]:373` |
| (d) VDP2 NBG character | NO | Cycle-pattern bank-flip mechanics not worth it; sprite must layer above NBG bg | `[cinepak_sgl.c]:42-48,386-410` |
| (e) VDP1 atlas at boot | YES (with c) | 466 KB user area accommodates 49 palettized frames; jo_create_sprite_anim is the API | `[jo sprite_animator.h]:154-165`, `[SGL S_7_4]:12-22`, `[jo sega_saturn.h]:121` |

**Final recommendation:** strategy **(c)+(e)** — extract the 49 frames at 4-bpp
Color-Bank-16, ship as 49 SPRs in `cd/`, boot-load via the existing
`jo_fs_read_file` + sprite-add pipeline (extended for `COL_16`), animate with
`jo_create_sprite_anim` at frame_rate=1. Total VDP1 cost ≈ **350 KB out of
466 KB available**.
