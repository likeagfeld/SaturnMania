/* Phase 1.3 — src/mania/Game.c
 *
 * Saturn-side port of `tools/_decomp_raw/SonicMania_Game.c::LinkGameLogicDLL`
 * (Title-scene subset) PLUS the Saturn-only asset bridge that resolves
 * decomp DrawSprite calls to the existing pre-converted .SPR/.ATL atlases
 * shipped in cd/.
 *
 * Phase 1.3 closes the Phase 1.2 black-screen regression by:
 *   * Calling title_assets_load() (in src/mania/Objects/Title/TitleAssets.c)
 *     which (a) loads every Title .SPR atlas + TSONIC.ATL into VDP1 sprite
 *     slots and (b) manufactures synthetic rsdk_sprite_animation_t entries
 *     in the global anims table so the per-class StageLoad's
 *     RSDK.LoadSpriteAnimation calls produce real list_ids with valid
 *     frame tables.
 *   * Pre-seeding the per-class list_id (via mania_set_*_list_id) BEFORE
 *     the scene loader runs, so when LoadSpriteAnimation tries the .bin
 *     and fails, the class's `TitleX->aniFrames` field already has the
 *     synthetic slot id.
 *   * Replacing the title_sprite_cb stub with the real resolver that maps
 *     (active_list_id, anim_id, frame_id) → asset slot → jo sprite_id,
 *     applying per-asset pivot offsets so the world (entity_pos + decomp
 *     pivot) lands at the right screen coord.
 *   * Fixing the create-entity position write-through (see comment on
 *     rsdk_create_entity in src/rsdk/object.c).
 *
 * Decomp citation: `SonicMania_Game.c:80-145` — the LinkGameLogicDLL chain
 * of `RSDK_REGISTER_OBJECT(Class)` invocations. Saturn-side
 * `rsdk_object_register_ex` mirrors that contract. */

/* Include jo first so SGL's MIN/MAX win over the local rsdk math macros
 * (avoids the "MAX redefined" warning when SL_DEF.H pulls in its own). */
#include <jo/jo.h>

#include "Game.h"

#include "../rsdk/object.h"
#include "../rsdk/animation.h"
#include "../rsdk/drawing.h"
#include "../rsdk/palette.h"
#include "../rsdk/math.h"
#include "../rsdk/string.h"
#include "../rsdk/scene.h"
#include "../rsdk/tilelayer.h"
#include "../rsdk/api.h"
#include "../rsdk/input.h"
#include "../rsdk/audio.h"
#include "../rsdk/save.h"
#include "../rsdk/storage.h"
#include "Objects/Title/TitleSetup.h"
#include "Objects/Title/TitleLogo.h"
#include "Objects/Title/TitleSonic.h"
#include "Objects/Title/TitleBG.h"
#include "Objects/Title/Title3DSprite.h"
#include "Objects/Title/TitleAssets.h"
#include "Objects/GHZ/GHZSetup.h"
#include "Objects/Global/Player.h"
#include "Objects/Global/InvisibleBlock.h"   /* Phase 2.4g.1 (Task #153) */
#include "Objects/Global/Zone.h"             /* Phase 2.4g.2 (Task #153) */
#include "Objects/Global/BoundsMarker.h"     /* Phase 2.4g.2 (Task #153) */
#include "Objects/Global/PlaneSwitch.h"      /* Phase 2.4g.3 (Task #153) */
#include "Objects/GHZ/Chopper.h"             /* Phase 2.4h */
#include "Objects/GHZ/Crabmeat.h"            /* Phase 2.4h */
#include "Objects/GHZ/Batbrain.h"            /* Phase 2.4h */
#include "Objects/Common/CollapsingPlatform.h" /* Phase 2.4-PLAT (Task #155) */
#include "Objects/GHZ/Bridge.h"                /* Phase 2.4-PLAT (Task #155) */
#include "Objects/Common/ForceSpin.h"          /* Phase 2.4-PLAT (Task #155) */
#include "Objects/Common/BreakableWall.h"      /* Phase 2.4-PLAT (Task #155) */
#include "Objects/Common/SpinBooster.h"        /* Phase 2.4-PLAT (Task #155) */
#include "Objects/Global/TitleCard.h"          /* Phase 2.4j.1 (Task #156) */
#include "Objects/Common/Entities.h"
#include "Objects/Menu/LogoSetup.h"   /* Phase 3.1 Path B (Task #123) */
/* Phase 3.2 + 3.2b REVERTED 2026-05-28 — MenuSetup include removed. */
/* Phase 3.2.a (Task #145) — UIControl + UIBackground foundation.
 * Phase 3.2.b (Task #146) — UIWidgets shared widget service.
 * Phase 3.2.c.1 (Task #148) — UIButton + UIButtonPrompt + UISubHeading. */
#include "Objects/Menu/UIControl.h"
#include "Objects/Menu/UIBackground.h"
#include "Objects/Menu/UIWidgets.h"
#include "Objects/Menu/UIButton.h"
#include "Objects/Menu/UIButtonPrompt.h"
#include "Objects/Menu/UISubHeading.h"
#include "../rsdk/scene_ghz.h"

/* Phase 1.20 — title-side palette CRAM drain helper (defined in src/main.c). */
extern void mania_title_palette_drain(void);

#include <string.h>

/* === Static-vars pointers (mirrors `ObjectTitleSetup *TitleSetup;` in
 *     the decomp). Each is assigned by mania_engine_init after the
 *     per-class register call returns the static_vars pointer. ====== */

ObjectTitleSetup    *TitleSetup     = NULL;
ObjectTitleLogo     *TitleLogo      = NULL;
ObjectTitleSonic    *TitleSonic     = NULL;
ObjectTitleBG       *TitleBG        = NULL;
ObjectTitle3DSprite *Title3DSprite  = NULL;
ObjectGHZSetup      *GHZSetup       = NULL;

/* === ScreenInfo + SceneInfo mirrors (decomp call sites) =============== */

static ManiaScreenInfo s_screen_info_storage;
static ManiaSceneInfo  s_scene_info_storage;
ManiaScreenInfo *ScreenInfo = &s_screen_info_storage;
ManiaSceneInfo  *SceneInfo  = &s_scene_info_storage;

/* === Per-class list-id (set in setup_title_assets, exposed via getters) === */

static int s_list_id_titlesetup = -1;
static int s_list_id_titlelogo  = -1;
static int s_list_id_titlesonic = -1;
static int s_list_id_titlebg    = -1;

/* === (list_id, anim_id) → TITLE_ASSET_* resolver =====================
 *
 * Phase 1.23 GAP A — list_id arrives via the draw callback's `animator->
 * list_id` (stored at rsdk_set_sprite_animation time, propagated through
 * rsdk_draw_sprite_ex → s_sprite_cb). Removed the §11.28 round-2
 * `s_active_list_id` global which broke under entity-interleaved
 * draws (every TitleX_Draw used to set it; last-writer-wins races with
 * cross-class draw ordering).
 *
 * Decomp anim ids per `tools/_decomp_raw/SonicMania_Objects_Title_TitleLogo.c`:
 *   anim 0 = Emblem      -> MWINGS
 *   anim 1 = Ribbon Wave -> MRIBSIDE
 *   anim 2 = Ribbon Wave 2-> MRIBSIDE  (same atlas; FlashIn swaps to this)
 *   anim 3 = Ribbon Center-> MRIBBON  (ribbonCenterAnimator)
 *   anim 4 = Game Title  -> MLOGO     (frame 0 = MANIA)
 *   anim 5 = Power LED   -> never drawn (destroyed in AnimateUntilFlash)
 *   anim 6 = Copyright   -> deferred (off-screen on 320 wide Saturn)
 *   anim 7 = Ring Bottom -> MRING
 *   anim 8 = Press Start -> MPRESS
 *
 * For TitleSonic: anim 0 = body (49 TSONIC.ATL frames), anim 1 = finger
 *   (deferred). */

static int resolve_asset(int list_id, int anim_id)
{
    if (list_id == s_list_id_titlelogo) {
        switch (anim_id) {
            case 0: return TITLE_ASSET_TLOGO_EMBLEM;
            case 1: return TITLE_ASSET_TLOGO_RIBSIDE;
            case 2: return TITLE_ASSET_TLOGO_RIBSIDE;
            case 3: return TITLE_ASSET_TLOGO_RIBCENTER;
            case 4: return TITLE_ASSET_TLOGO_GAMETITLE;
            case 6: return TITLE_ASSET_TLOGO_COPYRIGHT;
            case 7: return TITLE_ASSET_TLOGO_RINGBOT;
            case 8: return TITLE_ASSET_TLOGO_PRESSSTART;
            default: return -1;
        }
    }
    if (list_id == s_list_id_titlesonic) {
        if (anim_id == 0) return TITLE_ASSET_TSONIC_BODY;
        if (anim_id == 1) return TITLE_ASSET_TSONIC_FINGER;
        return -1;
    }
    /* TitleBG sprites are placeholder-only in Phase 1.3 (no Saturn-side
     * 5-layer BG composite is wired; NBG2 TITLE.DAT serves as the visible
     * backdrop). Returns -1 -> draw callback no-op. */
    return -1;
}

/* === Sprite draw callback ============================================ */

/* Phase 1.3 diagnostic counters. Distinguish three failure modes:
 *   - cb_count == 0          : per-frame Draw never invoked
 *                              (state machine stuck OR object loop broken)
 *   - cb_count > 0, resolved == 0 : Draw invoked but list_id/anim_id maps
 *                              to no asset (resolver bug)
 *   - resolved > 0, drawn == 0    : asset resolved but base_sprite_id is
 *                              invalid (asset load failed)
 * Read via Mednafen savestate inspection of these symbol addresses. */
volatile uint32_t g_title_sprite_cb_count    = 0;
volatile uint32_t g_title_sprite_cb_resolved = 0;
volatile uint32_t g_title_sprite_cb_drawn    = 0;

/* Phase 1.24 Saturn-port deviation per docs/COMPREHENSIVE_PLAN.md §11.30:
 *
 * Bug A — the .SPR canvases for EMBLEM and RIBSIDE are asymmetric around the
 * entity origin (pivot at canvas left edge: pivot_x = -144 over a 144-px wide
 * canvas for EMBLEM, pivot_x = -121 over a 56-px canvas for RIBSIDE). The
 * decomp's TitleLogo_Draw issues FLIP_NONE + FLIP_X pairs (TitleLogo.c:42-48
 * for EMBLEM, :50-58 for RIBBON wave). `_world_to_jo` correctly resolves the
 * FLIP_NONE centre (jo_x = -72 for EMBLEM, -93 for RIBSIDE) but uses the SAME
 * jo coords for FLIP_X — RSDK's FLIP_X mirrors the canvas about the ENTITY
 * ORIGIN, so the mirrored centre lies at `+jo_x_unflipped_offset_from_origin`
 * not at the same place. The Phase 1.20 direct_draw fallback proved
 * (-72,+72) for EMBLEM and (-93,+93) for RIBSIDE visible against the golden.
 *
 * Saturn-fit deviation: when direction == FLIP_X for these two assets, the
 * FLIP_X jo_x is computed as `-(pivot_x + w/2)` referenced to the entity
 * origin, then offset by the same `(world_x - cam_x - 160)` term that
 * `_world_to_jo` applied for the FLIP_NONE pass.
 *
 * Bug B — entity-path z = 200 - draw_group*25 = 100 for drawGroup-4 (all Title
 * sprites). The direct_draw fallback used per-asset z values 165..200 that
 * landed above the NBG2 backdrop and above each other in the documented stack
 * order (EMBLEM rear, then RING, ribbon wings, ribbon centre, MANIA wordmark,
 * Sonic body, finger, PRESSSTART front). The Saturn-fit deviation table below
 * maps each Title asset to its proven-visible direct_draw z so the entity-
 * driven path renders at the same depth ordering.
 *
 * No new offsets for GAMETITLE / RIBCENTER / RINGBOT / PRESSSTART — their
 * canvases are symmetric and `_world_to_jo` lands them at exactly the same
 * jo coords the direct_draw fallback used. */
static int title_asset_saturn_fit_z(int asset)
{
    switch (asset) {
        case TITLE_ASSET_TLOGO_EMBLEM:     return 200;
        case TITLE_ASSET_TLOGO_RINGBOT:    return 195;
        case TITLE_ASSET_TLOGO_RIBSIDE:    return 190;
        case TITLE_ASSET_TLOGO_RIBCENTER:  return 185;
        case TITLE_ASSET_TLOGO_GAMETITLE:  return 180;
        case TITLE_ASSET_TSONIC_BODY:      return 170;
        case TITLE_ASSET_TSONIC_FINGER:    return 169;
        case TITLE_ASSET_TLOGO_PRESSSTART: return 165;
        default:                           return -1;     /* fall through to draw_group-based */
    }
}

/* Compute the FLIP_X mirrored jo_x for an asymmetric-canvas asset.
 *
 * `_world_to_jo` formula: jo_x = world + frame_pivot + w/2 - cam - 160.
 * Canvas mirror about origin: jo_x_flipped = world - frame_pivot - w/2 - cam - 160.
 * So jo_x_flipped = jo_x_unflipped - 2*(frame_pivot_x + w/2).
 *
 * The pivot comes from the FRAME table (rsdk_sprite_frame_t) — NOT from
 * `g_title_assets[].pivot_x` which load_spr_atlas leaves at 0. The
 * synthetic anim list's fill_frame() calls set the real pivots in the
 * sprite_frame_t storage; `s_sprite_cb` receives the frame ptr directly. */
static int title_flip_x_mirror_jo_x(int jo_x_unflipped,
                                    const rsdk_sprite_frame_t *frame)
{
    if (!frame) return jo_x_unflipped;
    return jo_x_unflipped - 2 * ((int)frame->pivot_x + ((int)frame->width >> 1));
}

static void title_sprite_cb(int list_id, int anim_id, int frame_id,
                            uint8_t direction, uint8_t ink_effect,
                            int32_t alpha, int jo_x, int jo_y,
                            const rsdk_sprite_frame_t *frame, int z)
{
    (void)ink_effect; (void)alpha;
    ++g_title_sprite_cb_count;

    /* Phase 1.23 GAP A — list_id arrives via the animator (set at
     * rsdk_set_sprite_animation time). Replaces the §11.28 round-2
     * `s_active_list_id` global hack. */
    int asset = resolve_asset(list_id, anim_id);
    if (asset < 0) return;
    ++g_title_sprite_cb_resolved;
    const title_asset_t *a = &g_title_assets[asset];
    if (a->base_sprite_id < 0 || a->frame_count == 0) return;
    ++g_title_sprite_cb_drawn;

    /* Phase 1.24 Saturn-port deviation (Bug B) — per-asset z value that
     * matches the Phase 1.20 direct_draw fallback's proven-visible depth
     * ordering. Falls back to the draw_group-derived z for any asset not
     * in the Title table (Phase 2+ gameplay sprites). */
    int saturn_fit_z = title_asset_saturn_fit_z(asset);
    int final_z = (saturn_fit_z >= 0) ? saturn_fit_z : (200 - z);

    /* TSONIC body + finger use the 4-bpp slDispSprite path (palette +
     * PMOD bits). Phase 1.20: anim 1 (finger) shares the same VDP1 +
     * CRAM palette infrastructure as anim 0 (body); the asset slot
     * selects the right base_sprite_id and `frame_id` is already a
     * local 0..frame_count-1 ordinal (animator->frame_id, supplied
     * by rsdk_draw_sprite_ex). */
    if ((asset == TITLE_ASSET_TSONIC_BODY || asset == TITLE_ASSET_TSONIC_FINGER)
        && a->is_4bpp) {
        title_tsonic_draw_frame(asset, frame_id, jo_x, jo_y, final_z, direction);
        return;
    }

    int f = frame_id;
    if (f < 0 || f >= a->frame_count) f = 0;
    int sid = a->base_sprite_id + f;

    /* Phase 1.24 Saturn-port deviation (Bug A) — asymmetric-canvas FLIP_X
     * mirror for EMBLEM + RIBSIDE. The decomp's TitleLogo_Draw issues paired
     * FLIP_NONE + FLIP_X draws for these two assets and RSDK mirrors the
     * canvas about the entity origin (not the canvas centre). _world_to_jo
     * yields the FLIP_NONE-correct jo coords; FLIP_X needs the mirrored value.
     *
     * GAMETITLE / RIBCENTER / RINGBOT / PRESSSTART canvases are symmetric or
     * never drawn with FLIP_X, so they take the unaltered (jo_x, jo_y). */
    int jx = jo_x;
    int jy = jo_y;
    bool needs_mirror = (direction == FLIP_X) &&
                       (asset == TITLE_ASSET_TLOGO_EMBLEM ||
                        asset == TITLE_ASSET_TLOGO_RIBSIDE);
    if (needs_mirror) {
        jx = title_flip_x_mirror_jo_x(jo_x, frame);
    }

    if (direction == FLIP_X) {
        jo_sprite_enable_horizontal_flip();
        jo_sprite_draw3D(sid, jx, jy, final_z);
        jo_sprite_disable_horizontal_flip();
    } else {
        jo_sprite_draw3D(sid, jx, jy, final_z);
    }
}

/* === Engine init / per-frame tick =================================== */

/* setup_title_assets() loads the .SPR atlases AND manufactures the
 * synthetic anim lists, then captures the per-class list_ids for later
 * pre-seeding. Called from mania_engine_init. */
/* === Phase 2.3c diagnostic probe (option alpha) ====================== */

/* 16x16 BGR1555 probe pattern. Static const so it lives in .data (not BSS,
 * not heap) and there's no zero-init race between register-time and
 * draw-time. MSB=1 forces opaque (jo's TL handling treats MSB=0 as
 * transparent for BGR1555). Two colours so the checkerboard is unambiguous:
 *   0xFFFF = MSB=1 + (B=31,G=31,R=31) = solid white
 *   0xFC1F = MSB=1 + (B=31,G=0,R=31)  = solid magenta
 */
#define MANIA_DIAG_PROBE_W 16
#define MANIA_DIAG_PROBE_H 16
static const jo_color s_diag_probe_pixels[MANIA_DIAG_PROBE_W * MANIA_DIAG_PROBE_H] = {
    /* row 0..15: 16 alternating cells of W/M */
#define _W 0xFFFF
#define _M 0xFC1F
    _W,_W,_M,_M,_W,_W,_M,_M, _W,_W,_M,_M,_W,_W,_M,_M,
    _W,_W,_M,_M,_W,_W,_M,_M, _W,_W,_M,_M,_W,_W,_M,_M,
    _M,_M,_W,_W,_M,_M,_W,_W, _M,_M,_W,_W,_M,_M,_W,_W,
    _M,_M,_W,_W,_M,_M,_W,_W, _M,_M,_W,_W,_M,_M,_W,_W,
    _W,_W,_M,_M,_W,_W,_M,_M, _W,_W,_M,_M,_W,_W,_M,_M,
    _W,_W,_M,_M,_W,_W,_M,_M, _W,_W,_M,_M,_W,_W,_M,_M,
    _M,_M,_W,_W,_M,_M,_W,_W, _M,_M,_W,_W,_M,_M,_W,_W,
    _M,_M,_W,_W,_M,_M,_W,_W, _M,_M,_W,_W,_M,_M,_W,_W,
    _W,_W,_M,_M,_W,_W,_M,_M, _W,_W,_M,_M,_W,_W,_M,_M,
    _W,_W,_M,_M,_W,_W,_M,_M, _W,_W,_M,_M,_W,_W,_M,_M,
    _M,_M,_W,_W,_M,_M,_W,_W, _M,_M,_W,_W,_M,_M,_W,_W,
    _M,_M,_W,_W,_M,_M,_W,_W, _M,_M,_W,_W,_M,_M,_W,_W,
    _W,_W,_M,_M,_W,_W,_M,_M, _W,_W,_M,_M,_W,_W,_M,_M,
    _W,_W,_M,_M,_W,_W,_M,_M, _W,_W,_M,_M,_W,_W,_M,_M,
    _M,_M,_W,_W,_M,_M,_W,_W, _M,_M,_W,_W,_M,_M,_W,_W,
    _M,_M,_W,_W,_M,_M,_W,_W, _M,_M,_W,_W,_M,_M,_W,_W,
#undef _W
#undef _M
};

