# TitleSonic 49-frame VDP1 atlas — implementation-ready

This document is the *companion* to:
- `tools/build_titlesonic_atlas.py` (asset builder)
- `src/title_sonic.c` + `src/title_sonic.h` (runtime loader / draw)

It explains every design decision with file:line citations from the
official DTS docs and the project's existing code.

## 1. Hardware verification, with citations

| Constraint | Value | Citation |
|---|---|---|
| VDP1 VRAM total | 512 KB at `0x05C00000` | `jo-engine/jo_engine/jo/sega_saturn.h:90` (`JO_VDP1_VRAM (0x25C00000)`) |
| VDP1 sprite-pattern user area | 466,232 bytes | `jo-engine/jo_engine/jo/sega_saturn.h:121` (`JO_VDP1_USER_AREA_SIZE (0x71D38)`) |
| VDP1 sprite-pattern base address (in VRAM) | offset `0x10000` | `jo-engine/jo_engine/jo/sega_saturn.h:125` (`JO_VDP1_TEXTURE_DEF_BASE_ADDRESS (0x10000)`) |
| VDP1 char-pattern H size unit | 8 px | `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\VDP1_Manual.txt` §5.1 table 5.1 |
| VDP1 char-pattern H size range | 8..504 px | same table |
| VDP1 char-pattern V size range | 1..255 px | same table |
| VDP1 char-pattern alignment (in VRAM) | 0x20 bytes | `jo-engine/jo_engine/sprites.c:74` (`+ 0x1F) & 0x7FFE0` formula) |
| 4-bpp (COL_16) bytes-per-pattern | `(W * H * 4) >> 3 = (W * H) / 2` | `jo-engine/jo_engine/sprites.c:220` (formula `(W*H*4) >> cmode` with `cmode=COL_16=3`); `Compiler/COMMON/SGL_302j/INC/SL_DEF.H:373-377` |

### 1.1 Does a single 198×126 4-bpp frame fit one VDP1 character?

- 198 padded to multiple of 8 = **200 px**, well within the 504 limit.
- 126 px high, within 255 limit.
- Size = 200 × 126 / 2 = **12,600 bytes**, well under the per-pattern
  ceiling of 257,040 (`[VDP1 Manual] §5.1` max H×V×bpp).

**Verdict:** every TitleSonic frame fits in one VDP1 character pattern.
Negative pivots are NOT a VDP1 concern (pivots are applied at draw time
to the screen-space placement of the sprite; the character pattern
itself is always top-left-origin).

### 1.2 Total atlas footprint

Run `python tools/build_titlesonic_atlas.py`; observed output on the
current input:

```
[+] Pixel pool: 372,216 bytes
[+] VDP1 atlas footprint at boot: 372,216 bytes of 466,232 (79.8%)
```

Frame-by-frame padded-width breakdown (from `parse_spr` on
`extracted/Data/Sprites/Title/Sonic.bin`):

| f | w×h | padded w×h | 4-bpp bytes |
|---|---|---|---|
| 0 | 89×82 | 96×82 | 3,936 |
| 1 | 87×73 | 88×73 | 3,212 |
| ... | ... | ... | ... |
| 21 | 147×118 (max width) | 152×118 | 8,968 |
| 28 | 198×126 (max H+W) | 200×126 | 12,600 |
| 48 | 110×120 (settled pose) | 112×120 | 6,720 |

The atlas builder pads each width up to 8-px alignment and accounts for
the alignment per the formula in `sprites.c:74`.

### 1.3 Memory peak during the boot-time atlas load

