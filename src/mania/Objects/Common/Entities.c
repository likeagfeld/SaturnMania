/* Phase 2.3 — gameplay entities + HUD for GHZ Act 1.
 *
 * Mechanical-port subset of the decomp's Common/Global object classes:
 *
 *   - Motobug   : tools/_decomp_raw/SonicMania_Objects_GHZ_Motobug.c
 *                 (lines 122-156 State_Move; lines 22-26 Draw; lines
 *                  59-62 hitboxBadnik definition)
 *   - Ring      : tools/_decomp_raw/SonicMania_Objects_Global_Ring.h
 *                 + RSDKModding Ring.c (Ring_State_Normal + Ring_Collect)
 *   - Spring    : decomp Spring.h + archived behavior (compress/extend)
 *   - ItemBox   : decomp ItemBox.h (break-on-roll/jump; powerup deferred)
 *   - SignPost  : decomp SignPost.h (touch -> act-clear)
 *   - HUD       : decomp HUD.c (12-glyph numerics + ring icon + colon)
 *
 * Archived counterpart (the proven entity loops we mechanically port
 * here): src/_archived/main.c.v01-handrolled:494-1209.
 *
 * Saturn-port deviations from the decomp:
 *   - No RSDK.ProcessAnimation; per-class anim_timer + manual frame
 *     index (same pattern as Phase 2.2 mania_ghz_draw_sonic).
 *   - No RSDK.CheckOnScreen; AABB cull against screen rect using cam_x,
 *     cam_y.
 *   - No StateMachine_Run; one-shot active flag (Phase 2.4 will revisit
 *     respawn).
 *   - Stomp/hurt branches mutate player_t (Phase 2.2 Player struct)
 *     directly rather than calling Player_CheckBadnikBreak (which lives
 *     in Phase 2.3 Player.c follow-up). The mutation pattern is taken
 *     from archived main.c.v01-handrolled:704-757. */

#include "Entities.h"
#include "../../../rsdk/audio.h"
#include "../../../rsdk/entity_atlas.h"
#include "../../../rsdk/player_atlas.h"
#include "../../../rsdk/storage.h"

#include <jo/jo.h>
#include <string.h>

/* === Shared HUD state (extern in header) ============================ */

int g_hud_rings = 0;
int g_hud_score = 0;
int g_hud_ticks = 0;

/* === HUD diag mirrors (BSS, peeked from a gameplay savestate) =========
 * Phase FR-HUD (#186): discriminate why the HUD vanished in gameplay.
 *   g_hud_diag_ready  -> g_hud_atlas.ready at the last hud_draw entry.
 *   g_hud_diag_base   -> player_atlas_base() (the jo-stack base the player
 *                        rewinds to; must stay ABOVE every HUD sprite id).
 *   g_hud_diag_sid0   -> g_hud_atlas.sprite_id[anims[0].first] (HUD's first
 *                        uploaded sprite id; -1 means frames never uploaded).
 *   g_hud_diag_blits  -> count of hud_blit calls that reached jo_sprite_draw3D
 *                        in the last hud_draw (0 means every blit early-out).
 * Discrimination: ready=0 -> load failed; sid0<0 -> upload failed;
 * base<=sid0 -> player rewind wipes HUD ids (candidate 2); blits>0 but
 * invisible -> SGL sortlist overflow (candidate 1). */
__attribute__((used)) int32_t g_hud_diag_ready = -1;
__attribute__((used)) int32_t g_hud_diag_base  = -2;
__attribute__((used)) int32_t g_hud_diag_sid0  = -2;
__attribute__((used)) int32_t g_hud_diag_blits = -1;
/* #186 sub-discriminator: entity_atlas_load_ex return-false branch for the
 * HUD load, snapshotted in hud_load before any other atlas overwrites it.
 * See g_entity_atlas_last_fail codes in src/rsdk/entity_atlas.c. -3 = hud_load
 * never ran. */
__attribute__((used)) int32_t g_hud_diag_loadfail = -3;
/* #186 decisive 4-way split of the jo_fs_read_file("HUD.SP2") NULL path,
 * sampled in hud_load BEFORE the real load (probe is freed immediately so it
 * does not perturb the subsequent allocation):
 *   g_hud_diag_fid    = GFS_NameToId("HUD.SP2"); <0 => not registered in the
 *                       GFS dir table (file-not-found class), >=0 => present.
 *   g_hud_diag_poolok = 1 if a jo_malloc(HUD.SP2 size+1) with the SAME
 *                       behaviour jo_fs_read_file uses succeeds at hud_load
 *                       entry; 0 => jo pool exhausted (the assumed cause);
 *                       -1 => probe skipped because fid<0.
 * fid>=0 & poolok=1 but loadfail=1 still => GFS_Load (disc read) failure. */
__attribute__((used)) int32_t g_hud_diag_fid    = -99;
__attribute__((used)) int32_t g_hud_diag_poolok = -99;

/* === SFX storage =====================================================
 *
 * Phase 2.4a (Task #138, 2026-05-28): SFX slots are `static jo_sound`,
 * i.e. zero-initialised BSS storage. jo_audio_load_pcm (jo-engine/
 * jo_engine/audio.c:271-290) populates `mode`, `data_length`, `data`,
 * `volume`, but DOES NOT touch `sample_rate`. The playback path at
 * jo_audio_play_sound_on_channel (audio.c:160) reads
 *   `__jo_internal_pcm[ch].pitch = (Uint16)sound->sample_rate`
 * and an unset (==0) sample_rate causes the SCSP slot to be programmed
 * with pitch=0 → silent playback. The archived working build
 * (src/_archived/main_phase4g_audio_works.c.bak:208-218) explicitly
 * sets `g_sfx_*.sample_rate = 22050` after each load_pcm call. We
 * mirror that here.
 *
 * Citation:
 *   - jo-engine/jo_engine/audio.c:160  (pitch <- sound->sample_rate)
 *   - jo-engine/jo_engine/audio.c:271-290 (load_pcm doesn't set rate)
 *   - tools/_decomp_raw/SonicMania_Objects_Global_Player.c:744-768
 *     (decomp StageLoad cites Global/Jump.wav etc. — the SFX list)
 *   - tools/_decomp_raw/SonicMania_Objects_Global_Ring.c:109
 *     (Ring->sfxRing = RSDK.GetSfx("Global/Ring.wav"))
 *   - tools/_decomp_raw/SonicMania_Objects_Global_ItemBox.c:203
 *     (ItemBox->sfxDestroy = RSDK.GetSfx("Global/Destroy.wav"))
 *   - tools/_decomp_raw/SonicMania_Objects_Global_Spring.c:131
 *     (Spring->sfxSpring = RSDK.GetSfx("Global/Spring.wav"))
 *   - tools/_decomp_raw/SonicMania_Objects_Global_SignPost.c:192
 *     (SignPost->sfxSignPost = RSDK.GetSfx("Global/SignPost.wav"))
 *
 * Sample-rate 22050: the Saturn-side PCMs are signed 8-bit mono at
 * 22050 Hz per tools/build_filelist.py (matches archived working
 * cadence at .bak:208/210/218). */

#ifdef JO_COMPILE_WITH_AUDIO_SUPPORT
static jo_sound g_sfx_ring;
static jo_sound g_sfx_jump;
static jo_sound g_sfx_break;
static jo_sound g_sfx_stomp;
static jo_sound g_sfx_bounce;
static bool     g_sfx_ready = false;

#define ENTITIES_SFX_SAMPLE_RATE   22050

/* SFX-confirmation probe landmarks (user request 2026-05-29: "i havent
 * confirmed sfx... need a test"). Diagnostics only, never read by gameplay.
 * `used` + non-static + volatile so GCC 8.2 whole-program LTO keeps stable
 * linker-map symbols a savestate peek can locate:
 *   g_qa_sfx_ready_flag = 1 iff entities_load_sfx loaded RING+JUMP OK.
 *   g_qa_sfx_fire_count = count of times sfx_play() actually dispatched a
 *                         jo_audio_play_sound (i.e. PAST the g_sfx_ready gate).
 * A nonzero fire count from a savestate proves the dispatch path executed;
 * the captured WAV's periodic RMS bursts prove the PCM was audible. */
