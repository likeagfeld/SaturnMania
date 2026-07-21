/* ============================================================================
 * p6_sfx.c -- P6.8 gameplay-SFX residency: the S8@22050 pack (cd/GHZSFX.PCM,
 * built by tools/build_sfx_pack.py) uploaded to SCSP sound RAM + a per-frame
 * pump that keys-on an SCSP voice when the verbatim decomp PlaySfx arms a
 * channel. Fixes the dead-gameplay-SFX defect (only 3/256 SFX loaded because
 * Mania decodes to F32 in the ~32 KB DATASET_SFX WRAM pool -> exhaustion). See
 * memory/dead-sfx-rootcause-f32-pool-exhaustion.md.
 *
 * FRONT-END-CHAIN ONLY (#if P6_FRONTEND_LOGOS): the default GHZ image never
 * compiles this (WRAM-H #228 budget) and stays byte-identical.
 *
 * SCSP programming mirrors the gate-validated p6_snd.c / Coup coup_audio.c
 * (ST-077-R2 SCSP_Manual.txt fig 4.2 confirmed): slot regs at 0x25B00000 +
 * slot*0x20; SA_CTRL(0x00) = KYONEX(1<<12) KYONB(1<<11) ... PCM8B(1<<4)
 * SA[19:16]; PITCH(0x10) = ((OCT&0xF)<<11)|FNS. The ONLY differences from the
 * single-buffer MenuBleep path are: PCM8B=1 (8-bit samples) and PITCH 0x7800
 * (OCT=-1 -> 22050 Hz replay), plus a per-SFX SA/LEA from the pack table.
 * ========================================================================== */
#if defined(P6_FRONTEND_LOGOS)

#include <jo/jo.h>

#define P6_SFX_SRAM_BASE  0x25A00000UL
#define P6_SFX_REG_BASE   0x25B00000UL
/* Samples at +0x6D000: 4 KB above p6_snd.c's MenuBleep buffer (0x6C000), inside
 * the measured ~76 KB free window before sound-RAM end (0x80000). */
#define P6_SFX_PCM_OFF    0x6D000UL

#define P6_SFX_MAX        16
#define P6_SFX_SLOT_BASE  24        /* our voices 24-27; MenuBleep uses 28-31 */
#define P6_SFX_NSLOTS     4
#define P6_SCSP_KYONEX    (1u << 12)
#define P6_SCSP_KYONB     (1u << 11)
#define P6_SCSP_PCM8B     (1u << 4)
#define P6_SFX_NUM_SLOTS  32

typedef struct { unsigned int key, off, count; } p6_sfx_ent;
static p6_sfx_ent s_sfx[P6_SFX_MAX];
static int s_sfx_n  = 0;
static int s_sfx_rr = 0;
static signed char s_slot2pack[256];   /* engine sfxList slot -> pack idx (-1) */

/* qa_p6_sfx_residency companions + oracle witnesses. */
__attribute__((used)) int p6_w_sfx_pack_loaded = 0; /* # entries uploaded */
__attribute__((used)) int p6_w_sfx_keyons      = 0; /* monotonic SCSP key-ons */
__attribute__((used)) int p6_w_sfx_bound       = 0; /* # engine slots bound to pack */

static unsigned int p6_be32(const unsigned char *p)
{
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16)
         | ((unsigned int)p[2] << 8)  |  (unsigned int)p[3];
}
static unsigned int p6_be16(const unsigned char *p)
{
    return ((unsigned int)p[0] << 8) | (unsigned int)p[1];
}

/* djb2 -- MUST match build_sfx_pack.py + the engine's Audio.cpp:430 idiom. */
unsigned int p6_sfx_djb2(const char *s)
{
    unsigned int h = 5381u;
    while (*s) { h = ((h << 5) + h) ^ (unsigned char)*s; ++s; }
    return h;
}

/* Load cd/GHZSFX.PCM into sound RAM. MUST be called BEFORE the engine mounts
 * Data.rsdk (jo's GFS is free then -> no p6_gfs second-open trap; s_pack_refs==0).
 * main.c calls this right after jo_core_init, before p6_engine_boot_and_run. */
void p6_sfx_load(void)
{
    int i;
    for (i = 0; i < 256; ++i) s_slot2pack[i] = -1;
    s_sfx_n = 0;

    int len = 0;
    unsigned char *d = (unsigned char *)jo_fs_read_file("GHZSFX.PCM", &len);
    if (!d || len < 16)
        return;
    if (!(d[0] == 'P' && d[1] == '6' && d[2] == 'S' && d[3] == 'F'))
        return;

    int cnt = (int)p6_be16(d + 6);
    if (cnt > P6_SFX_MAX) cnt = P6_SFX_MAX;
    int dataoff = 16 + cnt * 12;

    volatile unsigned char *sram =
        (volatile unsigned char *)(P6_SFX_SRAM_BASE + P6_SFX_PCM_OFF);

    for (i = 0; i < cnt; ++i) {
        const unsigned char *e = d + 16 + i * 12;
        unsigned int key = p6_be32(e);
        unsigned int off = p6_be32(e + 4);
        unsigned int n   = p6_be32(e + 8);
        const unsigned char *src = d + dataoff + off;
        unsigned int b;
        for (b = 0; b < n; ++b)
            sram[off + b] = src[b];      /* S8 sample byte -> sound RAM */
        s_sfx[s_sfx_n].key   = key;
        s_sfx[s_sfx_n].off   = off;
        s_sfx[s_sfx_n].count = n;
        ++s_sfx_n;
    }
    p6_w_sfx_pack_loaded = s_sfx_n;
}