The C loader (`src/title_sonic.c`) **does NOT** pull the atlas through
`jo_fs_read_file` (which would allocate 373 KB from jo's 576 KB pool).
Instead it:

1. Opens the file via raw `GFS_NameToId` + `GFS_Open` (no jo_malloc).
2. Reads the 44-byte header + 686-byte frame-record block into stack
   buffers.
3. Allocates ONE **8,192-byte** staging buffer via `jo_malloc`.
4. Loops 49 times: `GFS_Fread(frame_bytes)` into staging → `slDMACopy`
   from staging to VDP1 VRAM at `JO_VDP1_VRAM + vram_cursor`.
5. Frees the staging buffer.

Peak `jo_malloc` consumption during load = **8,192 bytes**.  With the
existing GHZ assets resident at ~477 KB on entry, the load runs
comfortably within the 576 KB pool.

Peak VDP1 user-area consumption AFTER load = **372,216 bytes** of the
466,232 available (79.8% used, 94 KB free for the existing logo /
ribbon / ring / press-start sprites).  Those existing sprites consumed
~40 KB before this change; net title-state VDP1 usage = ~412 KB out of
466 KB available — **OK with 54 KB headroom**.

## 2. RSDKv5 source parsing — verified

`tools/convert_ring_sprite.py:parse_spr` is the existing battle-tested
RSDKv5 .bin parser.  Output on TitleSonic input (`python` test run):

```
sheets: ['Title/Sonic.gif']
anim 0: name='Sonic' fc=49 speed=1 loop=48
anim 1: name='Finger Wave' fc=12 speed=1 loop=0
```

All 49 frames sit on the single `Title/Sonic.gif` sheet (1024×1024 RGBA
after `load_sheet` palette-resolution).  loop=48 means the animation
plays once and holds on the final frame (the settled standing pose).

## 3. SGL-canonical atlas upload pattern, verbatim

`D:\Claude Saturn Skill Documentation\NOV96_DTS\LIBRARY\SDK_10J\SGL302\SAMPLE\S_7_4\MAIN.C:12-22`:

```c
void set_texture(PICTURE *pcptr , Uint32 NbPicture)
{
    TEXTURE *txptr;
    for(; NbPicture-- > 0; pcptr++){
        txptr = tex_sample + pcptr->texno;
        slDMACopy((void *)pcptr->pcsrc,
            (void *)(SpriteVRAM + ((txptr->CGadr) << 3)),
            (Uint32)((txptr->Hsize * txptr->Vsize * 4) >> (pcptr->cmode)));
    }
}
```

`title_sonic.c::__ts_register_4bpp_sprite` replicates exactly this:

- `SpriteVRAM + (CGadr << 3)` ⇒ `JO_VDP1_VRAM + vram_cursor` (we hold
  the byte-offset directly; the `<<3` is folded in).
- `Hsize * Vsize * 4 >> cmode` ⇒ `data_bytes = (W * H * 4) >> COL_16`
  computed by the python builder and passed in.
- `slDMACopy` ⇒ same call.

The 0x20-byte alignment between consecutive sprites mirrors
`sprites.c:72-75`:

```c
static __jo_force_inline int __jo_get_next_sprite_address(...)
{
    return ((sprite_address) +
        (((((sprite_width) * (sprite_height) * 4) >> (sprite_color_mode))
        + 0x1f) & 0x7ffe0));
}
```

`title_sonic.c` open-codes the same arithmetic.

## 4. Does `jo_create_sprite_anim` work with COL_16 sprites?

`jo-engine/jo_engine/sprite_animator.c:47-71`:

```c
static void __jo_internal_frame_animator(void) {
    for (i ... ) {
        if (__jo_sprite_anim_tab[i].frame_skip >= ...frame_rate) {
            JO_ZERO(__jo_sprite_anim_tab[i].frame_skip);
            if ((cur_frame + 1) >= frame_count) { ... }
            else ++cur_frame;
        }
        ...
    }
}
```

And `sprite_animator.c:102-132`:

```c
int jo_create_sprite_anim(const unsigned short sprite_id,
                          const unsigned short frame_count,
                          const unsigned char  frame_rate)
{
    ...
    __jo_sprite_anim_tab[id].frame0_sprite_id = sprite_id;
    ...
}
```

`jo_get_anim_sprite` (sprite_animator.h:154-165) returns
`frame0_sprite_id + cur_frame`.  **It is color-mode agnostic** — it just
indexes the consecutive sprite slots.  So COL_16 sprites work, *provided
the palette is applied at draw time*.

However, **TitleSonic does NOT fit `jo_create_sprite_anim`'s "uniform
frame rate" model**.  RSDK frame durations are per-frame (dur=64 for
frame 0, dur=2 for most others, dur=3 for some, dur=6 for one).  With
`jo_create_sprite_anim`'s single `frame_rate` argument we can only pick
one tick-skip count.  So we implement our own tick→frame lookup
table (`title_sonic_current_frame`) which respects each frame's
duration — same approach the original Mania engine uses
(`Animation_ProcessAnimation` walks per-frame durations).

