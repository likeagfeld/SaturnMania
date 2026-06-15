/* Task #180 step 4c - RAM-resident compressed collision-layout streamer.
 *
 * Line-for-line C mirror of tools/qa_ghz_colwindow_gate.py ColWindow (the
 * window-fetch contract collision.c s_layer_tile reads). See colwindow.h for
 * the design rationale.
 *
 * WHY THIS WAS REWRITTEN (#180 step 4b -> 4c regression fix):
 *   step 4b streamed each collision column from CD on demand (GFS_Seek +
 *   GFS_Fread per column, every camera advance). On the single-head Saturn CD
 *   that contended with the GHZ CD-DA music track (Game.c jo_audio_play_cd_
 *   track(2,2,true)) and stalled the main loop -> the user-reported >10s
 *   title-card delay, no level music/SFX, super-slow gameplay, invisible
 *   HUD/title-card/player. The fix: ship the layout block-DEFLATE compressed
 *   (cd/GHZ1COL.BIN 'GCO3', ~59 KB, built by tools/build_collayout.py), load
 *   the WHOLE file ONCE into a resident LWRAM buffer at scene start, and
 *   decode columns RAM->RAM via the embedded puff inflater during play. ZERO
 *   CD access during gameplay. Lossless: tools/qa_ghz_cd_contention_gate.py P4
 *   round-trips every block back to the Scene1.bin column-major bytes.
 *
 * 'GCO3' format (all multi-byte fields big-endian; SH-2 is big-endian so a raw
 * memcpy of a decoded column run leaves the u16 entries native - NO swap):
 *   off 0  : magic 'GCO3'
 *   off 4  : u16 num_layers, u16 tile_size, u16 block_cols, u16 _reserved
 *   off 12 : per-layer descriptor (num_layers x 12 B)
 *              u16 layer_id, xsize, ysize, width_shift, num_blocks, _pad
 *   then   : per-layer block-index table, (num_blocks+1) x u32 BE absolute
 *            file offsets; block b payload = [off[b], off[b+1]).
 *   then   : raw-DEFLATE block payloads. Block b decompresses to
 *            cols_in_block*ysize u16 BE entries COLUMN-MAJOR (cols_in_block =
 *            block_cols, or the remainder for the last block).
 */

#include "colwindow.h"
#include "puff.h"

#include <string.h>  /* memcpy                                              */

/* One-shot pool-free GFS loader (storage.c). The ONLY path to the CD in this
 * TU, used exactly once per scene in colwindow_open. */
extern int rsdk_storage_load_to_lwram(const char *iso9660_name,
                                      void *dst, unsigned int max_bytes);

/* ===== LWRAM carve =======================================================
 *
 * | region            | addr       | size    | lifetime                     |
 * |-------------------|------------|---------|------------------------------|
 * | GHZ1MASK.BIN blob | 0x00200000 | 40960 B | scene (collision bind)       |
 * | window buffer     | 0x0020A000 | 24576 B | scene (2 layers x 48 x 128)  |
 * | resident GCO3     | 0x002C0000 | 61440 B | scene (whole compressed file)|
 * | block-decode buf  | 0x002CF000 |  4096 B | scratch (one 16-col block)   |
 *
 * #180 step 4c sizing (measured, see tools/qa_ghz_lwram_layout_gate.py):
 * cd/GHZ1MASK.BIN now covers the UNION of BOTH GHZ collision layers (FG Low
 * 217 + FG High 150 = 260 distinct base tiles) like the decomp, which grew it
 * 33452 -> 39644 B. The mask carve is therefore 0xA000 (40960 B) and the
 * window buffer slides up to 0x0020A000, ending exactly at FG.TMP (0x210000) -
 * leaving NO room for the decode scratch in the low carve. The 4 KB decode
 * scratch instead lives at the TAIL of the resident GCO3 region: the blob is
 * 60575 B, capped at 0xF000 (61440 B), and the scratch fills 0x002CF000..
 * 0x002D0000 (the player MRU pool ceiling). The resident region (0x2C0000..
 * 0x2D0000) is the 64 KB freed by shrinking SCENE_LWRAM_ARENA 256->192 KB
 * (storage.c SCENE_LWRAM_ARENA_SIZE; GHZ peaks ~41 KB -> 4.6x margin). No
 * region overlaps the FR-2 entity MRU pool (#189, 0x260000..0x290000), the
 * scene arena (0x290000..0x2C0000), the player MRU pool (0x2D0000+), or
 * FG.TMP. Max raw block = block_cols*MAX_YSIZE*2 = 16*128*2 = 4096 B. */