static int s_diag_probe_sprite_id = -1;

/* __jo_sprite_def[] is already declared extern in jo/sprites.h (pulled
 * in via <jo/jo.h> above) — we use it directly below for the hand-built
 * SPR_ATTR encoding in path B. */

/* Phase 2.3c — register the probe sprite. Idempotent (re-call is no-op).
 * jo_sprite_add copies s_diag_probe_pixels into VDP1 VRAM via slDMACopy
 * at the current __jo_sprite_addr cursor; the cursor advances by
 * 16*16*2 = 512 bytes. */
void mania_diag_probe_register(void)
{
    jo_img img;

    if (s_diag_probe_sprite_id >= 0) return;

    img.width  = MANIA_DIAG_PROBE_W;
    img.height = MANIA_DIAG_PROBE_H;
    img.data   = (jo_color *)s_diag_probe_pixels;
    s_diag_probe_sprite_id = jo_sprite_add(&img);
}

int mania_diag_probe_sprite_id(void) { return s_diag_probe_sprite_id; }

/* Phase 2.3c diagnostic draw — called from mania_tick UNCONDITIONALLY
 * (both title and GHZ branches). Two paths so we can distinguish a
 * jo-wrapper bug from an SGL/VDP1-level bug.
 *
 * Path A: jo_sprite_draw3D — jo's normal SGL wrapper. If sprites work
 *         in title and GHZ via this path, no Phase 2.3 bug at all.
 * Path B: direct slDispSprite — bypasses jo's wrapper, encodes a fresh
 *         SPR_ATTR pointing at the probe's existing VDP1 VRAM character
 *         address. If A fails but B succeeds, the suppressor lives in
 *         jo's wrapper layer (e.g. SGL sortlist saturation, sprite-attr
 *         table state). If both fail, the suppressor is below jo at the
 *         SGL/VDP1 register level.
 *
 * Coordinates:
 *   Path A: (0, +80) world relative — same coordinate space as
 *           jo_sprite_draw3D used elsewhere (centred on JO_TV_WIDTH_2 /
 *           JO_TV_HEIGHT_2).
 *   Path B: (+24, +80) — slight rightward offset so the two probes are
 *           visually distinguishable.
 *
 * Z=200 puts both probes ABOVE typical gameplay sprites (which use
 * z=10..100 in Phase 2.3). */
void mania_diag_probe_draw(void)
{
    jo_pos3D pos;

    if (s_diag_probe_sprite_id < 0) return;

    /* Path A — jo wrapper. */
    pos.x = 0;
    pos.y = 80;
    pos.z = 200;
    jo_sprite_draw3D(s_diag_probe_sprite_id, pos.x, pos.y, pos.z);

    /* Path B — direct slDispSprite. Mirrors jo's SGL branch
     * (sprites.c:422-447) verbatim except the sprite_id resolution is
     * pinned to our probe and the position is shifted +24 px on X.
     *
     * SPR_ATTRIBUTE(t, c, g, a, d):
     *   t = texture id (same as jo's: it later overrides via
     *       __jo_set_sprite_attributes — we DON'T call that; instead
     *       we encode the texture pointer ourselves)
     *   c = No_Palet     (BGR1555 — no palette)
     *   g = No_Gouraud
     *   a = ECdis        (disable end-code so 0x0000 isn't interpreted
     *                    as a row terminator)
     *   d = sprNoflip | FUNC_Sprite
     *
     * texno = __jo_sprite_def[sprite_id].adr value (the VRAM character
     * address divided by 8) — SGL uses it as the texture id; the per-
     * texture metadata (size, color mode) is set up earlier by slInit
     * via SGL_REG... oh wait, jo doesn't go through SGL's texture
     * registration; jo calls slDispSprite with attr.texno = 0 and SGL
     * reads __jo_set_sprite_attributes' write to the SAME SPR_ATTR
     * fields. So mirror that: __jo_set_sprite_attributes does:
     *   attr.texno = sprite_id (NOT the VRAM addr — sprite_id is the
     *   index into SGL's texture table maintained by jo's sprite-add
     *   path).
     *
     * Reference: jo-engine/jo_engine/sprites.c:441-447. */
    {
        FIXED        sgl_pos[5];
        SPR_ATTR     attr = SPR_ATTRIBUTE(0, No_Palet, No_Gouraud, ECdis,
                                         sprNoflip | FUNC_Sprite);
        /* Mirror jo's centred-coords branch (sprites.c:431-435 +
         * the default JO_NO_ZOOM scale). */
        sgl_pos[0] = jo_int2fixed(24);
        sgl_pos[1] = jo_int2fixed(80);
        sgl_pos[2] = jo_int2fixed(200);
        sgl_pos[3] = JO_NO_ZOOM;
        sgl_pos[4] = JO_NO_ZOOM;
        /* __jo_set_sprite_attributes writes ONLY attr.texno = sprite_id
         * in the SGL branch (sprites.c:357). The width/height come from
         * SGL's TEXTURE table (TEXTURE *)__jo_sprite_def passed to
         * slInitSystem in jo-engine/jo_engine/core.c:192, which SGL reads
         * by indexing __jo_sprite_def[attr.texno].{width,height,adr,size}
         * automatically. So we mirror that minimal initialisation: set
         * texno only; SGL handles the rest. */
        attr.texno = s_diag_probe_sprite_id;
        slDispSprite(sgl_pos, &attr, 0);
    }
}

static void setup_title_assets(void)
{
    title_assets_load();
    s_list_id_titlelogo  = title_assets_titlelogo_list_id();
    s_list_id_titlesonic = title_assets_titlesonic_list_id();
    s_list_id_titlebg    = title_assets_titlebg_list_id();
    s_list_id_titlesetup = title_assets_titlesetup_list_id();
}

void mania_engine_init(void)
{
    rsdk_math_init();
    rsdk_palette_init();
    rsdk_string_init();
    rsdk_tilelayer_init();
    rsdk_api_init();
    rsdk_scene_init();

    /* Init the Mania-side ScreenInfo mirror. */
    memset(&s_screen_info_storage, 0, sizeof(s_screen_info_storage));
    s_screen_info_storage.size.x   = 320;
    s_screen_info_storage.size.y   = 224;
    s_screen_info_storage.center.x = 160;
    s_screen_info_storage.center.y = 112;
    s_screen_info_storage.clipBound_X2 = 320;
    s_screen_info_storage.clipBound_Y2 = 224;

    memset(&s_scene_info_storage, 0, sizeof(s_scene_info_storage));

    /* TitleSetup */
    rsdk_object_register_ex("TitleSetup",
                            sizeof(EntityTitleSetup),
                            sizeof(ObjectTitleSetup),
                            TitleSetup_Update,
                            TitleSetup_LateUpdate,
                            TitleSetup_StaticUpdate,
                            TitleSetup_Draw,
                            TitleSetup_Create,
                            TitleSetup_StageLoad,
                            (void **)&TitleSetup);

    /* TitleLogo */
    rsdk_object_register_ex("TitleLogo",
                            sizeof(EntityTitleLogo),
                            sizeof(ObjectTitleLogo),
                            TitleLogo_Update,
                            TitleLogo_LateUpdate,
                            TitleLogo_StaticUpdate,
                            TitleLogo_Draw,
                            TitleLogo_Create,
                            TitleLogo_StageLoad,
                            (void **)&TitleLogo);

    /* TitleSonic */
    rsdk_object_register_ex("TitleSonic",
                            sizeof(EntityTitleSonic),
                            sizeof(ObjectTitleSonic),
                            TitleSonic_Update,
                            TitleSonic_LateUpdate,
                            TitleSonic_StaticUpdate,
                            TitleSonic_Draw,
                            TitleSonic_Create,
                            TitleSonic_StageLoad,
                            (void **)&TitleSonic);

    /* TitleBG */
    rsdk_object_register_ex("TitleBG",
                            sizeof(EntityTitleBG),
                            sizeof(ObjectTitleBG),
                            TitleBG_Update,
                            TitleBG_LateUpdate,
                            TitleBG_StaticUpdate,
                            TitleBG_Draw,
                            TitleBG_Create,
                            TitleBG_StageLoad,
                            (void **)&TitleBG);

    /* Title3DSprite */
    rsdk_object_register_ex("Title3DSprite",
                            sizeof(EntityTitle3DSprite),
                            sizeof(ObjectTitle3DSprite),
                            Title3DSprite_Update,
                            Title3DSprite_LateUpdate,
                            Title3DSprite_StaticUpdate,
                            Title3DSprite_Draw,
                            Title3DSprite_Create,
                            Title3DSprite_StageLoad,
                            (void **)&Title3DSprite);

    /* Phase 2.1 — GHZSetup. Registered here so the class is present at
     * boot; the per-entity Create happens when the GHZ scene loads (or,
     * for Phase 2.1's title→GHZ transition, the static_vars pointer
     * GHZSetup is the only thing we touch and we call StageLoad
     * directly from the transition state). */
    rsdk_object_register_ex("GHZSetup",
                            sizeof(EntityGHZSetup),
                            sizeof(ObjectGHZSetup),
                            GHZSetup_Update,
                            GHZSetup_LateUpdate,
                            GHZSetup_StaticUpdate,
                            GHZSetup_Draw,
                            GHZSetup_Create,
                            GHZSetup_StageLoad,
                            (void **)&GHZSetup);

    /* Phase 2.4g.1 (Task #153) — InvisibleBlock. First GHZ entity on the
     * RSDK entity engine per memory/ghz-pivot-to-rsdk-engine.md. Its 18
     * GHZ Scene1.bin instances spawn into RSDK slots when the GHZ scene
     * loads; InvisibleBlock_StageLoad is a no-op so registering it here
     * adds zero cost to the all-class StageLoad pass at title boot. */
    rsdk_object_register_ex("InvisibleBlock",
                            sizeof(EntityInvisibleBlock),
                            sizeof(ObjectInvisibleBlock),
                            InvisibleBlock_Update,
                            InvisibleBlock_LateUpdate,
                            InvisibleBlock_StaticUpdate,
                            InvisibleBlock_Draw,
                            InvisibleBlock_Create,
                            InvisibleBlock_StageLoad,
                            (void **)&InvisibleBlock);

    /* Phase 2.4g.2 (Task #153) — BoundsMarker. Second GHZ entity on the
     * RSDK entity engine. Its 22 GHZ Scene1.bin instances spawn into RSDK
     * slots when the GHZ scene loads (same class-agnostic spawn pipeline
     * as InvisibleBlock; scene.c compaction now budgeted for IB 18 + BM 22
     * = 40 via RSDK_TEMPENTITY_COUNT=0x30). BoundsMarker_StageLoad is a
     * no-op so registering it adds zero cost to the all-class StageLoad
     * pass at title boot. BoundsMarker writes the Zone camera/player/death
     * bounds globals (Zone.c subset) which the GHZ camera clamp reads. */
    rsdk_object_register_ex("BoundsMarker",
                            sizeof(EntityBoundsMarker),
                            sizeof(ObjectBoundsMarker),
                            BoundsMarker_Update,
                            BoundsMarker_LateUpdate,
                            BoundsMarker_StaticUpdate,
                            BoundsMarker_Draw,
                            BoundsMarker_Create,
                            BoundsMarker_StageLoad,
                            (void **)&BoundsMarker);

    /* Phase 2.4g.3 (Task #153) — PlaneSwitch. Third (last + largest) GHZ
     * entity on the RSDK entity engine. Its 106 GHZ Scene1.bin instances
     * spawn into RSDK slots when the GHZ scene loads (same class-agnostic
     * spawn pipeline as InvisibleBlock/BoundsMarker; scene.c compaction now
     * budgeted for IB 18 + BM 22 + PS 106 = 146 via RSDK_TEMPENTITY_COUNT=
     * 0xA0=160). PlaneSwitch_StageLoad is a no-op so registering it adds
     * zero cost to the all-class StageLoad pass at title boot. PlaneSwitch
     * writes player->collisionPlane (A/B path select) which the two-plane
     * tile-collision bridge (Player.h sms_world_t raw_alt + active_path)
     * consumes. */
    rsdk_object_register_ex("PlaneSwitch",
                            sizeof(EntityPlaneSwitch),
                            sizeof(ObjectPlaneSwitch),
                            PlaneSwitch_Update,
                            PlaneSwitch_LateUpdate,
                            PlaneSwitch_StaticUpdate,
                            PlaneSwitch_Draw,
                            PlaneSwitch_Create,
                            PlaneSwitch_StageLoad,
                            (void **)&PlaneSwitch);

    /* Phase 2.4h — GHZ Act 1 badniks (Chopper 13 + Crabmeat 11 +
     * Batbrain 7). Registered as RSDK objects so scene.c spawns each
     * from GHZ Scene1.bin (same compaction pipeline as the 2.4g
     * overflow classes; RSDK_TEMPENTITY_COUNT bumped to 0xC0=192 to hold
     * the combined 171-instance overflow). Each _StageLoad only fills the
     * hitbox table (zero boot asset I/O); SPR2/MET atlases load lazily
     * via <badnik>_load_assets on GHZ entry. Collision via
     * Player_CheckCollisionBox in each _Update (memory rule). */
    rsdk_object_register_ex("Chopper",
                            sizeof(EntityChopper),
                            sizeof(ObjectChopper),
                            Chopper_Update,
                            Chopper_LateUpdate,
                            Chopper_StaticUpdate,
                            Chopper_Draw,
                            Chopper_Create,
                            Chopper_StageLoad,
                            (void **)&Chopper);

    rsdk_object_register_ex("Crabmeat",
                            sizeof(EntityCrabmeat),
                            sizeof(ObjectCrabmeat),
                            Crabmeat_Update,
                            Crabmeat_LateUpdate,
                            Crabmeat_StaticUpdate,
                            Crabmeat_Draw,
                            Crabmeat_Create,
                            Crabmeat_StageLoad,
                            (void **)&Crabmeat);

    rsdk_object_register_ex("Batbrain",
                            sizeof(EntityBatbrain),
                            sizeof(ObjectBatbrain),
                            Batbrain_Update,
                            Batbrain_LateUpdate,
                            Batbrain_StaticUpdate,
                            Batbrain_Draw,
                            Batbrain_Create,
                            Batbrain_StageLoad,
                            (void **)&Batbrain);

    /* Phase 2.4-PLAT (Task #155) — GHZ Act 1 platforming entities.
     * CollapsingPlatform/ForceSpin/BreakableWall/SpinBooster are
     * IN-GAME INVISIBLE triggers/walls (FG tilemap surface; no atlas).
     * Bridge is the sole VISIBLE class (draws GHZ/Bridge.bin planks via
     * Bridge_draw_only + cd/BRIDGE.SP2 atlas loaded lazily by
     * Bridge_load_assets on GHZ entry). Each _StageLoad is a no-op (zero
     * boot asset I/O). Player interaction routes through
     * Player_CheckCollisionBox in each _Update (memory rule), gated on
     * mania_is_ghz_active() + single g_ghz_player. GHZ Scene1.bin
     * instance counts: CollapsingPlatform 15, Bridge 13, ForceSpin 13,
     * BreakableWall 23, SpinBooster 4. */
    rsdk_object_register_ex("CollapsingPlatform",
                            sizeof(EntityCollapsingPlatform),
                            sizeof(ObjectCollapsingPlatform),
                            CollapsingPlatform_Update,
                            CollapsingPlatform_LateUpdate,
                            CollapsingPlatform_StaticUpdate,
                            CollapsingPlatform_Draw,
                            CollapsingPlatform_Create,
                            CollapsingPlatform_StageLoad,
                            (void **)&CollapsingPlatform);

    rsdk_object_register_ex("Bridge",
                            sizeof(EntityBridge),
                            sizeof(ObjectBridge),
                            Bridge_Update,
                            Bridge_LateUpdate,
                            Bridge_StaticUpdate,
                            Bridge_Draw,
                            Bridge_Create,
                            Bridge_StageLoad,
                            (void **)&Bridge);

    rsdk_object_register_ex("ForceSpin",
                            sizeof(EntityForceSpin),
                            sizeof(ObjectForceSpin),
                            ForceSpin_Update,
                            ForceSpin_LateUpdate,
                            ForceSpin_StaticUpdate,
                            ForceSpin_Draw,
                            ForceSpin_Create,
                            ForceSpin_StageLoad,
                            (void **)&ForceSpin);

    rsdk_object_register_ex("BreakableWall",
                            sizeof(EntityBreakableWall),
                            sizeof(ObjectBreakableWall),
                            BreakableWall_Update,
                            BreakableWall_LateUpdate,
                            BreakableWall_StaticUpdate,
                            BreakableWall_Draw,
                            BreakableWall_Create,
                            BreakableWall_StageLoad,
                            (void **)&BreakableWall);

    rsdk_object_register_ex("SpinBooster",
                            sizeof(EntitySpinBooster),
                            sizeof(ObjectSpinBooster),
                            SpinBooster_Update,
                            SpinBooster_LateUpdate,
                            SpinBooster_StaticUpdate,
                            SpinBooster_Draw,
                            SpinBooster_Create,
                            SpinBooster_StageLoad,
                            (void **)&SpinBooster);

    /* Phase 2.4j.1 (Task #156) — TitleCard act-intro. Bridge-model:
     * registered so the RSDK callbacks land in game.map, but driven by the
     * bespoke titlecard_tick/_draw_only because no TitleCard slot entity is
     * created for GHZ (no Scene.bin placement). */
    rsdk_object_register_ex("TitleCard",
                            sizeof(EntityTitleCard),
                            sizeof(ObjectTitleCard),
                            TitleCard_Update,
                            TitleCard_LateUpdate,
                            TitleCard_StaticUpdate,
                            TitleCard_Draw,
                            TitleCard_Create,
                            TitleCard_StageLoad,
                            (void **)&TitleCard);

    /* Phase 3.2 + 3.2b REVERTED 2026-05-28 — rsdk_object_register_ex
     * call for MenuSetup removed. */

    /* Phase 3.2.a (Task #145) — UIControl + UIBackground foundation.
     * Registers the two foundational menu classes so Menu/Scene1.bin's
     * 27 UIControl entity hashes and 27 UIBackground entity hashes
     * resolve at scene-load time instead of being silently dropped.
     * The full menu UI widget tree (UIButton, UIWidgets, UIHeading,
     * UISaveSlot, UISubHeading, UIChoice, UIDiorama, ...) lands in
     * Phase 3.2.b/c/f per docs/menusetup_decomposition_plan.md.
     *
     * Decomp citations:
     *   tools/_decomp_raw/SonicMania_Objects_Menu_UIControl.c L126-132
     *   tools/_decomp_raw/SonicMania_Objects_Menu_UIBackground.c L42 */
    rsdk_object_register_ex("UIControl",
                            sizeof(EntityUIControl),
                            sizeof(ObjectUIControl),
                            UIControl_Update,
                            UIControl_LateUpdate,
                            UIControl_StaticUpdate,
                            UIControl_Draw,
                            UIControl_Create,
                            UIControl_StageLoad,
                            (void **)&UIControl);

    rsdk_object_register_ex("UIBackground",
                            sizeof(EntityUIBackground),
                            sizeof(ObjectUIBackground),
                            UIBackground_Update,
                            UIBackground_LateUpdate,
                            UIBackground_StaticUpdate,
                            UIBackground_Draw,
                            UIBackground_Create,
                            UIBackground_StageLoad,
                            (void **)&UIBackground);

    /* Phase 3.2.b (Task #146) — UIWidgets shared widget service.
     * UIWidgets is invoked as a singleton service via the
     * ObjectUIWidgets pointer; no per-entity instances exist in
     * Menu/Scene1.bin. Registration drives the per-frame StaticUpdate
     * timer advance + the StageLoad atlas + SFX loader.
     *
     * Decomp citations:
     *   tools/_decomp_raw/SonicMania_Objects_Menu_UIWidgets.c L16-24
     *     (StaticUpdate)
     *   tools/_decomp_raw/SonicMania_Objects_Menu_UIWidgets.c L30-69
     *     (StageLoad) */
    rsdk_object_register_ex("UIWidgets",
                            sizeof(EntityUIWidgets),
                            sizeof(ObjectUIWidgets),
                            UIWidgets_Update,
                            UIWidgets_LateUpdate,
                            UIWidgets_StaticUpdate,
                            UIWidgets_Draw,
                            UIWidgets_Create,
                            UIWidgets_StageLoad,
                            (void **)&UIWidgets);

    /* Phase 3.2.c.1 (Task #148) — UIButton + UIButtonPrompt + UISubHeading.
     *
     * UIButton is the bedrock interactive menu widget — every Save Select,
     * Time Attack, Options, Extras, Competition menu entity hash in
     * Menu/Scene1.bin resolves to a UIButton (or a UIButton subclass).
     * UIButtonPrompt is the controller-glyph prompt; UISubHeading is the
     * section header.
     *
     * Decomp citations:
     *   tools/_decomp_raw/SonicMania_Objects_Menu_UIButton.c L96-147
     *   tools/_decomp_raw/SonicMania_Objects_Menu_UIButtonPrompt.c L130-168
     *   tools/_decomp_raw/SonicMania_Objects_Menu_UISubHeading.c L57-76 */
    rsdk_object_register_ex("UIButton",
                            sizeof(EntityUIButton),
                            sizeof(ObjectUIButton),
                            UIButton_Update,
                            UIButton_LateUpdate,
                            UIButton_StaticUpdate,
                            UIButton_Draw,
                            UIButton_Create,
                            UIButton_StageLoad,
                            (void **)&UIButton);

    rsdk_object_register_ex("UIButtonPrompt",
                            sizeof(EntityUIButtonPrompt),
                            sizeof(ObjectUIButtonPrompt),
                            UIButtonPrompt_Update,
                            UIButtonPrompt_LateUpdate,
                            UIButtonPrompt_StaticUpdate,
                            UIButtonPrompt_Draw,
                            UIButtonPrompt_Create,
                            UIButtonPrompt_StageLoad,
                            (void **)&UIButtonPrompt);

    rsdk_object_register_ex("UISubHeading",
                            sizeof(EntityUISubHeading),
                            sizeof(ObjectUISubHeading),
                            UISubHeading_Update,
                            UISubHeading_LateUpdate,
                            UISubHeading_StaticUpdate,
                            UISubHeading_Draw,
                            UISubHeading_Create,
                            UISubHeading_StageLoad,
                            (void **)&UISubHeading);

    /* drawGroup defaults — match decomp Create-body assignments. */
    int id;
    if ((id = rsdk_object_find_class("TitleBG"))       >= 0)
        rsdk_object_set_class_draw_group((uint16_t)id, 1);
    if ((id = rsdk_object_find_class("Title3DSprite")) >= 0)
        rsdk_object_set_class_draw_group((uint16_t)id, 2);
    if ((id = rsdk_object_find_class("TitleLogo"))     >= 0)
        rsdk_object_set_class_draw_group((uint16_t)id, 4);
    if ((id = rsdk_object_find_class("TitleSonic"))    >= 0)
        rsdk_object_set_class_draw_group((uint16_t)id, 4);
    if ((id = rsdk_object_find_class("TitleSetup"))    >= 0)
        rsdk_object_set_class_draw_group((uint16_t)id, 12);

    /* Saturn-side asset bring-up. MUST come BEFORE the scene loader runs
     * (which fires the per-class StageLoad callbacks). The StageLoad calls
     * rsdk_load_sprite_animation("Title/X.bin") which returns -1 on
     * Saturn (no .bin shipped); the per-class then falls back to the
     * mania_set_*_list_id() value we provide via the getter below. */
    setup_title_assets();

    /* Phase QA-VDP1 (2026-05-28) — entity asset loading MOVED OFF the
     * engine-init boot path into mania_load_ghz_scene() (the GHZ
     * scene-load pass). entities_load_assets() copies ~232 KB of GHZ
     * gameplay-entity sprites into the 512 KB VDP1 VRAM via jo_sprite_add.
     * Loading them at boot — AFTER setup_title_assets() has placed the
     * title sprites in VDP1 VRAM — overflowed the VRAM cursor and
     * clobbered the title sprite char-data region: every title sprite read
     * null/garbage char data, so the MANIA logo / SONIC banner / Sonic
     * body / ribbons all vanished and the overflow painted bright-green
     * full-height bars at both viewport edges (measured RED baseline
     * qa_arc_*.png: ~20,367 green-edge px, 0 red-banner px). This is the
     * documented suspect H4 (VDP1 char-RAM overflow). Entities are GHZ
     * objects with no business loading during the title; the decomp loads
     * them in the GHZ StageLoad pass (RetroEngine.cpp:361 InitObjects ->
     * per-class stageLoad). Saturn mirror: entities_load_assets() now runs
     * synchronously inside mania_load_ghz_scene() Step 3, after the title
     * VDP1 sprites are no longer needed (the title backdrop buffers are
     * freed at Step 1 and the state machine has left the title). Gate:
     * tools/qa_title_vdp1_corruption_gate.py. */

    /* Phase 2.3c diagnostic option (alpha) — probe sprite for VDP1 sprite-
     * suppression bisect. Registered LAST (after all real asset loads)
     * so its VRAM cursor position is downstream of the entity sprites;
     * if char-RAM overflow (H4) is the cause, the probe is the first
     * thing to overflow.
     *
     * Drawn unconditionally from mania_tick (both title and GHZ states)
     * via TWO paths:
     *   path A: jo_sprite_draw3D (jo's standard SGL wrapper)
     *   path B: direct slDispSprite with hand-built SPR_ATTR (bypasses
     *           jo's wrapper layer)
     *
     * See docs/COMPREHENSIVE_PLAN.md Sec 12.3c. */
    mania_diag_probe_register();

    /* Register the sprite-draw callback. */
    rsdk_drawing_set_sprite_callback(title_sprite_cb);
}

