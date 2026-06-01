#ifndef MANIA_TITLE_TITLEASSETS_H
#define MANIA_TITLE_TITLEASSETS_H

/* Phase 1.3 — Title asset bridge.
 *
 * The Saturn build doesn't ship the RSDK `.bin` sprite-animation files
 * (Title/Logo.bin, Title/Sonic.bin, Title/Background.bin, Title/Electricity
 * .bin) so the decomp ports' RSDK.LoadSpriteAnimation calls (with names
 * like Title/Logo.bin) return -1 and the animators stay frame-empty
 * (rsdk_draw_sprite_ex
 * early-returns at `rsdk_animator_current_frame == NULL`).
 *
 * Phase 1.3 closes this regression by:
 *   * Loading the existing Saturn-side .SPR atlases (MWINGS, MRIBSIDE,
 *     MLOGO, MRIBBON, MRING, MPRESS) plus TSONIC.ATL into VDP1 sprite
 *     slots at engine init (mania_engine_init -> setup_title_assets).
 *   * Manufacturing SYNTHETIC `rsdk_sprite_animation_t` entries in the
 *     global rsdk_sprite_anims table whose per-frame pivots come from
 *     docs/title_ground_truth.md + the decomp Logo.bin/Sonic.bin spec.
 *   * Pre-populating g_titlelogo_list_id / g_titlesonic_list_id /
 *     g_titlebg_list_id / g_titlesetup_list_id BEFORE the per-class
 *     StageLoad runs, so the StageLoad's `LoadSpriteAnimation` call still
 *     gets a real slot but it's wired to our synthetic anim entries.
 *
 * The resolver in Game.c then maps (list_id, anim_id, frame_id) to the
 * jo sprite_id table indexed below. */

#include <stdint.h>
#include <stdbool.h>

/* Asset slot ids (Saturn-side jo sprite IDs are keyed off these). */
enum {
    TITLE_ASSET_TLOGO_EMBLEM     = 0,   /* MWINGS.SPR  144x144 1 frame   */
    TITLE_ASSET_TLOGO_RIBSIDE    = 1,   /* MRIBSIDE.SPR 56x72 1 frame   */
    TITLE_ASSET_TLOGO_GAMETITLE  = 2,   /* MLOGO.SPR   144x48 2 frames  */
    TITLE_ASSET_TLOGO_RIBCENTER  = 3,   /* MRIBBON.SPR 176x56 1 frame   */
    TITLE_ASSET_TLOGO_COPYRIGHT  = 4,   /* (deferred -- COPYRIGHT off-screen
                                          on 320-wide Saturn) */
    TITLE_ASSET_TLOGO_RINGBOT    = 5,   /* MRING.SPR   120x32 1 frame   */
    TITLE_ASSET_TLOGO_PRESSSTART = 6,   /* MPRESS.SPR  176x24 10 frames */
    TITLE_ASSET_TSONIC_BODY      = 7,   /* TSONIC.ATL 4-bpp 49-frame    */
    TITLE_ASSET_TSONIC_FINGER    = 8,   /* (deferred -- not yet built)  */
    TITLE_ASSET_TBG_FULL         = 9,   /* TITLE.DAT NBG2 fallback      */
    TITLE_ASSET_ELECTRICITY      = 10,  /* Phase 1.27 §11.32 — ELECTRA.ATL
                                         * 4-bpp 8-keyframe pre-flash arc.
                                         * Decomp TitleSetup.c:83 +
                                         * Draw_DrawRing:393-402. */
    TITLE_ASSET_TITLE3D_BB       = 11,  /* Phase 1.32 — TITLE3D.ATL anim 5
                                         * 'Billboard Sprites' 5-frame
                                         * billboard family (Mountain L/M/S
                                         * + Tree + Bush) consumed by the
                                         * Title3DSprite class. Decomp
                                         * Title3DSprite.c:47 SetSprite
                                         * Animation(aniFrames, 5, ...). */
    TITLE_ASSET_COUNT            = 12
};

typedef struct {
    int      base_sprite_id;        /* first jo sprite ID (-1 if not loaded) */
    int      frame_count;
    int16_t  pivot_x;               /* applied uniformly across frames        */
    int16_t  pivot_y;
    int16_t  width;
    int16_t  height;
    uint8_t  is_4bpp;               /* TSONIC.ATL uses 4-bpp + per-frame meta */
} title_asset_t;

extern title_asset_t g_title_assets[TITLE_ASSET_COUNT];

