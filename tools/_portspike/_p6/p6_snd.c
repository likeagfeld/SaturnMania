/* ============================================================================
 * p6_snd.c -- P6.6b (Task #209): play an ENGINE-loaded SFX through the SCSP.
 *
 * DIRECT SCSP SLOT BACKEND, after the Coup reference implementation
 * (D:\Claude Saturn Skill Documentation\Coup...\examples\coup\coup_audio.c
 * :59-316 -- the skill's documented WORKING homebrew): PCM uploaded to
 * Sound RAM once, SCSP slots 28-31 (untouched by the SGL M68K driver map
 * {0xFF,0xFF,0xFF,0xFF} jo_audio_init loads) programmed directly from the
 * SH-2, KYONEX-isolated key-on. Coup's header records WHY not slPCMOn:
 * "The previous slPCMOn approach went through the M68K driver which
 * degraded SFX quality" -- and MEASURED here 2026-06-10, jo's slPCMOn
 * accepted every play (slSndPCMNum=1, free channel found) yet moved ZERO
 * sample bytes into SCSP RAM across 464 plays in this build. Direct slot
 * programming makes the data path deterministic AND gives qa_p6_scsp.py
 * an exact-region witness: the engine's byte-exact S16 buffer sits at a
 * FIXED Sound RAM address.
 *
 * SCSP semantics per ST-077-R2 (slot regs at 0x25B00000 + slot*0x20):
 *   +0x00 SA_CTRL: KYONEX(1<<12) KYONB(1<<11) LPCTL=00 one-shot,
 *         PCM8B=0 (16-bit), SA[19:16]
 *   +0x02 SA[15:0]   +0x04 LSA   +0x06 LEA (sample count - 1)
 *   +0x08 ENV1 AR=31 instant     +0x0A ENV2 RR=31
 *   +0x0C TL (0=loudest)         +0x10 PITCH: OCT[14:11] FNS[9:0],
 *         rate = 44100 * 2^OCT * (1+FNS/1024) -> 44100 = OCT 0, FNS 0
 *   +0x16 MIXLVL: DISDL=7 max direct send, center pan
 * KYONEX is GLOBAL (fires key-on/off for ALL slots from their KYONB
 * bits): clear KYONB everywhere first, then key the target (Coup
 * scsp_safe_keyon, coup_audio.c:176-203).
 *
 * Sound RAM layout: samples at +0x6C000 (clear of the M68K driver's
 * program/data area, Coup's measured-safe choice). SH-2 and SCSP are both
 * big-endian -- direct 16-bit copies.
 * ========================================================================== */

#include <jo/jo.h> /* P6.6c: jo_audio_play_cd_track (proven CD-DA start) */

#define P6_SND_SRAM_BASE  0x25A00000UL
#define P6_SND_REG_BASE   0x25B00000UL
/* MenuBleep relocated to the TOP 8 KB (0x7E000-0x80000) so the SFX pack
 * (p6_sfx.c) gets the contiguous 0x40000-0x7E000 = 248 KB window -- enough for
 * the FULL GHZ SFX set with no drops (user 2026-07-21). MenuBleep.wav is ~7 KB.
 * SGL uses nothing above 0x40000 (MEASURED savestate SCSP dumps). */
#define P6_SND_PCM_OFFSET 0x7E000UL

#define P6_SCSP_KYONEX (1u << 12)
#define P6_SCSP_KYONB  (1u << 11)
#define P6_SND_SLOT    28
#define P6_SND_NSLOTS  32

/* qa_p6_scsp.py A3 sanity witness: bytes uploaded to Sound RAM. */
__attribute__((used)) int p6_w_snd_upbytes = 0;

static unsigned int s_snd_count = 0; /* sample count of the uploaded SFX */
static int s_snd_rr = 0;             /* round-robin over slots 28-31 */

static volatile unsigned short *p6_slot_reg(int slot, int offset)
{
    return (volatile unsigned short *)(P6_SND_REG_BASE
                                       + (unsigned long)slot * 0x20UL
                                       + (unsigned long)offset);
}