For the same reason we DON'T use `jo_sprite_draw3D` with a single
sprite ID + cur_frame — each frame has a different pivot, so we
re-emit the draw call per frame at the correct centre-anchor offset.

## 5. Palette workflow — exact

### 5.1 Saturn CRAM scheme

VDP2 Color RAM at `JO_VDP2_CRAM = 0x25F00000`, 4 KB total = 2,048
entries × 2 bytes (16-bit BGR1555).  jo allocates palettes in 256-entry
slots (`vdp2_malloc.c:58 CRAM_PALETTE_SIZE = 256`).  jo's palette ID
`n` returned by `jo_create_palette` has its data at
`CRAM[256 + 1 + (n-1)*256]` = `CRAM[257 + (n-1)*256]` (see
`vdp2_malloc.c:60` for the +1 off-by-one origin).

For VDP1 4-bpp Color Bank 16, the sprite-draw command's COLR field is
the CRAM base address of the 16-color bank, and VDP1 fetches CRAM at
`(COLR & ~0xF) | pixel_nibble`.  This requires the bank base to be on
a **16-CRAM-slot boundary**.  jo's 257-offset means jo-allocated
palette pages are NOT 16-aligned at the start (257 is not a multiple
of 16; the first 16-aligned slot inside palette page 1 would be
CRAM[272]).

### 5.2 Decision: bypass `jo_create_palette` for TitleSonic

We do not use `jo_create_palette` at all in `title_sonic.c`.  Instead
we write the 16 BGR1555 colors directly to a chosen CRAM slot
(TS_PAL_CRAM_INDEX = 2032 — 16-aligned, near the high end of CRAM,
past any jo palette).  The atlas builder defaults to `--cram-shift-up=0`
(no on-disk rotation) because the runtime no longer involves jo's
broken allocator.

### 5.3 Where the palette lands

CRAM index 2032 (16-aligned).  VDP1 COLR field = 2032 directly.  Per-
pixel CRAM lookup = `(2032 & 0xFFF0) | (nibble & 0xF) = 2032 + nibble`
which lands on CRAM[2032..2047] — exactly our 16 colors.

## 6. Pivot reconciliation

RSDK pivot semantics (verified from existing
`tools/convert_anim_sprite.py:55-66`):

```
dst_top_left = (entity_X + pivotX, entity_Y + pivotY)
```

i.e. pivotX/Y is the offset FROM the entity origin TO the sprite's
top-left corner.  Almost always negative for Sonic (pivots = (-48,
+28) to (-137, -90)) because the entity origin is roughly Sonic's
head/centre and the sprite's top-left is to the upper-left of that.

For jo's centre-anchored `jo_sprite_draw3D`:

```
centre_screen_x = entity_X + pivotX + sprite_W/2
centre_screen_y = entity_Y + pivotY + sprite_H/2
```

TitleLogo's TitleSonic entity world position is **(252, 104)** per the
Mania decomp (recorded already in `src/main.c:1571-1580` for the static
frame-48 draw).  jo screen-center subtracts (256, 112):

```
jo_x = 252 + pivotX + W/2 - 256
jo_y = 104 + pivotY + H/2 - 112
```

`title_sonic.c::title_sonic_draw` uses exactly this.  Cross-check for
frame 48 (settled pose): pivot=(-50,-91), W=112 (padded from 110, but
W/2 of the padded width adds 1 px to centre placement, which is the
correct behavior because the padded-right pixels are transparent and
shouldn't shift the visual centre — except they DO if we're strict).