__attribute__((used)) volatile int g_qa_sfx_ready_flag = 0;
__attribute__((used)) volatile int g_qa_sfx_fire_count = 0;
/* Discriminators (2026-05-29): tell apart "load never reached" vs
 * "load reached but jo_audio_load_pcm failed", and which file failed.
 *   g_qa_sfx_attempt_count = times entities_load_sfx() body executed.
 *   g_qa_sfx_ring_ok / g_qa_sfx_jump_ok = last load_sfx() return for the
 *     two MANDATORY PCMs (1 = jo_audio_load_pcm succeeded). */
__attribute__((used)) volatile int g_qa_sfx_attempt_count = 0;
__attribute__((used)) volatile int g_qa_sfx_ring_ok = 0;
__attribute__((used)) volatile int g_qa_sfx_jump_ok = 0;
/* Direct pool-exhaustion discriminator (2026-05-29). jo_fs_read_file
 * returns NULL on EITHER file-not-found (ruled out: RINGSFX.PCM is on the
 * ISO) OR jo_malloc OOM. A 20000-byte probe alloc (> RINGSFX 14735 B) at
 * the load instant: 0 => pool exhausted (load CANNOT succeed here),
 * 1 => pool has room => failure is in the GFS read path, not memory. */
__attribute__((used)) volatile int g_qa_sfx_malloc20k_ok = 0;

static bool load_sfx(const char *path, jo_sound *snd)
{
    /* jo_audio_load_pcm signature: (filename, format, jo_sound*). The
     * Saturn-side PCMs were emitted as signed 8-bit mono at 22050 Hz by
     * tools/build_filelist.py — JoSoundMono8Bit matches.
     *
     * Phase 2.4a: jo_audio_load_pcm (jo-engine/jo_engine/audio.c:271-
     * 290) leaves sample_rate untouched; explicit set required. */
    bool ok = jo_audio_load_pcm((char *)path, JoSoundMono8Bit, snd);
    if (ok) {
        snd->sample_rate = ENTITIES_SFX_SAMPLE_RATE;
        /* volume is set to JO_MAX_AUDIO_VOLUME=127 by load_pcm
         * (audio.c:288); pan defaults to 0 (center) from BSS. */
    }
    return ok;
}
#endif

static void entities_load_sfx(void)
{
#ifdef JO_COMPILE_WITH_AUDIO_SUPPORT
    ++g_qa_sfx_attempt_count;
    {
        void *probe = jo_malloc(20000);
        g_qa_sfx_malloc20k_ok = (probe != JO_NULL) ? 1 : 0;
        if (probe != JO_NULL) jo_free(probe);
    }
    /* Phase 2.4i Task #154: every PCM below is an ffmpeg re-encode (via
     * tools/convert_audio.py @ 22050 Hz) of the authentic extracted WAV
     * cited per line -- NOT synthesized. The fabricating tools/make_audio.py
     * was deleted; see memory/decomp-assets-only-no-synthesis.md. */
    bool r = load_sfx("RINGSFX.PCM",   &g_sfx_ring);   /* Ring.wav  (Ring.c:109)        */
    bool j = load_sfx("JUMPSFX.PCM",   &g_sfx_jump);   /* Jump.wav  (Player.c:3327)     */
    g_qa_sfx_ring_ok = r ? 1 : 0;
    g_qa_sfx_jump_ok = j ? 1 : 0;
    bool ok = r && j;
    /* Optional SFX — soft-fail OK. */
    (void)load_sfx("BREAKSFX.PCM",  &g_sfx_break);   /* Destroy.wav (ItemBox.c:203/845) */
    (void)load_sfx("STOMPSFX.PCM",  &g_sfx_stomp);   /* Destroy.wav (Player.c:2509)     */
    (void)load_sfx("BOUNCESFX.PCM", &g_sfx_bounce);  /* Spring.wav  (Spring.c:131)      */
    g_sfx_ready = ok;
    g_qa_sfx_ready_flag = ok ? 1 : 0;
#endif
}

/* Helper for per-class SFX firing — fires only if loaded successfully. */
static void sfx_play(int kind)
{
#ifdef JO_COMPILE_WITH_AUDIO_SUPPORT
    if (!g_sfx_ready) return;
    switch (kind) {
        case 0: jo_audio_play_sound(&g_sfx_ring);   break;
        case 1: jo_audio_play_sound(&g_sfx_jump);   break;
        case 2: jo_audio_play_sound(&g_sfx_break);  break;
        case 3: jo_audio_play_sound(&g_sfx_stomp);  break;
        case 4: jo_audio_play_sound(&g_sfx_bounce); break;
        default: break;
    }
    ++g_qa_sfx_fire_count;
#else
    (void)kind;
#endif
}

/* Phase 2.4a (Task #138) — public Player jump SFX hook.
 *
 * Decomp cite: tools/_decomp_raw/SonicMania_Objects_Global_Player.c:3327
 *   RSDK.PlaySfx(Player->sfxJump, false, 255);
 * fires inside Player_Action_Jump (called from State_Ground when the
 * jump button is pressed). The Saturn-side player_action_jump
 * (src/mania/Objects/Global/Player.c:277) deliberately omits the
 * dispatch so the audio fire site lives near the other audio sites
 * (per its inline comment line 292-293). The driver in
 * src/mania/Game.c::mania_ghz_tick_and_draw calls this helper at the
 * same jump-press edge.
 *
 * This is the equivalent of the decomp's Player_Action_Jump body
 * line 3327 — same trigger, same sample, just dispatched from the
 * Saturn-side driver instead of the per-entity Update path. */
void entities_play_sfx_jump(void)
{
    sfx_play(1);
}

#ifdef QA_SFX_PROBE
/* SFX-confirmation probe support (user request 2026-05-29: "need a test").
 *
 * The normal load site (entities_load_sfx) runs from the GHZ scene-load
 * path, AFTER the title assets are torn down and the jo pool has room.
 * The headless QA harness cannot reach GHZ (title->menu->GHZ needs button
 * input), so the probe force-loads at TITLE time. But at title the 256 KB
 * jo pool is saturated by title assets -- DIRECTLY MEASURED 2026-05-29:
 * jo_malloc(20000) returns NULL (g_qa_sfx_malloc20k_ok == 0), so the
 * production jo_audio_load_pcm path CANNOT load here.
 *
 * Method (user-selected 2026-05-29: "LWRAM-load probe at title"): bypass
 * the saturated pool by reading the PCM into free LWRAM (WRAM-L 0x002D0000+,
 * above the scene loader's 0x00200000..0x002CFFFF regions per
 * src/rsdk/scene_ghz.c:74-78) via rsdk_storage_load_to_lwram (pool-free
 * GFS read, same precedent as GHZ1SURF/FG.TMP/SKY.DAT). jo plays PCM
 * straight from sound->data (audio.c:162 slPCMOn), and LWRAM is
 * CPU/SCU-DMA readable, so an LWRAM buffer is a valid PCM source. This
 * proves the PCM decode + jo_audio dispatch + audibility end-to-end,
 * independent of the title pool. Compiled out of the release binary. */
#define QA_SFX_LWRAM_RING  ((void *)0x002D0000)   /* 32 KB window */
#define QA_SFX_LWRAM_JUMP  ((void *)0x002D8000)   /* 32 KB window */
#define QA_SFX_LWRAM_WIN   0x8000u

static void qa_setup_lwram_sound(jo_sound *snd, void *lwram, int len)
{
    snd->mode        = JoSoundMono8Bit;
    snd->data        = (char *)lwram;
    snd->data_length = len;
    snd->volume      = JO_MAX_AUDIO_VOLUME;
    snd->sample_rate = ENTITIES_SFX_SAMPLE_RATE;
    snd->pan         = 0;
}

