#ifndef RSDK_PALETTE_H
#define RSDK_PALETTE_H

/* Phase 1.1 — Palette module, Saturn port of RSDKv5/RSDK/Graphics/Palette.
 *
 * Source contracts (read but not reproduced):
 *   tools/_decomp_raw/_RSDKv5_Graphics_Palette.hpp:7-8     PALETTE_BANK_COUNT/SIZE
 *   tools/_decomp_raw/_RSDKv5_Graphics_Palette.hpp:44-48   SetActivePalette
 *   tools/_decomp_raw/_RSDKv5_Graphics_Palette.hpp:50-71   GetPaletteEntry,
 *                                                          SetPaletteEntry,
 *                                                          SetPaletteMask
 *   tools/_decomp_raw/_RSDKv5_Graphics_Palette.hpp:84-105  CopyPalette,
 *                                                          RotatePalette
 *   tools/_decomp_raw/_RSDKv5_Graphics_Palette.cpp:79-103  SetPaletteFade
 *                                                          (==SetLimitedFade)
 *
 * Upstream stores 8 palette banks of 256 entries each, each entry an
 * RGB565 uint16. On Saturn, CRAM (0x05F00000) holds 2048 16-bit entries
 * in MODE 0 (RGB555, 5-bit per channel + transparency bit). The Saturn
 * port:
 *   * Mirrors the 8x256 RGB565 layout in g_rsdk_palette (16 KB resident
 *     in Work RAM-H — well within budget).
 *   * On RotatePalette / SetPaletteEntry / SetLimitedFade we update the
 *     in-RAM mirror first, then schedule a CRAM upload via a dirty-bit
 *     mask. The actual CRAM DMA happens in V-blank (called from the
 *     existing fg_vblank pipeline in main.c — to be wired in Phase 1.2
 *     once Title-scene rendering lands).
 *   * RGB565 -> Saturn RGB555 conversion drops the bottom green bit
 *     (Mania uses 5-6-5; Saturn is 5-5-5 + alpha). Acceptable; matches
 *     the jo_create_palette_from convention already in use.
 *
 * Phase 1.1 deliverable: the in-RAM API surface is complete + matches
 * decomp call sites byte-for-byte. CRAM upload is staged but the
 * V-blank hook is wired in Phase 1.2 alongside the actual title sprite
 * draw pipeline (so palette + sprite arrive in sync).
 *
 * Decomp call-site witness (verified by Grep on tools/_decomp_raw/):
 *   * SonicMania_Objects_GHZ_GHZSetup.c:30-39     RotatePalette x4 +
 *                                                 SetLimitedFade x2
 *   * SonicMania_Objects_Title_TitleBG.c (~L160)  RotatePalette for water
 *   * SonicMania_Objects_Common_Palette.c         SetPaletteEntry by-name
 *   * (~244 total call sites in cached subset) */

#include <stdint.h>
#include <stdbool.h>

#define RSDK_PALETTE_BANK_COUNT 8
#define RSDK_PALETTE_BANK_SIZE  0x100   /* 256                            */

/* RGB565 mirror — same layout as upstream `fullPalette[][]`. */
extern uint16_t g_rsdk_palette[RSDK_PALETTE_BANK_COUNT][RSDK_PALETTE_BANK_SIZE];

/* Per-scanline active-bank map (mirrors upstream gfxLineBuffer).
 * Used by SetActivePalette to switch banks mid-screen. Saturn doesn't
 * have a native per-scanline palette switch — we emulate with the line-
 * color-screen path when this map is non-uniform; for the Title scene
 * it stays uniform (bank 0). */
extern uint8_t  g_rsdk_active_palette[224]; /* SCREEN_YSIZE                */

extern int32_t  g_rsdk_palette_mask;     /* RGB565 mask color (index 0)    */

/* === Public API ===================================================== */

/* Init: zero all banks + active-palette map. Idempotent. */
void rsdk_palette_init(void);

/* Bank assignment per scanline range (Palette.hpp:44-48). */
void rsdk_set_active_palette(uint8_t bank, int32_t y_start, int32_t y_end);

/* Direct CRAM-mirror reads/writes (Palette.hpp:50-71). The 24-bit color
 * argument is the upstream API's RGB888; conversion to RGB565 happens
 * internally. */
uint32_t rsdk_get_palette_entry(uint8_t bank, uint8_t index);
void     rsdk_set_palette_entry(uint8_t bank, uint8_t index, uint32_t rgb888);
void     rsdk_set_palette_mask (uint32_t rgb888);

/* Cross-bank block copy (Palette.hpp:84-91). */
void     rsdk_copy_palette(uint8_t src_bank, uint8_t src_start,
                           uint8_t dst_bank, uint8_t dst_start,
                           uint8_t count);

/* In-bank rotate (Palette.hpp:93-105). The `right` flag matches upstream:
 * true rotates entries toward higher indices; false toward lower. */
void     rsdk_rotate_palette(uint8_t bank, uint8_t start_index,
                             uint8_t end_index, bool right);

/* Cross-bank fade (Palette.cpp:79-103). Mania calls this as
 * "SetLimitedFade" — the same function, different export name. */
void     rsdk_set_limited_fade(uint8_t dst_bank, uint8_t src_bank_a,
                               uint8_t src_bank_b, int16_t blend_amount,
                               int32_t start_index, int32_t end_index);

/* Saturn-side helper: mark a bank range dirty for the next V-blank CRAM
 * upload. Callers don't usually invoke this directly — the API above
 * already calls it. Exposed for the V-blank consumer in main.c. */
void     rsdk_palette_mark_dirty(uint8_t bank, uint8_t start, uint8_t end);

/* Consumes the dirty marks + returns the (bank, start, count) ranges that
 * need uploading this V-blank. Returns count of ranges (0..8). out_ranges
 * is a caller-provided array of 8 entries minimum. */
typedef struct { uint8_t bank, start, count; } rsdk_palette_range_t;
int      rsdk_palette_consume_dirty(rsdk_palette_range_t *out_ranges, int max_ranges);

#endif /* RSDK_PALETTE_H */