#define COLWINDOW_LWRAM_WIN_ADDR     ((uint16_t *)0x0020A000)
#define COLWINDOW_LWRAM_WIN_SIZE     0x6000u    /* 24576 B (2 x 48 x 128)     */
#define COLWINDOW_DECODE_ADDR        ((uint8_t  *)0x002CF000)
#define COLWINDOW_DECODE_SIZE        0x1000u    /* 4096 B (one decoded block) */
#define COLWINDOW_RESIDENT_ADDR      ((uint8_t  *)0x002C0000)
#define COLWINDOW_RESIDENT_SIZE      0xF000u    /* 61440 B (GCO3 file <= this)*/

typedef struct {
    const uint8_t *index;            /* (num_blocks+1) u32 BE offset table  */
    int32_t   num_blocks;
    int32_t   xsize;                 /* full level width  (tiles)           */
    int32_t   ysize;                 /* full level height (tiles)           */
    uint8_t   width_shift;
    uint16_t  layer_id;
    uint16_t *win;                   /* slot-major: win[slot*ysize + row]   */
    int32_t   loaded[COLWINDOW_W];   /* slot -> resident world col (-1 free) */
} colwindow_layer_t;

static bool              s_open    = false;
static int32_t           s_nlayers = 0;
static int32_t           s_winW    = COLWINDOW_W;
static int32_t           s_base    = 0;
static int32_t           s_block_cols = 16;
static colwindow_layer_t s_layers[COLWINDOW_MAX_LAYERS];

/* Which (layer,block) currently sits decoded in COLWINDOW_DECODE_ADDR. */
static int32_t           s_dec_layer = -1;
static int32_t           s_dec_block = -1;

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

/* Decode layer L's block b into COLWINDOW_DECODE_ADDR (RAM->RAM via puff),
 * unless it is already resident there. Returns false on a corrupt block. */
static bool colwindow_decode_block(int32_t li, colwindow_layer_t *L, int32_t b)
{
    if (s_dec_layer == li && s_dec_block == b) return true;
    if (b < 0 || b >= L->num_blocks) return false;

    uint32_t off0 = be32(L->index + b * 4);
    uint32_t off1 = be32(L->index + (b + 1) * 4);
    if (off1 <= off0 || off1 > COLWINDOW_RESIDENT_SIZE) return false;

    int32_t cols = L->xsize - b * s_block_cols;
    if (cols > s_block_cols) cols = s_block_cols;
    unsigned long destlen = (unsigned long)cols * (unsigned long)L->ysize * 2u;
    if (destlen > COLWINDOW_DECODE_SIZE) return false;

    unsigned long srclen = off1 - off0;
    int rc = puff(COLWINDOW_DECODE_ADDR, &destlen,
                  COLWINDOW_RESIDENT_ADDR + off0, &srclen);
    if (rc != 0) { s_dec_layer = -1; s_dec_block = -1; return false; }

    s_dec_layer = li;
    s_dec_block = b;
    return true;
}

/* Mirror of ColWindow.stream_column: place world column c into wrap slot
 * (c % W). Decodes the owning 16-col block (cached) then a single ysize*2-byte
 * RAM->RAM copy of the column slice. NO CD access. */
static void colwindow_stream_column(int32_t li, colwindow_layer_t *L, int32_t c)
{
    if (c < 0 || c >= L->xsize) return;

    int32_t b = c / s_block_cols;
    if (!colwindow_decode_block(li, L, b)) return;

    int32_t local    = c - b * s_block_cols;   /* column within block       */
    int32_t colbytes = L->ysize * 2;
    int32_t slot     = c % s_winW;

    memcpy(L->win + slot * L->ysize,
           COLWINDOW_DECODE_ADDR + local * colbytes, (size_t)colbytes);
    L->loaded[slot] = c;
}