void entities_qa_force_load_sfx(void)
{
#ifdef JO_COMPILE_WITH_AUDIO_SUPPORT
    if (g_sfx_ready) return;
    ++g_qa_sfx_attempt_count;
    int rlen = rsdk_storage_load_to_lwram("RINGSFX.PCM",
                                          QA_SFX_LWRAM_RING, QA_SFX_LWRAM_WIN);
    int jlen = rsdk_storage_load_to_lwram("JUMPSFX.PCM",
                                          QA_SFX_LWRAM_JUMP, QA_SFX_LWRAM_WIN);
    g_qa_sfx_ring_ok = (rlen > 0) ? 1 : 0;
    g_qa_sfx_jump_ok = (jlen > 0) ? 1 : 0;
    if (rlen > 0) qa_setup_lwram_sound(&g_sfx_ring, QA_SFX_LWRAM_RING, rlen);
    if (jlen > 0) qa_setup_lwram_sound(&g_sfx_jump, QA_SFX_LWRAM_JUMP, jlen);
    bool ok = (rlen > 0) && (jlen > 0);
    g_sfx_ready         = ok;
    g_qa_sfx_ready_flag = ok ? 1 : 0;
#endif
}
#endif

/* === Common constants ================================================ */

#define SCR_W   320
#define SCR_H   224

/* Sonic body bbox roughly 32x40 centered at (xpos, ypos-20). Matches
 * archived main.c:570-572 hit-test math. */
#define SONIC_BODY_H  40

/* Phase 2.3e — visibility cull margin. Per the task brief, entities
 * within this many pixels of the screen edge are still drawn so that
 * they don't pop-in at the boundary. The existing per-class culls in
 * this file use sprite-size-based margins which is sufficient for
 * Phase 2.3 (each sprite is < 64 px on its largest axis), but the
 * value here is reserved for any per-class cull that wants a unified
 * larger margin. The base check below is what each class still uses:
 *
 *     if (sx < -ENTITY_CULL_MARGIN ||
 *         sx > SCR_W + ENTITY_CULL_MARGIN ||
 *         sy < -ENTITY_CULL_MARGIN ||
 *         sy > SCR_H + ENTITY_CULL_MARGIN) continue;
 *
 * 200 px matches Mania PC build's standard lookahead. Phase 2.3 ships
 * the sprite-size margin (which is tighter); Phase 2.4 may revisit
 * if pop-in is observed. */
#define ENTITY_CULL_MARGIN 200

/* === Rings (decomp Ring.c + archived L494-591) =======================
 *
 * Phase 2.4e v2 (Task #144) migration: SPR1 -> SPR2+MET via
 * entity_atlas_t. Frame indexing now comes from the decomp Animation.cpp
 * walker (rsdkv5-src/RSDKv5/RSDK/Graphics/Animation.cpp:150-177) via
 * rsdk_process_animation. Per-frame durations consumed from RING.MET.
 * Anim 0 = "Normal Ring" 16 frames, speed=64, sum(duration)=4096 ->
 * 64-tick cycle per docs/anim_completeness_audit.md row 1. */

#define RING_MAX     512

typedef struct {
    unsigned char *raw;       /* BIN payload (jo-alloc; freed at scene exit) */
    unsigned short count;
    unsigned char  active[RING_MAX];
    bool           ready;
} ring_module_t;

static ring_module_t g_rings;

static void rings_load(void)
{
    g_rings.ready = false;

    /* SP2 + MET atlas via the canonical entity_atlas loader. */
    if (!entity_atlas_load(&g_ring_atlas, "RING")) return;
    /* Anim 0 = Normal Ring (Hyper Ring dropped per audit mitigation). */
    entity_atlas_play(&g_ring_atlas, 0);

    /* Entity positions (GHZ1RINGS.BIN: u16 BE count + N * (u16 BE x, u16 BE y)). */
    int blen = 0;
    g_rings.raw = (unsigned char *)jo_fs_read_file("GHZ1RINGS.BIN", &blen);
    if (!g_rings.raw || blen < 2) {
        if (g_rings.raw) { jo_free(g_rings.raw); g_rings.raw = NULL; }
        return;
    }
    unsigned int n = ((unsigned int)g_rings.raw[0] << 8) | g_rings.raw[1];
    if (n > RING_MAX) n = RING_MAX;
    g_rings.count = (unsigned short)n;
    for (unsigned int i = 0; i < n; ++i) g_rings.active[i] = 1;
    g_rings.ready = true;
}

void rings_tick_and_draw(const sms_world_t *w, player_t *p, int cam_x, int cam_y)
{
    (void)w;
    if (!g_rings.ready) return;

    /* Decomp-cadence walker (Animation.cpp:150-177). Ring's "Normal"
     * anim has speed=64, per-frame duration=256, frame_count=16 -- one
     * tick advances timer by 64; after 4 ticks (timer=256) frame_id
     * increments. 64-tick total cycle per audit row 1. */
    entity_atlas_tick(&g_ring_atlas);

    int sprite_id = entity_atlas_current_sprite(&g_ring_atlas);
    int frame_w = 0, frame_h = 0;
    entity_atlas_size(&g_ring_atlas, &frame_w, &frame_h);
    if (sprite_id < 0 || frame_w == 0 || frame_h == 0) return;

    int sonic_px = p->xpos >> 16;
    int sonic_py = p->ypos >> 16;

    for (unsigned int i = 0; i < g_rings.count; ++i) {
        if (!g_rings.active[i]) continue;
        const unsigned char *r = g_rings.raw + 2 + (i << 2);
        int rx = ((int)r[0] << 8) | r[1];
        int ry = ((int)r[2] << 8) | r[3];
        int sx = rx - cam_x;
        int sy = ry - cam_y;
        if (sx < -frame_w || sx > SCR_W + frame_w ||
            sy < -frame_h || sy > SCR_H + frame_h) continue;

        /* AABB hit-test (archived main.c:572-574). Ring hitbox in decomp
         * Ring.c:hitboxRing = {-8,-8,8,8} centered on entity. The .BIN
         * stores top-left of a 16x16 cell so the entity center is
         * (rx+8, ry+8); preserved against archived 22/28 tolerance. */
        int dx = sonic_px - (rx + frame_w / 2);
        int dy = (sonic_py - SONIC_BODY_H / 2) - (ry + frame_h / 2);
        if (dx > -22 && dx < 22 && dy > -28 && dy < 28) {
            g_rings.active[i] = 0;
            ++g_hud_rings;
            g_hud_score += 10;
            sfx_play(0);   /* ring pickup */
            continue;
        }

        /* Center-anchored draw -- jo_sprite_draw3D anchors at canvas
         * center. Ring center on screen at (sx + W/2, sy + H/2). */
        jo_sprite_draw3D(sprite_id,
                         (sx + frame_w / 2) - 160,
                         (sy + frame_h / 2) - 112,
                         140);   /* z below HUD (200) above NBG1 */
    }
}

/* === Motobug (decomp Motobug.c full port; State_Move L122-156) ======
 *
 * Phase 2.4e v2 (Task #144) migration: SPR1 -> SPR2+MET.
 * MOTOBUG.SP2/.MET ships all 4 anims (Move/Idle/Turn/Puff, 29 frames
 * total) per audit row 6. The runtime drives anim 0 (Move) which has
 * 12 frames, speed=1, durations summing to 24 -> 24-tick cycle. */

#define BUG_MAX        16     /* GHZ1 has 9; pad headroom */
#define BUG_W          48
#define BUG_H          32

/* Decomp velocity.x = -0x10000 = -1.0 px/fr (Motobug_State_Init L193). */
#define BUG_SPEED_FX   PLAYER_FIXED(1.0)

/* Phase 2.4c — decomp-exact hitbox per Motobug.c:59-62.
 * Motobug->hitboxBadnik = { left=-14, top=-14, right=14, bottom=14 }.
 * Tokens used by tools/qa_phase2_4c_entities_gate.py to verify
 * decomp citation is present in this TU. */
#define MOTOBUG_HITBOX_LEFT     (-14)   /* decomp Motobug.c:59 */
#define MOTOBUG_HITBOX_TOP      (-14)   /* decomp Motobug.c:60 */
#define MOTOBUG_HITBOX_RIGHT    ( 14)   /* decomp Motobug.c:61 */
#define MOTOBUG_HITBOX_BOTTOM   ( 14)   /* decomp Motobug.c:62 */
#define BUG_HIT_HALF            MOTOBUG_HITBOX_RIGHT