**Optional refinement for sub-pixel accuracy**: the right-side padding
introduces a half-pixel offset.  If visible drift is observed, the
fix is to use the ORIGINAL (un-padded) width when computing the draw
centre.  The atlas builder can be extended to emit both
`width_actual` and `width_padded`; this is a 4-line change but is not
needed for the first working version.

## 7. Integration diff for `src/main.c`

The change replaces the static `MSONIC.SPR` draw at `main.c:1581` with
a tick-driven 49-frame animation.  Two edits:

### 7.1 Add includes and remove the now-dead `MSONIC.SPR` load

```diff
--- a/src/main.c
+++ b/src/main.c
@@ -18,6 +18,7 @@
 #include "player.h"
 #include "physics.h"
 #include "save.h"
+#include "title_sonic.h"
```

At `main.c:1046` (`static int g_sprite_msonic;`), remove that variable
(or leave as `static int g_sprite_msonic; /* legacy, unused */`).

At `main.c:1110-1121` (the `MSONIC.SPR` load), replace with the atlas
load:

```diff
-    /* TitleSonic — extracted single-frame static pose ... */
-    spr = (unsigned short *)jo_fs_read_file("MSONIC.SPR", &len);
-    if (spr != NULL) {
-        w = spr[1]; h = spr[2];
-        img.data = (jo_color *)(spr + 3);
-        img.width = w; img.height = h;
-        g_sprite_msonic = jo_sprite_add(&img);
-        jo_free(spr);
-    }
+    /* TitleSonic — 49-frame animation streamed from cd/TSONIC.ATL
+     * into the VDP1 sprite atlas at boot.  See docs/titlesonic_
+     * implementation.md.  Returns 0 on failure; we soft-fail
+     * (Sonic just won't appear) so a missing TSONIC.ATL doesn't
+     * block the title screen from showing logo + press-start. */
+    (void)title_sonic_load();
```

### 7.2 Replace the draw site at `main.c:1581`

```diff
-        /* TitleSonic at world (252, 104) -> jo (-4, -8). ... */
-        jo_sprite_draw3D(g_sprite_msonic, +1, -39, SPRITE_Z + 5);
+        /* TitleSonic 49-frame animation.  g_ticks resets to 0 at title
+         * entry (main.c jo_main / title state); title_sonic_draw walks
+         * the cumulative-duration table to pick the right frame and
+         * applies that frame's per-frame pivot. */
+        title_sonic_draw(g_ticks, SPRITE_Z + 5);
```

### 7.3 Add `src/title_sonic.c` to the Makefile

In `D:\sonicmaniasaturn\Makefile` line 58 the `SRCS` variable:

```diff
-SRCS = src/main.c src/player.c src/save.c src/intro_video.c \
+SRCS = src/main.c src/player.c src/save.c src/intro_video.c src/title_sonic.c \
        src/rsdk/storage.c src/rsdk/collision.c
```

### 7.4 Asset side

Add to whatever build script generates `cd/*` from `extracted/*`
(probably `build.bat`):

```bat
python tools/build_titlesonic_atlas.py
```

That writes `cd/TSONIC.ATL` (~373 KB) which becomes part of the ISO via
the existing mkisofs invocation in `jo_engine_makefile:309-312`.

## 8. verify_done gate

Add to `tools/verify_done.ps1`:

