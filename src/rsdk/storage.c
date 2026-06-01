/* Phase A1 — Storage layer (Scene/StageConfig/SpriteAnim/TileConfig).
 *
 * Saturn port of RSDKv5/RSDK/Storage/. See src/rsdk/storage.h header for
 * the full design rationale and §section references into
 * docs/rsdkv5_engine_catalog.md.
 *
 * Implementation policy:
 *   * Reads pre-extracted asset files from CD via jo_fs_read_file. The
 *     offline tools/dump_data_rsdk.py converts Data.rsdk into per-file
 *     blobs under cd/, so the Saturn-side parser sees a flat filesystem.
 *   * Plain (non-compressed) sections are parsed in-place. Compressed
 *     sections (layer-layout zlib payload, TileConfig payload) are
 *     pre-extracted offline because Saturn doesn't ship a zlib inflater
 *     by default. Saturn loads the pre-inflated .raw versions instead.
 *   * Scene entity list IS parsed at runtime so per-entity coordinates
 *     drive sprite placement (per memory/title-positions-must-come-from-
 *     scene-data.md — no more hard-coded coords).
 *   * MD5 implementation is a self-contained 256-line public-domain
 *     RFC1321 port; no link dependency. */

#include "storage.h"

#include <jo/jo.h>      /* jo_fs_read_file, jo_malloc, jo_free, jo_printf */
#include <string.h>     /* memcpy, memcmp, memset, strlen, strncpy        */

/* ===== MD5 (RFC1321) — self-contained, public-domain style =========== */

typedef struct {
    uint32_t state[4];
    uint32_t count[2];
    uint8_t  buffer[64];
} md5_ctx_t;

static const uint8_t MD5_PADDING[64] = {0x80};

#define F(x,y,z) (((x) & (y)) | (~(x) & (z)))
#define G(x,y,z) (((x) & (z)) | ((y) & ~(z)))
#define H(x,y,z) ((x) ^ (y) ^ (z))
#define I(x,y,z) ((y) ^ ((x) | ~(z)))
#define ROL(x,n) (((x) << (n)) | ((x) >> (32 - (n))))
#define FF(a,b,c,d,x,s,ac) { (a) += F((b),(c),(d)) + (x) + (uint32_t)(ac); (a) = ROL((a),(s)); (a) += (b); }
#define GG(a,b,c,d,x,s,ac) { (a) += G((b),(c),(d)) + (x) + (uint32_t)(ac); (a) = ROL((a),(s)); (a) += (b); }
#define HH(a,b,c,d,x,s,ac) { (a) += H((b),(c),(d)) + (x) + (uint32_t)(ac); (a) = ROL((a),(s)); (a) += (b); }
#define II(a,b,c,d,x,s,ac) { (a) += I((b),(c),(d)) + (x) + (uint32_t)(ac); (a) = ROL((a),(s)); (a) += (b); }

static void md5_transform(uint32_t state[4], const uint8_t block[64])
{
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t x[16];
    int i, j;
    for (i = 0, j = 0; j < 64; ++i, j += 4) {
        x[i] = ((uint32_t)block[j]) | ((uint32_t)block[j+1] << 8)
             | ((uint32_t)block[j+2] << 16) | ((uint32_t)block[j+3] << 24);
    }
    FF(a,b,c,d,x[ 0], 7,0xD76AA478); FF(d,a,b,c,x[ 1],12,0xE8C7B756);
    FF(c,d,a,b,x[ 2],17,0x242070DB); FF(b,c,d,a,x[ 3],22,0xC1BDCEEE);
    FF(a,b,c,d,x[ 4], 7,0xF57C0FAF); FF(d,a,b,c,x[ 5],12,0x4787C62A);
    FF(c,d,a,b,x[ 6],17,0xA8304613); FF(b,c,d,a,x[ 7],22,0xFD469501);
    FF(a,b,c,d,x[ 8], 7,0x698098D8); FF(d,a,b,c,x[ 9],12,0x8B44F7AF);
    FF(c,d,a,b,x[10],17,0xFFFF5BB1); FF(b,c,d,a,x[11],22,0x895CD7BE);
    FF(a,b,c,d,x[12], 7,0x6B901122); FF(d,a,b,c,x[13],12,0xFD987193);
    FF(c,d,a,b,x[14],17,0xA679438E); FF(b,c,d,a,x[15],22,0x49B40821);
    GG(a,b,c,d,x[ 1], 5,0xF61E2562); GG(d,a,b,c,x[ 6], 9,0xC040B340);
    GG(c,d,a,b,x[11],14,0x265E5A51); GG(b,c,d,a,x[ 0],20,0xE9B6C7AA);
    GG(a,b,c,d,x[ 5], 5,0xD62F105D); GG(d,a,b,c,x[10], 9,0x02441453);
    GG(c,d,a,b,x[15],14,0xD8A1E681); GG(b,c,d,a,x[ 4],20,0xE7D3FBC8);
    GG(a,b,c,d,x[ 9], 5,0x21E1CDE6); GG(d,a,b,c,x[14], 9,0xC33707D6);
    GG(c,d,a,b,x[ 3],14,0xF4D50D87); GG(b,c,d,a,x[ 8],20,0x455A14ED);
    GG(a,b,c,d,x[13], 5,0xA9E3E905); GG(d,a,b,c,x[ 2], 9,0xFCEFA3F8);
    GG(c,d,a,b,x[ 7],14,0x676F02D9); GG(b,c,d,a,x[12],20,0x8D2A4C8A);
    HH(a,b,c,d,x[ 5], 4,0xFFFA3942); HH(d,a,b,c,x[ 8],11,0x8771F681);
    HH(c,d,a,b,x[11],16,0x6D9D6122); HH(b,c,d,a,x[14],23,0xFDE5380C);
    HH(a,b,c,d,x[ 1], 4,0xA4BEEA44); HH(d,a,b,c,x[ 4],11,0x4BDECFA9);
    HH(c,d,a,b,x[ 7],16,0xF6BB4B60); HH(b,c,d,a,x[10],23,0xBEBFBC70);
    HH(a,b,c,d,x[13], 4,0x289B7EC6); HH(d,a,b,c,x[ 0],11,0xEAA127FA);
    HH(c,d,a,b,x[ 3],16,0xD4EF3085); HH(b,c,d,a,x[ 6],23,0x04881D05);
    HH(a,b,c,d,x[ 9], 4,0xD9D4D039); HH(d,a,b,c,x[12],11,0xE6DB99E5);
    HH(c,d,a,b,x[15],16,0x1FA27CF8); HH(b,c,d,a,x[ 2],23,0xC4AC5665);
    II(a,b,c,d,x[ 0], 6,0xF4292244); II(d,a,b,c,x[ 7],10,0x432AFF97);
    II(c,d,a,b,x[14],15,0xAB9423A7); II(b,c,d,a,x[ 5],21,0xFC93A039);
    II(a,b,c,d,x[12], 6,0x655B59C3); II(d,a,b,c,x[ 3],10,0x8F0CCC92);
    II(c,d,a,b,x[10],15,0xFFEFF47D); II(b,c,d,a,x[ 1],21,0x85845DD1);
    II(a,b,c,d,x[ 8], 6,0x6FA87E4F); II(d,a,b,c,x[15],10,0xFE2CE6E0);
    II(c,d,a,b,x[ 6],15,0xA3014314); II(b,c,d,a,x[13],21,0x4E0811A1);
    II(a,b,c,d,x[ 4], 6,0xF7537E82); II(d,a,b,c,x[11],10,0xBD3AF235);
    II(c,d,a,b,x[ 2],15,0x2AD7D2BB); II(b,c,d,a,x[ 9],21,0xEB86D391);
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
}