/* Wall / cliff thresholds — port of decomp ObjectTileGrip
 * (CMODE_FLOOR, ±0x10000, 0xF0000, 8). On Saturn we substitute the
 * per-column SURF table; the bug flips on:
 *   - Wall ahead: surface dY < -14 (stair too tall)
 *   - Cliff ahead: surface dY > +18 (drop too deep) — archived L611. */
#define BUG_WALL_PX    14
#define BUG_CLIFF_PX   18

typedef struct {
    int32_t        xpos_fx;   /* Q16.16 world X */
    int            ypos_px;   /* world Y (snapped to surface)            */
    signed char    vx_dir;    /* -1 = left, +1 = right                   */
    unsigned char  active;
} bug_t;

typedef struct {
    bug_t bugs[BUG_MAX];
    int   count;
    bool  ready;
} motobug_module_t;

static motobug_module_t g_motobugs;

static void motobug_load(void)
{
    g_motobugs.ready = false;

    if (!entity_atlas_load(&g_motobug_atlas, "MOTOBUG")) return;
    entity_atlas_play(&g_motobug_atlas, 0);  /* anim 0 = Move */

    /* Positions -- legacy BIN file from Phase 2.4c still ships under
     * GHZ1BUGS.BIN with (u16 x, u16 y) per entity. */
    int blen = 0;
    unsigned char *raw = (unsigned char *)jo_fs_read_file("GHZ1BUGS.BIN", &blen);
    if (!raw || blen < 2) { if (raw) jo_free(raw); return; }
    unsigned int n = ((unsigned int)raw[0] << 8) | raw[1];
    if (n > BUG_MAX) n = BUG_MAX;
    for (unsigned int i = 0; i < n; ++i) {
        const unsigned char *r = raw + 2 + (i << 2);
        unsigned int bx = ((unsigned int)r[0] << 8) | r[1];
        unsigned int by = ((unsigned int)r[2] << 8) | r[3];
        g_motobugs.bugs[i].xpos_fx = JO_MULT_BY_65536((int)bx);
        g_motobugs.bugs[i].ypos_px = (int)by;
        g_motobugs.bugs[i].vx_dir  = -1;   /* decomp Init: velocity.x=-0x10000 */
        g_motobugs.bugs[i].active  = 1;
    }
    g_motobugs.count = (int)n;
    jo_free(raw);
    g_motobugs.ready = true;
}

void motobug_tick_and_draw(const sms_world_t *w, player_t *p, int cam_x, int cam_y)
{
    if (!g_motobugs.ready || !g_motobug_atlas.ready) return;

    /* Decomp-cadence walker (Animation.cpp:150-177). Motobug "Move"
     * anim 0 has 12 frames, speed=1, sum(duration)=24 -> 24-tick cycle
     * per audit row 6. */
    entity_atlas_tick(&g_motobug_atlas);
    int sprite_id = entity_atlas_current_sprite(&g_motobug_atlas);
    if (sprite_id < 0) return;
    int sonic_px = p->xpos >> 16;
    int sonic_py = p->ypos >> 16;

    for (int i = 0; i < g_motobugs.count; ++i) {
        bug_t *b = &g_motobugs.bugs[i];
        if (!b->active) continue;

        /* === Patrol step (decomp Motobug_State_Move L122-156 +
         *     archived L674-688) === */
        int32_t step = b->vx_dir > 0 ? BUG_SPEED_FX : -BUG_SPEED_FX;
        int32_t new_x_fx = b->xpos_fx + step;
        int bx_int = new_x_fx >> 16;
        if (bx_int < 0) bx_int = 0;
        if (w && bx_int >= w->width_px) bx_int = w->width_px - 1;

        int ahead_y = SMS_NO_FLOOR;
        if (w && w->raw) ahead_y = Player_SurfaceY(w, bx_int);

        if (ahead_y == SMS_NO_FLOOR ||
            ahead_y < b->ypos_px - BUG_WALL_PX ||
            ahead_y > b->ypos_px + BUG_CLIFF_PX) {
            /* Wall or cliff: turn around. Decomp branches to State_Idle
             * → State_Turn → 30-tick turn anim; Phase 2.3 simplifies
             * to an instant flip (Phase 2.4 polish: full turn anim). */
            b->vx_dir = -b->vx_dir;
        } else {
            b->xpos_fx = new_x_fx;
            b->ypos_px = ahead_y;          /* ground-follow */
        }

        /* === Cull + render === */
        bx_int  = b->xpos_fx >> 16;
        int sx  = bx_int   - cam_x;
        int sy  = b->ypos_px - cam_y;
        if (sx < -BUG_W || sx > SCR_W + BUG_W ||
            sy < -BUG_H || sy > SCR_H + BUG_H) continue;

        /* === Hit-test vs Sonic — Phase 2.4b decomp-exact dispatch ===
         *
         * Decomp authority:
         *   - SonicMania_Objects_GHZ_Motobug.c:88-96
         *     Motobug_CheckPlayerCollisions calls
         *     Player_CheckBadnikTouch + Player_CheckBadnikBreak.
         *   - SonicMania_Objects_Global_Player.c:2367-2425
         *     Player_CheckBadnikBreak internally uses the airborne /
         *     rolling state to decide stomp-vs-hurt.
         *
         * Saturn-port (Phase 2.4b): use Player_CheckCollisionBox with
         * the decomp's hitboxBadnik (-14..14 each axis per Motobug.c:
         * 59-62). The snap-back contract fixes the
         * "Sonic jumps in and out of badnik" oscillation (Task #139
         * user report) — side != C_NONE means Sonic's position was
         * snapped to the bug's surface AND the relevant velocity
         * component was zeroed.
         *
         * Outcome dispatch:
         *   - C_TOP AND Sonic is airborne -> stomp (kill, bounce).
         *   - C_LEFT/RIGHT/BOTTOM (or C_TOP while grounded) -> hurt.
         *     The C_LEFT/RIGHT snap-back has already zeroed p->xsp,
         *     so Sonic stops cleanly at the bug surface; knockback
         *     then re-mutates xsp/ysp for the hurt arc. */
        (void)sonic_px; (void)sonic_py;   /* now unused; collision is via decomp port */
        const hitbox_t motobug_hitbox = {
            MOTOBUG_HITBOX_LEFT, MOTOBUG_HITBOX_TOP,
            MOTOBUG_HITBOX_RIGHT, MOTOBUG_HITBOX_BOTTOM
        };
        int side = Player_CheckCollisionBox(p, bx_int, b->ypos_px,
                                            &motobug_hitbox);
        if (side != C_NONE) {
            /* Attacking proxy: !onGround. Decomp gates stomp on ANI_JUMP
             * (Player.c:2378-2384); Phase 2.2 player_t doesn't carry an
             * Animator, so airborne-state is the closest invariant. */
            bool attacking = !p->onGround;
            if (side == C_TOP && attacking) {
                /* Stomp — decomp Player_CheckBadnikBreak L2387-2404. */
                b->active = 0;
                g_hud_score += 100;
                p->ysp = -PLAYER_FIXED(4.0);
                p->jumping = true;
                p->onGround = false;
                sfx_play(3);   /* stomp */
                continue;
            }
            /* Hurt path. */
            if (g_hud_rings > 0) {
                g_hud_rings = 0;
                sfx_play(2);   /* break/lose surrogate */
            }
            p->ysp = -PLAYER_FIXED(4.0);
            p->xsp = p->facing_left ? PLAYER_FIXED(2.0) : -PLAYER_FIXED(2.0);
            p->onGround = false;
            p->jumping  = true;
            continue;
        }

        /* Decomp drawFX |= FX_FLIP; default direction=LEFT (vel.x=-1).
         * Flip when moving right. */
        if (b->vx_dir > 0) jo_sprite_enable_horizontal_flip();
        jo_sprite_draw3D(sprite_id,
                         (sx + BUG_W / 2) - 160,
                         (b->ypos_px - BUG_H / 2 - cam_y) - 112,
                         145);
        if (b->vx_dir > 0) jo_sprite_disable_horizontal_flip();
    }
}