/* One-time upload of the engine-converted S16 MONO 44.1 kHz buffer. */
void p6_snd_upload(const void *pcm16, unsigned int bytes)
{
    volatile unsigned short *dst =
        (volatile unsigned short *)(P6_SND_SRAM_BASE + P6_SND_PCM_OFFSET);
    const unsigned short *src = (const unsigned short *)pcm16;
    unsigned int i, words = bytes / 2;

    for (i = 0; i < words; ++i)
        dst[i] = src[i];
    s_snd_count      = words;
    p6_w_snd_upbytes = (int)bytes;
}

/* Key a one-shot playback of the uploaded buffer on a free high slot. */
void p6_snd_play(void)
{
    unsigned long sa;
    unsigned short sa_hi, sa_lo;
    int slot, i;

    if (!s_snd_count)
        return;
    slot     = P6_SND_SLOT + s_snd_rr;
    s_snd_rr = (s_snd_rr + 1) & 3;

    sa    = P6_SND_PCM_OFFSET;
    sa_hi = (unsigned short)((sa >> 16) & 0x000F);
    sa_lo = (unsigned short)(sa & 0xFFFF);

    *p6_slot_reg(slot, 0x02) = sa_lo;
    *p6_slot_reg(slot, 0x04) = 0;                                  /* LSA  */
    *p6_slot_reg(slot, 0x06) = (unsigned short)(s_snd_count - 1);  /* LEA  */
    *p6_slot_reg(slot, 0x08) = 0x001F;                             /* AR31 */
    *p6_slot_reg(slot, 0x0A) = 0x001F;                             /* RR31 */
    *p6_slot_reg(slot, 0x0C) = 0x0010;                             /* TL   */
    *p6_slot_reg(slot, 0x0E) = 0x0000;
    *p6_slot_reg(slot, 0x10) = 0x0000;            /* OCT 0 FNS 0 = 44100  */
    *p6_slot_reg(slot, 0x12) = 0x0000;
    *p6_slot_reg(slot, 0x14) = 0x0000;
    *p6_slot_reg(slot, 0x16) = (unsigned short)(7u << 13); /* DISDL=7 ctr */

    /* Re-trigger THIS slot ONLY. KYONEX is GLOBAL: a pulse KEY-OFFS every slot
     * whose KYONB=0. Writing KYONB=0 to all other slots first (the old code)
     * KEY-OFFED every concurrent voice -- truncating overlapping SFX to silence
     * (SCSP_Manual.txt Fig 4.8, :1802-1809; same root cause fixed in p6_sfx.c).
     * Leave other slots' KYONB intact; key the target off then on. */
    *p6_slot_reg(slot, 0x00) = P6_SCSP_KYONEX;              /* key-off target only */
    for (i = 0; i < 96; ++i)
        (void)*p6_slot_reg(0, 0x0C);                        /* settle the pulse   */
    *p6_slot_reg(slot, 0x00) =
        (unsigned short)(P6_SCSP_KYONEX | P6_SCSP_KYONB | sa_hi);
}

/* P6.6c (Task #209): start CD-DA for the engine's PlayStream request.
 * The Saturn AudioDevice::HandleStreamLoad (p6_io_main.cpp) resolves the
 * stream name to a CUE audio track; this routes the start through jo's
 * PROVEN CD-DA path (jo_audio_play_cd_track -> CDC_CdPlay, audio.c:60-83;
 * repeat -> CDC PM 0x0F endless, ST-38-R1 p.24). CD-DA mixes through the
 * SCSP EXTS inputs -- unaffected by the P6.6b direct-slot SFX KYONB
 * clears (slots and EXTS are separate paths). */