static void md5_init(md5_ctx_t *ctx)
{
    ctx->count[0] = ctx->count[1] = 0;
    ctx->state[0] = 0x67452301; ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE; ctx->state[3] = 0x10325476;
}

static void md5_update(md5_ctx_t *ctx, const uint8_t *in, uint32_t len)
{
    uint32_t i, index, partLen;
    index = (uint32_t)((ctx->count[0] >> 3) & 0x3F);
    if ((ctx->count[0] += len << 3) < (len << 3)) ctx->count[1]++;
    ctx->count[1] += len >> 29;
    partLen = 64 - index;
    if (len >= partLen) {
        memcpy(&ctx->buffer[index], in, partLen);
        md5_transform(ctx->state, ctx->buffer);
        for (i = partLen; i + 63 < len; i += 64)
            md5_transform(ctx->state, &in[i]);
        index = 0;
    } else i = 0;
    memcpy(&ctx->buffer[index], &in[i], len - i);
}

static void md5_final(md5_ctx_t *ctx, uint8_t digest[16])
{
    uint8_t  bits[8];
    uint32_t index, padLen;
    int i;
    for (i = 0; i < 8; ++i) bits[i] = (uint8_t)((ctx->count[i >> 2] >> ((i & 3) * 8)) & 0xFF);
    index = (uint32_t)((ctx->count[0] >> 3) & 0x3F);
    padLen = (index < 56) ? (56 - index) : (120 - index);
    md5_update(ctx, MD5_PADDING, padLen);
    md5_update(ctx, bits, 8);
    for (i = 0; i < 16; ++i) digest[i] = (uint8_t)((ctx->state[i >> 2] >> ((i & 3) * 8)) & 0xFF);
}

void rsdk_md5_name(const char *name, uint32_t out_hash[4])
{
    md5_ctx_t ctx;
    uint8_t   digest[16];
    md5_init(&ctx);
    md5_update(&ctx, (const uint8_t *)name, (uint32_t)strlen(name));
    md5_final(&ctx, digest);
    /* Layout matches Scene.bin's on-disk storage: 4× uint32 read raw from
     * the 16-byte MD5 digest (no endian swap, no word reorder per
     * Mania catalog §11 verification). */
    out_hash[0] = ((uint32_t)digest[ 0])      | ((uint32_t)digest[ 1] << 8)
                | ((uint32_t)digest[ 2] << 16) | ((uint32_t)digest[ 3] << 24);
    out_hash[1] = ((uint32_t)digest[ 4])      | ((uint32_t)digest[ 5] << 8)
                | ((uint32_t)digest[ 6] << 16) | ((uint32_t)digest[ 7] << 24);
    out_hash[2] = ((uint32_t)digest[ 8])      | ((uint32_t)digest[ 9] << 8)
                | ((uint32_t)digest[10] << 16) | ((uint32_t)digest[11] << 24);
    out_hash[3] = ((uint32_t)digest[12])      | ((uint32_t)digest[13] << 8)
                | ((uint32_t)digest[14] << 16) | ((uint32_t)digest[15] << 24);
}

/* ===== Byte-stream reader ============================================= */

typedef struct {
    const uint8_t *data;
    uint32_t       size;
    uint32_t       pos;
} r_t;

