/* Phase 3.1 Path B — Sega-only abbreviated pre-title splash.
 *
 * Decomp citation (state-machine adaptation source):
 *   tools/_decomp_raw/SonicMania_Objects_Menu_LogoSetup.c
 *     :32-44   Create()         self->state = State_ShowLogos +
 *                                self->timer = 1024 (RSDK fade unit)
 *     :96-111  State_ShowLogos  countdown timer -= 16/tick; on <=0 plays
 *                                sfxSega and chains to FadeToNext
 *     :113-122 State_FadeToNext frame counter to 120; transitions to
 *                                NextLogos
 *     :124-143 State_NextLogos  scrolls ScreenInfo->position.y by
 *                                SCREEN_YSIZE per logo OR LoadScene
 *
 * Path B Saturn-side simplification (per Task #123 acceptance):
 *   * Skip the State_NextLogos vertical-scroll cycle entirely (only one
 *     logo on Saturn — the next "scene" is the existing Title backdrop
 *     reached via the natural fall-through to title_state_tick once
 *     s_logo_state == LOGO_STATE_DONE).
 *   * Compress State_ShowLogos's 64-tick fade-in + State_FadeToNext's
 *     120-tick black-out into:
 *       LOGO_STATE_SHOW_SEGA: 60 ticks (logo visible at full intensity)
 *       LOGO_STATE_FADE_OUT:  60 ticks (logo blended toward black)
 *     The Saturn-side fade is approximated by gradually switching the
 *     sprite atrb from CL_Replace (opaque) to CL_Trans (50/50 blend
 *     with the back-color) for the second half of the fade window —
 *     a single Saturn-native primitive per ST-013-R3 §5.5.4 PMOD field.
 *     This is less smooth than the decomp's RSDK.FillScreen ramp but
 *     matches what's achievable in a single VDP1 pass and fits Path B's
 *     simplicity ceiling.
 *
 * Audio: skipped (the constraint from Task #123 states "If Sega.wav
 * SFX is absent, skip audio — don't ship broken audio path").  Wiring
 * the SFX cue would require:
 *   1. Building Sega.wav -> Saturn PCM (.PCM extension already used
 *      by build pipeline for JUMPSFX.PCM etc).
 *   2. Registering it through rsdk_get_sfx + rsdk_play_sfx the same
 *      way TitleSetup_StageLoad does for sfxMenuAccept.
 *   3. Confirming the SCSP/PCM driver is initialised by the time
 *      LogoSetup runs (currently audio init is post-engine-init).
 * That work belongs to Phase 3.2 once the visual splash is verified
 * GREEN.
 *
 * Self-contained ATL loader: deliberately does NOT route through
 * TitleAssets.c.  TitleAssets.c registers its asset slots + builds
 * synthetic anim lists that the Title-scene resolver consumes.
 * LogoSetup runs BEFORE the Title scene loads, so the TitleAssets
 * pipeline isn't necessarily ready yet (depends on call-order in
 * jo_main).  Keeping the LOGOS.ATL loader local to this file gives
 * us a clean independent dependency.
 *
 * Saturn build references read end-to-end before writing this file
 * (per CLAUDE.md §4.0 binding session methodology):
 *   * D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\
 *     ST-013-R3-061694.pdf §5.5.4 PMOD field encoding (CCM bits 5:3,
 *     ECdis bit 7, Color-Calc bits 2:0)
 *   * D:\Claude Saturn Skill Documentation\sega_saturn_docs\VDP1_Manual.txt
 *     same content as ST-013-R3 (cross-check)
 *   * NOV96_DTS/LIBRARY/SBL6/SEGASMP/SPR/SMPSPR2/SMPSPR20.C basic 4-bpp
 *     sprite draw pattern
 *   * NOV96_DTS/LIBRARY/SBL6/SEGASMP/GFS/* GFS_NameToId/Open/Fread/Seek
 *     contract (Sint32 nsct = sector count, bsize = buffer size; jo
 *     convention is 16 KB staging buffer)
 *   * src/mania/Objects/Title/TitleAssets.c::load_title3d_atlas as the
 *     proven template (Phase 1.32, v4 format, sector-aligned reads)
 *   * src/mania/Objects/Title/TitleAssets.c::title3d_bg_draw_frame for
 *     the half-transparent VDP1 sprite draw pattern (CL_Trans OR-in).
 */