/* === Saturn-side helpers exposed to per-class .c ===================== */

int mania_get_titlelogo_list_id(void) { return s_list_id_titlelogo; }
int mania_get_titlesonic_list_id(void){ return s_list_id_titlesonic; }
int mania_get_titlebg_list_id(void)   { return s_list_id_titlebg; }
int mania_get_titlesetup_list_id(void){ return s_list_id_titlesetup; }

void mania_set_titlelogo_list_id(int v) { (void)v; /* set by setup_title_assets */ }
void mania_set_titlesonic_list_id(int v){ (void)v; }
void mania_set_titlebg_list_id(int v)   { (void)v; }
void mania_set_titlesetup_list_id(int v){ (void)v; }

/* Music_PlayTrack / Music_Stop — Saturn maps to CD-DA track 3 (title). */
void Music_PlayTrack(uint8 trackID)
{
    (void)trackID;
#if defined(JO_COMPILE_WITH_AUDIO_SUPPORT) && !defined(QA_SFX_PROBE)
    /* TRACK_STAGE at title -> CD-DA track 3 per build.bat layout.
     * Suppressed under QA_SFX_PROBE so the SFX capture is BGM-free. */
    jo_audio_play_cd_track(3, 3, true);
#endif
}

void Music_Stop(void)
{
#ifdef JO_COMPILE_WITH_AUDIO_SUPPORT
    jo_audio_stop_cd();
#endif
}

void mania_screen_sync(void)
{
    /* Keep the engine-compat g_rsdk_screen in sync with the Mania-side
     * ScreenInfo mirror so per-class draw code reads consistent values. */
    extern rsdk_screen_info_t g_rsdk_screen;
    g_rsdk_screen.position_x = ScreenInfo->position.x;
    g_rsdk_screen.position_y = ScreenInfo->position.y;
    g_rsdk_screen.size_x     = ScreenInfo->size.x;
    g_rsdk_screen.size_y     = ScreenInfo->size.y;
    g_rsdk_screen.center_x   = ScreenInfo->center.x;
    g_rsdk_screen.center_y   = ScreenInfo->center.y;
}

/* Phase 1.17 — title-scene state machine + direct-draw layout.
 *
 * The entity-driven path (rsdk_object_draw_all → per-class Draw → resolver)
 * has a remaining bug in scene-entity creation that the Phase 1.17 audit
 * did not fully isolate (diagnostic shows g_rsdk_class_count >= 6 +
 * find_class("TitleLogo") >= 0 succeed, but entity table walk shows
 * zero TitleLogo entities). Rather than ship a broken title, we ship a
 * direct-draw layout that mirrors the decomp's per-frame draw using the
 * authoritative pivots from docs/title_ground_truth.md. The state
 * machine drives the visible/active flags via a self-contained timer in
 * this function.
 *
 * Decomp citations:
 *   - docs/title_ground_truth.md (entity X/Y per Scene1.bin)
 *   - tools/_decomp_raw/SonicMania_Objects_Title_TitleSetup.c:137-266
 *     (state machine timing: 64 frames Wait → 1 frame flash → 0x300
 *     WaitForSonic → 120 SetupLogo → WaitForEnter)
 *   - tools/_decomp_raw/SonicMania_Objects_Title_TitleLogo.c:62-64
 *     (PRESSSTART blink predicate `!(timer & 0x10)`)
 *
 * Saturn-side world→jo conversion (per docs/title_ground_truth.md):
 *   jo_x = world_x - 256  (since camera_x = 256 - center.x = 96, and
 *                          jo's draw3D centers at (160,112); world 256
 *                          maps to jo 0)
 *   jo_y = world_y - 112  (no camera Y offset)
 *
 * The per-asset .SPR canvas has the decomp pivot baked in by
 * tools/convert_anim_sprite.py, so we pass (jo_x, jo_y) directly. */
#include "Objects/Title/TitleAssets.h"

/* Title-scene state machine — locally-owned for direct-draw. */
typedef enum {
    TS_FADE_IN = 0,         /* black->visible fade (~64 frames)            */
    TS_FLASH,               /* 1-frame white flash                         */
    TS_WAIT_FOR_SONIC,      /* 0x300 / 16 = ~48 frames Sonic settle        */
    TS_PRESS_REVEAL,        /* 120 frames hold before PRESS START          */
    TS_WAIT_FOR_ENTER,      /* permanent (until START press → GHZ)         */
    /* Phase 3.2 TS_MENU_ACTIVE enum REVERTED 2026-05-28. */
    TS_TRANSITION_TO_GHZ,   /* Phase 2.1: stop title BGM, hide NBG2 title, */
                            /* swap to GHZ FG+sky, start track 2           */
    TS_GHZ_ACTIVE,          /* Phase 2.1: GHZ visible (Player is Phase 2.2)*/
    TS_COUNT
} title_state_t;

/* Phase 2.1 — auto-advance to GHZ for QA captures.
 *
 * The QA capture pipeline (tools/qa_boot.ps1) does not yet simulate
 * input, so the title→GHZ transition needs a fallback path that fires
 * without user input. The build defines GHZ_AUTOADVANCE_TICKS as the
 * tick count after entering TS_WAIT_FOR_ENTER at which the transition
 * auto-fires. Set to 0 to disable auto-advance (production behavior
 * once Player + input are wired through). Phase 2.1 ships with
 * auto-advance enabled so we can capture both title and GHZ from a
 * single QA boot. */
/* Phase 1.31 Fix #2 (2026-05-27) — DISABLE auto-advance to GHZ.
 *
 * Diagnostic capture (90 frames @ 3 fps via tools/qa_boot.ps1 -Wait 4
 * -Every 0.33 -Shots 90, analyzed via tools/analyze_diag_tail.py):
 *
 *   frame 86 t=28.05s  delta=13.33  (settled title, neon-green ~115K px)
 *   frame 87 t=28.38s  delta=103.73 (title->GHZ transition fires)
 *   frame 88 t=28.71s  delta=0.00   (STUCK — GHZ NBG1 garbage frozen)
 *   frame 89 t=29.04s  delta=0.00   (STUCK)
 *   frame 90 t=29.37s  delta=0.00   (STUCK)
 *
 * Frame 87 screenshot (qa_diag_87.png) shows a vertical-stripe NBG1
 * tile garbage pattern — GHZ tried to load but the deferred audio
 * kick (s_ts_timer==30) and/or deferred player kick (s_ts_timer==45)
 * post-transition hangs the SH-2 in a way that leaves VDP2 frozen.
 * The phase 2.3c probes (mania_diag_probe_draw) remain rendered as
 * the magenta-checker + white-stripe squares at screen-bottom-center,
 * proving VDP1 is alive but the GHZ chain (mania_ghz_deferred_*) is
 * not.  This is a pre-existing GHZ-side issue tracked separately as
 * Task #88 (Phase 2.3f: visible Sonic+entities in GHZ — LTO/volatile/
 * deferred-kick audit).
 *
 * For Phase 1.31 (title backdrop work), the title must stay alive
 * indefinitely so longer-window 3 fps captures (Fixes #1, #3, #4)
 * land in TS_WAIT_FOR_ENTER, not in the broken GHZ load chain.
 * Setting GHZ_AUTOADVANCE_TICKS to 0 disables the auto-advance
 * (production behavior anyway — see comment block above; auto-advance
 * was a QA-time convenience that has outlived its usefulness now
 * that the title-side work needs >30 s captures and the GHZ side
 * is independently broken).
 *
 * Restoration path: when Phase 2.3f resolves Task #88 (GHZ Sonic/
 * entity/HUD visibility), restore GHZ_AUTOADVANCE_TICKS to 480 or
 * use a build-time -D override per the original Phase 2.1 comment.
 * The fix is one-line, fully reversible.
 *
 * Gate: tools/qa_phase1_31_deadlock_gate.py asserts frames 75..90 of
 * a fresh 90-frame 3 fps capture contain no 4-frame identical run.
 * RED-baseline: stuck run of 4 frames at 87..90 (delta=0.00 each).
 * GREEN target: all inter-frame deltas in window stay > 1.0. */
#ifndef GHZ_AUTOADVANCE_TICKS
#define GHZ_AUTOADVANCE_TICKS 0
#endif

/* Phase 2.3f (Task #88) — extern-visible volatile state flag fix.
 *
 * Title-state machine + GHZ flags need volatile + extern-linkage because
 * LTO whole-program analysis in the Docker Linux build
 * (jo-engine_makefile:276-278 adds `-flto` for non-Windows) was
 * internalizing static-volatile into `.lto_priv` symbols and CSE'ing
 * reads inside `title_state_tick`. Empirical evidence: Phase 2.3e Round
 * 3c counter sentinel at top of `mania_ghz_tick_and_draw` never
 * exceeded 60 across 30s capture (Game.c:2105-2113), indicating the
 * state machine was stuck. Removing `static` AND adding `volatile`
 * gives the symbol persistent storage with an addressable BSS slot
 * the qa_phase2_3f gate can peek. */
volatile int            s_ts_state = TS_FADE_IN;
volatile unsigned int   s_ts_timer = 0;
static unsigned int     s_ts_total_frames = 0;

/* === Phase 2.2 — Player runtime + Sonic-sprite + camera follow ========
 *
 * Owned by Game.c because the title→GHZ transition is here. Visible to
 * main.c via mania_ghz_*() in Game.h.
 *
 * Per docs/COMPREHENSIVE_PLAN.md §12.2 (Phase 2.2 plan). */

static player_t       g_ghz_player;
static sms_world_t    g_ghz_world;
static int            g_ghz_sonic_idle_sid  = -1;  /* jo sprite id (32x40 idle) */
static int            g_ghz_sonic_walk_sid0 = -1;  /* first walk frame (12 frames consecutive) */
static int            g_ghz_sonic_walk_count = 0;
/* Phase 2.3f (Task #88) — extern-visible volatile to defeat LTO CSE
 * across the mania_tick -> ghz_is_active() -> mania_ghz_tick_and_draw
 * chain. Removing `static` keeps the symbol in the .map (LTO whole-
 * program analysis was internalizing static-volatile and CSE'ing reads;
 * an external-linkage symbol forces persistent storage with an
 * addressable name the qa_phase2_3f gate can peek). */
volatile bool         g_ghz_player_ready = false;

/* Phase 2.4b (Task #139) — savestate-peek landmark for the
 * qa_phase2_4b_collision_gate P3/P4 predicates. g_ghz_player is
 * static so LTO is free to inline / rename its address; the gate
 * needs a stable address to peek the player_t fields from a .mc0
 * dump. This `used` external pointer pins the address as a non-
 * inlinable symbol exposed in the linker map. NOT a cross-TU
 * volatile readiness flag (per memory/sync-load-eliminates-cross-
 * tu-volatile.md) — just an address landmark for QA tooling, never
 * read by gameplay code. */
__attribute__((used))
player_t * const g_ghz_player_addr = &g_ghz_player;

/* Phase 2.5.1 (Task #162) — savestate-peek landmarks for
 * qa_phase2_5_1_gate.py P3 (runtime --with-savestate). Mirror the player
 * state-machine selector + ground speed into stable `used` globals updated
 * once per physics tick, so the gate can assert "DOWN at speed -> ROLL"
 * (state == PLAYER_STATE_ROLL, |gsp| >= 0x8000) from a captured .mc0 without
 * needing g_ghz_player's LTO-mangled address. Same QA-landmark role as
 * g_ghz_player_addr; never read by gameplay code. */
__attribute__((used)) uint8_t g_player_diag_state = 0;
__attribute__((used)) int32_t g_player_diag_gsp   = 0;
/* Phase 2.5.2 (Task #165) — spindash charge landmark for
 * qa_phase2_5_2_gate.py P3a (mid-charge: state == SPINDASH, charge > 0). */
__attribute__((used)) int32_t g_player_diag_charge = 0;

static bool           g_ghz_autorun = false;
static int            g_ghz_input_grace = 0;
static bool           g_ghz_prev_left = false;
static bool           g_ghz_prev_right = false;
static bool           g_ghz_prev_jump = false;
static unsigned int   g_ghz_anim_timer = 0;

/* Phase 2.3e — cached camera position from the most recent
 * mania_ghz_tick_and_draw call. Read by mania_ghz_draw_only when
 * that function runs at mania_tick body scope (after the
 * ghz_is_active() block has updated the camera for this frame).
 *
 * Initialized to (-1,-1) sentinel so mania_ghz_draw_only can
 * detect "no tick yet" and early-return cleanly. */