/* TSONIC.ATL has per-frame size/pivot/duration tables and a custom CRAM
 * palette upload. Exposed here so the resolver can issue direct slDispSprite
 * calls with the right SPR_ATTR bits (CCM=0 Color Bank 16, ECdis=1). */
#define TITLE_TSONIC_MAX_FRAMES 64
typedef struct {
    uint16_t width;
    uint16_t height;
    int16_t  pivot_x;
    int16_t  pivot_y;
    uint16_t duration;
    uint16_t cumulative;
} title_tsonic_frame_t;

extern title_tsonic_frame_t g_tsonic_frames[TITLE_TSONIC_MAX_FRAMES];
extern int                  g_tsonic_frame_count;
extern int                  g_tsonic_total_ticks;
extern int                  g_tsonic_palette_cram_index;
extern int                  g_tsonic_loaded;

/* Phase 1.27 §11.32 — anim 0 / anim 1 first-frame indices into
 * g_tsonic_frames[] so the direct-draw path can compute per-keyframe
 * pivots without re-parsing the atlas. Populated by load_tsonic_atlas. */
extern int                  g_tsonic_anim0_frame_count;
extern int                  g_tsonic_anim0_first_frame;
extern int                  g_tsonic_anim1_frame_count;
extern int                  g_tsonic_anim1_first_frame;

/* Phase 1.27 §11.32 — ELECTRA.ATL exposed frame metadata. 8 culled keyframes
 * from the 40-frame Electricity anim (per `tools/build_electricity_atlas.py`
 * keyframe-cull list). Pivot/size live here so the direct-draw bridge can
 * compute canvas-centre jo coords per-keyframe. */
#define TITLE_ELECTRA_MAX_FRAMES 8
extern title_tsonic_frame_t g_electra_frames[TITLE_ELECTRA_MAX_FRAMES];
extern int                  g_electra_frame_count;
extern int                  g_electra_palette_cram_index;
extern int                  g_electra_loaded;

/* Draw a single electricity keyframe (4-bpp atlas) at (jo_x, jo_y, z) with
 * the given direction byte (0 = FLIP_NONE, 1 = FLIP_X per RSDK contract).
 * Per decomp TitleSetup.c:393-402 the title scene calls Draw_DrawRing once
 * per tick which issues TWO of these (FLIP_NONE + FLIP_X) at world (256, 108). */
void title_electra_draw_frame(int frame_id, int jo_x, int jo_y, int z,
                              uint8_t direction);

/* Phase 1.32 — TITLE3D.ATL anim 5 'Billboard Sprites' exposed frame
 * metadata. 5 frames: MountainL (16x30), MountainM (10x18), MountainS
 * (6x10), Tree (14x18), Bush (8x9). Each frame becomes a sequential
 * jo sprite ID starting at g_title_assets[TITLE_ASSET_TITLE3D_BB].
 * base_sprite_id. Per-frame pivot/size lives here so Title3DSprite_Draw
 * can compute canvas-centre jo coords per-frame. */
#define TITLE3D_BB_MAX_FRAMES 5
extern title_tsonic_frame_t g_title3d_bb_frames[TITLE3D_BB_MAX_FRAMES];
extern int                  g_title3d_bb_frame_count;
extern int                  g_title3d_palette_cram_index;
extern int                  g_title3d_loaded;

/* Draw a single billboard frame (4-bpp atlas) at (jo_x, jo_y, z).
 * Used by Title3DSprite_Draw_All. Direction is always FLIP_NONE for the
 * 5-frame billboard family per decomp Title3DSprite_Draw which uses
 * DrawSprite with useDelta=true but no FLIP. */
void title3d_bb_draw_frame(int frame_id, int jo_x, int jo_y, int z);

/* Phase 1.32b — same as title3d_bb_draw_frame but with explicit uniform
 * H/V scale. `hv_scale` is Saturn FIXED (Q16.16) where 0x10000 = 1.0x
 * (sprite at source size). Closer billboards get larger scale per the
 * decomp Title3DSprite_Draw math (line 32) — see Title3DSprite_Draw_All
 * for the depth->scale conversion. */
void title3d_bb_draw_frame_scaled(int frame_id, int jo_x, int jo_y, int z,
                                  int hv_scale);