bool colwindow_open(const char *iso9660_name)
{
    colwindow_close();
    if (!iso9660_name) return false;

    /* One-shot: read the WHOLE compressed file into the resident LWRAM blob.
     * This is the ONLY CD touch for the scene's lifetime. */
    int rd = rsdk_storage_load_to_lwram(iso9660_name,
                                        COLWINDOW_RESIDENT_ADDR,
                                        COLWINDOW_RESIDENT_SIZE);
    if (rd < 24) return false;

    const uint8_t *blob = COLWINDOW_RESIDENT_ADDR;
    if (blob[0] != 'G' || blob[1] != 'C' || blob[2] != 'O' || blob[3] != '3')
        return false;

    int32_t nlayers = (int32_t)be16(blob + 4);
    s_block_cols    = (int32_t)be16(blob + 8);
    if (nlayers <= 0 || nlayers > COLWINDOW_MAX_LAYERS) return false;
    if (s_block_cols <= 0) return false;

    int32_t desc_off  = 12;
    int32_t index_off = desc_off + nlayers * 12;
    int32_t win_words_used = 0;
    int32_t win_capacity   = (int32_t)(COLWINDOW_LWRAM_WIN_SIZE / 2);

    /* First pass: read descriptors, validate, sum block-index sizes. */
    int32_t idx_cursor = index_off;
    for (int32_t i = 0; i < nlayers; ++i) {
        const uint8_t *e = blob + desc_off + i * 12;
        colwindow_layer_t *L = &s_layers[i];
        L->layer_id    = be16(e + 0);
        L->xsize       = (int32_t)be16(e + 2);
        L->ysize       = (int32_t)be16(e + 4);
        L->width_shift = (uint8_t)be16(e + 6);
        L->num_blocks  = (int32_t)be16(e + 8);

        if (L->xsize <= s_winW || L->ysize <= 0 ||
            L->ysize > COLWINDOW_MAX_YSIZE || L->num_blocks <= 0)
            return false;
        /* worst-case decoded block must fit the 4 KB decode buffer */
        if ((int32_t)(s_block_cols * L->ysize * 2) > (int32_t)COLWINDOW_DECODE_SIZE)
            return false;

        L->index = blob + idx_cursor;
        idx_cursor += (L->num_blocks + 1) * 4;
        if (idx_cursor > rd) return false;       /* index table truncated   */

        int32_t layer_win_words = s_winW * L->ysize;
        if (win_words_used + layer_win_words > win_capacity) return false;
        L->win = COLWINDOW_LWRAM_WIN_ADDR + win_words_used;
        win_words_used += layer_win_words;

        for (int32_t s = 0; s < s_winW; ++s) L->loaded[s] = -1;
    }

    s_nlayers   = nlayers;
    s_base      = 0;
    s_dec_layer = -1;
    s_dec_block = -1;
    s_open      = true;
    return true;
}

void colwindow_close(void)
{
    s_open      = false;
    s_nlayers   = 0;
    s_base      = 0;
    s_dec_layer = -1;
    s_dec_block = -1;
}

bool colwindow_is_open(void) { return s_open; }

int32_t colwindow_layer_count(void) { return s_open ? s_nlayers : 0; }
int32_t colwindow_xsize(void)       { return s_open ? s_layers[0].xsize : 0; }
int32_t colwindow_ysize(void)       { return s_open ? s_layers[0].ysize : 0; }
uint8_t colwindow_width_shift(void) { return s_open ? s_layers[0].width_shift : 0; }
int32_t colwindow_winW(void)        { return s_winW; }
int32_t colwindow_base(void)        { return s_base; }

const uint16_t *colwindow_layer_win(int li)
{
    if (!s_open || li < 0 || li >= s_nlayers) return 0;
    return s_layers[li].win;
}

/* Mirror of ColWindow.ensure_band: clamp base to [0, xsize-W]; for each layer
 * stream every column in [base, base+W) whose wrap slot does not already hold
 * it (initial fill streams the whole band; a +1 advance streams 1 new col).
 * All streaming is now RAM->RAM block decode - no CD. */
int32_t colwindow_ensure_band(int32_t base)
{
    if (!s_open) return 0;

    int32_t xs = s_layers[0].xsize;
    if (base < 0) base = 0;
    if (base > xs - s_winW) base = xs - s_winW;

    for (int32_t li = 0; li < s_nlayers; ++li) {
        colwindow_layer_t *L = &s_layers[li];
        for (int32_t c = base; c < base + s_winW; ++c) {
            if (L->loaded[c % s_winW] != c)
                colwindow_stream_column(li, L, c);
        }
    }
    s_base = base;
    return base;
}