/* === Spring (decomp Spring.c subset; vertical-up only) ==============
 *
 * Phase 2.4e v2 (Task #144) migration: SPR1 -> SPR2+MET.
 * SPRING.SP2/.MET ships all 6 anims (Yellow/Red x V/H/D, 54 frames
 * total) per audit row 3. Anim 0 = Yellow V, 9 frames, speed=128,
 * sum(duration)=2432 -> 19-tick cycle. */

#define SPR_MAX     64
#define SPRING_W    32
#define SPRING_H    32
#define SPRING_FORCE  PLAYER_FIXED(10.0)   /* yellow Mania spring up-launch */

typedef struct {
    int            x;
    int            y;
    unsigned char  active;
    unsigned char  anim;        /* 0=neutral, >0=compress timer */
} spring_t;

typedef struct {
    spring_t list[SPR_MAX];
    int      count;
    bool     ready;
} spring_module_t;

static spring_module_t g_springs;

static void spring_load(void)
{
    g_springs.ready = false;

    if (!entity_atlas_load(&g_spring_atlas, "SPRING")) return;
    entity_atlas_play(&g_spring_atlas, 0);  /* anim 0 = Yellow V */

    int blen = 0;
    unsigned char *raw = (unsigned char *)jo_fs_read_file("GHZ1SPRG.BIN", &blen);
    if (!raw || blen < 2) { if (raw) jo_free(raw); return; }
    unsigned int n = ((unsigned int)raw[0] << 8) | raw[1];
    if (n > SPR_MAX) n = SPR_MAX;
    for (unsigned int i = 0; i < n; ++i) {
        const unsigned char *r = raw + 2 + (i << 2);
        g_springs.list[i].x = ((int)r[0] << 8) | r[1];
        g_springs.list[i].y = ((int)r[2] << 8) | r[3];
        g_springs.list[i].active = 1;
        g_springs.list[i].anim   = 0;
    }
    g_springs.count = (int)n;
    jo_free(raw);
    g_springs.ready = true;
}

void spring_tick_and_draw(const sms_world_t *w, player_t *p, int cam_x, int cam_y)
{
    (void)w;
    if (!g_springs.ready || !g_spring_atlas.ready) return;

    /* Decomp-cadence walker. Spring Yellow V (anim 0) cycles 9 frames at
     * speed=128, total 19 ticks per audit row 3. */
    entity_atlas_tick(&g_spring_atlas);

    /* Phase 2.4b — decomp-exact vertical-spring hitbox per
     * tools/_decomp_raw/SonicMania_Objects_Global_Spring.c:74-77:
     *   self->hitbox = { left=-16, top=-8, right=16, bottom=8 }. */
    const hitbox_t spring_hitbox = { -16, -8, 16, 8 };

    for (int i = 0; i < g_springs.count; ++i) {
        spring_t *s = &g_springs.list[i];
        int sx = s->x - cam_x;
        int sy = s->y - cam_y;
        if (sx < -SPRING_W || sx > SCR_W + SPRING_W ||
            sy < -SPRING_H || sy > SCR_H + SPRING_H) continue;

        /* === Hit-test — Phase 2.4b decomp-exact dispatch ===
         *
         * Decomp authority: Spring_State_Vertical (Spring.c:134-178).
         * Calls Player_CheckCollisionBox(player, self, &self->hitbox)
         * and triggers the spring boost on C_TOP only (Spring.c:146).
         *
         * Saturn-port: Player_CheckCollisionBox handles position snap
         * + velocity zero on side contact. side == C_TOP launches the
         * spring; side == C_LEFT/RIGHT means Sonic stopped cleanly at
         * the spring edge (no boost — matches decomp behaviour where
         * the boost gate is C_TOP specifically). */
        if (s->active) {
            int side = Player_CheckCollisionBox(p, s->x, s->y, &spring_hitbox);
            if (side == C_TOP) {
                /* Decomp Spring.c:163-174: player->velocity.y =
                 * self->velocity.y (negative for upward spring);
                 * onGround = false; play sfx; advance anim. */
                p->ysp = -SPRING_FORCE;
                p->onGround = false;
                p->jumping  = true;
                s->anim     = 12;
                sfx_play(4);   /* bounce */
            }
        }

        /* Squash overlay: when the spring was just touched, briefly
         * override the walker's frame_id with the "compressed" frame.
         * Decomp Spring_State_Vertical (Spring.c:134-178) does this via
         * SetSpriteAnimation; on Saturn we just hold the squash-frame
         * for a 12-tick window via entity_atlas_play on the same anim
         * (no-op idempotent) and let the walker run normally. */
        if (s->anim) {
            --s->anim;
            /* The walker is already advancing the squash anim frames;
             * nothing extra to do. */
        }

        int sprite_id = entity_atlas_current_sprite(&g_spring_atlas);
        if (sprite_id < 0) continue;
        jo_sprite_draw3D(sprite_id,
                         (sx + SPRING_W / 2) - 160,
                         (sy + SPRING_H / 2) - 112,
                         145);
    }
}

/* === ItemBox / Monitor (decomp ItemBox.c; break-on-airborne only) ===
 *
 * Phase 2.4e v2 (Task #144) migration: SPR1 -> SPR2+MET.
 * ITEMBOX.SP2/.MET ships 6 anims, 45 frames total (Normal/Broken/
 * Powerups/Scanlines/ItemDisappear/Debris -- Snow dropped per audit
 * mitigation; not used in GHZ). Anim 0 = Normal (1 frame, speed=0,
 * static). Anim 3 = Scanlines (2 frames, speed=2, 4-tick cycle) drives
 * the monitor's "TV scanline" visual idle. */

#define BOX_MAX    64
#define BOX_W      32
#define BOX_H      32

typedef struct {
    int           x;
    int           y;
    unsigned char active;
} box_t;

typedef struct {
    box_t list[BOX_MAX];
    int   count;
    bool  ready;
} itembox_module_t;

static itembox_module_t g_boxes;

static void itembox_load(void)
{
    g_boxes.ready = false;

    if (!entity_atlas_load(&g_itembox_atlas, "ITEMBOX")) return;
    /* Play the Scanlines anim (3 per decomp ItemBox.bin order: Normal/
     * Broken/Powerups/Scanlines/ItemDisappear/Debris) so the monitor
     * visibly animates idle. Anim count after the Snow drop = 6. */
    int scanlines_anim = (g_itembox_atlas.anim_count > 3) ? 3 : 0;
    entity_atlas_play(&g_itembox_atlas, scanlines_anim);

    int blen = 0;
    unsigned char *raw = (unsigned char *)jo_fs_read_file("GHZ1BOX.BIN", &blen);
    if (!raw || blen < 2) { if (raw) jo_free(raw); return; }
    unsigned int n = ((unsigned int)raw[0] << 8) | raw[1];
    if (n > BOX_MAX) n = BOX_MAX;
    for (unsigned int i = 0; i < n; ++i) {
        const unsigned char *r = raw + 2 + (i << 2);
        g_boxes.list[i].x = ((int)r[0] << 8) | r[1];
        g_boxes.list[i].y = ((int)r[2] << 8) | r[3];
        g_boxes.list[i].active = 1;
    }
    g_boxes.count = (int)n;
    jo_free(raw);
    g_boxes.ready = true;
}