/* Phase 1.34 — TITLE3D.ATL anims 0..4 expose TitleBG sub-type frames.
 *
 * Anim 0 Mountain1   (176x16, pivot -88,-16) TitleBG type 0
 * Anim 1 Mountain2   (192x16, pivot -96,-16) TitleBG type 1
 * Anim 2 Reflection  (176x16, pivot -88,  0) TitleBG type 2
 * Anim 3 WaterSpark  (192x16, pivot -96,  0) TitleBG type 3
 * Anim 4 WingShine   (64x128, pivot -32,-64) TitleBG type 4
 *
 * Per `tools/build_title3d_atlas.py` lines 11-15 + ATL header dump.
 *
 * Each anim is a single-frame entry in the TITLE3D.ATL global frame
 * table (frame_index == anim_index for anims 0..4 because their
 * frame_count is 1 each). The 5 BG sprite IDs are sequential starting
 * at g_title3d_bg_first_sid (set by load_title3d_atlas). Per-frame
 * pivot+size live in g_title3d_bg_frames[]. */
#define TITLE3D_BG_MAX_FRAMES 5
extern title_tsonic_frame_t g_title3d_bg_frames[TITLE3D_BG_MAX_FRAMES];
extern int                  g_title3d_bg_first_sid;
extern int                  g_title3d_bg_frame_count;

/* Draw a single TitleBG anim frame (4-bpp Color Bank 16). `anim_id` in
 * [0..4] selects MOUNTAIN1/MOUNTAIN2/REFLECTION/WATERSPARKLE/WINGSHINE.
 * `direction` byte mirrors the RSDK FLIP_X contract: bit 0 = HF on
 * VDP1 PMOD (ST-013-R3 §5.5.4). FLIP_NONE is the decomp default
 * (per Create at TitleBG.c:67 self->drawFX = FX_FLIP, but no entity
 * sets self->direction in TitleBG_Create or Update, so direction is
 * FLIP_NONE = 0 throughout the title scene).
 *
 * Phase 1.34c — `half_transparency`: when non-zero, OR `CL_Trans`
 * (SGL SL_DEF.H:194 = 3, the Color-Calc field at PMOD bits 2:0 per
 * ST-013-R3 §5.5.4) into the sprite atrb so VDP1 blends the sprite
 * with the framebuffer 50/50. Used for the decomp INK_BLEND
 * (MOUNTAIN2), INK_ADD (REFLECTION + WATERSPARKLE), and INK_MASKED
 * (WINGSHINE) sub-types — all collapse to half-transparency on
 * Saturn since VDP1 has a single alpha primitive per pass. MOUNTAIN1
 * passes 0 (opaque, matching the decomp default). */
void title3d_bg_draw_frame(int anim_id, int jo_x, int jo_y, int z,
                           uint8_t direction, int half_transparency);

/* Phase 1.37 — non-uniform H/V scaled BG-frame draw via slDispSpriteHV.
 * `hv_scale_x` / `hv_scale_y` are Saturn FIXED Q16.16 (0x10000 = 1.0x).
 * Used by central_island_draw in Game.c to scale Mountain1 (176x16
 * source aspect 11:1) into a substantial bottom-anchored silhouette.
 * Per SGL ST-238-R1 §slDispSpriteHV + SL_DEF.H:93 enum + SL_DEF.H:912
 * prototype. */
void title3d_bg_draw_frame_hv(int anim_id, int jo_x, int jo_y, int z,
                              int hv_scale_x, int hv_scale_y,
                              uint8_t direction, int half_transparency);

/* Phase 1.3 — Public asset-bridge API. */

/* Load every Title-scene .SPR atlas (and TSONIC.ATL) into VDP1 sprite slots.
 * Manufacture synthetic rsdk_sprite_animation_t entries in the global
 * anims table. Sets g_titlelogo_list_id et al. so the per-class StageLoad
 * functions see a real slot. */
void title_assets_load(void);

/* Returns the list_id assigned to a given Title class. -1 if not yet loaded. */
int  title_assets_titlelogo_list_id(void);
int  title_assets_titlesonic_list_id(void);
int  title_assets_titlebg_list_id(void);
int  title_assets_titlesetup_list_id(void);

/* Draw helper for TSONIC.ATL's 4-bpp frames. Called from the sprite_cb
 * resolver when (list_id, anim_id) maps to TitleSonic body or finger.
 *
 * Phase 1.20: parameterised on asset slot so anim 1 (finger overlay)
 * shares the same VDP1+CRAM path as anim 0 (body). `frame_id` is the
 * LOCAL frame index inside the slot (animator->frame_id), NOT a global
 * TSONIC.ATL frame ordinal. */
void title_tsonic_draw_frame(int asset_slot, int frame_id, int jo_x, int jo_y,
                             int z, uint8_t direction);

#endif /* MANIA_TITLE_TITLEASSETS_H */