/* Called by Audio.cpp LoadSfxToSlot on Saturn: does this SFX name live in the
 * pack? Returns pack idx or -1. */
int p6_sfx_lookup(const char *filename)
{
    if (s_sfx_n == 0 || !filename)
        return -1;
    unsigned int k = p6_sfx_djb2(filename);
    int i;
    for (i = 0; i < s_sfx_n; ++i)
        if (s_sfx[i].key == k)
            return i;
    return -1;
}

/* Bind the engine sfxList slot (== the soundID PlaySfx writes to channels[]) to
 * a pack idx so the per-frame pump can map an armed channel back to its sample. */
void p6_sfx_bind(int sfxSlot, int packIdx)
{
    if (sfxSlot < 0 || sfxSlot >= 256 || packIdx < 0 || packIdx >= s_sfx_n)
        return;
    s_slot2pack[sfxSlot] = (signed char)packIdx;
    ++p6_w_sfx_bound;
}

static volatile unsigned short *p6_sfx_reg(int slot, int off)
{
    return (volatile unsigned short *)(P6_SFX_REG_BASE
                                       + (unsigned long)slot * 0x20UL
                                       + (unsigned long)off);
}

/* Key a one-shot playback of pack entry `idx` on a free voice (round-robin
 * 24-27). PCM8B=1 (8-bit), OCT=-1 (22050). Mirrors p6_snd_play's KYONEX
 * isolation (Coup scsp_safe_keyon). */
static void p6_sfx_keyon(int idx)
{
    if (idx < 0 || idx >= s_sfx_n)
        return;
    unsigned long sa = P6_SFX_PCM_OFF + s_sfx[idx].off;
    unsigned short sa_hi = (unsigned short)((sa >> 16) & 0x000F);
    unsigned short sa_lo = (unsigned short)(sa & 0xFFFF);
    int slot = P6_SFX_SLOT_BASE + s_sfx_rr;
    int i;
    s_sfx_rr = (s_sfx_rr + 1) & 3;

    p6_sfx_reg(slot, 0x02)[0] = sa_lo;
    p6_sfx_reg(slot, 0x04)[0] = 0;                                        /* LSA  */
    p6_sfx_reg(slot, 0x06)[0] = (unsigned short)(s_sfx[idx].count - 1);   /* LEA  */
    p6_sfx_reg(slot, 0x08)[0] = 0x001F;                                   /* AR31 */
    p6_sfx_reg(slot, 0x0A)[0] = 0x001F;                                   /* RR31 */
    p6_sfx_reg(slot, 0x0C)[0] = 0x0010;                                   /* TL   */
    p6_sfx_reg(slot, 0x0E)[0] = 0x0000;
    p6_sfx_reg(slot, 0x10)[0] = 0x7800;   /* PITCH: OCT=-1 FNS=0 -> 22050 */
    p6_sfx_reg(slot, 0x12)[0] = 0x0000;
    p6_sfx_reg(slot, 0x14)[0] = 0x0000;
    p6_sfx_reg(slot, 0x16)[0] = (unsigned short)(7u << 13);               /* DISDL=7 */

    /* KYONEX isolation: clear KYONB on every other slot, settle, key on with
     * PCM8B set. (Clearing KYONB does NOT stop already-started slots.) */
    for (i = 0; i < P6_SFX_NUM_SLOTS; ++i)
        if (i != slot)
            p6_sfx_reg(i, 0x00)[0] = 0x0000;
    for (i = 0; i < 128; ++i)
        (void)p6_sfx_reg(0, 0x0C)[0];
    p6_sfx_reg(slot, 0x00)[0] =
        (unsigned short)(P6_SCSP_KYONEX | P6_SCSP_KYONB | P6_SCSP_PCM8B | sa_hi);
    ++p6_w_sfx_keyons;
}

/* Per-frame pump: called by p6_audio_witness when a channel NEWLY enters
 * CHANNEL_SFX. Maps the armed soundID to a pack entry and keys-on the SCSP. */
void p6_sfx_pump(int soundID)
{
    int pk;
    if (soundID < 0 || soundID >= 256)
        return;
    pk = s_slot2pack[soundID];
    if (pk >= 0)
        p6_sfx_keyon(pk);
}

#endif /* P6_FRONTEND_LOGOS */