static uint8_t  r_u8 (r_t *r) { return (r->pos < r->size) ? r->data[r->pos++] : 0; }
static uint16_t r_u16(r_t *r) { uint16_t v = (uint16_t)r_u8(r); v |= ((uint16_t)r_u8(r)) << 8; return v; }
static uint32_t r_u32(r_t *r) { uint32_t v = r_u8(r); v |= ((uint32_t)r_u8(r)) << 8; v |= ((uint32_t)r_u8(r)) << 16; v |= ((uint32_t)r_u8(r)) << 24; return v; }
static int32_t  r_i32(r_t *r) { return (int32_t)r_u32(r); }
static void     r_skip(r_t *r, uint32_t n) { r->pos += n; if (r->pos > r->size) r->pos = r->size; }
/* RSDK String: u8 length followed by length bytes (UTF-8). */
static void r_pstr(r_t *r, char *out, int out_max)
{
    int n = r_u8(r);
    int copy = (n < out_max - 1) ? n : (out_max - 1);
    int i;
    for (i = 0; i < copy; ++i) out[i] = (char)r_u8(r);
    out[copy] = '\0';
    if (n > copy) r_skip(r, (uint32_t)(n - copy));
}
/* RSDK 16-byte MD5 hash field — 4 uint32 LE. */
static void r_hash(r_t *r, uint32_t out[4]) {
    out[0] = r_u32(r); out[1] = r_u32(r); out[2] = r_u32(r); out[3] = r_u32(r);
}
/* Read a "compressed" block: u32 totalLen (BE-ish wrap) + u32 uncompressedSize
 * + (totalLen - 4) bytes zlib payload. We DON'T decompress on Saturn (zlib
 * not linked) — we just skip the payload and report its location for
 * offline tooling. The pre-extracted .raw files are loaded separately. */
static void r_skip_compressed(r_t *r)
{
    uint32_t total = r_u32(r);
    r_skip(r, total);  /* total already counts the 4-byte usize prefix */
}

/* ===== rsdk_scene_load =============================================== */

/* Phase 2.4g.1 diagnostics — `used` so GCC 8.2 whole-program LTO keeps a
 * stable named address in game.map for the savestate harness to peek.
 *   g_scene_diag_raw_len   = byte count returned by jo_fs_read_file
 *                            (-1 if it returned NULL).
 *   g_scene_diag_load_fail = where rsdk_scene_load bailed:
 *       0 = success (reached end), 1 = file read NULL, 2 = bad signature,
 *       3 = entities jo_malloc NULL, 4 = entity_data jo_malloc NULL.
 *   g_scene_diag_ent_total = entity_total computed by the first pass.
 *   g_scene_diag_data_total= entity_data_total computed by the first pass.
 * Never read by gameplay — diagnostics only. */
__attribute__((used)) volatile int      g_scene_diag_raw_len    = 0;
__attribute__((used)) volatile uint32_t g_scene_diag_load_fail  = 0;
__attribute__((used)) volatile uint32_t g_scene_diag_ent_total  = 0;
__attribute__((used)) volatile uint32_t g_scene_diag_data_total = 0;

/* Phase 2.4g.1 — LWRAM-backed scene load (binding rule:
 * memory/ghz-sky-dat-lwram-bypass.md, ">50 KB stage asset across a
 * transition must use LWRAM, not jo's 256 KB pool").
 *
 * Measured failure (samples/qa_phase2_4g1_probe.mcs, GHZ-active):
 *   g_scene_diag_load_fail == 1, g_scene_diag_raw_len == -1 ->
 *   jo_fs_read_file("GHZSCN1.BIN") returned NULL because its internal
 *   jo_malloc(85191) failed against the saturated pool (FG/SKY residue +
 *   entity SPRs). GHZSCN1.BIN is present + correct in the ISO.
 *
 * LWRAM free-region map (src/rsdk/scene_ghz.c:74-78): 0x00278000..
 * 0x002FFFFF = 544 KB free after GHZ1SURF (64) + FG.TMP (320) + SKY.DAT
 * (96). We carve:
 *   0x00278000 + 96 KB  = transient file-read scratch (reused each load).
 *   0x00290000 + 256 KB = persistent entity-table + attr-blob arena
 *                         (bump, reset at the start of each LWRAM load).
 * Peak GHZ demand = 85 KB file + 20.3 KB table + 20.8 KB blob; all fit.
 * Authority: ST-097-R5 §2.1 (LWRAM 1 MB at 0x00200000). */
#define SCENE_LWRAM_RAW_ADDR    ((void *)0x00278000)
#define SCENE_LWRAM_RAW_SIZE    0x18000u           /* 96 KB file scratch   */
#define SCENE_LWRAM_ARENA_ADDR  ((uint8_t *)0x00290000)
#define SCENE_LWRAM_ARENA_SIZE  0x40000u           /* 256 KB table + blob  */

static bool     s_scene_use_lwram    = false;
static uint32_t s_scene_arena_cursor = 0;

void rsdk_scene_set_lwram_mode(bool on) { s_scene_use_lwram = on; }

/* 4-byte-aligned bump allocator over the LWRAM scene arena. NULL when the
 * arena is exhausted (caller treats as load failure). */
static void *scene_arena_alloc(uint32_t bytes)
{
    bytes = (bytes + 3u) & ~3u;
    if (s_scene_arena_cursor + bytes > SCENE_LWRAM_ARENA_SIZE) return NULL;
    void *p = SCENE_LWRAM_ARENA_ADDR + s_scene_arena_cursor;
    s_scene_arena_cursor += bytes;
    return p;
}