/* Phase 2.3f (Task #88) — extern-visible volatile so the cached camera
 * coordinates set inside mania_ghz_tick_and_draw are visible to
 * mania_ghz_draw_only on the SAME frame's body-scope call. Removing
 * `static` keeps the symbol resolvable by the qa gate. */
volatile int          g_ghz_cached_cam_x = -1;
volatile int          g_ghz_cached_cam_y = -1;

/* Camera math constant — Mania-faithful CAM_FOOT_OFFSET. The decomp's
 * Camera.c sets `offset.y = 0x200000` in Player_Action_Jump (decomp
 * Player.c:3309) = 32 px in integer space. The archived build used 28;
 * we use the decomp value 32 per CLAUDE.md §1 binding directive. */
#define MANIA_CAM_FOOT_OFFSET   32

/* Sonic anchor on screen — decomp uses ScreenInfo center.x (=160) so
 * Sonic stays centered horizontally. Archived used 140 (off-center for
 * the lookahead bias). Mania-faithful: 160. */
#define MANIA_SONIC_SCR_X       160

/* Sprite vertical pivot relative to sprite center. The 40 px tall walk
 * sprite has the feet at the bottom — pivot adjustment so the sprite
 * draws with feet at p->ypos. jo_sprite_draw3D anchors at canvas
 * center, so we subtract H/2 - 4 (8 px below center for a 40 px sprite
 * leaves the feet at +20 px, then -4 for a slight lift so Sonic's feet
 * sit ON the surface line, not embedded in it). */
#define MANIA_SONIC_SPR_FEET_OFF 16

/* Phase 2.2 — title→GHZ transition helper. Performs the engine-layer
 * NBG2 swap (title backdrop → GHZ sky), NBG1 unhide, BGM switch. */
extern void mania_free_title_bg_buffers(void);   /* defined in src/main.c */
extern void mania_free_clouds_bg_buffers(void);  /* Phase 1.34b — src/main.c */

/* Phase 2.3d Lead A probe — sentinel-green 16x16 BGR1555 sprite
 * registered at the END of mania_ghz_player_load_assets (downstream
 * of all Sonic SPR loads, INSIDE the title->GHZ transition window).
 *
 * Decision matrix:
 *   - If this probe renders in GHZ but neither Sonic nor entity SPRs do
 *     -> Lead A FALSIFIED: post-transition jo_sprite_add chain works
 *     fine; visibility is purely coord (Lead B).
 *   - If this probe DOESN'T render -> Lead A CONFIRMED: a second race
 *     exists in the post-transition jo_sprite_add chain itself. Apply
 *     the Phase 2.3c 30-tick deferral pattern to the asset loads.
 *
 * Static .data placement (not BSS, not heap) — no zero-init race.
 * MSB=1 forces opaque per jo BGR1555 convention. 0x83E0 = MSB=1 + green. */
#define MANIA_PHASE23D_PROBE_W 16
#define MANIA_PHASE23D_PROBE_H 16
static const jo_color s_phase23d_probe_pixels[MANIA_PHASE23D_PROBE_W *
                                              MANIA_PHASE23D_PROBE_H] = {
    /* Solid sentinel-green 16x16 (256 pixels), all 0x83E0. */
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
    0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,0x83E0,
};
static int s_phase23d_probe_sprite_id = -1;
int mania_phase23d_probe_sprite_id(void) { return s_phase23d_probe_sprite_id; }

/* Phase 2.2 — Sonic sprite loader. cd/SONIC.SPR (1 frame 32x40 idle) +
 * cd/SONWALK.SPR (12 frames 40x40 walk) in the canonical Saturn-side
 * BGR1555 sprite header format (u16 BE frame_count + width + height +
 * N*W*H*u16 BE BGR1555 pixels — verified by tools/_phase22_inspect.py). */
void mania_ghz_player_load_assets(void)
{
    if (g_ghz_sonic_idle_sid >= 0) return;  /* already loaded */

    /* Idle frame. */
    int len = 0;
    unsigned char *raw = (unsigned char *)jo_fs_read_file("SONIC.SPR", &len);
    if (raw && len >= 6) {
        unsigned short fc = ((unsigned short)raw[0] << 8) | raw[1];
        unsigned short w  = ((unsigned short)raw[2] << 8) | raw[3];
        unsigned short h  = ((unsigned short)raw[4] << 8) | raw[5];
        if (fc == 1 && (int)(6 + (int)w * (int)h * 2) == len) {
            jo_img img;
            img.data   = (jo_color *)(raw + 6);
            img.width  = w;
            img.height = h;
            g_ghz_sonic_idle_sid = jo_sprite_add(&img);
        }
    }
    if (raw) jo_free(raw);

    /* Walk frames — load as N consecutive sprite ids by chopping the
     * payload into per-frame jo_img calls. jo_sprite_add returns
     * sequential ids when called in a row. */
    len = 0;
    raw = (unsigned char *)jo_fs_read_file("SONWALK.SPR", &len);
    if (raw && len >= 6) {
        unsigned short fc = ((unsigned short)raw[0] << 8) | raw[1];
        unsigned short w  = ((unsigned short)raw[2] << 8) | raw[3];
        unsigned short h  = ((unsigned short)raw[4] << 8) | raw[5];
        int per = w * h * 2;
        if ((int)(6 + (int)fc * per) == len) {
            for (int f = 0; f < (int)fc; ++f) {
                jo_img img;
                img.data   = (jo_color *)(raw + 6 + f * per);
                img.width  = w;
                img.height = h;
                int sid = jo_sprite_add(&img);
                if (f == 0) g_ghz_sonic_walk_sid0 = sid;
            }
            g_ghz_sonic_walk_count = fc;
        }
    }
    if (raw) jo_free(raw);

    /* Phase 2.3d Lead A probe — register a sentinel-green 16x16 BGR1555
     * sprite AFTER all Sonic SPR loads (and AFTER entities_load_assets
     * has run earlier at engine init). This probe registration occurs
     * INSIDE the title->GHZ transition window, downstream of the Sonic
     * jo_sprite_add chain. If this renders, the deferred-audio fix from
     * Phase 2.3c was sufficient to clear ALL VDP1 races during the
     * transition; entity/Sonic SPRs are loaded correctly and invisibility
     * is purely coord-driven (Lead B). If it doesn't render, a second
     * race exists in the post-transition jo_sprite_add chain itself —
     * apply the Phase 2.3c 30-tick deferral pattern to the asset loads.
     *
     * See docs/COMPREHENSIVE_PLAN.md §12.3d. */
    if (s_phase23d_probe_sprite_id < 0) {
        jo_img img;
        img.width  = MANIA_PHASE23D_PROBE_W;
        img.height = MANIA_PHASE23D_PROBE_H;
        img.data   = (jo_color *)s_phase23d_probe_pixels;
        s_phase23d_probe_sprite_id = jo_sprite_add(&img);
    }
}

/* Phase 2.2b — Preload world collision table into LWRAM (NOT jo pool).
 *
 * Pre-2.2b implementation called jo_fs_read_file which allocated 64 KB
 * from jo's malloc pool. The combined Phase 2.2 GHZ residents (FG 357 +
 * SKY 90 + Sonic SPRs 40 + SURF 64 = 551 KB) exceeded jo's 393 KB pool,
 * causing jo_fs_read_file to silently return NULL on one of the GHZ
 * assets and the NBG1 layer to display a vertical-stripe scramble.
 *
 * Phase 2.2b moves the SURF table to Saturn Work RAM-L (LWRAM) at
 * 0x00200000 (1 MB region, unused by jo since jo's heap lives in
 * Work RAM-H at 0x06xxxxxx). LWRAM is SH-2 directly addressable and
 * accessible to SCU DMA. The collision table is pure read-only data
 * with a static base pointer — ideal LWRAM resident.
 *
 * Loader bypasses jo_fs_read_file (which always jo_malloc's). Uses the
 * Phase 2.2b helper rsdk_storage_load_to_lwram which calls the SBL
 * GFS API (GFS_NameToId/Open/GetFileSize/Fread/Close) directly with a
 * caller-supplied destination buffer. See storage.c:rsdk_storage_load_
 * to_lwram for the GFS call sequence (DTS-136-R2-093094 §3.1, §4).
 *
 * Authority: ST-097-R5 §2.1 (Saturn memory map) — LWRAM 0x00200000-
 * 0x002FFFFF (1 MB). docs/COMPREHENSIVE_PLAN.md §12.2b for the full
 * pool-budget rationale.
 *
 * Failure path preserved: if the SBL load fails, g_ghz_world.raw stays
 * NULL and Phase 2.2's graceful-degrade keeps Sonic floating instead
 * of crashing (per Phase 2.2 anti-regression policy). */
#define GHZ_SURF_LWRAM_ADDR  ((unsigned char *)0x00200000)
#define GHZ_SURF_LWRAM_SIZE  0x10000   /* 64 KB — exact GHZ1SURF.BIN size */

void mania_ghz_player_preload_world(void)
{
    if (g_ghz_world.raw) return;   /* idempotent */

    int read = rsdk_storage_load_to_lwram("GHZ1SURF.BIN",
                                          (void *)GHZ_SURF_LWRAM_ADDR,
                                          GHZ_SURF_LWRAM_SIZE);

    if (read == (int)GHZ_SURF_LWRAM_SIZE) {
        /* Load succeeded — point the world struct at the LWRAM region.
         * No jo_malloc was issued; pool consumption is unchanged. */
        g_ghz_world.raw       = (const unsigned char *)GHZ_SURF_LWRAM_ADDR;
        g_ghz_world.width_px  = 16384;
        /* world height set later in init_on_transition after GHZSetup. */
        g_ghz_world.height_px = 2048;  /* placeholder until GHZSetup runs */
        /* Phase 2.4g.3 — two-plane bridge. GHZ1SURF.BIN carries only the
         * primary plane, so the B path (raw_alt) is degenerate / identical
         * to A for now. PlaneSwitch toggles active_path between them; when a
         * divergent plane-B asset is extracted only this seed changes. */
        g_ghz_world.raw_alt     = g_ghz_world.raw;
        g_ghz_world.active_path = 0;
    } else {
        /* Soft fail — Player still inits with raw=NULL, falls back to
         * "Sonic floats at fixed Y" per Phase 2.2 graceful-degrade. */
        g_ghz_world.raw       = NULL;
        g_ghz_world.width_px  = 16384;
        g_ghz_world.height_px = 2048;
        g_ghz_world.raw_alt     = NULL;
        g_ghz_world.active_path = 0;
    }
}

/* Phase 2.4f (Task #143) — canonical Mania-mode spawn from Scene1.bin.
 *
 * Decomp authority:
 *   - tools/_decomp_raw/SonicMania_Objects_Global_Player.c:542-672
 *     Player_Create reads entity->position from the entity-table
 *     dispatch (rsdkv5-src/RSDKv5/RSDK/Scene/Scene.cpp:528-665
 *     LoadSceneAssets -> ProcessObjects -> per-class Create).
 *   - extracted/Data/Stages/GHZ/Scene1.bin Player object (hash
 *     md5("Player") = 636da1d35e805b00eae0fcd8333f9234). Slot 887
 *     holds the active Mania-mode Sonic spawn at integer pixel
 *     (108, 947) with characterID enum=3 (ID_KNUCKLES) for the
 *     paired-AI sidekick assignment; default Mania play spawns
 *     ID_SONIC at the same coord.
 *
 * Saturn-port deviation (acknowledged per docs/MANIA_MODE_PARITY_PLAN.md
 * §3 policy): the Approach A canonical path (rsdk_load_scene -> per-
 * entity Create dispatch with attribute table) is the long-term target
 * but requires substantial new engine plumbing (scene_ghz currently
 * bypasses rsdk_load_scene). Phase 2.4f's lowest-impact mitigation per
 * §11.5 risk policy: extract (x,y) from Scene1.bin at BUILD TIME via
 * tools/extract_ghz_spawn.py, ship as cd/GHZ1SPWN.BIN (12 B big-endian
 * blob: i32 x, i32 y, u32 characterID), read at transition. The COORD
 * IS DECOMP-CANONICAL; only the read path is Saturn-fit. */
#define GHZ_SPWN_FALLBACK_X  108   /* Scene1.bin slot 887 (canonical) */
#define GHZ_SPWN_FALLBACK_Y  947   /* Scene1.bin slot 887 (canonical) */

static bool ghz1_spawn_from_cd(int *out_x_px, int *out_y_px)
{
    /* Read cd/GHZ1SPWN.BIN (12 B big-endian: i32 x, i32 y, u32 cid).
     * Uses jo_fs_read_file (small one-shot read, ~64 B max) — drops
     * into jo's malloc pool briefly, frees immediately after copy. */
    int len = 0;
    void *raw = jo_fs_read_file("GHZ1SPWN.BIN", &len);
    if (!raw || len < 8) {
        if (raw) jo_free(raw);
        return false;
    }
    const unsigned char *b = (const unsigned char *)raw;
    /* Big-endian i32 decode. Promote signed via sign-extend on bit 31. */
    int32_t x = ((int32_t)b[0] << 24) | ((int32_t)b[1] << 16)
              | ((int32_t)b[2] <<  8) |  (int32_t)b[3];
    int32_t y = ((int32_t)b[4] << 24) | ((int32_t)b[5] << 16)
              | ((int32_t)b[6] <<  8) |  (int32_t)b[7];
    jo_free(raw);
    /* Plausibility clamp: GHZ1 world is 16384x2048 px. Out-of-range
     * value means a corrupted asset; reject and fall back. */
    if (x < 0 || x >= 16384 || y < 0 || y >= 2048) return false;
    *out_x_px = (int)x;
    *out_y_px = (int)y;
    return true;
}

/* Phase 2.2 — Player init called from the title→GHZ transition. By the
 * time this runs, mania_ghz_player_preload_world + mania_ghz_player_
 * load_assets have already brought up the heap-resident collision table
 * and Sonic SPRs, and GHZSetup_StageLoad has set g_ghz_fg_xs/ys (so the
 * world height for the fall-clamp boundary is now known).
 *
 * Phase 2.4f (Task #143) — canonical spawn from Scene1.bin replaces the
 * pre-fix "spawn at world col 0 with autorun=true" Saturn-fit shortcut.
 * Per memory/post-button-press-canon-scope.md the post-button-press
 * path must be decomp-canonical. */
void mania_ghz_player_init_on_transition(void)
{
    /* Refine world height now that GHZSetup has run. */
    int h = ghz_world_height_px();
    if (h > 0) g_ghz_world.height_px = h;

    /* Phase 2.4g.2 (Task #153) — seed the Zone camera/player/death bounds
     * from the GHZ world size (decomp Zone.c:221-235 default-bounds init).
     * Runs once per GHZ load now that g_ghz_fg_xs/ys are known. Each
     * BoundsMarker_Create's ApplyBounds(setPos=true) then overrides
     * cameraBoundsB/T (+ paired playerBounds + deathBoundary) as the
     * spawn-time player position crosses each marker's X. */
    {
        int ww = ghz_world_width_px();
        int wh = ghz_world_height_px();
        if (ww > 0 && wh > 0)
            zone_init_default_bounds(ww, wh);
    }

    /* Phase 2.4f — decomp-canonical spawn from Scene1.bin slot 887
     * via the build-extracted cd/GHZ1SPWN.BIN artefact. Compile-time
     * fallback (GHZ_SPWN_FALLBACK_X/Y) holds the same Scene1.bin
     * value so a missing CD asset still places Sonic at the canonical
     * coord (defense-in-depth against asset-staging regressions). */
    int spawn_x = GHZ_SPWN_FALLBACK_X;
    int spawn_y = GHZ_SPWN_FALLBACK_Y;
    (void)ghz1_spawn_from_cd(&spawn_x, &spawn_y);

#ifdef QA_INVBLOCK_PROBE
    /* Phase 2.4g.1 gate P4 capture ONLY (tools/qa_phase2_4g1_gate.py
     * --with-savestate). The canonical Mania spawn (108,947) sits 1288 px
     * from the nearest InvisibleBlock (slot 1016 at world (1396,920) per
     * GHZ Scene1.bin), and Phase 2.4f removed autorun, so a resting player
     * never overlaps a block. The plan explicitly verifies P4 "with the
     * player placed over an InvisibleBlock instance"; this define provides
     * that deterministic placement. Compiled out of the release binary
     * (same QA-instrument precedent as QA_MODE's title-hold). */
    spawn_x = 1396;
    spawn_y = 920;
#endif

    Player_Init(&g_ghz_player, PLAYER_ID_SONIC,
                ((int32_t)spawn_x) << 16, ((int32_t)spawn_y) << 16);
    g_ghz_player.onGround = true;

    /* Phase 2.4f (Task #143) — autorun + input-grace REMOVED per
     * memory/post-button-press-canon-scope.md ("NO `g_ghz_autorun` —
     * wait for ControllerState input per decomp Player.c"). The
     * decomp's Player_Input_P1 (Player.c:Player_Input_*) reads
     * ControllerState every frame; Sonic stays at the spawn coord
     * until the player presses a direction. The pre-fix Saturn-fit
     * autorun is a forbidden deviation. */
    g_ghz_prev_left = g_ghz_prev_right = g_ghz_prev_jump = false;
    g_ghz_anim_timer  = 0;
    g_ghz_player_ready = true;
}

/* Phase 2.2 — Sonic sprite draw at player screen position.
 * Animation: walk frame index = (anim_timer >> 1) % 12 when moving,
 * idle frame when stationary. Decomp Player_HandleGroundAnimation
 * (Player.c:2917-3080) handles full anim selection (idle/bored/walk/
 * jog/run/dash/skid/push/spindash); Phase 2.2 ships idle + walk only. */
static void mania_ghz_draw_sonic(int cam_x, int cam_y)
{
    int px = g_ghz_player.xpos >> 16;
    int py = g_ghz_player.ypos >> 16;
    int sx = (px - cam_x) - MANIA_SONIC_SCR_X;
    int sy = (py - cam_y) - 112 - MANIA_SONIC_SPR_FEET_OFF;

    /* Frame select. */
    bool moving = g_ghz_player.onGround &&
                  (g_ghz_player.gsp > PLAYER_FIXED(0.4) ||
                   g_ghz_player.gsp < -PLAYER_FIXED(0.4));
    int sid = g_ghz_sonic_idle_sid;
    if (moving && g_ghz_sonic_walk_count > 0 && g_ghz_sonic_walk_sid0 >= 0) {
        /* Faster cycle when going fast — decomp Player_HandleGround-
         * Animation tunes animator.speed by |groundVel|. Phase 2.2
         * uses a coarse 2-tier: normal cycle / fast cycle. */
        int absgsp = g_ghz_player.gsp < 0 ? -g_ghz_player.gsp : g_ghz_player.gsp;
        int divisor = absgsp > PLAYER_FIXED(3.0) ? 1 : 2;
        int frame = (g_ghz_anim_timer / divisor) % g_ghz_sonic_walk_count;
        sid = g_ghz_sonic_walk_sid0 + frame;
    }
    if (sid < 0) return;

    /* z value below NBG2 sky (1) above NBG1 ground (6) — sprite layer.
     * Decomp drawGroup=2 for player; we use z=150 to sit between any
     * future foreground objects (z=100 monitors) and the HUD (z=200). */
    int z = 150;
    if (g_ghz_player.facing_left) {
        jo_sprite_enable_horizontal_flip();
        jo_sprite_draw3D(sid, sx, sy, z);
        jo_sprite_disable_horizontal_flip();
    } else {
        jo_sprite_draw3D(sid, sx, sy, z);
    }
}