#include "LogoSetup.h"

#include <jo/jo.h>
#include <string.h>

#include "../../../rsdk/storage.h"

/* ----------------------------------------------------------------------
 * LOGOS.ATL v4 file-format constants — shared with the TS family.
 * --------------------------------------------------------------------*/
#define LOGOS_MAGIC       0x5453   /* 'TS' shared atlas magic         */
#define LOGOS_VERSION     0x0004
#define LOGOS_PAL_SIZE    16
#define LOGOS_PAL_CRAM    1984     /* 16 below TITLE3D's 2000          */
#define LOGOS_HEADER_SZ   44
#define LOGOS_ANIM_SZ     8
#define LOGOS_FRAME_SZ    14
#define LOGOS_SECTOR_SZ   2048
#define LOGOS_STAGING_SZ  8192     /* 4 sectors -- Sega frame = 5.6 KB */

/* ----------------------------------------------------------------------
 * VDP1 PMOD bits used in this file (per ST-013-R3 §5.5.4).
 * --------------------------------------------------------------------*/
#define VDP1_CCM_BANK16    (0 << 3)
#define VDP1_ECD_DISABLE   (1 << 7)
/* CL_Trans (Color-Calc bits 2:0 = 3) per SGL SL_DEF.H:194 — same constant
 * already pulled in via <jo/jo.h>. */

/* ----------------------------------------------------------------------
 * Internal asset state — Sega-frame metadata + jo sprite id.
 * --------------------------------------------------------------------*/
static int      s_sega_sprite_id      = -1;
static int      s_sega_palette_cram   = 0;
static int      s_sega_width          = 0;
static int      s_sega_height         = 0;
static int16_t  s_sega_pivot_x        = 0;
static int16_t  s_sega_pivot_y        = 0;
static int      s_logos_loaded        = 0;

/* ----------------------------------------------------------------------
 * State-machine state.  s_logo_state starts at LOGO_STATE_INIT; the first
 * tick attempts the asset load and on success advances to SHOW_SEGA.  If
 * the load fails (missing file, GFS error, sprite-add full) the state
 * jumps directly to LOGO_STATE_DONE so the title scene proceeds normally
 * — degrades cleanly.
 * --------------------------------------------------------------------*/
static logo_state_t s_logo_state = LOGO_STATE_INIT;
static unsigned int s_logo_timer = 0;
static int          s_logo_started = 0;

/* Sub-state durations (60 Hz ticks). */
#define SEGA_SHOW_TICKS     60
#define SEGA_FADE_TICKS     60

/* be_u16/be_s16/be_u32 — same big-endian helpers used in TitleAssets.c. */
static inline unsigned short be_u16(const unsigned char *p) {
    return ((unsigned short)p[0] << 8) | p[1];
}
static inline short be_s16(const unsigned char *p) {
    return (short)be_u16(p);
}
static inline unsigned int be_u32(const unsigned char *p) {
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] <<  8) |  (unsigned int)p[3];
}

/* ----------------------------------------------------------------------
 * load_logos_atlas — open cd/LOGOS.ATL via GFS, parse the v4 header,
 * upload the 16-color palette to CRAM, and stream the single Sega
 * frame into a VDP1 4-bpp sprite slot.  Mirrors load_electricity_atlas
 * (single-anim, single-frame) verbatim — see TitleAssets.c:907 for the
 * proven template.
 *
 * Returns 0 on success, -1 on any failure (file missing, bad header,
 * GFS error, sprite-add failure).  On failure leaves s_logos_loaded = 0
 * and the state machine treats this as a clean skip.
 * --------------------------------------------------------------------*/