bool rsdk_scene_load(const char *filename, rsdk_scene_t *scene)
{
    bool  use_lwram = s_scene_use_lwram;
    void *raw;
    int   len;

    if (use_lwram) {
        /* Reset the persistent arena for this scene, then read the whole
         * scene file into the fixed LWRAM scratch (pool-free GFS read). */
        s_scene_arena_cursor = 0;
        len = rsdk_storage_load_to_lwram((const char *)filename,
                                         SCENE_LWRAM_RAW_ADDR,
                                         SCENE_LWRAM_RAW_SIZE);
        raw = (len > 0) ? SCENE_LWRAM_RAW_ADDR : NULL;
    } else {
        raw = jo_fs_read_file((char *)filename, &len);
    }
    g_scene_diag_raw_len = raw ? len : -1;
    if (!raw || len <= 0) { g_scene_diag_load_fail = 1; return false; }

    r_t R = { (const uint8_t *)raw, (uint32_t)len, 0 };
    memset(scene, 0, sizeof(*scene));

    /* §1.4 step 1: signature 'SCN\0' */
    if (r_u32(&R) != RSDK_SIGNATURE_SCN) { g_scene_diag_load_fail = 2; if (!use_lwram) jo_free(raw); return false; }

    /* step 2: editor metadata block (16-byte fixed + stamp pstring +
     * trailing null).
     *
     * Phase 1.22 fix (§11.28 #1) — the prior code dropped the trailing
     * null byte that the decomp reads via `Seek_Cur(strLen + 1)`. Without
     * the +1 skip the stream is 1 byte behind for the rest of the file,
     * cascading into the layer + object parsers and producing
     * g_scene_diag_class_resolved == 0. Decomp cite:
     * tools/_decomp_raw/_RSDKv5_Scene.cpp:361-364 -- `Seek_Cur(&info,
     * strLen + 1)`. */
    r_skip(&R, 0x10);
    char stamp[64]; r_pstr(&R, stamp, sizeof(stamp));
    r_skip(&R, 1);                                       /* stamp NUL terminator */

    /* step 3: layers — byte-for-byte mirror of decomp Scene.cpp:367-450.
     *
     * Phase 1.22 fixes (§11.28 #2 + #3):
     *   - width_shift/height_shift are NOT on disk. The decomp computes
     *     them via a `while (val < xsize) shift++` loop at load time
     *     (Scene.cpp:382-399). The prior code read 2 phantom bytes per
     *     layer and corrupted every subsequent field offset.
     *   - The standalone `u8 deform` field is NOT on disk. What follows
     *     the scroll_speed u16 is a `u16 scrollInfoCount` then
     *     scrollInfoCount * 6 bytes (i16 parallax + i16 scroll +
     *     u8 deform + u8 unknown per scroll-info row). Cite:
     *     Scene.cpp:416-426.
     *
     * The Python reference parser tools/parse_title_entities.py walks
     * SCENE1.BIN's 2589 bytes cleanly using this exact layout. */
    scene->layer_count = r_u8(&R);
    if (scene->layer_count > 8) scene->layer_count = 8;
    for (int li = 0; li < scene->layer_count; ++li) {
        rsdk_scene_layer_t *L = &scene->layers[li];
        r_u8(&R);                                        /* visibleInEditor */
        r_pstr(&R, L->name, sizeof(L->name));
        L->type         = r_u8(&R);
        L->draw_group   = r_u8(&R);
        L->xsize        = r_u16(&R);
        L->ysize        = r_u16(&R);
        L->parallax_factor = (int16_t)r_u16(&R);
        L->scroll_speed    = (int16_t)r_u16(&R);
        /* Compute width_shift/height_shift from sizes (decomp:382-399). */
        {
            int shift  = 1;
            int shift2 = 1;
            int val    = 0;
            do { shift = shift2; val = 1 << shift2++; }
            while (val < L->xsize);
            L->width_shift = (uint8_t)shift;
            shift  = 1; shift2 = 1; val = 0;
            do { shift = shift2; val = 1 << shift2++; }
            while (val < L->ysize);
            L->height_shift = (uint8_t)shift;
        }
        /* scrollInfo: u16 count + count * (i16 parallax + i16 scroll +
         * u8 deform + u8 unknown) = 2 + count*6 bytes. */
        uint16_t scroll_info_count = r_u16(&R);
        r_skip(&R, (uint32_t)scroll_info_count * 6u);
        /* deform field stored per-scrollInfo row above; the per-layer
         * deform struct field stays 0 (not used by the Saturn path which
         * doesn't run scanline FX in Phase 1). */
        L->deform = 0;
        r_skip_compressed(&R);                           /* line-scroll indexes */
        r_skip_compressed(&R);                           /* tile layout         */
        L->layout = NULL;                                /* offline-extracted   */
        L->layout_len = 0;
    }

    /* step 4: object classes + entities */
    scene->class_count = r_u8(&R);
    if (scene->class_count > 64) scene->class_count = 64;

    /* First pass: read class headers (hash + attribs). We accumulate the
     * per-class entity_data offset as we walk so we can index later. */
    uint32_t entity_data_total = 0;
    uint16_t entity_total = 0;
    {
        r_t T = R;  /* clone for two-pass scan */
        for (int ci = 0; ci < scene->class_count; ++ci) {
            rsdk_scene_class_t *C = &scene->classes[ci];
            r_hash(&T, C->hash);
            C->var_count = r_u8(&T);
            if (C->var_count > 16) C->var_count = 16;
            /* attribs (var_count - 1 stored — index 0 is implicit filter)*/
            for (int v = 1; v < C->var_count; ++v) {
                r_hash(&T, C->attribs[v].hash);
                C->attribs[v].type = r_u8(&T);
            }
            C->entity_count = r_u16(&T);
            entity_total += C->entity_count;
            /* Skip per-entity payload to find size — slot(2) + posX(4) +
             * posY(4) + sum of attribute payload sizes per entity. */
            /* Phase 1.22 fix (§11.28) — attribute type sizes per decomp
             * Scene.cpp:550-664 VAR_* dispatch:
             *   0 UINT8 / 3 INT8                       -> 1 byte
             *   1 UINT16 / 4 INT16                     -> 2 bytes
             *   2 UINT32 / 5 INT32 / 6 ENUM / 7 BOOL32
             *     / 10 FLOAT / 11 COLOR                -> 4 bytes
             *   9 VECTOR2                              -> 8 bytes
             *   8 STRING                               -> 2 + 2 * len
             * BOOL was previously sized as 1 byte; ENUM/BOOL/COLOR all
             * round-trip through ReadInt32(&info, false) per decomp
             * cases VAR_ENUM (586-596), VAR_BOOL (598-608), VAR_COLOR
             * (653-663). */
            uint32_t per_entity_attr_bytes = 0;
            for (int v = 1; v < C->var_count; ++v) {
                switch (C->attribs[v].type) {
                    case 0: case 3:                  per_entity_attr_bytes += 1; break;
                    case 1: case 4:                  per_entity_attr_bytes += 2; break;
                    case 2: case 5: case 6: case 7:
                    case 10: case 11:                per_entity_attr_bytes += 4; break;
                    case 9:                          per_entity_attr_bytes += 8; break;
                    default: /* type 8 (string) variable -- bail back to
                              * the slow per-entity scan below. */
                              per_entity_attr_bytes = 0xFFFFFFFFu; break;
                }
                if (per_entity_attr_bytes == 0xFFFFFFFFu) break;
            }
            if (per_entity_attr_bytes == 0xFFFFFFFFu) {
                /* String attribute encountered — fall back to a per-entity
                 * read pass to measure sizes.  Accumulator below mirrors
                 * the same type table.  Phase 1.22: fixed ternary-
                 * precedence bug that mapped uint16 (type 1) to 1 byte
                 * because `ty <= 1` matched first. */
                uint32_t class_blob_bytes = 0;
                for (int e = 0; e < C->entity_count; ++e) {
                    r_skip(&T, 2 + 4 + 4);                  /* slot+x+y    */
                    uint32_t this_entity_bytes = 0;
                    for (int v = 1; v < C->var_count; ++v) {
                        uint8_t ty = C->attribs[v].type;
                        uint32_t sz;
                        if      (ty == 0 || ty == 3)            sz = 1;
                        else if (ty == 1 || ty == 4)            sz = 2;
                        else if (ty == 9)                       sz = 8;
                        else if (ty == 8) {                     /* string */
                            uint16_t n = r_u16(&T);
                            r_skip(&T, (uint32_t)n * 2);
                            this_entity_bytes += 2 + (uint32_t)n * 2;
                            continue;
                        }
                        else                                    sz = 4;
                        r_skip(&T, sz);
                        this_entity_bytes += sz;
                    }
                    class_blob_bytes += this_entity_bytes;
                }
                entity_data_total += class_blob_bytes;
                continue;
            }
            r_skip(&T, (uint32_t)C->entity_count *
                       (10 + per_entity_attr_bytes));
            entity_data_total += (uint32_t)C->entity_count
                               * per_entity_attr_bytes;
        }
    }

    /* Allocate entity table + attribute payload blob. In LWRAM mode both
     * come from the bump arena (never jo_free'd); rsdk_scene_free skips the
     * free and the arena is reset on the next LWRAM load. */
    g_scene_diag_ent_total  = entity_total;
    g_scene_diag_data_total = entity_data_total;
    scene->entity_count = entity_total;
    scene->mem_lwram    = use_lwram ? 1 : 0;
    if (entity_total > 0) {
        uint32_t tbytes = sizeof(rsdk_scene_entity_t) * entity_total;
        scene->entities = (rsdk_scene_entity_t *)
            (use_lwram ? scene_arena_alloc(tbytes) : jo_malloc(tbytes));
        if (!scene->entities) { g_scene_diag_load_fail = 3; if (!use_lwram) jo_free(raw); return false; }
        memset(scene->entities, 0, tbytes);
    }
    if (entity_data_total > 0) {
        scene->entity_data = (uint8_t *)
            (use_lwram ? scene_arena_alloc(entity_data_total)
                       : jo_malloc(entity_data_total));
        if (!scene->entity_data) {
            g_scene_diag_load_fail = 4;
            if (!use_lwram && scene->entities) jo_free(scene->entities);
            scene->entities = NULL;
            if (!use_lwram) jo_free(raw);
            return false;
        }
        scene->entity_data_size = entity_data_total;
    }

    /* Second pass: actually read entities + attribute payloads. */
    uint32_t attr_blob_pos = 0;
    uint16_t entity_index = 0;
    for (int ci = 0; ci < scene->class_count; ++ci) {
        rsdk_scene_class_t *C = &scene->classes[ci];
        r_hash(&R, C->hash);
        C->var_count = r_u8(&R);
        if (C->var_count > 16) C->var_count = 16;
        for (int v = 1; v < C->var_count; ++v) {
            r_hash(&R, C->attribs[v].hash);
            C->attribs[v].type = r_u8(&R);
        }
        C->entity_count = r_u16(&R);

        for (int e = 0; e < C->entity_count; ++e) {
            rsdk_scene_entity_t *E = &scene->entities[entity_index++];
            E->class_index        = (uint16_t)ci;
            E->slot               = r_u16(&R);
            E->pos_x              = r_i32(&R);
            E->pos_y              = r_i32(&R);
            E->filter             = 0xFF;                  /* default      */
            E->attr_payload_offset = attr_blob_pos;
            /* Copy attribute payload bytes verbatim into the blob; user
             * code re-parses via rsdk_entity_attr_*. */
            for (int v = 1; v < C->var_count; ++v) {
                uint8_t ty = C->attribs[v].type;
                uint32_t sz;
                /* Phase 1.22 fix (§11.28) — VAR_BOOL (7) is bool32 (4 B);
                 * UINT16 (1) is 2 B; ENUM/COLOR/FLOAT also u32 (per decomp
                 * Scene.cpp:586-663). Prior code mis-sized BOOL as 1 B. */
                if      (ty == 0 || ty == 3)            sz = 1;
                else if (ty == 1 || ty == 4)            sz = 2;
                else if (ty == 9)                       sz = 8;
                else if (ty == 8) {
                    uint16_t n = r_u16(&R);
                    /* prefix the length back into the blob */
                    if (attr_blob_pos + 2 <= scene->entity_data_size) {
                        scene->entity_data[attr_blob_pos    ] = (uint8_t)(n & 0xFF);
                        scene->entity_data[attr_blob_pos + 1] = (uint8_t)((n >> 8) & 0xFF);
                    }
                    attr_blob_pos += 2;
                    sz = (uint32_t)n * 2;
                }
                else                                    sz = 4;
                if (attr_blob_pos + sz <= scene->entity_data_size) {
                    memcpy(&scene->entity_data[attr_blob_pos],
                           &R.data[R.pos], sz);
                }
                r_skip(&R, sz);
                attr_blob_pos += sz;
            }
        }
    }

    if (!use_lwram) jo_free(raw);
    g_scene_diag_load_fail = 0;
    return true;
}