/* Phase 2.2/2.3e — per-frame Player physics tick + camera follow + sky
 * parallax. **NO SPRITE DRAWS** — Phase 2.3e Path-4 moved all sprite
 * draws to mania_ghz_draw_only which runs at mania_tick body scope
 * (NOT inside the if (ghz_is_active()) block, because §12.3d empirical
 * finding established that draws issued inside that conditional are
 * silently dropped by SGL).
 *
 * Called from mania_tick INSIDE the if (ghz_is_active()) block — its
 * side effects (player state, camera, sky scroll, work-ram page) DO
 * depend on the conditional. Only sprite draws moved out.
 *
 * Mirrors the archived main.c game_tick body (L1700-1817) reduced to
 * the Phase 2.2 subset (no rings/badniks/hud/death-respawn yet).
 *
 * Phase 2.3f (Task #88) — __attribute__((used)) prevents LTO whole-
 * program elision. This function is called only from
 * mania_tick at body scope, inside an `if (ghz_is_active())` block;
 * LTO's caller analysis combined with the previously-non-volatile
 * `g_ghz_fg_ready/sky_ready` flags allowed the optimizer to treat
 * this function as never-reached and elide it (or worse, inline
 * trivially around the volatile sentinel). The attribute forces
 * the compiler to emit the symbol and prevents whole-program
 * elision even when the call site appears dead. Cite GCC attribute
 * reference: "used" — "This attribute, attached to a function,
 * means that code must be emitted for the function even if it
 * appears that the function is not referenced." */
__attribute__((used))
void mania_ghz_tick_and_draw(void)
{
    /* Phase 2.3j — quantitative gate evidence. Incremented unconditionally
     * at the top of this function (BEFORE the player-ready early-return)
     * so a captured savestate at any post-transition frame proves whether
     * the call site reached this function. Peek via
     * tools/qa_phase2_3j_sync_load_gate.py P3 predicate. */
    ++g_ghz_active_tick_counter;

    if (!g_ghz_player_ready) return;

    /* Phase 2.4j.1 (Task #156) — TitleCard act-intro tick. Runs during the
     * intro freeze; g_titlecard_active stays 1 until the card slides away,
     * gating Player_Tick / jump SFX / HUD below (decomp parity: gameplay is
     * paused behind the act-intro card). */
    titlecard_tick();

    ++g_ghz_anim_timer;

    /* === Input merging + autorun takeover ===
     *
     * Archived main.c:1733-1754 — 30-tick boot-transient grace; first
     * rising edge on LEFT/RIGHT/A/B/C after grace disables autorun. */
    const rsdk_controller_state_t *c = rsdk_controller_state(CONT_ANY);
    bool now_l = c && c->key_left.down;
    bool now_r = c && c->key_right.down;
    bool now_d = c && c->key_down.down;
    bool now_u = c && c->key_up.down;
    bool now_j = c && (c->key_a.down || c->key_b.down || c->key_c.down);

    if (g_ghz_input_grace) {
        --g_ghz_input_grace;
    } else if (g_ghz_autorun) {
        if ((now_l && !g_ghz_prev_left) ||
            (now_r && !g_ghz_prev_right) ||
            (now_j && !g_ghz_prev_jump)) {
            g_ghz_autorun = false;
        }
    }
    g_ghz_prev_left  = now_l;
    g_ghz_prev_right = now_r;
    g_ghz_prev_jump  = now_j;

    bool in_left  = now_l;
    bool in_right = now_r || g_ghz_autorun;
    bool in_down  = now_d;
    bool in_up    = now_u;
    bool in_jump  = now_j;

    /* === Auto-jump AI (archived main.c:1756-1784) ===
     *
     * Fires while autorun is on. Either:
     *   - moving (|gsp| > 1) and the 24px-ahead column is >17 px above
     *     or below current Y (steep step/cliff)
     *   - blocked (gsp==xsp==0) and the ahead column is shallower than
     *     current ypos (jumpable mound). */
    if (g_ghz_autorun && g_ghz_player.onGround && g_ghz_world.raw) {
        int look_x = (g_ghz_player.xpos >> 16) + 24;
        int look_y = Player_SurfaceY(&g_ghz_world, look_x);
        int cur_y  = g_ghz_player.ypos >> 16;
        bool moving  = (g_ghz_player.gsp > PLAYER_FIXED(1.0) ||
                        g_ghz_player.gsp < -PLAYER_FIXED(1.0));
        bool blocked = g_ghz_player.gsp == 0 && g_ghz_player.xsp == 0;
        bool step_change = (look_y != SMS_NO_FLOOR &&
                            (look_y < cur_y - 17 || look_y > cur_y + 17));
        bool wall_ahead  = (look_y != SMS_NO_FLOOR && look_y < cur_y - 8);
        if ((moving && step_change) || (blocked && wall_ahead)) {
            /* Trigger a synthetic jump press for the next Player_Tick. */
            in_jump = true;
            g_ghz_prev_jump = false;   /* force edge detect inside Tick */
        }
    }

    /* === Phase 2.4a — Player jump SFX (decomp parity) ===
     *
     * Decomp cite: tools/_decomp_raw/SonicMania_Objects_Global_Player.c:3327
     *   RSDK.PlaySfx(Player->sfxJump, false, 255);
     * fires inside Player_Action_Jump when the jump button transitions
     * from released to pressed AND the player is on the ground (the
     * decomp also gates on jumpAbilityState/super state; Phase 2.4a
     * ports the basic ground-jump case only — air jump abilities
     * land in Phase 2.5).
     *
     * Detect the same edge here, BEFORE Player_Tick mutates onGround.
     * `in_jump` already collapses the auto-jump-AI + raw button press
     * and the edge is gated on g_ghz_prev_jump==false (set up at
     * lines 1122-1124). The synthetic-jump-from-autorun path also
     * forces g_ghz_prev_jump=false so this edge fires for both real
     * and synthetic jumps — matching the decomp's PlaySfx-on-every-
     * Action_Jump-call behaviour. */
    bool jump_edge = !g_titlecard_active && in_jump && g_ghz_player.onGround;
    if (jump_edge) {
        entities_play_sfx_jump();
    }

#ifdef QA_SFX_PROBE
    /* SFX-confirmation probe (user request 2026-05-29: "need a test").
     * Independently of gameplay events, fire the jump SFX on a fixed
     * cadence so a WAV capture records deterministic PCM bursts. BGM is
     * suppressed (Music_PlayTrack + the GHZ track-2 start are #ifndef'd
     * out below) so the capture is SFX-only: any nonzero audio in the WAV
     * must be the PCM sample. Fires every QA_SFX_PROBE_PERIOD ticks
     * (default 30 = 0.5s at 60 Hz). Compiled out of the release binary. */
#ifndef QA_SFX_PROBE_PERIOD
#define QA_SFX_PROBE_PERIOD 30
#endif
    {
        static int s_qa_sfx_probe_tick = 0;
        if ((s_qa_sfx_probe_tick++ % QA_SFX_PROBE_PERIOD) == 0) {
            entities_play_sfx_jump();
        }
    }
#endif

    /* === Player physics tick ===
     * Phase 2.4j.1 — frozen while the TitleCard act-intro is showing. */
    if (!g_titlecard_active)
        Player_Tick(&g_ghz_player, &g_ghz_world,
                    in_left, in_right, in_down, in_up, in_jump);

    /* Phase 2.5.1 — refresh the QA-landmark mirrors AFTER the tick so a
     * savestate captured at any frame reflects the player's current state +
     * ground speed (qa_phase2_5_1_gate.py P3). */
    g_player_diag_state  = (uint8_t)g_ghz_player.state;
    g_player_diag_gsp    = g_ghz_player.gsp;
    g_player_diag_charge = g_ghz_player.spindashCharge;  /* Phase 2.5.2 P3a */

#ifdef QA_INVBLOCK_PROBE
    /* Phase 2.4g.1 gate P4 capture ONLY. Pin the player at the slot-1016
     * InvisibleBlock center (world 1396,920 per GHZ Scene1.bin) every tick
     * and zero its velocity so the player remains inside the block's hitbox
     * AABB (x[1372,1420] y[904,936]) at the deep capture frame. Without the
     * pin, Player_Tick gravity drops the player out of the AABB within a few
     * frames and InvisibleBlock_Update (run from rsdk_object_tick) never
     * raises collisionFlagV. The probe verifies the real RSDK-engine tick ->
     * Player_CheckCollisionBox -> collisionFlagV path; it does not synthesize
     * the flag. Compiled out of the release binary. */
    /* Pin 30 px ABOVE the block center (1396,920), not at the center. At
     * dead-center Player_CheckCollisionBox resolves the deepest-overlap axis
     * as horizontal (C_LEFT -> collisionFlagH); resting just above the top
     * edge makes the vertical penetration shallow so side-selection picks
     * C_TOP -> collisionFlagV |= 1 (the field the P4 gate asserts). The block
     * hitbox is y[904,936]; player feet ~y+19 reach 909, 5 px into the top. */
    g_ghz_player.xpos = ((int32_t)1396) << 16;
    g_ghz_player.ypos = ((int32_t)890)  << 16;
    g_ghz_player.xsp  = 0;
    g_ghz_player.ysp  = 0;
    g_ghz_player.gsp  = 0;
#endif

    /* === Camera follow ===
     *
     * Mania-faithful CAM_FOOT_OFFSET=32 per decomp Player_Action_Jump
     * (offset.y=0x200000=32 in integer space). */
    int px = g_ghz_player.xpos >> 16;
    int py = g_ghz_player.ypos >> 16;
    int cam_x = px - MANIA_SONIC_SCR_X;
    int cam_y = py - 112 - MANIA_CAM_FOOT_OFFSET;
    ghz_set_camera(cam_x, cam_y);
    int actual_cx = ghz_camera_x();
    int actual_cy = ghz_camera_y();

    /* Phase 2.4g.1 — sync the RSDK entity-engine camera so spawned GHZ
     * entities (InvisibleBlock and every later port) pass the ACTIVE_BOUNDS
     * gate in rsdk_entity_in_bounds. Pass the camera CENTER (top-left +
     * half-screen) in Q16.16 and the half-screen extents as the bounds
     * offset. Without this s_cam_x_fixed stays 0 (object.c:56) and every
     * entity beyond update_range of world origin is culled before Update().
     * rsdk_object_tick runs at mania_tick scope (Game.c) one frame before
     * this body, so the value set here gates the NEXT frame's ticks. */
    rsdk_object_set_camera(((int32_t)(actual_cx + 160)) << 16,
                           ((int32_t)(actual_cy + 112)) << 16,
                           ((int32_t)160) << 16,
                           ((int32_t)112) << 16);

    /* === Sky parallax — 1/4 of camera. */
    ghz_sky_scroll(actual_cx, actual_cy);

    /* === Phase 2.3e Path 4 — entity ticks have been split into a
     * tick-only pass (here, mutates state) and a draw-only pass (in
     * mania_ghz_draw_only, runs at mania_tick body scope). The HUD
     * timer tick stays here because it's logic, not draw.
     *
     * Cache actual_cx/cy so mania_ghz_draw_only can read the same
     * camera position as this frame's tick saw. */
    g_ghz_cached_cam_x = actual_cx;
    g_ghz_cached_cam_y = actual_cy;

    /* HUD timer tick — gameplay logic. The HUD DRAW is in
     * mania_ghz_draw_only (Path 4). */
    if (!g_titlecard_active && !entities_act_cleared()) hud_tick();
}

/* Phase 2.3e Path 4 — sprite-draw-only pass.
 *
 * Called from mania_tick at body scope (NOT inside the
 * if (ghz_is_active()) conditional) so the sprite draws issued here
 * are guaranteed to land in the same SGL command context as the
 * mania_diag_probe_draw probes (which empirically render fine).
 *
 * Reads g_ghz_cached_cam_x/y populated by the most recent
 * mania_ghz_tick_and_draw call. Until that has run at least once
 * (cam < 0 sentinel) OR until the player is ready, this function
 * is a clean no-op.
 *
 * Per-class entity tick+draw functions in Entities.c still bundle
 * tick+draw together; that's a Phase 2.4 refactor target if any
 * tick logic turns out to need separation. For Phase 2.3e it's
 * sufficient that the same draws now issue at the correct call
 * site.
 *
 * §12.3e binding hypothesis: this restores in-GHZ sprite visibility
 * for Sonic, rings, motobug, springs, monitors, signposts, and HUD.
 * If the symptom persists, see plan §12.3e for fallback hypotheses
 * H-D / H-E / H-F.
 *
 * Phase 2.3f (Task #88) — __attribute__((used)) per the LTO audit. */
__attribute__((used))
void mania_ghz_draw_only(void)
{
    if (!g_ghz_player_ready) return;
    if (g_ghz_cached_cam_x < 0 || g_ghz_cached_cam_y < 0) return;

    int cx = g_ghz_cached_cam_x;
    int cy = g_ghz_cached_cam_y;

    /* === Phase 2.3 — entity ticks (Rings, Motobug, Spring, ItemBox,
     *     SignPost). Order matters because per-class tick+draw is
     *     still bundled; entities first so they can mutate Sonic
     *     state before mania_ghz_draw_sonic reads it for the draw.
     *
     * Per-class lives in src/mania/Objects/Common/Entities.c — each
     * loop already has visibility-cull (AABB vs screen rect + sprite
     * size margin) so the ~550 entity-class population reduces to
     * ~30-60 sprite draws per frame.
     *
     * HUD numerics from decomp HUD.c via the archived draw_hud
     * entry point (main.c.v01-handrolled:1183-1209).
     *
     * Phase 2.4j.1 — the entire gameplay paint (entities + Sonic + HUD)
     * is suppressed while the TitleCard act-intro covers the screen. The
     * decomp runs the card under ENGINESTATE_PAUSED so ONLY the card
     * draws; the Saturn analogue is g_titlecard_active. A full-screen
     * card BG at the 1:1 polygon plane (TitleCard.c TCZ_POLY_PLANE=150,
     * DS~=156) cannot occlude nearer gameplay sprites (z~=100-150) by Z
     * ordering alone, so the gameplay layer must be gated off for the
     * duration of the freeze, exactly as the engine PAUSE does. The
     * walkers' tick side-effects (state machines) are driven from
     * mania_ghz_tick, not from this draw_only path, so suppressing the
     * draw does not stall entity logic. g_titlecard_active clears at the
     * SlideAway start (TitleCard.c:511), restoring the gameplay layer
     * for the slide-away reveal. */
    if (!g_titlecard_active) {
        rings_tick_and_draw   (&g_ghz_world, &g_ghz_player, cx, cy);
        motobug_tick_and_draw (&g_ghz_world, &g_ghz_player, cx, cy);
        spring_tick_and_draw  (&g_ghz_world, &g_ghz_player, cx, cy);
        itembox_tick_and_draw (&g_ghz_world, &g_ghz_player, cx, cy);
        /* Phase 2.4c Task #140 — priority entity ports inserted between
         * itembox and signpost so the same drawGroup ordering matches
         * decomp Zone->objectDrawGroup[0] (per BuzzBomber_Create L45 +
         * Spikes_Create L337-339). Soft-fail when assets missing. */
        spikes_tick_and_draw     (&g_ghz_world, &g_ghz_player, cx, cy);
        buzzbomber_tick_and_draw (&g_ghz_world, &g_ghz_player, cx, cy);
        /* Phase 2.4c.2 Task #147 — Platform first (drawGroup[0]+1 per decomp
         * Platform_Create L158), then SpikeLog (drawGroup[0] per decomp
         * SpikeLog_Create L38), then Newtron (drawGroup[1] per decomp
         * Newtron_Create L52/L68). */
        platform_tick_and_draw   (&g_ghz_world, &g_ghz_player, cx, cy);
        spikelog_tick_and_draw   (&g_ghz_world, &g_ghz_player, cx, cy);
        newtron_tick_and_draw    (&g_ghz_world, &g_ghz_player, cx, cy);
        /* Phase 2.4h Task #154 — GHZ Act 1 badniks (Chopper/Crabmeat/Batbrain).
         * RSDK-object architecture: state machine + collision already ticked by
         * rsdk_object_tick (mania_tick scope); these draw_only walkers iterate
         * the RSDK slots and emit VDP1 sprites via the shared entity_atlas.
         * drawGroup ordering (decomp Create): Crabmeat drawGroup[1]+Chopper
         * drawGroup[1]+Batbrain drawGroup[0]; drawn after Newtron, before
         * SignPost, matching Zone->objectDrawGroup paint order. */
        Chopper_draw_only (cx, cy);
        Crabmeat_draw_only(cx, cy);
        Batbrain_draw_only(cx, cy);
        /* Phase 2.4-PLAT Task #155 — Bridge is the sole VISIBLE platforming
         * class. State machine + plank collision are ticked by
         * rsdk_object_tick; this walker iterates the Bridge RSDK slots and
         * emits the GHZ/Bridge.bin plank sprites via cd/BRIDGE.SP2
         * (drawGroup[0], per decomp Bridge_Create). The four invisible
         * classes (CollapsingPlatform/ForceSpin/BreakableWall/SpinBooster)
         * have no draw_only walker — they are FG-tilemap/trigger entities. */
        Bridge_draw_only(cx, cy);
        signpost_tick_and_draw(&g_ghz_world, &g_ghz_player, cx, cy);

        /* === Sonic sprite draw (frozen at spawn during the card freeze). */
        mania_ghz_draw_sonic(cx, cy);
    }

    /* Phase 1.39 REMOVED 2026-05-28 (Task #122) — Phase 2.3e validation
     * probe (sentinel-green 16x16 sprite at screen (80, 0) Z=150) per
     * user 'remove test visualizations' directive. The probe was a
     * Phase 2.3d Lead A diagnostic; Path 4 was empirically verified
     * (per docs/COMPREHENSIVE_PLAN.md §12.3e Findings) so the probe is
     * no longer needed. The probe registration in mania_ghz_player_
     * load_assets is kept (it advances jo's VRAM cursor mid-Sonic-load,
     * removing it would shift downstream sprite ids); only the draw is
     * suppressed.
     *
     * Original draw (commented for restoration):
     *     if (s_phase23d_probe_sprite_id >= 0) {
     *         jo_sprite_draw3D(s_phase23d_probe_sprite_id, 80, 0, 150);
     *     }
     */

    /* === HUD overlay (drawn last so it sits on top). Suppressed during
     * the TitleCard freeze with the rest of the gameplay layer (the
     * decomp HUD does not draw under ENGINESTATE_PAUSED while the card
     * holds; matches g_titlecard_active gating above). */
    if (!g_titlecard_active)
        hud_draw();

    /* === TitleCard act-intro overlay (Phase 2.4j.1, Task #156). Drawn
     * after the HUD so the card covers the whole screen during the intro
     * freeze. Inert once g_titlecard_active clears (state Supressed). */
    titlecard_draw_only();
}