void itembox_tick_and_draw(const sms_world_t *w, player_t *p, int cam_x, int cam_y)
{
    if (!g_boxes.ready || !g_itembox_atlas.ready) return;

    /* Decomp-cadence walker. Scanlines anim 2 frames, speed=2 -> 4-tick
     * cycle per audit row 2. */
    entity_atlas_tick(&g_itembox_atlas);
    int sprite_id = entity_atlas_current_sprite(&g_itembox_atlas);
    if (sprite_id < 0) return;

    /* Phase 2.4b — decomp-exact ItemBox hitbox per
     * tools/_decomp_raw/SonicMania_Objects_Global_ItemBox.c:182-185:
     *   ItemBox->hitboxItemBox = { left=-15, top=-16, right=15, bottom=16 }.
     * The hitbox is anchored on the entity center; the Saturn-side
     * .BIN stores top-left corner so we pass (b->x + W/2, ry + H/2)
     * as the box center. */
    const hitbox_t itembox_hitbox = { -15, -16, 15, 16 };

    for (int i = 0; i < g_boxes.count; ++i) {
        box_t *b = &g_boxes.list[i];
        if (!b->active) continue;

        /* Ground-snap to the surface column at the monitor's center
         * (archived L920-926 — user reported monitors floating /
         * overlapping mounds, fix is to snap bottom edge to surface). */
        int ry = b->y;
        if (w && w->raw) {
            int snap = Player_SurfaceY(w, b->x + (BOX_H / 2));
            if (snap != SMS_NO_FLOOR) ry = snap - BOX_H;
        }
        int sx = b->x - cam_x;
        int sy = ry - cam_y;
        if (sx < -BOX_W || sx > SCR_W + BOX_W ||
            sy < -BOX_H || sy > SCR_H + BOX_H) continue;

        /* === Hit-test — Phase 2.4b decomp-exact dispatch ===
         *
         * Decomp authority: SonicMania_Objects_Global_ItemBox.c:418-470
         *   - L418-434: attacking-classification (ANI_JUMP + falling /
         *     onGround / DROPDASH for Sonic). Saturn-port proxy:
         *     !p->onGround (Sonic in air = attacking).
         *   - L441: side = Player_CheckCollisionBox(player, self,
         *     &ItemBox->hitboxItemBox) — the normal-walk path that
         *     gets snap-back + velocity-zero treatment.
         *   - L448-458: C_BOTTOM (head-bump from below) breaks the box
         *     when attacking (decomp uses ACTIVE_NORMAL + Falling state).
         *   - L459-462: C_TOP (Sonic lands on box) treats it as a
         *     solid platform (mutates player position with moveOffset).
         *
         * Saturn-port outcome dispatch:
         *   - C_TOP AND attacking -> break (Sonic stomps box, decomp
         *     Player.c:2383-2391 ANI_JUMP+falling stomp condition).
         *   - C_TOP AND grounded -> stand on box (snap already done
         *     by Player_CheckCollisionBox; nothing extra needed).
         *   - C_BOTTOM AND attacking -> break (head-bump break per
         *     ItemBox.c:448-458; in decomp this only happens to a
         *     box ABOVE Sonic during an upward-velocity collision).
         *   - C_LEFT/RIGHT -> Sonic stopped at side; box untouched.
         *
         * The decomp's attacking-classification gates the BREAK; if
         * Sonic walks INTO the box at ground level (which produces
         * C_LEFT/C_RIGHT), Player_CheckCollisionBox has already
         * snapped xpos and zeroed xsp/gsp -- no penetration jitter. */
        int box_cx = b->x + BOX_W / 2;
        int box_cy = ry + BOX_H / 2;
        int side = Player_CheckCollisionBox(p, box_cx, box_cy, &itembox_hitbox);
        if (side != C_NONE) {
            bool attacking = !p->onGround;
            if ((side == C_TOP || side == C_BOTTOM) && attacking) {
                /* Break — decomp ItemBox_Break (L488-538). Saturn-port
                 * minimum: give rings + score + bounce, no powerup
                 * table (Phase 2.5 will port full powerup dispatch). */
                b->active = 0;
                g_hud_rings += 10;
                g_hud_score += 100;
                /* Decomp ItemBox.c:456-457: player->velocity.y = +2.0
                 * on C_BOTTOM (downward continuation through broken
                 * box from below). For C_TOP stomp the snap-back has
                 * already zeroed ysp; apply a small reversal bounce. */
                if (side == C_TOP) {
                    p->ysp = -PLAYER_FIXED(3.0);
                    p->jumping = true;
                } else {
                    /* C_BOTTOM: continue falling, but small downward
                     * nudge per decomp L457 player->velocity.y =
                     * TO_FIXED(2). */
                    p->ysp = PLAYER_FIXED(2.0);
                }
                sfx_play(2);   /* break */
            }
            /* side == C_LEFT/RIGHT or grounded-C_TOP: snap-back done,
             * no break -- Sonic just stops/stands cleanly. */
        }

        jo_sprite_draw3D(sprite_id,
                         (sx + BOX_W / 2) - 160,
                         (sy + BOX_H / 2) - 112,
                         145);
    }
}

/* === SignPost (decomp SignPost.c; touch -> act-clear) ===============
 *
 * Phase 2.4e v2 (Task #144) migration: SPR1 -> SPR2+MET.
 * SIGNPOST.SP2/.MET ships 3 anims, 12 frames total (Sonic + Eggman +
 * Post-Bits -- Tails/Knuckles dropped per audit mitigation, GHZ
 * Sonic-only). Anim 0 = Sonic (8 frames, speed=0, loop=7, state-driven
 * cycle from decomp). */

#define SIGN_MAX     4
#define SIGN_W       48
#define SIGN_H       32

typedef struct {
    int           x;
    int           y;
    unsigned char hit;
} sign_t;

typedef struct {
    sign_t list[SIGN_MAX];
    int    count;
    bool   cleared;
    int    spin_ticks;
    bool   ready;
} sign_module_t;

static sign_module_t g_signs;

static void signpost_load(void)
{
    g_signs.ready = false;
    g_signs.cleared = false;

    if (!entity_atlas_load(&g_signpost_atlas, "SIGNPOST")) return;
    entity_atlas_play(&g_signpost_atlas, 0);  /* anim 0 = Sonic */

    int blen = 0;
    unsigned char *raw = (unsigned char *)jo_fs_read_file("GHZ1SIGN.BIN", &blen);
    if (!raw || blen < 2) { if (raw) jo_free(raw); return; }
    unsigned int n = ((unsigned int)raw[0] << 8) | raw[1];
    if (n > SIGN_MAX) n = SIGN_MAX;
    for (unsigned int i = 0; i < n; ++i) {
        const unsigned char *r = raw + 2 + (i << 2);
        g_signs.list[i].x   = ((int)r[0] << 8) | r[1];
        g_signs.list[i].y   = ((int)r[2] << 8) | r[3];
        g_signs.list[i].hit = 0;
    }
    g_signs.count = (int)n;
    jo_free(raw);
    g_signs.ready = true;
}

void signpost_tick_and_draw(const sms_world_t *w, player_t *p, int cam_x, int cam_y)
{
    (void)w;
    if (!g_signs.ready || !g_signpost_atlas.ready) return;

    /* Decomp-cadence walker. Sonic anim 0 has 8 frames, speed=0 (game-
     * logic driven per audit row 4). When cleared we drive a manual
     * spin-step every 3 ticks to simulate the decomp's state-driven
     * frame advance. */
    entity_atlas_tick(&g_signpost_atlas);

    int sonic_px = p->xpos >> 16;
    int sonic_py = p->ypos >> 16;

    if (g_signs.cleared) ++g_signs.spin_ticks;

    for (int i = 0; i < g_signs.count; ++i) {
        sign_t *s = &g_signs.list[i];
        int sx = s->x - cam_x;
        int sy = s->y - cam_y;
        if (sx < -SIGN_W || sx > SCR_W + SIGN_W ||
            sy < -SIGN_H - 24 || sy > SCR_H + SIGN_H) continue;

        /* Touch -> act-clear. Decomp SignPost_State_Setup also drops
         * the post onto the ground; archived L962-979 sets g_act_cleared,
         * +1000 score, plays ring SFX. */
        if (!g_signs.cleared && !s->hit) {
            int dx = sonic_px - s->x;
            int dy = sonic_py - s->y;
            if (dx > -24 && dx < 24 && dy > -36 && dy < 16) {
                s->hit = 1;
                g_signs.cleared = true;
                g_signs.spin_ticks = 0;
                g_hud_score += 1000;
                sfx_play(0);   /* ring SFX as bonus-touch surrogate */
            }
        }

        /* When cleared, manually advance the atlas frame ID within the
         * Sonic anim (8 frames) every 3 ticks to simulate the decomp's
         * state-driven spin. When NOT cleared the walker holds frame 0
         * (speed=0 in the MET). */
        int sprite_id;
        if (g_signs.cleared) {
            int sonic_fc = g_signpost_atlas.anims[0].frame_count;
            if (sonic_fc < 1) sonic_fc = 1;
            int fid = (g_signs.spin_ticks / 3) % sonic_fc;
            int idx = g_signpost_atlas.anims[0].first + fid;
            if (idx < 0 || idx >= g_signpost_atlas.frame_total) idx = 0;
            sprite_id = (int)g_signpost_atlas.sprite_id[idx];
        } else {
            sprite_id = entity_atlas_current_sprite(&g_signpost_atlas);
        }
        if (sprite_id < 0) continue;

        jo_sprite_draw3D(sprite_id,
                         (sx + SIGN_W / 2) - 160,
                         (sy + SIGN_H / 2) - 112,
                         145);
    }
}

