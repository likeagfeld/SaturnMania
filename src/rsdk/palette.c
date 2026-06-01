/* Phase 1.1 — Palette module (Saturn implementation).
 *
 * Port reference (NOT reproduced verbatim — re-implemented per the API
 * contract in the header):
 *   tools/_decomp_raw/_RSDKv5_Graphics_Palette.hpp:50-105   accessors
 *   tools/_decomp_raw/_RSDKv5_Graphics_Palette.cpp:79-103   fade blend
 *
 * Saturn-side: in-RAM mirror only; CRAM upload is wired in Phase 1.2.
 * Rationale: Phase 1.1 ships the engine API surface — the per-frame
 * mirror -> CRAM DMA needs the V-blank callback in main.c which gets
 * touched when Title rendering lands. Until then, callers (the title
 * StaticUpdate calls scheduled to fire under Phase 1.2) operate on the
 * mirror and the dirty-mask grows; consumer (V-blank) drains it. */

#include "palette.h"
#include <string.h>

uint16_t g_rsdk_palette[RSDK_PALETTE_BANK_COUNT][RSDK_PALETTE_BANK_SIZE];
uint8_t  g_rsdk_active_palette[224];
int32_t  g_rsdk_palette_mask = 0;

/* Per-bank dirty range: tracks the lowest dirty index + highest dirty
 * index since last drain. Drain returns one range per dirty bank then
 * resets. */
static struct {
    uint16_t lo;        /* 0x100 == nothing dirty */
    uint16_t hi;        /* 0xFFFF when nothing dirty */
} s_dirty[RSDK_PALETTE_BANK_COUNT];

void rsdk_palette_init(void)
{
    memset(g_rsdk_palette, 0, sizeof(g_rsdk_palette));
    memset(g_rsdk_active_palette, 0, sizeof(g_rsdk_active_palette));
    g_rsdk_palette_mask = 0;
    for (int i = 0; i < RSDK_PALETTE_BANK_COUNT; ++i) {
        s_dirty[i].lo = 0x100;
        s_dirty[i].hi = 0;
    }
}

void rsdk_palette_mark_dirty(uint8_t bank, uint8_t start, uint8_t end)
{
    if (bank >= RSDK_PALETTE_BANK_COUNT) return;
    if (start > end) { uint8_t t = start; start = end; end = t; }
    if (start < s_dirty[bank].lo) s_dirty[bank].lo = start;
    if (end   > s_dirty[bank].hi) s_dirty[bank].hi = end;
}

int rsdk_palette_consume_dirty(rsdk_palette_range_t *out_ranges, int max_ranges)
{
    int n = 0;
    for (int b = 0; b < RSDK_PALETTE_BANK_COUNT && n < max_ranges; ++b) {
        if (s_dirty[b].lo <= s_dirty[b].hi && s_dirty[b].lo < 0x100) {
            out_ranges[n].bank  = (uint8_t)b;
            out_ranges[n].start = (uint8_t)s_dirty[b].lo;
            int count = (int)s_dirty[b].hi - (int)s_dirty[b].lo + 1;
            if (count > 0x100) count = 0x100;
            out_ranges[n].count = (uint8_t)count;
            ++n;
        }
        s_dirty[b].lo = 0x100;
        s_dirty[b].hi = 0;
    }
    return n;
}

/* RGB888 -> RGB565 pack helper (mirrors PACK_RGB888 macro from
 * Palette.hpp:36-38). */
static inline uint16_t rgb888_to_565(uint32_t c)
{
    uint8_t r = (uint8_t)((c >> 16) & 0xFF);
    uint8_t g = (uint8_t)((c >>  8) & 0xFF);
    uint8_t b = (uint8_t)( c        & 0xFF);
    return (uint16_t)((b >> 3) | ((g >> 2) << 5) | ((r >> 3) << 11));
}

/* RGB565 -> RGB888 (mirrors GetPaletteEntry pack-out at Palette.hpp:50-61). */
static inline uint32_t rgb565_to_888(uint16_t c)
{
    uint32_t R = ((uint32_t)c & 0xF800u) << 8;
    uint32_t G = ((uint32_t)c & 0x07E0u) << 5;
    uint32_t B = ((uint32_t)c & 0x001Fu) << 3;
    return R | G | B;
}

void rsdk_set_active_palette(uint8_t bank, int32_t y_start, int32_t y_end)
{
    if (bank >= RSDK_PALETTE_BANK_COUNT) return;
    if (y_start < 0) y_start = 0;
    if (y_end > 224) y_end = 224;
    for (int32_t l = y_start; l < y_end; ++l)
        g_rsdk_active_palette[l] = bank;
}