/* Phase 2.3j: legacy deferred-kick flags + functions retired.
 *
 * The Phase 2.3c/d deferred-kick architecture (s_audio_kicked,
 * s_player_load_deferred, s_player_load_kicked, mania_ghz_deferred_
 * audio_kick, mania_ghz_deferred_player_kick) existed to dodge a
 * downstream effect of the readiness-flag race that has been
 * permanently retired by the sync-load refactor. The audio CD-track
 * switch + player asset load + Player_Init now run synchronously
 * inside mania_load_ghz_scene(), at the same call site that did the
 * NBG1/NBG2 cell-mode bring-up. Whatever DMA conflict the deferral
 * was working around either (a) never actually existed and was
 * misdiagnosed because the gate-flag race was hiding the symptom or
 * (b) re-emerges as a measurable predicate in the Phase 2.3j gate, in
 * which case the fix is reinstated as a non-flag-based mechanism.
 *
 * Phase 2.3j gate verdict: the production capture is the proof. */

/* Phase 2.3j (2026-05-28) — synchronous title->GHZ scene-load.
 *
 * Replaces the Phase 2.3c/d deferred-kick architecture (audio @ +30,
 * player @ +45 post-transition). The deferral was a bandaid for the
 * cross-TU LTO-vs-volatile bug class permanently retired by the
 * sync-load refactor.
 *
 * Decomp chain mirrored:
 *   rsdkv5-src/RSDKv5/RSDK/Core/RetroEngine.cpp:345-384 ENGINESTATE_LOAD:
 *     LoadSceneFolder -> LoadSceneAssets -> InitObjects ->
 *     SKU::userCore->StageLoad -> ProcessInput -> ProcessObjects.
 *     All synchronous. PC budget: one dropped frame, hidden by
 *     title-card overlay. Saturn budget: one dropped frame, visible
 *     (acceptable per binding directive).
 *
 * Saturn sequence:
 *   1. Hide title NBG2 (drop priority, free DAT/PAL work-RAM mirrors).
 *   2. GHZSetup_StageLoad — engine-layer NBG1 FG + NBG2 sky bring-up
 *      (per tools/_decomp_raw/SonicMania_Objects_GHZ_GHZSetup.c:50-114).
 *   3. Player asset load + world preload + Player_Init.
 *   4. Audio CD-track switch (BGM 3 -> BGM 2).
 *
 * All synchronous. Caller (title_state_tick TS_TRANSITION_TO_GHZ case)
 * blocks until this returns, then advances to TS_GHZ_ACTIVE. */
bool mania_load_ghz_scene(void)
{
    /* Step 1: hide title NBG2 + free title work-RAM mirrors.
     *
     * Drop NBG2 priority to 0 while we rewrite the cell-mode bitmap so
     * any latent title-state draws don't bleed into the first GHZ frame.
     * ghz_setup_sky drops NBG2ON itself during the swap. */
    slPriorityNbg2(0);

    /* Phase 2.1 BSS reclaim: free the title backdrop's Work-RAM source
     * buffers (TITLE.DAT 112 KB + TITLE.PAL 0.5 KB) BEFORE the GHZ load
     * fires. Without this the jo malloc pool stays saturated by title
     * residents and FG.TMP (262 KB) load fails -> NBG1 cell-mode
     * displays garbage. VDP2 VRAM contents stay intact. */
    mania_free_title_bg_buffers();
    /* Phase 1.34b — release CLOUDS.DAT (64 KB) + CLOUDS.PAL (0.5 KB). */
    mania_free_clouds_bg_buffers();

    /* Step 2: engine-layer bring-up via the decomp's per-class StageLoad.
     * This is the synchronous equivalent of the decomp's InitObjects
     * pass that invokes every registered class's stageLoad callback
     * (RetroEngine.cpp:361 InitObjects).
     *
     * Phase 2.3k-mid: GHZSetup_StageLoad has a void return per the RSDK
     * class-registry stage_load contract — failure is communicated via
     * g_ghz_load_error_code (scene_ghz.h, bits 0..5 = FG.TMP/PAL/CEL/PAT
     * + SKY.PAL/DAT). Reset the bitmask before the call so we observe
     * THIS load attempt's failures (not residue from a prior tick).
     * If any bit lands set, bail and the title state machine stays at
     * TS_TRANSITION_TO_GHZ. Peek the bitmask via savestate to diagnose
     * the specific failed asset. */
    g_ghz_load_error_code = 0;
    GHZSetup_StageLoad();
    if (g_ghz_load_error_code != 0) {
        return false;
    }

    /* Step 3: Player asset load + world preload + Player_Init.
     *
     * Previously deferred to +45 ticks via s_player_load_deferred. The
     * deferral existed to dodge the Phase 2.3d "post-transition
     * jo_sprite_add silently produces zero pixels" symptom — which was
     * itself a downstream effect of the readiness-flag race that
     * prevented mania_ghz_tick_and_draw from ever running. With the
     * sync-load architecture there is no flag race: tick-and-draw is
     * gated by the state machine reaching TS_GHZ_ACTIVE, which only
     * happens AFTER this function returns. The post-transition sprite
     * add chain therefore lands in a fully-committed VDP2/VDP1 state.
     *
     * If the Phase 2.3d symptom recurs (a measurable Sonic-color pixel
     * count == 0 in the central ROI), reinstate a deferred kick. The
     * Phase 2.3j RED-firing gate (tools/qa_phase2_3j_sync_load_gate.py)
     * catches the symptom via the tick counter + the parallel pixel-
     * mass capture step.  */
    mania_ghz_player_load_assets();

    /* Phase QA-VDP1 (2026-05-28) — GHZ gameplay-entity sprite atlases +
     * position tables + SFX. Relocated here from mania_engine_init so the
     * ~232 KB of entity VDP1 VRAM is NOT allocated while the title is
     * showing (where it overflowed VRAM and suppressed every title
     * sprite — see the rationale at the old call site in
     * mania_engine_init). This is the synchronous GHZ-scene-load analogue
     * of the decomp's per-class StageLoad pass. Idempotent per
     * Entities.c:972, so safe even if mania_load_ghz_scene retries. */
    entities_load_assets();

    /* Phase 2.4j.1 (Task #156) — TitleCard act-intro. Load the glyph atlas
     * and spawn the GHZ Act 1 card ("GREEN HILL", act 1). titlecard_spawn
     * sets g_titlecard_active=1, which freezes Player_Tick / jump / HUD and
     * suppresses Sonic's draw until the card slides away (decomp parity with
     * the act-intro freeze). */
    titlecard_load_assets();
    titlecard_spawn("GREEN HILL", TC_ACT_1);

    /* Phase 2.4g.1 (Task #153) — spawn the GHZ scene's RSDK entities from
     * GHZSCN1.BIN (the verbatim decomp Scene1.bin object table) into the
     * engine slot table. memory/ghz-pivot-to-rsdk-engine.md: GHZ entities
     * now live on the RSDK entity engine (src/rsdk/object.c) instead of the
     * bespoke <class>_tick_and_draw modules. For 2.4g.1 only InvisibleBlock
     * is a registered GHZ class, so this populates the 18 InvisibleBlock
     * slots; un-registered classes resolve to nothing and are skipped.
     *
     * rsdk_load_scene_no_stage_load (NOT rsdk_load_scene) so the all-class
     * stage_load pass does NOT fire — GHZSetup_StageLoad above is the
     * explicit, ordered GHZ StageLoad and the all-class loop would
     * additionally re-fire the freed Title* StageLoads. */
    rsdk_set_scene(NULL, "GHZ");
    rsdk_load_scene_no_stage_load();

    mania_ghz_player_preload_world();
    mania_ghz_player_init_on_transition();

    /* Step 4: audio. Decomp's GHZSetup_StageLoad calls Music_PlayTrack
     * (RSDK.PlayStream("GHZ_Act1.ogg")). On Saturn this is CD-DA track 2. */
#ifdef JO_COMPILE_WITH_AUDIO_SUPPORT
    jo_audio_stop_cd();
#ifndef QA_SFX_PROBE
    /* Suppressed under QA_SFX_PROBE so the SFX capture is BGM-free. */
    jo_audio_play_cd_track(2, 2, true);
#endif
#endif
    return true;
}

/* Phase 2.3j — `mania_is_ghz_active()` replaces the removed
 * `ghz_is_active()` helper. The state-machine variable IS the
 * readiness signal under the sync-load architecture. */
bool mania_is_ghz_active(void)
{
    return s_ts_state == TS_GHZ_ACTIVE;
}

/* Phase 2.4h — expose the static GHZ surface world to RSDK-engine badnik
 * _Update callbacks (Chopper/Crabmeat/Batbrain) that probe the surface
 * table from rsdk_object_tick scope (no world parameter). Returns NULL
 * until the surface is loaded. */
const sms_world_t *mania_ghz_world(void)
{
    return g_ghz_world.raw ? &g_ghz_world : NULL;
}

/* Phase 2.3j: legacy compatibility shim. The pre-2.3j code retained a
 * `title_to_ghz_transition` -> `s_player_load_deferred = true` path
 * that fired the player kick at +45 ticks. This is now a no-op; the
 * synchronous mania_load_ghz_scene above replaces it entirely. Kept
 * as a stub so any latent caller compiles cleanly. */
static bool title_to_ghz_transition(void)
{
    return mania_load_ghz_scene();
}

/* Phase 2.3j: mania_ghz_deferred_player_kick REMOVED. The three calls
 * (mania_ghz_player_load_assets + preload_world + init_on_transition)
 * now run synchronously inside mania_load_ghz_scene at the same point
 * the engine-layer NBG1 FG + NBG2 sky bring-up completes. See the
 * Phase 2.3j comment block at the deferred-flags retirement site
 * above for the full retirement rationale. */

static void title_state_tick(void)
{
    ++s_ts_total_frames;
    switch (s_ts_state) {
        case TS_FADE_IN:
            if (++s_ts_timer >= 64) { s_ts_state = TS_FLASH; s_ts_timer = 0; }
            break;
        case TS_FLASH:
            if (++s_ts_timer >= 2) { s_ts_state = TS_WAIT_FOR_SONIC; s_ts_timer = 0; }
            break;
        case TS_WAIT_FOR_SONIC:
            if (++s_ts_timer >= 48) { s_ts_state = TS_PRESS_REVEAL; s_ts_timer = 0; }
            break;
        case TS_PRESS_REVEAL:
            if (++s_ts_timer >= 120) { s_ts_state = TS_WAIT_FOR_ENTER; s_ts_timer = 0; }
            break;
        case TS_WAIT_FOR_ENTER:
            ++s_ts_timer; /* keep counting for PRESS START blink */
            /* Phase 3.2 + 3.2b REVERTED 2026-05-28 — direct-to-GHZ on
             * START press restored. TS_MENU_ACTIVE branch removed. */
            {
                const rsdk_controller_state_t *c = rsdk_controller_state(CONT_ANY);
                bool start_press = (c != NULL && c->key_start.press);
                /* GHZ_AUTOADVANCE_TICKS == 0 disables auto-advance. Guard the
                 * `s_ts_timer >= TICKS` compare behind the preprocessor so a
                 * zero threshold doesn't compile an always-true unsigned `>= 0`
                 * (-Wtype-limits). Phase 2.4g.3: surfaced once the build.bat
                 * arg-forwarding fix let verify_done actually recompile Game.c. */
#if GHZ_AUTOADVANCE_TICKS > 0
                bool autoadvance = (s_ts_timer >= (unsigned)GHZ_AUTOADVANCE_TICKS);
#else
                bool autoadvance = false;
#endif
                if (start_press || autoadvance) {
                    s_ts_state = TS_TRANSITION_TO_GHZ;
                    s_ts_timer = 0;
                }
            }
            break;
        case TS_TRANSITION_TO_GHZ:
            /* Single-tick state: invoke the transition, then drop into
             * GHZ_ACTIVE on the next tick. Splitting it across two
             * ticks avoids re-entering the heavy asset loaders if
             * something later in this tick faults.
             *
             * Phase 2.3k-mid: only advance to TS_GHZ_ACTIVE on success.
             * If GHZSetup_StageLoad failed (FG.* / SKY.* sub-load), we
             * stay in TS_TRANSITION_TO_GHZ. The state machine will
             * retry once per tick — but g_ghz_load_error_code stays
             * non-zero, so the savestate harness can diagnose the
             * specific failed asset. Once Phase 2.3k root-causes and
             * fixes the load failure (likely FG.TMP GFS path), this
             * retry collapses to a single-tick success. */
            if (title_to_ghz_transition()) {
                s_ts_state = TS_GHZ_ACTIVE;
                s_ts_timer = 0;
            }
            break;
        case TS_GHZ_ACTIVE:
            ++s_ts_timer;
            /* Phase 2.3j: deferred-audio kick (+30) and deferred-player-
             * load kick (+45) REMOVED. All four operations (NBG bring-up
             * + player asset load + world preload + Player_Init + CD-DA
             * track switch) now run synchronously inside
             * mania_load_ghz_scene during TS_TRANSITION_TO_GHZ. */
            break;
        default: break;
    }
}

/* Draw a single asset frame via the asset bridge. Path mirrors what the
 * entity-driven path would do — same world→jo conversion. */
static void draw_title_asset(int asset_id, int world_x, int world_y,
                             int z, int flip_x)
{
    if (asset_id < 0 || asset_id >= TITLE_ASSET_COUNT) return;
    const title_asset_t *a = &g_title_assets[asset_id];
    if (a->base_sprite_id < 0 || a->frame_count == 0) return;
    int jx = world_x - 256;
    int jy = world_y - 112;
    if (flip_x) {
        jo_sprite_enable_horizontal_flip();
        jo_sprite_draw3D(a->base_sprite_id, jx, jy, z);
        jo_sprite_disable_horizontal_flip();
    } else {
        jo_sprite_draw3D(a->base_sprite_id, jx, jy, z);
    }
}

/* Phase 1.37 (Task #118) — central island silhouette via VDP1 sprite.
 *
 * Draws Mountain1 (TITLE3D.ATL anim 0 frame 0) as a large non-uniformly
 * scaled VDP1 sprite anchored at screen center-bottom.  Bypasses VDP2
 * NBG1 entirely after 5 failed NBG1 iterations (Phase 1.35-1.36b) and a
 * fresh-capture register-diff diagnostic (Phase 1.36b finding: all 18
 * VDP2 registers byte-identical across captures, falsifying the
 * cycle-pattern + CCCTL hypotheses).
 *
 * Citations:
 *   tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c:67
 *     -- TITLEBG_MOUNTAIN1 (default case, opaque INK_NONE).
 *   ST-238-R1-051795.pdf §slDispSpriteHV (p.65) -- H/V scaled sprite
 *     with 5-element pos [X, Y, Z, Sh, Sv] per SL_DEF.H:93 enum.
 *   jo-engine/jo_engine/sprites.c:445 -- jo's own slDispSpriteHV path
 *     with the angle=1 SGL-bug workaround (engine doesn't draw
 *     properly at angle 0).
 *   Phase 1.36b primary-agent fresh-capture diagnostic (2026-05-28)
 *     -- savestate harness cannot localise the NBG1 artifact at
 *     sufficient granularity; user pivoted to VDP1 sprite path.
 *
 * Asset data (verified via TITLE3D.ATL dump 2026-05-28):
 *   anim 0 frame 0 (Mountain1): 176x16 px, pivot (-88, -16).
 *   The source aspect (11:1) means uniform scaling cannot fill a
 *   roughly-square viewport region; non-uniform H/V scaling required.
 *
 * Saturn-deviation acknowledgement: the decomp does NOT place a
 * Mountain1 entity at screen center -- Scene1.bin slot 0 places the
 * single Mountain1 at world (104, 144) and Phase 1.34 already renders
 * that entity at the left edge with leftward drift motion.  Phase 1.37
 * adds a SECOND, scaled, STATIC Mountain1 instance at screen center
 * solely to make a visible central island silhouette without re-
 * engaging the NBG1 path.  User-accepted pragmatic Saturn-fit per the
 * task brief.
 *
 * Scale rationale (Mountain1 source 176x16):
 *   target visible silhouette ~= 224 wide x 64 tall pixels around
 *   screen X=160, Y=192 (bottom-anchored on the horizon).
 *   scale_x = 224/176 = 1.273  -> toFIXED(1.273) ~= 0x14655
 *   scale_y = 64/16  = 4.0     -> toFIXED(4.0)    = 0x40000
 *   With pivot (-88, -16) the source canvas centre offset is
 *   (-88 + 88, -16 + 8) = (0, -8). After scaling Sh=1.273, Sv=4.0
 *   the canvas centre offset becomes (0, -32) in framebuffer pixels.
 *   To land the silhouette's centre at Saturn screen (160, 192) =
 *   jo (0, 80) we issue (jo_x, jo_y) = (0, 80 - (-32)) = (0, 112).
 *   Wait: jo_y is the world Y centre of the sprite in jo-coords.
 *   Sprite centre at framebuffer Y = 192 -> jo Y = 192 - 112 = 80.
 *   The HV-scaled draw places the sprite centred on (pos[X], pos[Y])
 *   per jo_engine sprites.c:438-439 + slDispSpriteHV convention.
 *   So jo_x = 0, jo_y = 80 places the scaled sprite centred at
 *   framebuffer (160, 192).
 *
 * Z=205: between TitleBG MOUNTAIN1/MOUNTAIN2 (z=210, back row) and the
 * Title3DSprite billboards (z range 150-200 per Title3DSprite_Draw_All
 * scale->depth mapping).  Saturn perspective sort: smaller Z = closer
 * to viewer; z=205 puts the central silhouette IN FRONT of the
 * drifting Mountain1/2 strips but BEHIND the billboard formation.
 *
 * Opaque draw (half_transparency=0) matching the decomp INK_NONE
 * default for TITLEBG_MOUNTAIN1 (Create:67-83 default branch).
 */
__attribute__((unused)) static void central_island_draw(void)
{
    /* Mountain1 = anim 0 in TITLE3D.ATL TitleBG slot. */
    const int anim_id     = 0;
    const int jo_x        = 0;        /* horizontal centre */
    const int jo_y        = 80;       /* bottom-anchored at fb Y=192 */
    const int z           = 205;
    const int hv_scale_x  = (int)(1.273f * 65536.0f);  /* ~0x14655 */
    const int hv_scale_y  = (int)(4.0f   * 65536.0f);  /* 0x40000   */
    title3d_bg_draw_frame_hv(anim_id, jo_x, jo_y, z,
                             hv_scale_x, hv_scale_y,
                             /*direction=*/0, /*half_transparency=*/0);
}