void rsdk_scene_free(rsdk_scene_t *scene)
{
    /* LWRAM-backed scenes live in the bump arena (reset on next load) —
     * never hand those pointers to jo_free. */
    if (!scene->mem_lwram) {
        if (scene->entities)    jo_free(scene->entities);
        if (scene->entity_data) jo_free(scene->entity_data);
    }
    scene->entities = NULL;
    scene->entity_data = NULL;
    scene->entity_count = 0;
    scene->entity_data_size = 0;
    scene->mem_lwram = 0;
}

int rsdk_scene_find_class(const rsdk_scene_t *scene, const char *class_name)
{
    uint32_t target[4];
    rsdk_md5_name(class_name, target);
    for (int i = 0; i < scene->class_count; ++i) {
        if (memcmp(scene->classes[i].hash, target, 16) == 0) return i;
    }
    return -1;
}

const rsdk_scene_entity_t *rsdk_scene_entity_at(const rsdk_scene_t *scene,
                                                int class_index, int index)
{
    if (class_index < 0 || class_index >= scene->class_count) return NULL;
    int seen = 0;
    for (uint16_t i = 0; i < scene->entity_count; ++i) {
        if (scene->entities[i].class_index == (uint16_t)class_index) {
            if (seen == index) return &scene->entities[i];
            seen++;
        }
    }
    return NULL;
}