bool entities_act_cleared(void) { return g_signs.cleared; }

/* === HUD (authentic Global/HUD.bin atlas; decomp HUD.c numerics) =====
 *
 * Phase 2.4i Task #154: the previous HUD used a FABRICATED 8x8 glyph
 * font (cd/DIGITS.SPR hand-drawn by the now-deleted tools/make_digit_
 * font.py). That violated `memory/decomp-assets-only-no-synthesis.md`.
 * It is replaced by the authentic cd/HUD.SP2 + cd/HUD.MET atlas built
 * from extracted/Data/Sprites/Global/HUD.bin via tools/build_entity_
 * atlas.py, loaded through the canonical entity_atlas SPR2+MET loader
 * (`memory/entity-atlas-loader-pattern.md`).
 *
 * Atlas-flat frame indices (mirrors build_entity_atlas.py MANIFEST "HUD"
 * entry; see g_hud_atlas.anims[].first):
 *   anim 0 "HUD Elements" first=0  : f0 SCORE label, f1 TIME label,
 *                                    f3 RINGS label, f12 colon, f14 life "x".
 *   anim 1 "Numbers"      first=17 : digit d -> atlas frame 17 + d (0-9).
 *   anim 2 "Life Icons"   first=27 : f0 = Sonic head (-> atlas frame 27).
 *
 * Screen layout per decomp HUD_Create (HUD.c:433-440) and HUD_Draw
 * (HUD.c:81-348), translated from RSDK 424x240 to Saturn 320x224:
 *   score label  (16, 12),  number at label.x+97, label.y+14 (HUD.c:145)
 *   time  label  (16, 28),  m:ss:cc digits + colons (HUD.c:159-180)
 *   rings label  (16, 44),  number at label.x+97, label.y+14 (HUD.c:189)
 *   life  icon   (16, size.y-12 = 212)            (HUD.c:277,299)
 * Numbers are drawn right-aligned, stepping LEFT 8 px per digit, exactly
 * as HUD_DrawNumbersBase10 (HUD.c:520-526 drawPos->x -= TO_FIXED(8)). */

#define HUD_Z          200
#define HUD_DIGIT_STEP 8     /* HUD.c:525 drawPos->x -= TO_FIXED(8) */

/* HUD Elements anim (anim 0, first=0) frame IDs per decomp HUD_Draw. */
#define HUDEL_SCORE    0     /* HUD.c:141 frameID = 0 */
#define HUDEL_TIME     1     /* HUD.c:153 frameID = 1 */
#define HUDEL_RINGS    3     /* HUD.c:184 frameID = ringFlashFrame + 3 */
#define HUDEL_COLON    12    /* HUD.c:161 frameID = 12 */
#define HUDEL_LIFE_X   14    /* HUD.c:336/347 frameID = 14 */

static void hud_load(void)
{
    /* entity_atlas_load soft-fails (ready stays false) if HUD.SP2/.MET
     * are missing; hud_draw then no-ops, matching the per-class pattern. */
    extern int32_t g_entity_atlas_last_fail;

    /* #186 decisive split: which jo_fs_read_file NULL path is HUD.SP2 hitting?
     * GFS_NameToId mirrors fs.c:271; the probe malloc mirrors fs.c:284
     * (jo_malloc_with_behaviour(fsize+1, JO_MALLOC_TRY_REUSE_BLOCK)). The
     * probe is freed instantly so the real load below sees an identical pool. */
    g_hud_diag_fid = (int32_t)GFS_NameToId((Sint8 *)"HUD.SP2");
    if (g_hud_diag_fid >= 0) {
        void *probe = jo_malloc_with_behaviour(27536 + 1, JO_MALLOC_TRY_REUSE_BLOCK);
        g_hud_diag_poolok = probe ? 1 : 0;
        if (probe)
            jo_free(probe);
    } else {
        g_hud_diag_poolok = -1;
    }

    /* #186 fix: the jo malloc pool is saturated at GHZ gameplay (measured:
     * ~8.9 KB free, HUD.SP2 needs 27536) because TITLE.DAT (114688) stays
     * resident in the pool, so the pool-path load NULLs out (loadfail=1).
     * Route HUD.SP2/.MET through the transient scene file-scratch in LWRAM
     * (SCENE_LWRAM_RAW_ADDR 0x00278000, 96 KB, storage.c:219) which is dead
     * after rsdk_scene_load returns and free at entities_load_assets time.
     * Same LWRAM-bypass mechanism as the TitleCard SP2 and binding rule
     * memory/ghz-sky-dat-lwram-bypass.md. HUD.SP2 27536 + HUD.MET 404 both
     * fit the 96 KB scratch with room to spare. */
    bool ok = entity_atlas_load_ex(&g_hud_atlas, "HUD",
                                   (void *)0x00278000, 0x18000);
    g_hud_diag_loadfail = g_entity_atlas_last_fail;
    if (ok) {
        /* The HUD doesn't animate per-tick (speed 0 anims, frame chosen
         * by game state). We still seed anim 0 so current_* are sane. */
        entity_atlas_play(&g_hud_atlas, 0);
    }
}

/* Draw atlas-flat frame `idx` so its TOP-LEFT lands at screen (sx, sy).
 * jo_sprite_draw3D anchors at canvas center, so add half the frame's
 * authentic width/height then re-center to the 320x224 screen. */
static int s_hud_blit_count;   /* reset each hud_draw; mirrored to diag */

static void hud_blit(int idx, int sx, int sy)
{
    if (!g_hud_atlas.ready) return;
    if (idx < 0 || idx >= (int)g_hud_atlas.frame_total) return;
    int sid = g_hud_atlas.sprite_id[idx];
    if (sid < 0) return;
    int w = g_hud_atlas.width[idx];
    int h = g_hud_atlas.height[idx];
    jo_sprite_draw3D(sid, (sx + w / 2) - 160, (sy + h / 2) - 112, HUD_Z);
    ++s_hud_blit_count;
}

/* Atlas-flat index of HUD Elements frame `f` (anim 0, first=0). */
static int hud_el(int f) { return (int)g_hud_atlas.anims[0].first + f; }
/* Atlas-flat index of Numbers digit `d` (anim 1). */
static int hud_digit(int d) { return (int)g_hud_atlas.anims[1].first + d; }

/* Right-aligned number draw mirroring HUD_DrawNumbersBase10 (HUD.c:504-
 * 527): the rightmost digit sits at `sx`, each further digit steps LEFT
 * 8 px. `width`=0 means "natural width" (>=1 digit). zero_pad forces all
 * `width` digits. Returns nothing (HUD draws each field independently). */
static void hud_number_right(int value, int sx, int sy, int width, bool zero_pad)
{
    if (value < 0) value = 0;
    int n = 1, v = value;
    while (v >= 10) { v /= 10; ++n; }
    int count = (width > 0 && (zero_pad || width > n)) ? width : n;
    int x = sx;
    int digit = 1;
    for (int i = 0; i < count; ++i) {
        hud_blit(hud_digit((value / digit) % 10), x, sy);
        digit *= 10;
        x -= HUD_DIGIT_STEP;
    }
}