static void title_direct_draw(void)
{
    /* Pre-flash (TS_FADE_IN): electricity ring build animation only
     *   (decomp `TitleSetup.c:393-402` Draw_DrawRing two-pass at world
     *    (256, 108) during State_AnimateUntilFlash).
     * Post-flash: EMBLEM/RIBBON/GAMETITLE/RINGBOTTOM + TitleSonic.
     * Post-PressReveal: + PRESSSTART blink.
     * Phase 2.1: post-transition (TS_GHZ_ACTIVE) suppresses all title
     * sprite draws — GHZ is now visible via NBG1/NBG2 and the title
     * sprites would composite on top spuriously. */
    if (s_ts_state == TS_TRANSITION_TO_GHZ) return;
    if (s_ts_state == TS_GHZ_ACTIVE) return;
    /* Phase 3.2 + 3.2b REVERTED 2026-05-28 — TS_MENU_ACTIVE
     * early-return removed. */

    /* Phase 1.27 §11.32 Item 3 — Electricity ring pre-flash animation.
     *
     * Decomp `tools/_decomp_raw/SonicMania_Objects_Title_TitleSetup.c:393-402`
     * (`Draw_DrawRing`):
     *   self->direction = FLIP_NONE;
     *   DrawSprite(&self->animator, &self->drawPos, false);
     *   self->direction = FLIP_X;
     *   DrawSprite(&self->animator, &self->drawPos, false);
     *
     * drawPos = (256<<16, 108<<16) per Create at TitleSetup.c:46-47.
     *
     * Saturn-side: gated to TS_FADE_IN window because the flash
     * transition (TS_FLASH) extinguishes the arc per decomp's
     * Draw_Flash replacing Draw_DrawRing at State_FlashIn.
     *
     * Phase 1.28 §11.34 Item B' — animation timing.  Decomp authoritative
     * speed: anim 0 has 40 frames * 2 ticks/frame, state machine exits
     * at frameID==31 = 64 ticks total arc.  Saturn TS_FADE_IN window is
     * 64 ticks.  Our 8 culled keyframes span the same decomp anim; map
     * 8 keyframes * 8 ticks each = 64 ticks (exact decomp match).  Start
     * the arc at tick 0 (was tick 12) because the Saturn state machine
     * compresses decomp State_Wait + State_AnimateUntilFlash into a
     * single 64-tick TS_FADE_IN. */
    if (s_ts_state == TS_FADE_IN) {
        if (g_electra_loaded && g_electra_frame_count > 0) {
            unsigned int arc_tick = s_ts_timer;
            int kf = (int)(arc_tick / 8);
            if (kf >= g_electra_frame_count) kf = g_electra_frame_count - 1;

            /* World drawPos = (256, 108). For each keyframe the source
             * RSDK frame has pivot (px, py); the canvas spans
             * [px, px+w] x [py, py+h] relative to entity origin. Canvas
             * centre = (px + w/2, py + h/2). jo_x = (256 + cx) - 256,
             * jo_y = (108 + cy) - 112 = cy - 4.
             *
             * Per Draw_DrawRing FLIP_NONE first then FLIP_X. */
            int fw = g_electra_frames[kf].width;
            int fh = g_electra_frames[kf].height;
            int px = g_electra_frames[kf].pivot_x;
            int py = g_electra_frames[kf].pivot_y;
            int canvas_cx = px + (fw >> 1);
            int canvas_cy = py + (fh >> 1);
            int jo_x = canvas_cx;            /* world 256 -> jo 0 */
            int jo_y = canvas_cy - 4;        /* world 108 -> jo -4 */

            /* Pass 1 FLIP_NONE. */
            title_electra_draw_frame(kf, jo_x, jo_y, 175, 0);
            /* Pass 2 FLIP_X — mirror canvas-centre across entity origin. */
            title_electra_draw_frame(kf, -jo_x, jo_y, 175, 1);
        }
        return;
    }

    /* Phase 1.18 — Two-pass FLIP_NONE+FLIP_X composition.
     *
     * The source RSDK frames are SIDE-ONLY: anim 0 EMBLEM is 144x144 with
     * pivot (-144,-72) — i.e. the pivot is at the RIGHT edge of the canvas,
     * so FLIP_NONE places the left wing at world [X-144, X). FLIP_X mirrors
     * the canvas and places the right wing at world [X, X+144).
     * Anim 1 RIBBON is 56x72 with pivot (-121,-13) — same pattern.
     *
     * Decomp TitleLogo.c:40-48 (EMBLEM) and TitleLogo.c:50-59 (RIBBON).
     *
     * tools/convert_anim_sprite.py composes a single canvas containing the
     * RSDK source frame at its pivot offset. For EMBLEM (pivot -144,-72,
     * size 144x144) the canvas spans X=[-144..0], Y=[-72..72] relative to
     * the entity origin. The canvas CENTRE (where jo_sprite_draw3D anchors)
     * is at X = -72 = halfway between -144 and 0.
     *
     * So FLIP_NONE: canvas centre at world X-72; jo_x = (X-72) - 256.
     *    FLIP_X:    canvas mirrors; centre at world X+72; jo_x = (X+72) - 256.
     *
     * For X=256:  jo_x_left = -72,  jo_x_right = +72.
     * For X=256 RIBBON: jo half-width = 28; jo_x_left=-93, jo_x_right=-35
     *   (asymmetric because pivot -121 != -56/2; the ribbon side wave
     *   anchors at -121 from origin, canvas spans X=[-121..-65], centre
     *   at -93; flipped centre at +65/2 from origin = ... let's verify).
     *
     * Actually: for any source frame with pivot (px, py) and size (w, h),
     * the canvas built by convert_anim_sprite.py spans [px, px+w] x [py, py+h]
     * (relative to entity origin). Canvas centre = (px + w/2, py + h/2).
     *
     *   EMBLEM:  (-144 + 72, -72 + 72) = (-72, 0)              -> jo (-72, -4)
     *   FLIP_X EMBLEM: centre mirrors around origin: (+72, 0)  -> jo (+72, -4)
     *
     *   RIBSIDE: (-121 + 28, -13 + 36) = (-93, +23)            -> jo (-93,...)
     *   FLIP_X RIBSIDE: (+93, +23)                             -> jo (+93,...)
     *
     * Pass order matches the decomp exactly. */

    /* Phase 1.28 §11.34 Item B — Z-order per decomp scene-bin slot order
     * (Scene1.bin, parsed by tools/parse_title_entities.py):
     *   slot  5: EMBLEM     z=200 (back)
     *   slot  6: RINGBOTTOM z=195
     *   slot  8: TitleSonic z=190 (body) / 188 (finger)  <- behind banner
     *   slot  9: RIBBON     z=185 (sides) / 180 (ribbonCenter SONIC banner)
     *   slot 15: GAMETITLE  z=175
     *   slot 74: PRESSSTART z=165 (front)
     *
     * Smaller Z = closer to viewer per Saturn perspective projection
     * (SGL ST-238-R1 §slDispSprite); the decomp slot order = back-to-front
     * so smaller-slot entities get LARGER Z.
     */

    /* EMBLEM — Decomp TitleLogo.c:40-48 (FLIP_NONE then FLIP_X).
     * World (256, 108) -> canvas Y centre at world_y + 0 = 108 -> jo_y = -4.
     * Decomp ScreenInfo center.y = 112, position.y = 0, so screen_y = world_y. */
    {
        /* World (X, Y) = (256, 108). Canvas centre half-width = 72. */
        int wy = 108;
        int wx = 256;
        /* Pass 1 FLIP_NONE: canvas centre at world (wx-72, wy). */
        jo_sprite_draw3D(g_title_assets[TITLE_ASSET_TLOGO_EMBLEM].base_sprite_id,
                         (wx - 72) - 256, wy - 112, 200);
        /* Pass 2 FLIP_X: canvas centre mirrors to world (wx+72, wy). */
        jo_sprite_enable_horizontal_flip();
        jo_sprite_draw3D(g_title_assets[TITLE_ASSET_TLOGO_EMBLEM].base_sprite_id,
                         (wx + 72) - 256, wy - 112, 200);
        jo_sprite_disable_horizontal_flip();
    }

    /* RINGBOT — ring underlay from MRING.SPR (120x32) at (256, 148).
     * Pivot (-60,-16) -> canvas span [-60,60] x [-16,16], centre (0,0).
     * Single draw, no flip (anim 7 default branch of Draw_default). */
    draw_title_asset(TITLE_ASSET_TLOGO_RINGBOT, 256, 148, 195, 0);

    /* RIBBON side wave (MRIBSIDE.SPR 56x72, pivot -121,-13).
     * Decomp TitleLogo.c:50-55 (FLIP_X first, then FLIP_NONE).
     * Canvas span [-121,-65] x [-13,59], centre (-93, +23).
     * World (256, 144): centre_world_x = 256 + (-93) = 163, jo_x = -93.
     * FLIP_X: mirror canvas centre across entity origin -> jo_x = +93. */
    {
        int wy = 144;
        int half = 93;       /* canvas centre offset from origin */
        int yoff = 23;       /* canvas centre Y offset from origin */
        int sid = g_title_assets[TITLE_ASSET_TLOGO_RIBSIDE].base_sprite_id;
        if (sid >= 0) {
            /* Pass 1 FLIP_X (decomp draws flipped first). */
            jo_sprite_enable_horizontal_flip();
            jo_sprite_draw3D(sid, +half, (wy - 112) + yoff, 185);
            jo_sprite_disable_horizontal_flip();
            /* Pass 2 FLIP_NONE. */
            jo_sprite_draw3D(sid, -half, (wy - 112) + yoff, 185);
        }
    }

    /* RIBCENTER (SONIC wordmark on red banner; MRIBBON.SPR 176x52,
     * pivot -79,-24). Single draw, no flip. Canvas centre offset
     * = (-79+88, -24+26) = (9, 2). World (256, 144).
     * Phase 1.28 §11.34 Item B: z=180 (was 185) — banner draws IN FRONT
     * of TitleSonic body (z=190) so it covers Sonic's belly per decomp. */
    {
        int sid = g_title_assets[TITLE_ASSET_TLOGO_RIBCENTER].base_sprite_id;
        if (sid >= 0) {
            jo_sprite_draw3D(sid, 9, (144 - 112) + 2, 180);
        }
    }

    /* GAMETITLE (MANIA wordmark; MLOGO.SPR 144x48 cropped to first frame).
     * Pivot (-68,-23). Canvas span [-68,76] x [-23,25], centre (4, 1).
     * World (256, 182). Phase 1.28 §11.34: z=175 (was 180). */
    {
        int sid = g_title_assets[TITLE_ASSET_TLOGO_GAMETITLE].base_sprite_id;
        if (sid >= 0) {
            jo_sprite_draw3D(sid, 4, (182 - 112) + 1, 175);
        }
    }

    /* TitleSonic — Phase 1.27 §11.32 Items 1 + 2.
     *
     * Item 1 (Sonic facing): the TSONIC.ATL source GIF orients Sonic
     * facing LEFT (per RSDK source-atlas convention); the PC Steam
     * reference has him facing RIGHT toward the gold MANIA wordmark.
     * Apply FLIP_X (direction byte = 1) so the Saturn build mirrors
     * the body canvas around the entity origin.
     *
     * Item 2 (intro arc): Phase 1.20/1.23 loaded 8 keyframes
     * {0, 6, 12, 18, 24, 30, 36, 48} from the 49-frame body anim.
     * Walk through 0..7 over a ~42-tick arc starting at TS_FLASH
     * (so the body appears as the flash settles), then latch on
     * keyframe 7 (= atlas frame 48, the iconic settled pose) for the
     * rest of the scene.
     *
     * Per decomp `tools/_decomp_raw/SonicMania_Objects_Title_TitleSonic.c:18-19`
     * the finger anim only ticks once the body lands on its last
     * frame — keep that gate by deferring finger draws until the
     * body walker latches. */
    if (g_tsonic_loaded && g_tsonic_frame_count > 0 &&
        s_ts_state != TS_FADE_IN) {
        const title_asset_t *a = &g_title_assets[TITLE_ASSET_TSONIC_BODY];
        int body_fc = a->frame_count;
        if (body_fc < 1) body_fc = 1;

        /* Per-tick body-frame walker.  Phase 1.28 §11.34 Item B':
         * decomp anim 0 = 49 frames * 2 ticks/frame = 98 ticks total.
         * Saturn maps to 8 keyframes; 12 ticks per keyframe = 96 ticks
         * total (matches decomp within 2%).  Was 6 ticks/keyframe (48
         * ticks = ~2x too fast). */
        static unsigned int s_body_arc_tick = 0;
        static int          s_body_done     = 0;
        ++s_body_arc_tick;
        int body_kf;
        if (s_body_done) {
            body_kf = body_fc - 1;
        } else {
            body_kf = (int)(s_body_arc_tick / 12u);
            if (body_kf >= body_fc) {
                body_kf = body_fc - 1;
                s_body_done = 1;
            }
        }

        /* Per-keyframe pivot + size lookup. Phase 1.23 GAP B' stored
         * per-keyframe metadata in the synthetic anim's frame array,
         * but the direct-draw path reads from the global TSONIC.ATL
         * metadata table (`g_tsonic_frames`) via the keyframe offset
         * map.  Recompute canvas centre per-keyframe so the body
         * doesn't translate as the source frame sizes change. */
        static const int s_body_kf_offsets[8] = { 0, 6, 12, 18, 24, 30, 36, 48 };
        int kf_gfi = g_tsonic_anim0_first_frame +
                     s_body_kf_offsets[body_kf < 8 ? body_kf : 7];
        if (kf_gfi >= TITLE_TSONIC_MAX_FRAMES) kf_gfi = g_tsonic_anim0_first_frame;

        int world_x = 252, world_y = 104;
        int kfw = g_tsonic_frames[kf_gfi].width;
        int kfh = g_tsonic_frames[kf_gfi].height;
        int kfx = g_tsonic_frames[kf_gfi].pivot_x;
        int kfy = g_tsonic_frames[kf_gfi].pivot_y;
        int canvas_cx_world = world_x + kfx + (kfw >> 1);
        int canvas_cy_world = world_y + kfy + (kfh >> 1);

        /* Item 1 — FLIP_X mirrors canvas-centre across entity origin.
         * Entity world X = 252 -> mirrored canvas centre at
         * world_x - (canvas_cx_world - world_x) = 2*world_x - canvas_cx_world.
         * jo_x = (2*world_x - canvas_cx_world) - 256. */
        int flipped_canvas_cx = 2 * world_x - canvas_cx_world;
        /* slDispSprite with attr.dir bit 4 = HF mirrors the sprite about
         * its OWN centre on the framebuffer.  Phase 1.28 §11.34 Item B:
         * body z=190 (was 170) so the RIBBON's ribbonCenter banner
         * (z=180) draws IN FRONT of Sonic's belly per decomp slot order. */
        title_tsonic_draw_frame(TITLE_ASSET_TSONIC_BODY, body_kf,
                                flipped_canvas_cx - 256,
                                canvas_cy_world - 112,
                                190, 1);

        /* Phase 1.28 §11.34 Item A — finger overlay driven by the
         * per-frame duration table from Sonic.bin anim 1.  Decomp
         * authoritative durations: [4,3,2,1,2,3,4,3,2,1,2,3] =
         * 30 ticks/loop with a slow-extend / fast-curl / slow-extend
         * cadence.  Was uniform 2 ticks/frame (Phase 1.27) which lost
         * the rest-points at frames 0 and 6 (dur=4 holds), blurring
         * the visible wave shape.
         *
         * Phase 1.28 Item B: finger z=188 (was 169) — in front of body
         * (z=190), behind banner (z=180) per strict decomp slot order. */
        if (s_body_done) {
            const title_asset_t *fa = &g_title_assets[TITLE_ASSET_TSONIC_FINGER];
            if (fa->base_sprite_id >= 0 && fa->frame_count > 0) {
                static unsigned int s_finger_phase = 0;
                ++s_finger_phase;

                /* Sum per-frame durations to compute the loop length. */
                int loop_total = 0;
                for (int fi = 0; fi < fa->frame_count; ++fi) {
                    int gfi = g_tsonic_anim1_first_frame + fi;
                    if (gfi >= TITLE_TSONIC_MAX_FRAMES) break;
                    int d = (int)g_tsonic_frames[gfi].duration;
                    if (d <= 0) d = 1;
                    loop_total += d;
                }
                if (loop_total <= 0) loop_total = fa->frame_count;

                /* Walk durations to find the active frame for this phase. */
                unsigned int phase_in_loop = s_finger_phase % (unsigned int)loop_total;
                int finger_local = 0;
                int acc = 0;
                for (int fi = 0; fi < fa->frame_count; ++fi) {
                    int gfi = g_tsonic_anim1_first_frame + fi;
                    if (gfi >= TITLE_TSONIC_MAX_FRAMES) break;
                    int d = (int)g_tsonic_frames[gfi].duration;
                    if (d <= 0) d = 1;
                    acc += d;
                    if ((unsigned int)acc > phase_in_loop) {
                        finger_local = fi;
                        break;
                    }
                }

                /* Phase 1.27b §11.34c — body-anchored finger placement.
                 *
                 * Decomp `tools/_decomp_raw/SonicMania_Objects_Title_TitleSonic.c:36`:
                 * `RSDK.DrawSprite(&self->animatorFinger, NULL, false)` draws
                 * the finger at the SAME entity position as the body
                 * (line 31), with NO relative offset in object code — the
                 * body/finger geometry is encoded ENTIRELY in their
                 * per-frame pivots (Title/Sonic.bin anim 0 vs anim 1).
                 *
                 * Audit (`tools/audit_finger_pivot_report.json`):
                 *   body settled (kf=7, atlas 48): px=-50 w=110 -> cx_flip_none=257
                 *   finger frame 0:                px=  6 w= 50 -> cx_flip_none=283
                 *   FLIP_NONE relative offset:     finger is +26 px RIGHT of body
                 *
                 * Old code at this site mirrored the finger centroid about
                 * entity_x (`flipped_fcx = 2*world_x - fcx`) which sent the
                 * finger to -26 LEFT of body — but `attr.dir=0` at
                 * TitleAssets.c:579 means the pixels never actually flip,
                 * so the asymmetric finger landed in the wrong screen
                 * position. User feedback 2026-05-27: "Sonic appears
                 * correct and is facing the right direction... its only
                 * his arm that is placed incorrectly!!" — i.e. body's
                 * mirrored centroid is the accepted reference; finger
                 * must anchor off the body's drawn centroid and apply
                 * the decomp-canonical RELATIVE offset on top.
                 *
                 * Per CLAUDE.md §4.5.1 Audit 3 (pivot+flip composite
                 * math): each sprite's RSDK position formula uses its own
                 * pivot; the relative geometry between two co-positioned
                 * sprites is `pivot_a + (w_a/2) - (pivot_b + (w_b/2))`.
                 * For the mirrored-body anchor we reuse `flipped_canvas_cx`
                 * (body's drawn centroid X, in world coords) and apply the
                 * FLIP_NONE relative offset on top.  Y is not mirrored
                 * (matches body's `canvas_cy_world` handling at line 1639). */
                int finger_gfi = g_tsonic_anim1_first_frame + finger_local;
                if (finger_gfi >= TITLE_TSONIC_MAX_FRAMES)
                    finger_gfi = g_tsonic_anim1_first_frame;
                int fpx = g_tsonic_frames[finger_gfi].pivot_x;
                int fpy = g_tsonic_frames[finger_gfi].pivot_y;
                int fw  = (int)g_tsonic_frames[finger_gfi].width;
                int fh  = (int)g_tsonic_frames[finger_gfi].height;

                /* Decomp-canonical FLIP_NONE world centroids for body
                 * (settled keyframe metadata at kfx/kfy/kfw/kfh above)
                 * and for the active finger frame. */
                int body_flipnone_cx   = world_x + kfx + (kfw >> 1);
                int body_flipnone_cy   = world_y + kfy + (kfh >> 1);
                int finger_flipnone_cx = world_x + fpx + (fw  >> 1);
                int finger_flipnone_cy = world_y + fpy + (fh  >> 1);

                /* Relative offset body -> finger in source-art coords.
                 * Per audit JSON: this is +26 (px right) at frame 0 and
                 * walks 23..26 across the 12 finger frames. */
                int rel_x = finger_flipnone_cx - body_flipnone_cx;
                int rel_y = finger_flipnone_cy - body_flipnone_cy;

                /* Anchor finger at body's actual DRAWN centroid + decomp
                 * relative offset.  flipped_canvas_cx is the body's
                 * post-mirror world X (line 1632).  canvas_cy_world is
                 * the body's un-mirrored world Y (line 1626). */
                int finger_cx_saturn = flipped_canvas_cx + rel_x;
                int finger_cy_saturn = canvas_cy_world   + rel_y;

                /* Keep direction=1 for audit-trail consistency with the
                 * body call (line 1640): even though TitleAssets.c:558,579
                 * hardcode attr.dir=0 (no hardware flip), the intent at
                 * this layer is "compose alongside the mirrored body".
                 * The pixel-mirror question (Path A/B/C from Phase 1.27b
                 * Step 1) is OUT OF SCOPE — user is OK with body as-is. */
                title_tsonic_draw_frame(TITLE_ASSET_TSONIC_FINGER, finger_local,
                                        finger_cx_saturn - 256,
                                        finger_cy_saturn - 112,
                                        188, 1);
            }
        }
    }

    /* PRESSSTART — only after press-reveal hold completes. Blink at
     * 16-on / 16-off (decomp predicate `!(timer & 0x10)`).
     * MPRESS.SPR frame 0 = "PRESS START" (or "PRESS ANY BUTTON" 120x8,
     * pivot -60,-4). Canvas centre offset = (0, 0). World (256, 212). */
    if (s_ts_state == TS_WAIT_FOR_ENTER) {
        if (!(s_ts_timer & 0x10)) {
            int sid = g_title_assets[TITLE_ASSET_TLOGO_PRESSSTART].base_sprite_id;
            if (sid >= 0) {
                jo_sprite_draw3D(sid, 0, (212 - 112), 165);
            }
        }
    }
}

