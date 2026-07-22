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
/* Samples at +0x40000: the 176 KB window 0x40000-0x6C000 reads ALL-ZERO at GHZ
 * gameplay AND AIZ (MEASURED 2026-07-21 savestate SCSP dumps). SGL's driver +
 * scene PCM blocks stay below 0x40000 (a solid 64 KB block at 0x30000-0x40000
 * persists -- do NOT go below 0x40000). This is 2.3x the prior conservative
 * 0x6D000 (76 KB) window, enough for the full GHZ SFX set incl. spindash. */
#define P6_SFX_PCM_OFF    0x40000UL

#define P6_SFX_MAX        40
/* SCSP voices 28-31 -- the ONLY slots the SGL M68K sound driver (SDDRVS.DAT via
 * slInitSound) does NOT manage, per the Coup reference (coup_audio.c:14-16,85-86
 * SFX_SLOT_BASE=28). The prior 24-27 were STOMPED by the driver every frame:
 * p6_sfx_keyon fired (keyon witness climbed) but the voice was zeroed before it
 * reached the DAC -> SILENT despite key-ons (user 2026-07-22 "no SFX at all").
 * MenuBleep (p6_snd.c) also uses 28-31, but it fires ONLY in the menu and SFX
 * ONLY in gameplay -- never simultaneous, so they share the 4 safe slots. */
#define P6_SFX_SLOT_BASE  28
#define P6_SFX_NSLOTS     4
#define P6_SCSP_KYONEX    (1u << 12)
#define P6_SCSP_KYONB     (1u << 11)
#define P6_SCSP_PCM8B     (1u << 4)
#define P6_SFX_NUM_SLOTS  32

typedef struct { unsigned int key, off, count, oct, fmt; } p6_sfx_ent;
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

    /* v4: 16-byte entries [key, off, sampleCount, flags]; flags = octNibble(low
     * byte) | fmt<<8 (fmt 0=S8/PCM8B, 1=S16). v3/v2 back-compat: 16-/12-byte, all
     * S8. `count` is the SAMPLE count (LEA); byte size = count*(S16?2:1). */
    int ver = (int)p6_be16(d + 4);
    int stride = (ver >= 3) ? 16 : 12;
    int cnt = (int)p6_be16(d + 6);
    if (cnt > P6_SFX_MAX) cnt = P6_SFX_MAX;
    int dataoff = 16 + cnt * stride;

    /* SCSP sound RAM is WORD-ONLY from the SH-2: "the main CPU cannot access in
     * units of 8 bits, so read and write in 16 bit units" (SCSP_Manual.txt
     * SS-3.1(1) line 1229). The prior BYTE-write upload corrupted the waveform ->
     * slots played silence (SoundStack=0). Upload in 16-bit words, exactly like
     * the proven-audible p6_snd.c MenuBleep + Coup. Pack entry offsets + byte
     * lengths are 2-byte aligned (build_sfx_pack.py), so word indexing is exact. */
    volatile unsigned short *sram =
        (volatile unsigned short *)(P6_SFX_SRAM_BASE + P6_SFX_PCM_OFF);

    for (i = 0; i < cnt; ++i) {
        const unsigned char *e = d + 16 + i * stride;
        unsigned int key   = p6_be32(e);
        unsigned int off   = p6_be32(e + 4);              /* byte offset (even) */
        unsigned int n     = p6_be32(e + 8);              /* sample count */
        unsigned int flags = (stride == 16) ? p6_be32(e + 12) : 0xF;
        unsigned int oct   = flags & 0xF;                 /* 0xF=22050 */
        unsigned int fmt   = (flags >> 8) & 1;            /* 1=S16 (PCM8B off) */
        unsigned int nbytes = n * (fmt ? 2u : 1u);
        const unsigned short *src = (const unsigned short *)(d + dataoff + off);
        unsigned int woff = off >> 1;                     /* off is 2-byte aligned */
        unsigned int words = (nbytes + 1u) >> 1;
        unsigned int w;
        for (w = 0; w < words; ++w)
            sram[woff + w] = src[w];     /* 16-bit word writes (SS-3.1 line 1229) */
        s_sfx[s_sfx_n].key   = key;
        s_sfx[s_sfx_n].off   = off;
        s_sfx[s_sfx_n].count = n;
        s_sfx[s_sfx_n].oct   = oct;
        s_sfx[s_sfx_n].fmt   = fmt;
        ++s_sfx_n;
    }
    p6_w_sfx_pack_loaded = s_sfx_n;
    /* Samples now live in SCSP sound RAM -- free the 240 KB WRAM read buffer
     * (jo_fs_read_file jo_malloc'd it; leaking 240 KB at boot would starve the
     * pre-Data.rsdk heap). */
    jo_free(d);
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
    p6_sfx_reg(slot, 0x0C)[0] = 0x0000;   /* TL=0 full volume (SFX were too quiet at 0x10) */
    p6_sfx_reg(slot, 0x0E)[0] = 0x0000;
    /* PITCH: per-entry OCT nibble (0xF=-1=22050, 0xE=-2=11025) FNS=0. */
    p6_sfx_reg(slot, 0x10)[0] = (unsigned short)((s_sfx[idx].oct & 0xF) << 11);
    p6_sfx_reg(slot, 0x12)[0] = 0x0000;
    p6_sfx_reg(slot, 0x14)[0] = 0x0000;
    p6_sfx_reg(slot, 0x16)[0] = (unsigned short)(7u << 13);               /* DISDL=7 */

    /* Re-trigger THIS slot ONLY. KYONEX is GLOBAL: a pulse acts on every slot's
     * KYONB at once -- KYONB=1 slots KEY-ON, KYONB=0 slots KEY-OFF, and a slot
     * whose KYONB is held =1 across pulses is "Ignore"d (SCSP_Manual.txt Fig 4.8,
     * :1802-1809). The prior "isolation" loop wrote KYONB=0 to ALL 31 other slots
     * before the pulse, so every SFX key-on KEY-OFFED every concurrent/prior SFX
     * -> gameplay SFX truncated to silence (root cause of the dead-SFX defect;
     * the lone test tone survived only because nothing keyed after it). Fix: leave
     * other slots' KYONB untouched (their sounds keep playing / are Ignore'd) and
     * re-trigger the target by keying it OFF (KYONB=0 + pulse) then ON. PCM8B only
     * for 8-bit entries; 16-bit (fmt=1) plays with PCM8B clear. */
    unsigned short pcm8b = s_sfx[idx].fmt ? 0u : (unsigned short)P6_SCSP_PCM8B;
    p6_sfx_reg(slot, 0x00)[0] = P6_SCSP_KYONEX;             /* key-off target only */
    for (i = 0; i < 96; ++i)
        (void)p6_sfx_reg(0, 0x0C)[0];                       /* settle the pulse   */
    p6_sfx_reg(slot, 0x00)[0] =
        (unsigned short)(P6_SCSP_KYONEX | P6_SCSP_KYONB | pcm8b | sa_hi);
    ++p6_w_sfx_keyons;
}