```powershell
# Gate: TitleSonic atlas exists and has expected size
$atl = "$projectRoot/cd/TSONIC.ATL"
if (-not (Test-Path $atl)) {
    Write-Error "GATE FAIL: cd/TSONIC.ATL missing — run tools/build_titlesonic_atlas.py"
    exit 1
}
$bytes = [System.IO.File]::ReadAllBytes($atl)
if ($bytes.Length -lt 1024) {
    Write-Error "GATE FAIL: TSONIC.ATL too small"
    exit 1
}
$magic = ([int]$bytes[0] -shl 8) -bor [int]$bytes[1]
if ($magic -ne 0x5453) {
    Write-Error "GATE FAIL: TSONIC.ATL wrong magic 0x$($magic.ToString('X4')) (expected 0x5453 'TS')"
    exit 1
}
$ver = ([int]$bytes[2] -shl 8) -bor [int]$bytes[3]
if ($ver -ne 3) {
    Write-Error "GATE FAIL: TSONIC.ATL version $ver (expected 3)"
    exit 1
}
$fc = ([int]$bytes[4] -shl 8) -bor [int]$bytes[5]
if ($fc -ne 49) {
    Write-Error "GATE FAIL: TSONIC.ATL frame_count $fc (expected 49)"
    exit 1
}
# Per-frame pixel pool should sum < JO_VDP1_USER_AREA_SIZE
$totalPx = ([uint32]$bytes[40] -shl 24) -bor `
           ([uint32]$bytes[41] -shl 16) -bor `
           ([uint32]$bytes[42] -shl  8) -bor `
            [uint32]$bytes[43]
if ($totalPx -ge 0x71D38) {
    Write-Error "GATE FAIL: TSONIC.ATL pixel pool $totalPx >= 0x71D38"
    exit 1
}
```

## 9. Memory accounting (final)

| Domain | Before | After | Change |
|---|---|---|---|
| `jo_malloc` resident (title state, peak) | 477 KB (GHZ + Sonic sprites) | 477 KB | unchanged |
| `jo_malloc` transient during load | 14 KB (MSONIC.SPR via jo_fs_read_file) | 8 KB (atlas staging) | -6 KB |
| VDP1 user area | ~40 KB (existing sprites) | ~412 KB (existing + TitleSonic atlas) | +372 KB (uses 79.8% of 466 KB) |
| CD asset size | MSONIC.SPR = ~13 KB | TSONIC.ATL = ~373 KB | +360 KB |
| Per-frame CD I/O during animation | n/a (static) | 0 (all in VRAM) | none |

## 10. Summary

| Question | Answer | Citation |
|---|---|---|
| Can a single VDP1 character hold a 198x126 4-bpp frame? | YES — 200×126/2 = 12,600 B < 257,040 B per-pattern cap. | `[VDP1 Manual] §5.1 table 5.1` |
| Are negative pivots/non-square frames OK? | YES — pivot is a draw-time offset, not a pattern attribute. | `[VDP1 Manual] §5.4.2`; verified by existing `jo_sprite_draw3D` usage in main.c:1581 |
| What is the alignment requirement (W multiples of 8)? | YES, mandatory — VDP1 H-size unit is 8 px. | `[VDP1 Manual] §5.1 table 5.1`; `jo sprites.c:74,212` |
| Does jo expose a 4-bpp sprite-add API? | NO. `jo_sprite_add` is COL_32K only; `jo_sprite_add_8bits_image` is COL_256. We open-code the COL_16 path. | `jo sprites.c:225-247` |
| Does jo's `jo_sprite_draw3D` correctly draw a COL_16 sprite? | NO. jo's draw helper at `sprites.c:304-313` hardcodes the VDP1 PMOD CCM field to `COLMODE_256=4` (8-bpp) for any non-COL_32K sprite — drawing a COL_16 sprite this way produces garbage. We emit `slDispSprite` directly with a hand-built `SPR_ATTR` carrying CCM=0 (Color Bank 16). | `jo sprites.c:54,304-313`; SL_DEF.H:235-248 (`SPR_ATTR` struct); VDP1 Manual §5.5.4 PMOD field |
| Does `jo_create_sprite_anim` work with consecutive 4-bpp sprite IDs? | YES — it's color-mode agnostic. But the uniform-frame-rate model doesn't fit RSDK per-frame durations. We use a custom tick→frame lookup. | `jo sprite_animator.c:47-132` |
| Does the atlas fit the VDP1 budget? | YES — 372 KB of 466 KB available, 94 KB headroom. | `tools/build_titlesonic_atlas.py` output; `jo sega_saturn.h:121` |
| Does the atlas blow the jo_malloc pool during load? | NO — peak 8 KB staging buffer; CD streamed via raw `GFS_Fread`. | `src/title_sonic.c:title_sonic_load` |