/* Phase 1.19 — diagnostic globals are kept in scene.c + TitleAssets.c
 * (volatile, +16 sentinel encoded into back-color) and read at runtime
 * via the screen-edge sample in verify_done's V1 capture. The encoding
 * call lives in mania_tick_diag_encode below (compiled out for ship
 * builds by leaving the call commented out — the back-color stays at
 * jo_core_init's (96,128,224) sky-blue which is the title's natural
 * edge fill). */
extern volatile uint32_t g_scene_diag_entity_total;
extern volatile uint32_t g_scene_diag_class_resolved;
extern volatile uint32_t g_scene_diag_class_unresolved;
extern volatile uint32_t g_scene_diag_create_ok;
extern volatile uint32_t g_scene_diag_create_fail;
extern volatile int      g_tsonic_load_step;
extern volatile int      g_tsonic_load_anim_count;
extern volatile int      g_tsonic_load_total_fc;
extern volatile int      g_tsonic_load_first_sid;
extern volatile int      g_tsonic_load_loaded;

/* Phase 1.26b §11.33 — title RBG0 backdrop rotation, driven by src/main.c.
 * Called BEFORE title_direct_draw in mania_tick's title branch so VDP1
 * sprite output composites on top of the rotating plane.  Skipped
 * internally when s_title_bg_ready is 0 (RBG0 setup failed or torn
 * down by mania_free_title_bg_buffers).  See docs/COMPREHENSIVE_PLAN.md
 * §11.33 for the full citation chain (BIPLANE/MAIN.C:317-337 +
 * demo - vdp2 plane/main.c:80-98 + jo/vdp2.h:403-406). */
extern void mania_title_3d_backdrop_draw(void);

/* Phase 1.24 diagnostic — encode per-asset (unflipped, flipped) draw counts
 * into VDP1 colour bank registers so a one-shot mednafen capture can read
 * them via a known pixel position. Writes happen at frame 60 only (settled
 * title state) so the back-color stays sky-blue for the rest of the run.
 *
 * Encoding: 8 cells of 16x4 each side-by-side at the top-of-screen line
 * via a tiny 8-bpp sprite. Actually too involved — simpler: write the
 * 8 cb-draw counts (4 bits per asset, 0..15) into a single 32-bit
 * back-color word at one specific tick. Caller checks via screen-edge
 * sample. */
/* Phase 1.24 diag scaffolding removed after revert to direct_draw path.
 * The per-class .c files no longer extern these — bridge stays clean
 * for the next agent who re-attempts the entity-driven path. */

/* Phase 1.22 (§11.28) — entity-driven scene path restored after the
 * storage.c layer-parser desync fix. With g_scene_diag_class_resolved
 * now > 0 (was 0 in Phase 1.17..1.21), rsdk_object_tick/draw_all reach
 * every registered Title-class entity and the canonical decomp Update +
 * Draw callbacks run.
 *
 * `title_direct_draw` (the Phase 1.17 hard-coded fallback) is gated OFF
 * via the flag below.  Kept callable so a future bisect can A/B it
 * against the entity-driven path without re-introducing the hard-coded
 * positions.  Phase 1.23 may delete it once the entity-driven Draw pass
 * is visually verified at the same fidelity as the Phase 1.20 baseline.
 *
 * Bound to a static (not extern) so the entity-driven path is the only
 * production path; flipping back requires a code edit, not a runtime
 * tweak. */
/* Phase 1.23 GAP A retired the §11.28 round-2 residual sky-blue gap.
 * The entity-driven Draw bridge now flows list_id through the animator
 * struct (rsdk_set_sprite_animation -> rsdk_draw_sprite_ex -> title_sprite_cb)
 * so every Title-class Draw resolves to the correct asset without the
 * old `s_active_list_id` last-writer-wins race.
 *
 * `title_direct_draw` (the Phase 1.17 hard-coded fallback) is kept
 * callable but gated OFF so the shipped binary uses the canonical
 * entity-driven path. Flip to `true` only for A/B bisects against the
 * Phase 1.20 known-good baseline; Phase 1.24 may delete it once the
 * entity-driven Draw pass is visually verified across the full intro
 * choreography. */
/* Phase 1.24 — REVERTED to Phase 1.20 known-good baseline. The entity-
 * driven bridge has a structural blocker (only EMBLEM + RIBBON
 * TitleLogo entities reach Draw — GAMETITLE/COPYRIGHT/RINGBOTTOM/
 * PRESSSTART never activate because State_FlashIn entity-activation
 * isn't propagating to the rsdk_object_draw_all bucket). Phase 1.24
 * iteration on the entity-driven path produced WORSE visual fidelity
 * than Phase 1.20's hand-coded direct_draw — user-flagged 2026-05-27.
 *
 * Until the State_FlashIn → entity-activation gap is properly
 * diagnosed (Phase 1.25+), the direct_draw fallback is the production
 * code path. Phase 1.20 finger-wave + 8-keyframe Sonic loader (Phase
 * 1.23 GAP B') still run through this path because the direct_draw
 * body explicitly invokes title_tsonic_draw_frame for both body and
 * finger.
 *
 * The entity-driven path remains compiled (TitleLogo_Draw, etc) so
 * that the eventual fix lands quickly once State_FlashIn entity
 * activation is unblocked — but its sprite output is gated off at the
 * callback level (title_sprite_cb stays registered, but the actual
 * jo_sprite_draw3D calls inside it overlap zero-info with the direct_
 * draw's output, which renders FIRST in the frame pipeline). */
static const bool s_title_direct_draw_enabled = true;

void mania_tick(void)
{
    mania_screen_sync();
    rsdk_input_tick();

#ifdef QA_SFX_PROBE
    /* SFX-confirmation probe (user request 2026-05-29: "need a test").
     * Fire the jump PCM on a fixed cadence from the TOP-LEVEL tick so the
     * test does not depend on reaching GHZ-active (the headless QA harness
     * never presses START to leave the title/menu). Lazy-load the PCM on
     * each fire attempt (no-op once loaded) so the capture records
     * deterministic bursts regardless of game state. BGM is suppressed
     * (#ifndef blocks below) so any audio in the WAV is the PCM sample.
     * Compiled out of the release binary. */
#ifndef QA_SFX_PROBE_PERIOD
#define QA_SFX_PROBE_PERIOD 30
#endif
    {
        static int s_qa_sfx_toplevel_tick = 0;
        if ((s_qa_sfx_toplevel_tick++ % QA_SFX_PROBE_PERIOD) == 0) {
            entities_qa_force_load_sfx();
            entities_play_sfx_jump();
        }
    }
#endif

    /* Phase 3.1 Path B (Task #123) — REVERTED 2026-05-28 after capture
     * showed pure sky-blue at t=12-15s (Sega logo never visible AND title
     * never drawn). LogoSetup_Update + IsDone gating disabled; .c file
     * + cd/LOGOS.ATL kept on disk for follow-on Phase 3.1b iteration that
     * fixes the integration (likely needs delayed atlas-load gating OR
     * different sprite-draw timing vs the existing mania_tick pipeline). */
    /* LogoSetup_Update(); */
    /* if (!LogoSetup_IsDone()) { LogoSetup_Draw(); return; } */

    rsdk_object_tick();
    mania_screen_sync();           /* re-sync after per-class state ticks */
    /* Phase 1.25-fix per docs/COMPREHENSIVE_PLAN.md §11.31:
     * gate rsdk_object_draw_all() on !s_title_direct_draw_enabled so that
     * only ONE sprite path emits per tick. Phase 1.25 measurement proved
     * State_FlashIn fires AND activation completes (Gate V1.25 S1 green
     * sentinel landed at bezels frame 90+) — entity Update/state-machine
     * ticks remain functional via rsdk_object_tick above. Suppressing the
     * entity-driven *Draw* pass eliminates the duplicate-wing regression
     * the user reported (frame 100: 64,876 feather pixels in 13 bands vs
     * Phase 1.20 baseline ~17,000 in 2 clean 144-px wing regions). The
     * structural FLIP_X canvas-mirror bug in the entity-Draw path
     * (§11.30 Bug A) remains a known gap; this gate scopes the regression
     * to remediation-only without touching the entity-Draw math. */
    if (!s_title_direct_draw_enabled) {
        rsdk_object_draw_all();
    }
    title_state_tick();
    /* Phase 1.26b §11.33 — RBG0 rotating backdrop pass.  Runs in the title
     * state only (guarded inside the callee by s_title_bg_ready, which is
     * cleared on title->GHZ transition by mania_free_title_bg_buffers).
     * MUST run before title_direct_draw so VDP1 sprite output composites
     * on top of the rotation plane.  The internal early-return makes this
     * call safe to issue unconditionally — it's a no-op once GHZ has
     * taken the visible state slot. */
    if (!mania_is_ghz_active()) {
        mania_title_3d_backdrop_draw();
        /* Phase 3.2b (Task #125) — Gate TitleBG_/Title3DSprite_ Tick+Draw
         * passes on `s_ts_state != TS_MENU_ACTIVE`.  User report 2026-
         * 05-28: in Phase 3.2 MVP, pressing START routes to TS_MENU_
         * ACTIVE but the title backdrop's mountain-strip + reflection +
         * billboard animations continued running, producing "wavy GHZ
         * background artifacts and the two animations" the user sees
         * after pressing buttons.  Title3DSprite_Tick_All advances
         * g_title_bg_angle every frame (decomp TitleBG_StaticUpdate:39-
         * 40) which keeps mutating sprite-attr-table entries; gating the
         * whole title bg pass on !TS_MENU_ACTIVE freezes those entries
         * cleanly so the menu overlay (mania_menu_draw, called later
         * with NBG0 text) shows on a static field.  Audit-1 (Z-order):
         * Title3DSprite drawGroup=2 < TitleLogo drawGroup=4 < Menu
         * NBG0 (priority 5); freezing back rows does not change ordering.
         *
         * RBG0 backdrop (mania_title_3d_backdrop_draw above) is kept
         * unconditional because its per-frame REPLACE block also re-
         * applies slScrAutoDisp + slPriorityRbg0(0) which we still
         * want to maintain in menu state (and Phase 3.2b appends
         * NBG0ON to that mask while menu-active — see main.c). */
        /* Phase 1.34 — TitleBG sub-types: Mountain Top strips, Reflection
         * + WaterSparkle strips, WingShine sparkles. Tick advances per-
         * entity position via the decomp TitleBG_Update math
         * (TitleBG.c:12-29); Draw_All issues 9 sprite draws via the
         * 4-bpp Color Bank 16 path (title3d_bg_draw_frame in
         * TitleAssets.c). Runs BEFORE Title3DSprite_Tick_All so the
         * Mountain Top strips sort BEHIND the orbiting 3DSprite
         * billboards on the Saturn perspective sort. */
        /* Phase 1.40 (user direction 2026-05-28): user wants "just
         * continuously vertically scrolling cloud GHZ instead" of the
         * Phase 1.34 mountain+reflection+sparkle composition. Disabling
         * TitleBG_Tick_All + TitleBG_Draw_All removes the 5 entities
         * (Mountain1, Mountain2, Reflection, 2x WaterSparkle per the
         * s_titlebg[5] table at TitleBG.c lines 175-200 after Phase 1.39
         * WingShine removal). Saturn-fit deviation from decomp.
         * tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c retains
         * canonical behavior; suppression here is title-only. */
        /* TitleBG_Tick_All(); */
        /* TitleBG_Draw_All(); */
        /* Phase 1.37 (Task #118) REVERTED 2026-05-28 — central island
         * via VDP1 sprite path SUPPRESSED ENTIRE TITLE SCENE.  Adding
         * the single scaled slDispSpriteHV(pos, attr, angle=1) call
         * with H=1.273x V=4.0x (Sh=0x14655, Sv=0x40000) caused all VDP1
         * sprite output (Sonic, MANIA wordmark, ring banner, ribbons,
         * Mountain1/2 strips, billboards) to vanish across the entire
         * 24-second capture window -- pure sky-blue back-color only.
         * Symptom class matches Phase 2.3e SGL sortlist overflow.
         *
         * central_island_draw() and title3d_bg_draw_frame_hv() are
         * KEPT compiled (no references to them in the live path) so
         * the diagnostic surface is available for the next agent, but
         * the call is NOT wired -- the title scene is now back to its
         * Phase 1.34c baseline visuals.
         *
         * Hand-off: the next attempt should likely (a) test the HV
         * draw in isolation FIRST (replace the existing Phase 1.34
         * TitleBG_Draw_All Mountain1 entry with the HV path at 1.0x to
         * verify slDispSpriteHV itself works in this build), (b) try a
         * MUCH smaller scale (e.g. 1.5x H, 1.5x V) to rule out a
         * char-size limit class, OR (c) measure VDP1 frame-buffer
         * char-RAM utilisation via the savestate harness to confirm
         * whether the issue is a single-sprite size cap, a per-frame
         * char-RAM exhaustion, or true sortlist overflow. */
        /* central_island_draw(); */
        /* Phase 1.32 — Title3DSprite billboard formation. Tick advances
         * g_title_bg_angle (decomp TitleBG_StaticUpdate:39-40) + computes
         * per-entity relativePos/zdepth; Draw_All issues 58 software-
         * projected billboard draws via the 4-bpp Color Bank 16 path.
         * Runs BEFORE title_direct_draw so the title logo composites on
         * top of the formation, matching the decomp slot/drawGroup order
         * (Title3DSprite drawGroup=2 < TitleLogo drawGroup=4).
         *
         * Phase 3.2b (Task #125): suppress in TS_MENU_ACTIVE per the
         * TitleBG_ rationale above (Game.c §gating-block comment).  When
         * the menu is open, the billboard formation freezes — no more
         * orbiting "rotating features" the user reported. */
        /* Phase 1.40 (user direction 2026-05-28): suppress 58-billboard
         * formation per same "just clouds" directive that disabled
         * TitleBG_*_All above. tools/_decomp_raw/SonicMania_Objects_
         * Title_Title3DSprite.c retains canonical behavior; suppression
         * here is title-only. */
        /* Title3DSprite_Tick_All(); */
        /* Title3DSprite_Draw_All(); */
    }
    if (s_title_direct_draw_enabled) {
        /* Phase 1.17 hard-coded fallback. Production code path while
         * Phase 1.25 §11.31 Bug A entity-Draw math is unresolved. */
        title_direct_draw();
    }

    /* Phase 3.2 + 3.2b REVERTED 2026-05-28 — mania_menu_draw call
     * removed. */

    /* Phase 1.39 REMOVED 2026-05-28 (Task #122) — Phase 2.3c
     * diagnostic probe per user 'remove test visualizations' directive.
     * The magenta-checker + white-stripe 16x16 sprite was registered at
     * mania_engine_init and drawn unconditionally from mania_tick at
     * world (0, 80) Z=200 via jo_sprite_draw3D and again at (24, 80)
     * via direct slDispSprite. Bisect (Phase 2.3c) and post-fix
     * (Phase 2.3e) confirmed VDP1 sprite pipeline integrity; the probe
     * is no longer needed and is visible in user-facing title captures.
     * The probe registration in mania_engine_init is kept for now (the
     * sprite-add advances jo's VRAM cursor; removing the call has knock-
     * on effects on subsequent sprite ids — leave the registration but
     * skip the draw). The mania_diag_probe_register function remains
     * available if a future regression bisect needs it.
     *
     * Original call (commented for restoration):
     *     mania_diag_probe_draw();
     */
    /* mania_diag_probe_draw(); */

    /* Phase 2.1 — per-frame GHZ Work-RAM page rebuild + VRAM upload.
     * Archived design split build (active tick) from upload (V-blank);
     * Phase 1.19 empirically found jo vblank callbacks don't fire under
     * SGL's own vblank handler, so we do both here. slDMAXCopy is SCU
     * DMA (async; returns immediately) — the SH-2 doesn't block on
     * completion. The cache-through alias keeps coherency despite
     * running outside V-blank.
     *
     * The function is a no-op until ghz_is_active() returns true.
     *
     * Phase 2.3e (3 rounds, all falsified — see plan §12.3e Findings):
     * the mania_ghz_draw_only refactor was hypothesized to restore
     * in-GHZ sprite visibility by moving sprite draws out of this
     * conditional. ALL THREE rounds (post-block call site, pre-vblank
     * inside block, pre-block body scope) failed to restore Sonic /
     * entity / HUD visibility in GHZ state. The Phase 2.3c probes
     * still render correctly throughout; the only sprite draws that
     * survive are the ones issued from mania_diag_probe_draw above.
     *
     * Diagnostic finding (Round 3c, captured 2026-05-27): a counter
     * sentinel placed at the top of mania_ghz_tick_and_draw (BEFORE
     * its own !g_ghz_player_ready early-return) never exceeded 60
     * across the entire 30-second capture, indicating ghz_is_active()
     * returns false for the bulk of the GHZ-visible window OR
     * mania_ghz_tick_and_draw is being optimized-away by LTO under
     * conditions that bypass even volatile counter increments. This
     * is the new lead for Phase 2.3f. See plan §12.3e for full
     * tactics catalog. */
    if (mania_is_ghz_active()) {
        /* Phase 2.3j — synchronous scene-load contract: by the time
         * s_ts_state == TS_GHZ_ACTIVE, mania_load_ghz_scene has
         * returned and NBG1 FG + NBG2 sky + Player are committed.
         * No readiness flag is read; the state machine variable IS
         * the readiness signal. */
        mania_ghz_tick_and_draw();
        mania_ghz_draw_only();

        ghz_fg_build_page();
        ghz_fg_vblank();
    } else {
        /* Phase 1.20 — title-side palette CRAM drain.
         *
         * TitleBG_StaticUpdate rotates palette bank 0 indices 140-143 every
         * 6 ticks (water-shimmer band per decomp TitleBG.c:42-45). The RAM
         * mirror has been updated by rsdk_rotate_palette during
         * rsdk_object_tick above; this call drains the dirty range to CRAM
         * at the title backdrop's allocated palette offset, captured at
         * setup_title_bg time. See src/main.c::mania_title_palette_drain
         * for the DTS96 ST-058-R2 §5 CRAM-access citation. */
        mania_title_palette_drain();
    }
}