/* DIAGNOSTIC (2026-07-22, #325): key a CONTINUOUS LOOPING tone on slot 31 from
 * pack entry 0 (Ring), so we can tell BY EAR whether ANY direct SCSP slot reaches
 * RetroArch's audio output. Default ON; the SFX pump is disabled while it runs so
 * nothing keys-off the tone. If a steady drone is audible -> direct slots reach
 * output (SFX bug is triggering); if silent -> direct-slot path is dead in this
 * engine -> switch to the driver PCM API. p6_dbg_test_tone=0 (poke) restores SFX. */
__attribute__((used)) int p6_dbg_test_tone = 0;
__attribute__((used)) int p6_w_test_keyed = 0;
void p6_sfx_test_tone(void)
{
    int i, slot = 31;
    unsigned long sa;
    unsigned short sa_hi, sa_lo;
    if (!p6_dbg_test_tone || s_sfx_n == 0)
        return;
    sa    = P6_SFX_PCM_OFF + s_sfx[0].off;
    sa_hi = (unsigned short)((sa >> 16) & 0x000F);
    sa_lo = (unsigned short)(sa & 0xFFFF);
    p6_sfx_reg(slot, 0x02)[0] = sa_lo;
    p6_sfx_reg(slot, 0x04)[0] = 0;                                     /* LSA=0 */
    p6_sfx_reg(slot, 0x06)[0] = (unsigned short)(s_sfx[0].count - 1);  /* LEA */
    p6_sfx_reg(slot, 0x08)[0] = 0x001F;
    p6_sfx_reg(slot, 0x0A)[0] = 0x0000;                               /* RR0: don't release */
    p6_sfx_reg(slot, 0x0C)[0] = 0x0000;                               /* TL=0 loudest */
    p6_sfx_reg(slot, 0x0E)[0] = 0x0000;
    p6_sfx_reg(slot, 0x10)[0] = 0x7800;                               /* OCT -1 = 22050 */
    p6_sfx_reg(slot, 0x12)[0] = 0x0000;
    p6_sfx_reg(slot, 0x14)[0] = 0x0000;
    p6_sfx_reg(slot, 0x16)[0] = (unsigned short)(7u << 13);           /* DISDL=7 */
    for (i = 0; i < P6_SFX_NUM_SLOTS; ++i)
        if (i != slot)
            p6_sfx_reg(i, 0x00)[0] = 0x0000;
    for (i = 0; i < 128; ++i)
        (void)p6_sfx_reg(0, 0x0C)[0];
    /* LPCTL=01 normal loop (bits[10:9]) + PCM8B + KYONEX|KYONB -> continuous. */
    p6_sfx_reg(slot, 0x00)[0] =
        (unsigned short)(P6_SCSP_KYONEX | P6_SCSP_KYONB | (1u << 9)
                         | P6_SCSP_PCM8B | sa_hi);
    ++p6_w_test_keyed;
}

/* Per-frame pump: called by p6_audio_witness when a channel NEWLY enters
 * CHANNEL_SFX. Maps the armed soundID to a pack entry and keys-on the SCSP. */
void p6_sfx_pump(int soundID)
{
    int pk;
    if (p6_dbg_test_tone)          /* diagnostic tone owns the slots -- no SFX keyons */
        return;
    if (soundID < 0 || soundID >= 256)
        return;
    pk = s_slot2pack[soundID];
    if (pk >= 0)
        p6_sfx_keyon(pk);
}

#endif /* P6_FRONTEND_LOGOS */