uint32_t rsdk_get_palette_entry(uint8_t bank, uint8_t index)
{
    return rgb565_to_888(g_rsdk_palette[bank & 7][index]);
}

void rsdk_set_palette_entry(uint8_t bank, uint8_t index, uint32_t rgb888)
{
    if (bank >= RSDK_PALETTE_BANK_COUNT) return;
    g_rsdk_palette[bank][index] = rgb888_to_565(rgb888);
    rsdk_palette_mark_dirty(bank, index, index);
}

void rsdk_set_palette_mask(uint32_t rgb888)
{
    g_rsdk_palette_mask = (int32_t)rgb888_to_565(rgb888);
}

void rsdk_copy_palette(uint8_t src_bank, uint8_t src_start,
                       uint8_t dst_bank, uint8_t dst_start,
                       uint8_t count)
{
    if (src_bank >= RSDK_PALETTE_BANK_COUNT) return;
    if (dst_bank >= RSDK_PALETTE_BANK_COUNT) return;
    for (int i = 0; i < count; ++i) {
        int s = src_start + i;
        int d = dst_start + i;
        if (s >= RSDK_PALETTE_BANK_SIZE || d >= RSDK_PALETTE_BANK_SIZE) break;
        g_rsdk_palette[dst_bank][d] = g_rsdk_palette[src_bank][s];
    }
    if (count > 0) rsdk_palette_mark_dirty(dst_bank, dst_start,
                                           (uint8_t)(dst_start + count - 1));
}

void rsdk_rotate_palette(uint8_t bank, uint8_t start_index,
                         uint8_t end_index, bool right)
{
    if (bank >= RSDK_PALETTE_BANK_COUNT) return;
    if (end_index <= start_index) return;
    uint16_t *p = g_rsdk_palette[bank];
    if (right) {
        /* Rotate toward higher index: last entry wraps to start. */
        uint16_t saved = p[end_index];
        for (int i = end_index; i > start_index; --i) p[i] = p[i - 1];
        p[start_index] = saved;
    } else {
        /* Rotate toward lower index: first entry wraps to end. */
        uint16_t saved = p[start_index];
        for (int i = start_index; i < end_index; ++i) p[i] = p[i + 1];
        p[end_index] = saved;
    }
    rsdk_palette_mark_dirty(bank, start_index, end_index);
}

void rsdk_set_limited_fade(uint8_t dst_bank, uint8_t src_bank_a,
                           uint8_t src_bank_b, int16_t blend_amount,
                           int32_t start_index, int32_t end_index)
{
    /* Implements the SetPaletteFade algorithm from Palette.cpp:79-103:
     * for each color index in [start..end], lerp between bankA and bankB
     * by blend_amount/255, storing the result in dst_bank. */
    if (dst_bank >= RSDK_PALETTE_BANK_COUNT) return;
    if (src_bank_a >= RSDK_PALETTE_BANK_COUNT) return;
    if (src_bank_b >= RSDK_PALETTE_BANK_COUNT) return;
    if (blend_amount < 0)   blend_amount = 0;
    if (blend_amount > 255) blend_amount = 255;
    if (end_index > RSDK_PALETTE_BANK_SIZE) end_index = RSDK_PALETTE_BANK_SIZE;
    if (start_index >= end_index) return;

    uint32_t blend_b = (uint32_t)blend_amount;
    uint32_t blend_a = 255u - blend_b;

    for (int32_t i = start_index; i <= end_index && i < RSDK_PALETTE_BANK_SIZE; ++i) {
        uint32_t a = rsdk_get_palette_entry(src_bank_a, (uint8_t)i);
        uint32_t b = rsdk_get_palette_entry(src_bank_b, (uint8_t)i);
        uint32_t r = blend_b * ((b >> 16) & 0xFF) + blend_a * ((a >> 16) & 0xFF);
        uint32_t g = blend_b * ((b >>  8) & 0xFF) + blend_a * ((a >>  8) & 0xFF);
        uint32_t bl= blend_b * ( b        & 0xFF) + blend_a * ( a        & 0xFF);
        uint32_t packed = ((r >> 8) << 16) | ((g >> 8) << 8) | (bl >> 8);
        g_rsdk_palette[dst_bank][i] = rgb888_to_565(packed);
    }
    rsdk_palette_mark_dirty(dst_bank, (uint8_t)start_index,
                            (uint8_t)(end_index >= RSDK_PALETTE_BANK_SIZE
                                      ? RSDK_PALETTE_BANK_SIZE - 1
                                      : end_index));
}