/* Walk to attr_index inside an entity's attribute payload. Returns the byte
 * offset, or 0xFFFFFFFFu if attr_index is out of range. */
static uint32_t _attr_offset(const rsdk_scene_t *scene,
                             const rsdk_scene_entity_t *ent, int attr_index)
{
    const rsdk_scene_class_t *C = &scene->classes[ent->class_index];
    if (attr_index < 1 || attr_index >= C->var_count) return 0xFFFFFFFFu;
    uint32_t p = ent->attr_payload_offset;
    for (int v = 1; v < attr_index; ++v) {
        uint8_t ty = C->attribs[v].type;
        if      (ty == 0 || ty == 3 || ty == 7) p += 1;
        else if (ty == 1 || ty == 4)            p += 2;
        else if (ty == 9)                       p += 8;
        else if (ty == 8) {
            uint16_t n = (uint16_t)scene->entity_data[p]
                       | ((uint16_t)scene->entity_data[p+1] << 8);
            p += 2 + (uint32_t)n * 2;
        } else p += 4;
    }
    return p;
}

uint8_t rsdk_entity_attr_u8(const rsdk_scene_t *scene,
                            const rsdk_scene_entity_t *ent, int attr_index)
{
    uint32_t p = _attr_offset(scene, ent, attr_index);
    return (p < scene->entity_data_size) ? scene->entity_data[p] : 0;
}

uint32_t rsdk_entity_attr_u32(const rsdk_scene_t *scene,
                              const rsdk_scene_entity_t *ent, int attr_index)
{
    uint32_t p = _attr_offset(scene, ent, attr_index);
    if (p + 4 > scene->entity_data_size) return 0;
    return ((uint32_t)scene->entity_data[p])
         | ((uint32_t)scene->entity_data[p+1] << 8)
         | ((uint32_t)scene->entity_data[p+2] << 16)
         | ((uint32_t)scene->entity_data[p+3] << 24);
}

int32_t rsdk_entity_attr_i32(const rsdk_scene_t *scene,
                             const rsdk_scene_entity_t *ent, int attr_index)
{
    return (int32_t)rsdk_entity_attr_u32(scene, ent, attr_index);
}

/* ===== Sprite animation parser ======================================= */