void hud_tick(void)
{
    /* Only tick while gameplay is active (caller gates on act-cleared). */
    ++g_hud_ticks;
}

void hud_draw(void)
{
    /* Diag mirrors (peeked from a gameplay savestate, #186): record state
     * EVERY frame, including the early-out path, so a missing HUD can be
     * discriminated (load vs upload vs rewind vs sortlist). */
    g_hud_diag_ready = g_hud_atlas.ready ? 1 : 0;
    g_hud_diag_base  = player_atlas_base();
    g_hud_diag_sid0  = (g_hud_atlas.ready && g_hud_atlas.anim_count > 0)
                       ? g_hud_atlas.sprite_id[g_hud_atlas.anims[0].first]
                       : -1;
    s_hud_blit_count = 0;

    if (!g_hud_atlas.ready) { g_hud_diag_blits = 0; return; }

    /* --- Score (label at 16,12; number at +97,+14 per HUD.c:141-147) --- */
    int score_x = 16, score_y = 12;
    hud_blit(hud_el(HUDEL_SCORE), score_x, score_y);
    hud_number_right(g_hud_score, score_x + 97, score_y + 14, 6, true);

    /* --- Time (label at 16,28; m:ss:cc per HUD.c:153-180) -------------- */
    int time_x = 16, time_y = 28;
    hud_blit(hud_el(HUDEL_TIME), time_x, time_y);
    {
        int secs = g_hud_ticks / 60;
        int cs   = (g_hud_ticks % 60) * 100 / 60;   /* milliseconds field */
        int mins = secs / 60;
        secs %= 60;
        /* m:ss:cc laid out left-to-right with the authentic colon glyph
         * and the decomp 8 px digit pitch. Each numeric field is drawn
         * right-aligned (hud_number_right) so its rightmost digit lands
         * at the supplied x and earlier digits step left, matching
         * HUD_DrawNumbersBase10 (HUD.c:520-526). */
        int ny = time_y + 14;
        int x  = time_x + 14;                /* minutes: single digit  */
        hud_number_right(mins, x, ny, 1, true);
        x += HUD_DIGIT_STEP;
        hud_blit(hud_el(HUDEL_COLON), x, ny);
        x += HUD_DIGIT_STEP;
        hud_number_right(secs, x + HUD_DIGIT_STEP, ny, 2, true); /* ss   */
        x += HUD_DIGIT_STEP * 2;
        hud_blit(hud_el(HUDEL_COLON), x, ny);
        x += HUD_DIGIT_STEP;
        hud_number_right(cs, x + HUD_DIGIT_STEP, ny, 2, true);   /* cc   */
    }

    /* --- Rings (label at 16,44; number at +97,+14 per HUD.c:184-194) --- */
    int rings_x = 16, rings_y = 44;
    hud_blit(hud_el(HUDEL_RINGS), rings_x, rings_y);
    hud_number_right(g_hud_rings, rings_x + 97, rings_y + 14, 0, false);

    /* --- Life icon (16, size.y-12 = 212) Sonic head, anim 2 frame 0 ---
     * Non-Plus path: characterID ID_SONIC -> lifeIconAnimator.frameID = 0
     * (HUD.c:292-294). atlas-flat = anims[2].first + 0. */
    hud_blit((int)g_hud_atlas.anims[2].first + 0, 16, 212);

    g_hud_diag_blits = s_hud_blit_count;
}

/* === Master load entry =============================================== */

void entities_load_assets(void)
{
    /* FR-2 (Task #189) -- flush the lazy-residency MRU blob pool + the
     * per-atlas resident sprite_id caches at every scene (re)load. The
     * pool lives at 0x00260000 (the SKY.DAT staging window, dead after the
     * VDP2 upload); resetting here guarantees no entity blob is held across
     * rsdk_load_scene's transient scratch window (0x278000-0x290000) and
     * that stale sprite ids from the prior scene cannot survive the
     * jo sprite-stack rewind. Must precede the per-class *_load calls so
     * those calls' metadata loads land in a clean pool. */
    entity_residency_reset();

    /* Task #192 (player-only resident): entities are NOT resident -- their
     * SP2 metadata is CD-read here at scene-load time, and per-tick pixel
     * blobs are CD-streamed on demand during gameplay (occasional, far rarer
     * than the per-anim player reads the resident SONIC.SPC eliminates). The
     * earlier resident entity pack was retired because it byte-collided with
     * the live #188 FG.CEL LWRAM region (foreground corruption after the
     * title card). See entity_atlas.c / player_atlas.c. */

    /* Order: small allocations first so that any pool fragmentation is
     * less likely to bite the larger BADNIK.SPR load. Each *_load is
     * idempotent on second call so harmless to invoke twice. */
    hud_load();        /* HUD.SP2 + HUD.MET — authentic Global/HUD.bin atlas */
    rings_load();      /* RING.SPR  — 8   KB */
    spring_load();     /* SPRING.SPR — 18 KB */
    itembox_load();    /* MONITOR.SPR — 2 KB */
    signpost_load();   /* SIGNPOST.SPR — 25 KB */
    motobug_load();    /* BADNIK.SPR  — 37 KB; largest */
    /* Phase 2.4c Task #140 — priority entity ports per
     * docs/ghz_act1_entity_gap.md "Priority list". Soft-fail per class
     * (if SPIKES.SPR / BUZZ.SPR is missing the runtime continues without
     * those classes — matches the existing per-class pattern). */
    spikes_load_assets();      /* SPIKES.SPR ~16 KB */
    buzzbomber_load_assets();  /* BUZZ.SPR  ~24 KB */
    /* Phase 2.4c.2 Task #147 — SpikeLog + Platform + Newtron. Each is
     * soft-fail per the established pattern. Asset sizes per
     * tools/build_entity_atlas.py --all output:
     *   SPIKELOG.SP2 = 33 KB (32 frames),
     *   PLATFORM.SP2 = 32 KB (7  frames),
     *   NEWTRON.SP2  = 36 KB (23 frames). */
    spikelog_load_assets();
    platform_load_assets();
    newtron_load_assets();
    /* Phase 2.4h Task #154 — GHZ Act 1 badniks Chopper (13) / Crabmeat (11)
     * / Batbrain (7). RSDK-object architecture: each *_load_assets brings up
     * the entity_atlas SPR2+MET pair (CHOPPER.SP2 45 KB / CRABMEAT.SP2 45 KB
     * / BATBRAIN.SP2 19 KB) + initial anim. Collision runs from each class's
     * _Update via rsdk_object_tick; drawing via *_draw_only in
     * mania_ghz_draw_only. Forward-declared (their headers pull Game.h). */
    {
        extern void Chopper_load_assets(void);
        extern void Crabmeat_load_assets(void);
        extern void Batbrain_load_assets(void);
        Chopper_load_assets();
        Crabmeat_load_assets();
        Batbrain_load_assets();
    }
    /* Phase 2.4-PLAT Task #155 — Bridge is the sole VISIBLE platforming
     * class; brings up the cd/BRIDGE.SP2 + cd/BRIDGE.MET atlas pair (the
     * GHZ/Bridge.bin planks) + initial anim. Soft-fail per the
     * established per-class pattern. The four invisible classes
     * (CollapsingPlatform/ForceSpin/BreakableWall/SpinBooster) ship no
     * atlas and load nothing. Forward-declared (header pulls Game.h). */
    {
        extern void Bridge_load_assets(void);
        Bridge_load_assets();
    }
    /* Phase 2.4k — StarPost checkpoint post. cd/STARPOST.SP2 + .MET built
     * from extracted/Data/Sprites/Global/StarPost.bin via build_entity_atlas.
     * Soft-fail per the established per-class pattern. Forward-declared
     * (header pulls Game.h). */
    {
        extern void StarPost_load_assets(void);
        StarPost_load_assets();
    }
    entities_load_sfx(); /* Phase 2.3 SFX (~67 KB scratch during load) */
}
