/* Probe 3 (engine true-port feasibility spike, Task #194).
 *
 * Settles the ONE architecturally-decisive unmeasured question from Probe 2:
 * can the 28 MHz SH-2 software-rasterize Mania's scene per RSDK's pixel path,
 * or must the Saturn device backend translate RSDK draw calls to VDP1/VDP2
 * hardware (as the existing hand-port does)?
 *
 * Method: compile the VERBATIM RSDK software-raster inner loops on the real
 * sh-none-elf-gcc-8.2.0 (-O2 -m2), count the SH-2 instructions the compiler
 * actually emits for the per-pixel body, and apply SH7604 instruction timing.
 * No emulator, no guess -- the generated assembly IS the measurement.
 *
 * Faithful source: RSDK::DrawSpriteFlipped, Drawing.cpp:
 *   - FLIP_NONE / INK_NONE  : 2924-2938  (the dominant opaque-sprite path)
 *   - FLIP_NONE / INK_BLEND : 2940-2954  (the 50% blend path)
 * Types match RSDK: sprite pixels uint8 (palette index), framebuffer uint16
 * (RGB555), fullPalette uint16[PALETTE_COUNT][PALETTE_SIZE].
 *
 * Functions are extern (non-static) so -O2 cannot inline/elide the loop; the
 * emitted body is exactly what would run per pixel on hardware.
 */
#include <stdint.h>

typedef uint8_t  uint8;
typedef uint16_t uint16;
typedef int32_t  int32;

/* RSDK globals (Drawing.hpp): fullPalette[PALETTE_COUNT=8][PALETTE_SIZE=0x100]. */
uint16 fullPalette[8][256];

/* blendLookupTable[0x20*0x100] (Drawing.hpp) -- used by setPixelBlend. */
uint16 blendLookupTable[0x20 * 0x100];

/* setPixelBlend, Drawing.hpp:  the INK_BLEND 50% mix RSDK actually emits. */
static inline void setPixelBlend(uint16 srcColor, uint16 *dst)
{
    *dst = blendLookupTable[*dst] + blendLookupTable[0x20 * 0x20 + srcColor];
}

/* ---- VERBATIM RSDK INK_NONE hot path (Drawing.cpp:2924-2938) ------------- */
void blit_ink_none(uint16 *frameBuffer, uint8 *pixels, uint8 *lineBuffer,
                   int32 width, int32 height, int32 pitch, int32 gfxPitch)
{
    while (height--) {
        uint16 *activePalette = fullPalette[*lineBuffer];
        lineBuffer++;
        int32 w = width;
        while (w--) {
            if (*pixels > 0)
                *frameBuffer = activePalette[*pixels];
            ++pixels;
            ++frameBuffer;
        }
        frameBuffer += pitch;
        pixels += gfxPitch;
    }
}

/* ---- VERBATIM RSDK INK_BLEND hot path (Drawing.cpp:2940-2954) ------------ */
void blit_ink_blend(uint16 *frameBuffer, uint8 *pixels, uint8 *lineBuffer,
                    int32 width, int32 height, int32 pitch, int32 gfxPitch)
{
    while (height--) {
        uint16 *activePalette = fullPalette[*lineBuffer];
        lineBuffer++;
        int32 w = width;
        while (w--) {
            if (*pixels > 0)
                setPixelBlend(activePalette[*pixels], frameBuffer);
            ++pixels;
            ++frameBuffer;
        }
        frameBuffer += pitch;
        pixels += gfxPitch;
    }
}