bool rsdk_sprite_animation_load(const char *filename,
                                rsdk_sprite_animation_t *anim)
{
    int len;
    void *raw = jo_fs_read_file((char *)filename, &len);
    if (!raw || len <= 0) return false;
    r_t R = { (const uint8_t *)raw, (uint32_t)len, 0 };
    memset(anim, 0, sizeof(*anim));
    if (r_u32(&R) != RSDK_SIGNATURE_SPR) { jo_free(raw); return false; }
    uint32_t total_frame_count = r_u32(&R);              /* sum across all */
    anim->sheet_count = r_u8(&R);
    if (anim->sheet_count > 8) anim->sheet_count = 8;
    for (int s = 0; s < anim->sheet_count; ++s)
        r_pstr(&R, anim->sheet_names[s], sizeof(anim->sheet_names[s]));
    uint8_t hb_count = r_u8(&R);
    for (int h = 0; h < hb_count; ++h) {
        char nm[32]; r_pstr(&R, nm, sizeof(nm));
    }
    anim->anim_count = r_u16(&R);
    if (anim->anim_count == 0) { jo_free(raw); return true; }
    anim->animations = (rsdk_sprite_animation_entry_t *)
        jo_malloc(sizeof(rsdk_sprite_animation_entry_t) * anim->anim_count);
    if (!anim->animations) { jo_free(raw); return false; }
    /* First pass: count total frames so we can allocate */
    uint32_t total_frames_alloc = total_frame_count;
    anim->frames = (rsdk_sprite_frame_t *)
        jo_malloc(sizeof(rsdk_sprite_frame_t) * total_frames_alloc);
    if (!anim->frames) {
        jo_free(anim->animations); anim->animations = NULL;
        jo_free(raw); return false;
    }
    uint32_t fpos = 0;
    for (int ai = 0; ai < anim->anim_count; ++ai) {
        rsdk_sprite_animation_entry_t *A = &anim->animations[ai];
        r_pstr(&R, A->name, sizeof(A->name));
        rsdk_md5_name(A->name, A->hash);
        A->frame_count    = r_u16(&R);
        A->speed          = r_u16(&R);
        A->loop_index     = r_u8(&R);
        A->rotation_style = r_u8(&R);
        A->frames = &anim->frames[fpos];
        for (uint16_t fi = 0; fi < A->frame_count; ++fi, ++fpos) {
            if (fpos >= total_frames_alloc) break;
            rsdk_sprite_frame_t *F = &anim->frames[fpos];
            F->sheet_id     = r_u8(&R);
            F->duration     = r_u16(&R);
            F->unicode_char = r_u16(&R);
            F->src_x        = r_u16(&R);
            F->src_y        = r_u16(&R);
            F->width        = r_u16(&R);
            F->height       = r_u16(&R);
            F->pivot_x      = (int16_t)r_u16(&R);
            F->pivot_y      = (int16_t)r_u16(&R);
            F->hitbox_count = hb_count;
            for (int h = 0; h < hb_count && h < 8; ++h) {
                F->hitboxes[h].left   = (int16_t)r_u16(&R);
                F->hitboxes[h].top    = (int16_t)r_u16(&R);
                F->hitboxes[h].right  = (int16_t)r_u16(&R);
                F->hitboxes[h].bottom = (int16_t)r_u16(&R);
            }
        }
    }
    jo_free(raw);
    return true;
}

void rsdk_sprite_animation_free(rsdk_sprite_animation_t *anim)
{
    if (anim->frames)     { jo_free(anim->frames);     anim->frames = NULL; }
    if (anim->animations) { jo_free(anim->animations); anim->animations = NULL; }
    anim->anim_count = 0;
}

/* ===== StageConfig.bin parser ========================================
 * Per Mania catalog §1.1: `CFG\0` + u8 loadGlobals + u8 objectCount +
 * objectCount strings + (palette banks/masks blob TBD; we keep the
 * tail as opaque trailing_blob for now). */

bool rsdk_stage_config_load(const char *filename, rsdk_stage_config_t *cfg)
{
    int len;
    void *raw = jo_fs_read_file((char *)filename, &len);
    if (!raw || len <= 0) return false;
    r_t R = { (const uint8_t *)raw, (uint32_t)len, 0 };
    memset(cfg, 0, sizeof(*cfg));
    if (r_u32(&R) != RSDK_SIGNATURE_CFG) { jo_free(raw); return false; }
    cfg->load_globals = r_u8(&R);
    cfg->object_count = r_u8(&R);
    if (cfg->object_count > 64) cfg->object_count = 64;
    for (int i = 0; i < cfg->object_count; ++i) {
        r_pstr(&R, cfg->object_names[i], sizeof(cfg->object_names[i]));
    }
    /* Trailing palette/SFX blob: copy verbatim for offline post-processing.
     * Saturn doesn't currently use this at runtime — assets pre-extracted.*/
    uint32_t remaining = (R.size > R.pos) ? (R.size - R.pos) : 0;
    if (remaining > 0) {
        cfg->trailing_blob = (uint8_t *)jo_malloc(remaining);
        if (cfg->trailing_blob) {
            memcpy(cfg->trailing_blob, &R.data[R.pos], remaining);
            cfg->trailing_size = remaining;
        }
    }
    jo_free(raw);
    return true;
}

void rsdk_stage_config_free(rsdk_stage_config_t *cfg)
{
    if (cfg->trailing_blob) { jo_free(cfg->trailing_blob); cfg->trailing_blob = NULL; }
    cfg->trailing_size = 0;
}