void p6_cdda_play(int track, int loop)
{
    /* 2026-07-17 (user: title music "starts, stops, restarts at the ring
     * animation"): IDEMPOTENCE GUARD -- a repeat request for the track that is
     * already playing is a no-op instead of a CDC_CdPlay RESTART. The decomp
     * fires Music_PlayTrack for a track that may already be audible (e.g. the
     * TitleSetup FlashIn PlayTrack after the scene-load arm, TitleSetup.c:
     * 137-150); on PC the stream engine dedups by buffer, on CD-DA a re-play
     * seeks to the track start = an audible restart. Track CHANGES still
     * restart (correct: zone transitions). Cleared on a data-read displacement?
     * No -- the CDC keeps the play context; a displaced play RESUMES via the
     * repeat mode (PM endless, ST-38-R1 p.24), so the guard stays valid. */
    static int s_cdda_current = -1;
    if (track <= 0 || track > 99)
        return;
    if (track == s_cdda_current)
        return;
    s_cdda_current = track;
    jo_audio_play_cd_track(track, track, loop != 0);
}

/* ---- #325 GHZ-BGM CD-DA displacement DIAGNOSTIC + poke-gated re-assert -------
 * MEASURED 2026-07-21 (RA netmem, chain c35d682): the GHZ GreenHill1 arm FIRES
 * (p6_w_ghz_bgm_arm=1, p6_w_str_track=2) yet gameplay BGM is silent -- and the
 * silence lives in the CD block, which netmem CANNOT read. This poll mirrors the
 * CD block status into WRAM so "is CD-DA actually playing at GHZ gameplay"
 * becomes measurable (RED-gate-first). p6_w_cdc_status == 3 (CDC_ST_PLAY) => CD-DA
 * is live and the silence is elsewhere (SCSP EXTS route / track content); == 1/2
 * (PAUSE/STANDBY) => a gameplay CD data-read displaced it (AIZ re-issues per beat
 * and stays audible; GHZ arms track 2 exactly ONCE with no re-assert). The
 * re-assert (re-issue the track when displaced) is poke-gated
 * (p6_dbg_cdda_reassert, default 0 = shipping-identical no-op) so the SAME build
 * measures the cause AND lets the fix be validated live before it is made
 * default. jo_audio_get_cd_status wraps CDC_GetCurStat (jo-engine/audio.c). */
__attribute__((used)) int p6_w_cdc_status      = -1; /* last CD status (3=PLAY,1=PAUSE,2=STANDBY) */
__attribute__((used)) int p6_w_cdc_polls       = 0;  /* # status polls run (proves the poll fired) */
__attribute__((used)) int p6_w_cdc_notplay     = 0;  /* # polls at GHZ with status PAUSE/STANDBY */
__attribute__((used)) int p6_w_cdc_reasserts   = 0;  /* # CD-DA re-issues fired */
/* SHIPPING FIX (default 1): VALIDATED live 2026-07-21 -- at GHZ gameplay the CD
 * block read PAUSE persistently (arm fired, str=2, but CD-DA displaced by the
 * load-settle data reads); poking this to 1 fired ONE re-assert and the CD block
 * flipped PAUSE->PLAY and STAYED play for 75+s of autorun (notplay froze). The
 * re-assert is self-limiting (fires ONLY on PAUSE/STANDBY, silent once PLAY), so
 * it recovers a displaced stream without a restart-loop. Set to 0 to disable. */
__attribute__((used)) int p6_dbg_cdda_reassert = 1;

void p6_cdda_poll_status(int track)
{
    int st = jo_audio_get_cd_status();
    p6_w_cdc_status = st;
    ++p6_w_cdc_polls;
    /* PLAY(3)=healthy; BUSY(0)/SEEK(4)=transient data-read in flight (will settle);
     * PAUSE(1)/STANDBY(2)=CD-DA genuinely stopped -> displaced. */
    if (st == 1 || st == 2) {
        ++p6_w_cdc_notplay;
        if (p6_dbg_cdda_reassert && track > 0) {
            /* direct re-issue: bypass p6_cdda_play's same-track idempotence guard,
             * which would otherwise suppress a re-play of the already-"current" track. */
            jo_audio_play_cd_track(track, track, 1);
            ++p6_w_cdc_reasserts;
        }
    }
}
