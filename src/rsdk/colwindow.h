#ifndef RSDK_COLWINDOW_H
#define RSDK_COLWINDOW_H

/* Task #180 step 3d - toroidal column-window collision streamer.
 *
 * C mirror of tools/qa_ghz_colwindow_gate.py ColWindow (the host-verified
 * reference). The decomp keeps the entire collision TileLayer resident;
 * Saturn cannot (GHZ1 = 1024x128 tiles x 2 layers x 2 B = 512 KB, far past
 * the LWRAM carve). We keep only a W-column, full-height window resident in
 * LWRAM and slide it as the camera advances.
 *
 * cd/GHZ1COL.BIN is stored COLUMN-MAJOR ('GCO2') so one world column is a
 * single contiguous ysize*2-byte run -> a fresh column streams into its wrap
 * slot (worldCol % W) as ONE contiguous CD copy.
 *
 * The window-fetch contract (mirrored by src/rsdk/collision.c s_layer_tile):
 *     resident band  = [base, base + W)             (base = leftmost world col)
 *     slot(col)      = col % W                       (toroidal wrap)
 *     fetch(col,row) = 0xFFFF if col < base or col >= base+W
 *                      else win[slot(col)*ysize + row]
 *     advance(base'): for col in [old_base+W, base'+W): stream col -> slot(col)
 *
 * The window buffer (slot-major: win[slot*ysize + row]) and the sector
 * staging scratch live in the 64 KB LWRAM carve at 0x00200000 shared with the
 * GHZ1MASK.BIN compact mask/info blob (which occupies the low ~33 KB). FG.TMP
 * starts at 0x00210000, so the carve must stay under 64 KB.
 *
 * GFS handle is opened once at scene load (colwindow_open) and held for the
 * scene; each column stream is a held-handle GFS_Seek + GFS_Fread sub-sector
 * read mirroring src/mania/Objects/Title/TitleAssets.c:444-456.
 */

#include <stdint.h>
#include <stdbool.h>

/* WINDOW_W / VIS_TILES / LEFT_MARGIN mirror tools/qa_ghz_colwindow_gate.py.
 * 48 cols x 128 rows x 2 layers x 2 B = 24576 B resident. */
#define COLWINDOW_W           48
#define COLWINDOW_VIS_TILES   20
#define COLWINDOW_LEFT_MARGIN 8
#define COLWINDOW_MAX_LAYERS  2   /* GHZ collision planes A/B               */
#define COLWINDOW_MAX_YSIZE   128 /* GHZ1 ysize; carve sized for this max   */
#define COLWINDOW_OUT_OF_BAND 0xFFFF

/* Open cd/GHZ1COL.BIN ('GCO2'), validate the header, hold the GFS handle for
 * the scene, and point each layer's window into the LWRAM carve. Does NOT
 * fill the band - call colwindow_ensure_band() once after open for the
 * initial fill. Returns true on success. */
bool colwindow_open(const char *iso9660_name);

/* Close the held GFS handle (safe when not open). */
void colwindow_close(void);

/* True once colwindow_open succeeded. */
bool colwindow_is_open(void);

/* Geometry accessors (valid after a successful open). */
int32_t colwindow_layer_count(void);
int32_t colwindow_xsize(void);
int32_t colwindow_ysize(void);
uint8_t colwindow_width_shift(void);
int32_t colwindow_winW(void);
int32_t colwindow_base(void);

/* Per-layer window buffer base (slot-major: win[slot*ysize + row]). NULL if
 * li is out of range or the streamer is not open. This pointer is passed to
 * rsdk_collision_set_layer_window(). */
const uint16_t *colwindow_layer_win(int li);

/* Advance the resident band to [base, base+W), streaming ONLY the
 * newly-entered columns from CD (the initial call streams the whole band).
 * Clamps base to [0, xsize-W]. Returns the clamped base actually applied. */
int32_t colwindow_ensure_band(int32_t base);

#endif /* RSDK_COLWINDOW_H */