static int load_logos_atlas(void)
{
    if (s_logos_loaded) return 0;

    Sint32 fid = GFS_NameToId((Sint8 *)"LOGOS.ATL");
    if (fid < 0) return -1;
    GfsHn gfs = GFS_Open(fid);
    if (gfs == JO_NULL) return -1;

    unsigned char *staging = (unsigned char *)jo_malloc(LOGOS_STAGING_SZ);
    if (!staging) { GFS_Close(gfs); return -1; }

    /* Sector 0 holds header(44) + 1 anim record(8) + 1 frame record(14)
     * = 66 bytes followed by the start of the pixel pool.  The Sega frame
     * is 5,568 bytes of pixel data — spans 3 sectors after the 66-byte
     * meta header.  We read up to LOGOS_STAGING_SZ (4 sectors = 8 KB) at
     * once which covers the entire atlas in a single GFS_Fread. */
    Sint32 r0 = GFS_Fread(gfs, 4, staging, LOGOS_STAGING_SZ);
    if (r0 <= 0) {
        jo_free(staging); GFS_Close(gfs); return -1;
    }

    const unsigned char *hdr = staging;
    if (be_u16(&hdr[0]) != LOGOS_MAGIC ||
        be_u16(&hdr[2]) != LOGOS_VERSION) {
        jo_free(staging); GFS_Close(gfs); return -1;
    }
    int anim_count = (int)be_u16(&hdr[4]);
    if (anim_count != 1) {
        jo_free(staging); GFS_Close(gfs); return -1;
    }
    if (be_u16(&hdr[6]) != LOGOS_PAL_SIZE) {
        jo_free(staging); GFS_Close(gfs); return -1;
    }

    /* Palette to CRAM band 1984..1999 (16 entries below TITLE3D's 2000). */
    {
        volatile jo_color *cram = ((volatile jo_color *)JO_VDP2_CRAM) + LOGOS_PAL_CRAM;
        for (int i = 0; i < LOGOS_PAL_SIZE; ++i) {
            cram[i] = (jo_color)be_u16(&hdr[8 + i * 2]);
        }
        s_sega_palette_cram = LOGOS_PAL_CRAM;
    }

    /* Anim record at offset 44. */
    const unsigned char *anim_rec = &staging[LOGOS_HEADER_SZ];
    int total_frame_count = (int)be_u16(&anim_rec[0]);
    if (total_frame_count != 1) {
        jo_free(staging); GFS_Close(gfs); return -1;
    }

    /* Frame record at offset 44 + 8 = 52. */
    const unsigned char *fr = &staging[LOGOS_HEADER_SZ + LOGOS_ANIM_SZ];
    int  w   = (int)be_u16(&fr[0]);
    int  h   = (int)be_u16(&fr[2]);
    int  px  = (int)be_s16(&fr[4]);
    int  py  = (int)be_s16(&fr[6]);
    /* Pool starts at offset 52 + 14 = 66 — pixel data is contiguous in the
     * staging buffer already, no second read needed for the 5,568-byte
     * Sega pixel pool (8 KB staging covers it). */
    unsigned int pool_off_in_buf = LOGOS_HEADER_SZ + LOGOS_ANIM_SZ + LOGOS_FRAME_SZ;
    unsigned int frame_bytes = ((unsigned int)w * (unsigned int)h) >> 1;

    if (w <= 0 || h <= 0 || frame_bytes == 0 ||
        pool_off_in_buf + frame_bytes > (unsigned int)r0) {
        jo_free(staging); GFS_Close(gfs); return -1;
    }

    /* Stream the Sega frame into VDP1 char-RAM via jo_sprite_add_4bits_image. */
    jo_img_8bits img;
    img.width  = (short)w;
    img.height = (short)h;
    img.data   = &staging[pool_off_in_buf];
    int sid = jo_sprite_add_4bits_image(&img);

    jo_free(staging);
    GFS_Close(gfs);

    if (sid < 0) return -1;

    s_sega_sprite_id = sid;
    s_sega_width     = w;
    s_sega_height    = h;
    s_sega_pivot_x   = (int16_t)px;
    s_sega_pivot_y   = (int16_t)py;
    s_logos_loaded   = 1;
    return 0;
}