/* ===== Phase 2.2b — raw SBL/GFS load to caller-supplied buffer =========
 *
 * Mechanically modelled on `src/mania/Objects/Title/TitleAssets.c::
 * load_tsonic_atlas` L239-258 (Phase 1.19 corrected GFS_Fread semantics)
 * but writes to a caller-supplied buffer rather than allocating from jo's
 * pool.
 *
 * GFS API call order per DTS-136-R2-093094:
 *   §3.1: Sint32 GFS_NameToId(Uint8 *fname)
 *   §3.1: GfsHn  GFS_Open(Sint32 fid)
 *   §3.1: void   GFS_GetFileSize(GfsHn gfs, Sint32 *sctsize,
 *                                Sint32 *nsct, Sint32 *lastsize)
 *         -> sctsize = sector size in bytes (always 2048 for FORM1)
 *         -> nsct    = number of sectors in file
 *         -> lastsize = bytes used in final sector
 *   §3.1: Sint32 GFS_Fread(GfsHn gfs, Sint32 nsct, void *buf, Sint32 bsize)
 *         -> reads `nsct` sectors into `buf`, truncated to `bsize`.
 *         -> returns bytes actually read, or <0 on error.
 *   §3.1: void   GFS_Close(GfsHn gfs)
 *
 * Total file bytes = (nsct-1)*sctsize + lastsize, but on Saturn we treat
 * each sector as 2048 B and the caller asserts max_bytes >= file size.
 *
 * No jo_malloc / jo_free — entirely pool-free. */
int rsdk_storage_load_to_lwram(const char *iso9660_name,
                               void *dst, uint32_t max_bytes)
{
    if (!iso9660_name || !dst || max_bytes == 0) return -1;

    Sint32 fid = GFS_NameToId((Sint8 *)iso9660_name);
    if (fid < 0) return -1;

    GfsHn gfs = GFS_Open(fid);
    if (gfs == JO_NULL) return -1;

    /* Query file dimensions so we can issue a single multi-sector read
     * covering the whole file. */
    Sint32 sct_size      = 0;
    Sint32 nsct          = 0;
    Sint32 last_sct_size = 0;
    GFS_GetFileSize(gfs, &sct_size, &nsct, &last_sct_size);

    if (nsct <= 0 || sct_size <= 0) {
        GFS_Close(gfs);
        return -1;
    }

    /* Total file size = (nsct - 1) * sct_size + last_sct_size. The buffer
     * MUST hold the full file. Reject if caller's max_bytes is too small. */
    uint32_t total = (uint32_t)(nsct - 1) * (uint32_t)sct_size
                   + (uint32_t)last_sct_size;
    if (total > max_bytes) {
        GFS_Close(gfs);
        return -1;
    }

    /* Single multi-sector read. GFS_Fread caps output at bsize, so passing
     * max_bytes as bsize guarantees no overflow. Returns bytes read. */
    Sint32 read = GFS_Fread(gfs, nsct, dst, (Sint32)max_bytes);
    GFS_Close(gfs);

    if (read <= 0) return -1;
    /* Truncate reported bytes to the logical file size: GFS_Fread may have
     * read the full last sector (2048 B) but only `last_sct_size` of that
     * is real data. Caller's checksum / size validation should use the
     * logical file size, not the sector-aligned read. */
    if ((uint32_t)read > total) read = (Sint32)total;
    return (int)read;
}

/* ===== TileConfig.bin loader =========================================
 * Source format: 'TIL\0' + zlib-compressed (2 paths × 1024 tiles × 38 B).
 * Saturn doesn't link zlib by default. Two options:
 *   (a) Load pre-decompressed cd/TILECFG.RAW (offline tools/dump tool).
 *   (b) Embed a minimal inflater (~5 KB) — TODO Phase A1.1.
 * For now we accept either: if the file starts with 'TIL\0' we assume
 * the .raw uncompressed variant ALREADY has the payload inlined after
 * the signature, and we read it directly. If we detect a compressed
 * envelope we report failure (caller falls back to the offline-built
 * GHZ*SURF.BIN flat collision tables which build_collision.py already
 * produces). */

bool rsdk_tile_config_load(const char *filename, rsdk_tile_config_t *out)
{
    int len;
    void *raw = jo_fs_read_file((char *)filename, &len);
    if (!raw || len <= 0) return false;
    r_t R = { (const uint8_t *)raw, (uint32_t)len, 0 };
    memset(out, 0, sizeof(*out));
    if (r_u32(&R) != RSDK_SIGNATURE_TIL) { jo_free(raw); return false; }
    /* Expect (2 * 1024 * 38) = 77824 bytes uncompressed payload following. */
    uint32_t expect = RSDK_CPATH_COUNT * RSDK_TILE_COUNT * 38;
    if (R.size - R.pos < expect) {
        /* Compressed envelope path (zlib not yet linked) — fail. */
        jo_free(raw);
        return false;
    }
    for (int p = 0; p < RSDK_CPATH_COUNT; ++p) {
        for (int t = 0; t < RSDK_TILE_COUNT; ++t) {
            rsdk_tile_t *T = &out->paths[p][t];
            memcpy(T->mask_heights, &R.data[R.pos], 16); R.pos += 16;
            memcpy(T->mask_active,  &R.data[R.pos], 16); R.pos += 16;
            T->y_flip           = r_u8(&R);
            T->floor_angle      = r_u8(&R);
            T->left_wall_angle  = r_u8(&R);
            T->right_wall_angle = r_u8(&R);
            T->roof_angle       = r_u8(&R);
            T->flag             = r_u8(&R);
        }
    }
    jo_free(raw);
    return true;
}