/* ----------------------------------------------------------------------
 * draw_sega_logo — issue one VDP1 sprite draw centred on the Saturn
 * 320x224 viewport with optional half-transparency.
 *
 * Per the decomp Sega frame metadata: source 187x58 padded to 192x58,
 * pivot (-93,-29) — the pivot lies at the top-LEFT of the visible
 * canvas, so the entity origin is the bottom-right corner of the
 * sprite.  RSDK convention places the entity at world (256, 112) (the
 * screen centre for the 320x224 viewport's title scene); jo_sprite_
 * draw3D's coordinate system anchors at (0, 0) = screen centre, so
 * jo_x = canvas_centre_x - 256, jo_y = canvas_centre_y - 112.
 *
 *   canvas_centre_x = entity_x + pivot_x + width/2  = 256 + (-93) + 96 = 259
 *   canvas_centre_y = entity_y + pivot_y + height/2 = 112 + (-29) + 29 = 112
 *
 * So jo coords are (+3, 0).  That places the Sega logo centred (within
 * 3 px of true centre) on the 320x224 viewport — visually centred to
 * the user.
 * --------------------------------------------------------------------*/
static void draw_sega_logo(int half_transparency)
{
    if (!s_logos_loaded || s_sega_sprite_id < 0) return;

    int canvas_cx = 256 + (int)s_sega_pivot_x + (s_sega_width  >> 1);
    int canvas_cy = 112 + (int)s_sega_pivot_y + (s_sega_height >> 1);
    int jo_x = canvas_cx - 256;
    int jo_y = canvas_cy - 112;

    FIXED pos[XYZS];
    SPR_ATTR attr;

    pos[X] = toFIXED((float)jo_x);
    pos[Y] = toFIXED((float)jo_y);
    pos[Z] = toFIXED((float)100);     /* far front — above all VDP2 layers */
    pos[S] = toFIXED(1.0);

    Uint16 atrb = (Uint16)(VDP1_CCM_BANK16 | VDP1_ECD_DISABLE);
    if (half_transparency) {
        atrb = (Uint16)(atrb | CL_Trans);
    }

    attr.texno = (Uint16)s_sega_sprite_id;
    attr.atrb  = atrb;
    attr.colno = (Uint16)s_sega_palette_cram;
    attr.gstb  = 0;
    attr.dir   = 0;

    slDispSprite(pos, &attr, 0);
}

/* ----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------*/

void LogoSetup_Update(void)
{
    if (s_logo_state == LOGO_STATE_DONE) return;

    switch (s_logo_state) {
    case LOGO_STATE_INIT: {
        /* First-tick: attempt the asset load.  On failure short-circuit
         * to DONE so the title scene proceeds without LogoSetup at all
         * (per CLAUDE.md §5 graceful-degradation rule). */
        s_logo_started = 1;
        if (load_logos_atlas() != 0) {
            s_logo_state = LOGO_STATE_DONE;
            return;
        }
        s_logo_state = LOGO_STATE_SHOW_SEGA;
        s_logo_timer = 0;
        return;
    }
    case LOGO_STATE_SHOW_SEGA:
        if (++s_logo_timer >= SEGA_SHOW_TICKS) {
            s_logo_state = LOGO_STATE_FADE_OUT;
            s_logo_timer = 0;
        }
        return;
    case LOGO_STATE_FADE_OUT:
        if (++s_logo_timer >= SEGA_FADE_TICKS) {
            s_logo_state = LOGO_STATE_DONE;
            s_logo_timer = 0;
        }
        return;
    case LOGO_STATE_DONE:
    default:
        return;
    }
}

void LogoSetup_Draw(void)
{
    if (s_logo_state == LOGO_STATE_DONE) return;

    /* SHOW_SEGA: opaque draw.
     * FADE_OUT:  draw at half-transparency for the SECOND half of the
     *            fade window so the logo dims naturally before vanishing.
     *            For the first half, draw opaque (Saturn has no smooth
     *            multi-step alpha primitive in a single VDP1 pass — the
     *            best fit per ST-013-R3 §5.5.4 is the 50/50 CL_Trans bit). */
    if (s_logo_state == LOGO_STATE_SHOW_SEGA) {
        draw_sega_logo(0);
    } else if (s_logo_state == LOGO_STATE_FADE_OUT) {
        int half = (s_logo_timer >= (SEGA_FADE_TICKS / 2)) ? 1 : 0;
        draw_sega_logo(half);
    }
}

int LogoSetup_IsDone(void)
{
    return s_logo_state == LOGO_STATE_DONE;
}

int LogoSetup_IsActive(void)
{
    return s_logo_started;
}

logo_state_t LogoSetup_State(void)
{
    return s_logo_state;
}
