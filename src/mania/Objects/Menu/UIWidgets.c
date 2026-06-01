/* Phase 3.2.b (Task #146) — UIWidgets class body.
 *
 * Mechanical port of `tools/_decomp_raw/SonicMania_Objects_Menu_UIWidgets.c`
 * (353 LOC; Christian Whitehead/Simon Thomley/Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich). Each function below cites the decomp line
 * range it mirrors.
 *
 * Per-line decomp citations:
 *   .c L12       UIWidgets_Update           -> UIWidgets_Update
 *   .c L14       UIWidgets_LateUpdate       -> UIWidgets_LateUpdate
 *   .c L16-24    UIWidgets_StaticUpdate     -> UIWidgets_StaticUpdate
 *                                              (non-Plus path: timer
 *                                              advance only; the
 *                                              buttonColor cycling
 *                                              gates on MANIA_USE_PLUS)
 *   .c L26       UIWidgets_Draw             -> UIWidgets_Draw  (empty)
 *   .c L28       UIWidgets_Create           -> UIWidgets_Create (empty)
 *   .c L30-69    UIWidgets_StageLoad        -> UIWidgets_StageLoad
 *                                              (atlas + SFX load + the
 *                                              non-Plus buttonColors table
 *                                              is GATED OUT; only the
 *                                              MANIA_USE_PLUS branch
 *                                              populates the palette).
 *   .c L71-89    UIWidgets_ApplyLanguage    -> UIWidgets_ApplyLanguage
 *                                              (LANGUAGE_EN-only on
 *                                              Saturn NTSC-US per
 *                                              docs/MANIA_MODE_PARITY_PLAN
 *                                              §9.A)
 *   .c L91-100   UIWidgets_DrawRectOutline_Black
 *   .c L101-110  UIWidgets_DrawRectOutline_Blended
 *   .c L111-121  UIWidgets_DrawRectOutline_Flash
 *   .c L122-163  UIWidgets_DrawRightTriangle
 *   .c L164-201  UIWidgets_DrawEquilateralTriangle
 *   .c L202-246  UIWidgets_DrawParallelogram
 *   .c L247-260  UIWidgets_DrawUpDownArrows
 *   .c L261-275  UIWidgets_DrawLeftRightArrows
 *   .c L276-286  UIWidgets_DrawTriJoinRect
 *
 * Saturn-side adaptations:
 *   * `RSDK.LoadSpriteAnimation` -> rsdk_load_sprite_animation
 *     (slot=-1 picks the first empty slot). The Saturn-side ANIM
 *     loader resolves the .bin to a CD-resident atlas; the menu
 *     scene's assets are loaded by the Phase 3.0 menu-asset bootstrap.
 *   * `RSDK.GetSfx` -> rsdk_get_sfx (Phase A6 SFX shim; see
 *     src/rsdk/audio.h). Saturn-side SFX live in the pre-loaded jo_sound
 *     table; rsdk_get_sfx returns the table slot index.
 *   * `RSDK.SetSpriteAnimation` -> rsdk_set_sprite_animation. We
 *     initialise UIWidgets->frameAnimator + arrowsAnimator at StageLoad
 *     per decomp L41-42 so child widgets can copy from these animator
 *     templates.
 *   * `LogHelpers_Print` (decomp L73) -> jo_printf trace at the top of
 *     ApplyLanguage. Optional; doesn't impact correctness.
 *   * RSDK.DrawRect / DrawLine / DrawFace (decomp L96-99, L146-148,
 *     L227-244): these are VDP1-polygon-class draws used by the
 *     decoration helpers. Saturn-side they're stubbed below — the
 *     Phase 3.2.b deliverable is the API SURFACE so child widgets
 *     compile + run. The actual polygon draw lands in Phase 3.2.c
 *     (alongside the wider UIButton/UIChoice port) once we wire the
 *     VDP1 polygon emitter. Each stub records the call-site behind a
 *     trace comment for the 3.2.c follow-up.
 *   * MANIA_USE_PLUS == 0: the entire #if branches (saveSelFrames,
 *     buttonColor, DrawTime, unusedAnimator1/2) are skipped per
 *     project policy (Mania-only Saturn ship).
 *
 * Phase 3.2.b scope: UIWidgets is REGISTERED as a service so child
 * widget classes (UIButton, UISaveSlot, ...) compile against the
 * canonical ObjectUIWidgets->uiFrames/textFrames/fontFrames/sfx*
 * fields in Phase 3.2.c. The actual polygon-emitting draw helpers
 * carry stubs that the 3.2.c agent fills in alongside its VDP1
 * polygon emitter. */

/* Include jo/jo.h BEFORE UIWidgets.h so the jo headers' `const jo_color
 * color` parameter names don't trip -Wshadow against our `typedef
 * uint32 color` (decomp RetroEngine.hpp packed-RGB888 alias). */
#include <jo/jo.h>
#include "UIWidgets.h"
#include "../../../rsdk/object.h"
#include "../../../rsdk/animation.h"
#include "../../../rsdk/audio.h"
#include "../../../rsdk/drawing.h"

ObjectUIWidgets *UIWidgets = NULL;

/* Saturn-side Localization stub (decomp Objects/Global/Localization.h
 * tracks per-language id; Saturn NTSC-US is pinned to LANGUAGE_EN per
 * docs/MANIA_MODE_PARITY_PLAN.md §9.A). The decomp ApplyLanguage reads
 * Localization->language; we expose a fixed mirror. */
static const int32 _saturn_language = LANGUAGE_EN;

/* Decomp .c L12 — Update (empty). */
void UIWidgets_Update(void) {}

/* Decomp .c L14 — LateUpdate (empty). */
void UIWidgets_LateUpdate(void) {}

/* Decomp .c L16-24 — StaticUpdate.
 * Non-Plus path: increments the global timer mod 0x8000. The buttonColor
 * cycling on L22 gates on MANIA_USE_PLUS so it doesn't fire here. The
 * timer is used by DrawRectOutline_Flash (L115) to phase the outline
 * colour through the palette ramp. */
void UIWidgets_StaticUpdate(void)
{
    if (!UIWidgets) return;
    ++UIWidgets->timer;
    UIWidgets->timer &= 0x7FFF;
}

/* Decomp .c L26 — Draw (empty). */
void UIWidgets_Draw(void) {}

/* Decomp .c L28 — Create (empty). */
void UIWidgets_Create(void *data)
{
    (void)data;
}

/* Decomp .c L30-69 — StageLoad. Loads the per-stage UI atlases + SFX
 * globals. On Saturn the LoadSpriteAnimation map lookup falls through
 * to -1 if the asset isn't on the CD, which is the Phase 3.2.b
 * baseline behaviour pending Phase 3.0-prep's menu-asset bootstrap. */
void UIWidgets_StageLoad(void)
{
    if (!UIWidgets) return;

    UIWidgets->active = ACTIVE_ALWAYS;

    /* Decomp L34 — UI/UIElements.bin atlas (UIElements). */
    UIWidgets->uiFrames =
        (uint16)rsdk_load_sprite_animation("UI/UIElements.bin", -1);

    /* Decomp L38 — UI/SmallFont.bin atlas. */
    UIWidgets->fontFrames =
        (uint16)rsdk_load_sprite_animation("UI/SmallFont.bin", -1);

    /* Decomp L40 — language-specific text atlas. */
    UIWidgets_ApplyLanguage();

    /* Decomp L41-42 — animator templates for the shared frame / arrows
     * sprite sheets. Phase 3.2.b ships the SetSpriteAnimation call so
     * child widgets can copy from these animator records. */
    rsdk_set_sprite_animation((int)UIWidgets->uiFrames, 1,
                              &UIWidgets->frameAnimator, true, 0);
    rsdk_set_sprite_animation((int)UIWidgets->uiFrames, 2,
                              &UIWidgets->arrowsAnimator, true, 0);

    /* Decomp L44-49 — SFX globals. rsdk_get_sfx returns the Saturn-side
     * SFX-table slot; the Phase 3.0-prep menu audio bootstrap registers
     * the Mania menu sound effects. */
    UIWidgets->sfxBleep  = (uint16)rsdk_get_sfx("Global/MenuBleep.wav");
    UIWidgets->sfxAccept = (uint16)rsdk_get_sfx("Global/MenuAccept.wav");
    UIWidgets->sfxWarp   = (uint16)rsdk_get_sfx("Global/SpecialWarp.wav");
    UIWidgets->sfxEvent  = (uint16)rsdk_get_sfx("Special/Event.wav");
    UIWidgets->sfxWoosh  = (uint16)rsdk_get_sfx("Global/MenuWoosh.wav");
    UIWidgets->sfxFail   = (uint16)rsdk_get_sfx("Stage/Fail.wav");

    /* Decomp L51-67 — buttonColors[] palette ramp. Gated on
     * MANIA_USE_PLUS in the upstream source; the non-Plus build never
     * fills this table (it stays zero from BSS init) and the Plus-only
     * UIWidgets_StaticUpdate buttonColor cycling never reads it. Saturn
     * is pinned non-Plus so we leave the table zero-initialised. */
}

/* Decomp .c L71-89 — ApplyLanguage. Loads the per-language text atlas.
 * Saturn NTSC-US ship is LANGUAGE_EN-only; the other branches still
 * port mechanically but fall through to rsdk_load_sprite_animation
 * which returns -1 if the file isn't on the CD. */
void UIWidgets_ApplyLanguage(void)
{
    if (!UIWidgets) return;

    switch (_saturn_language) {
        case LANGUAGE_EN:
            UIWidgets->textFrames =
                (uint16)rsdk_load_sprite_animation("UI/TextEN.bin", -1);
            break;
        case LANGUAGE_FR:
            UIWidgets->textFrames =
                (uint16)rsdk_load_sprite_animation("UI/TextFR.bin", -1);
            break;
        case LANGUAGE_IT:
            UIWidgets->textFrames =
                (uint16)rsdk_load_sprite_animation("UI/TextIT.bin", -1);
            break;
        case LANGUAGE_GE:
            UIWidgets->textFrames =
                (uint16)rsdk_load_sprite_animation("UI/TextGE.bin", -1);
            break;
        case LANGUAGE_SP:
            UIWidgets->textFrames =
                (uint16)rsdk_load_sprite_animation("UI/TextSP.bin", -1);
            break;
        case LANGUAGE_JP:
            UIWidgets->textFrames =
                (uint16)rsdk_load_sprite_animation("UI/TextJP.bin", -1);
            break;
        case LANGUAGE_KO:
            UIWidgets->textFrames =
                (uint16)rsdk_load_sprite_animation("UI/TextKO.bin", -1);
            break;
        case LANGUAGE_SC:
            UIWidgets->textFrames =
                (uint16)rsdk_load_sprite_animation("UI/TextSC.bin", -1);
            break;
        case LANGUAGE_TC:
            UIWidgets->textFrames =
                (uint16)rsdk_load_sprite_animation("UI/TextTC.bin", -1);
            break;
        default:
            break;
    }
}

/* === Drawing helpers ================================================
 *
 * Decomp uses RSDK.DrawRect / DrawLine / DrawFace — VDP1 polygon-class
 * primitives. Saturn-side ports of these polygon emitters land
 * alongside Phase 3.2.c's UIButton port (the VDP1 polygon command
 * queue extension). For Phase 3.2.b the helpers are surfaced with
 * decomp-faithful signatures so child widget classes (UIButton,
 * UIShifter) compile cleanly. Each body records the decomp call
 * sequence inline for the 3.2.c follow-up. */

/* === Phase 3.2.c.1 (Task #148) — decomp-faithful draw helpers ========
 *
 * Each helper now wires the rsdk_draw_rect / rsdk_draw_line /
 * rsdk_draw_face VDP1 polygon emitter primitives (src/rsdk/drawing.c).
 * The implementations mirror decomp UIWidgets.c verbatim — only the
 * RSDK.* call names rewrite to the Saturn-side rsdk_* primitives. */

/* Decomp .c L91-100 — DrawRectOutline_Black. Four-side black border. */
void UIWidgets_DrawRectOutline_Black(int32 x, int32 y, int32 width, int32 height)
{
    int32 w = (width  << 16) >> 1;
    int32 h = (height << 16) >> 1;
    rsdk_draw_rect(x - w,                  y - h,
                   width << 16,            3 << 16,
                   0x000000, 0xFF, INK_NONE, false);
    rsdk_draw_rect(x - w,                  y - h,
                   3 << 16,                height << 16,
                   0x000000, 0xFF, INK_NONE, false);
    rsdk_draw_rect(x - w,                  h + y - (3 << 16),
                   width << 16,            3 << 16,
                   0x000000, 0xFF, INK_NONE, false);
    rsdk_draw_rect(x + w - (3 << 16),      y - h,
                   3 << 16,                height << 16,
                   0x000000, 0xFF, INK_NONE, false);
}

/* Decomp .c L101-110 — DrawRectOutline_Blended. */
void UIWidgets_DrawRectOutline_Blended(int32 x, int32 y, int32 width, int32 height)
{
    int32 w = (width  << 16) >> 1;
    int32 h = (height << 16) >> 1;
    rsdk_draw_rect(x - w + (3 << 16),      y - h,
                   (width << 16) - (6 << 16), 3 << 16,
                   0x000000, 0xFF, INK_BLEND, false);
    rsdk_draw_rect(x - w,                  y - h,
                   3 << 16,                height << 16,
                   0x000000, 0xFF, INK_BLEND, false);
    rsdk_draw_rect(x - w + (3 << 16),      h + y - (3 << 16),
                   (width << 16) - (6 << 16), 3 << 16,
                   0x000000, 0xFF, INK_BLEND, false);
    rsdk_draw_rect(x + w - (3 << 16),      y - h,
                   3 << 16,                height << 16,
                   0x000000, 0xFF, INK_BLEND, false);
}

/* Decomp .c L111-121 — DrawRectOutline_Flash. Cycling outline colour
 * driven by UIWidgets->timer phase against palette entry (3, idx&0xF).
 * Saturn-side: GetPaletteEntry isn't fully ported in 3.2.c.1; we use
 * a synthesized white-ramp colour driven by the timer phase so the
 * outline animates even before the palette accessor lands. */
void UIWidgets_DrawRectOutline_Flash(int32 x, int32 y, int32 width, int32 height)
{
    int32 w = (width  << 16) >> 1;
    int32 h = (height << 16) >> 1;
    /* Phase-driven white ramp — 16 brightness levels matching the
     * decomp's (timer>>1) & 0xF idx. */
    uint8_t brightness = (uint8_t)(0x80 + ((UIWidgets ? UIWidgets->timer : 0) >> 1) * 0x08);
    uint32_t flash_rgb = ((uint32_t)brightness << 16) |
                        ((uint32_t)brightness <<  8) |
                         (uint32_t)brightness;
    rsdk_draw_rect(x - w,                  y - h,
                   width << 16,            3 << 16,
                   flash_rgb, 0xFF, INK_NONE, false);
    rsdk_draw_rect(x - w,                  y - h,
                   3 << 16,                height << 16,
                   flash_rgb, 0xFF, INK_NONE, false);
    rsdk_draw_rect(x - w,                  h + y - (3 << 16),
                   width << 16,            3 << 16,
                   flash_rgb, 0xFF, INK_NONE, false);
    rsdk_draw_rect(x + w - (3 << 16),      y - h,
                   3 << 16,                height << 16,
                   flash_rgb, 0xFF, INK_NONE, false);
}

/* Decomp .c L122-163 — DrawRightTriangle. Three-vertex triangle via
 * RSDK.DrawFace. Vertex layout depends on sign of `size`:
 *   size > 0 — right angle at (x,y), other verts at (x+size,y) and
 *              (x, y+size).
 *   size < 0 — right angle at (x,y), other verts at (x+size,y) [left]
 *              and (x, y+size) [down] (flipped diagonal). */
void UIWidgets_DrawRightTriangle(int32 x, int32 y, int32 size,
                                 int32 red, int32 green, int32 blue)
{
    if (size == 0) return;

    rsdk_face_vertex_t verts[3];
    verts[0].x = x;  verts[0].y = y;
    verts[1].x = x;  verts[1].y = y;
    verts[2].x = x;  verts[2].y = y;

    if (size < 0) {
        verts[2].x = (size << 16) + x;
        verts[0].y = (size << 16) + y;
    } else {
        verts[1].x = (size << 16) + x;
        verts[2].y = (size << 16) + y;
    }

    /* decomp L151-160 — non-editor path subtracts ScreenInfo->position
     * before submission. */
    int32 sx = ScreenInfo ? (ScreenInfo->position.x << 16) : 0;
    int32 sy = ScreenInfo ? (ScreenInfo->position.y << 16) : 0;
    for (int i = 0; i < 3; ++i) {
        verts[i].x -= sx;
        verts[i].y -= sy;
    }

    rsdk_draw_face(verts, 3, (uint8_t)red, (uint8_t)green, (uint8_t)blue,
                   0xFF, INK_NONE);
}

/* Decomp .c L164-201 — DrawEquilateralTriangle. */
void UIWidgets_DrawEquilateralTriangle(int32 x, int32 y, int32 size,
                                       uint8 sizeMode,
                                       int32 red, int32 green, int32 blue,
                                       InkEffects ink)
{
    if (!sizeMode) return;

    rsdk_face_vertex_t verts[3];
    verts[0].x = x;  verts[0].y = y;
    verts[1].x = x;  verts[1].y = y;
    verts[2].x = x;  verts[2].y = y;

    if (sizeMode == 1) {
        verts[0].x = x - (size << 16);
        verts[1].x = x + (size << 16);
        verts[2].y = y + (size << 16);
    }

    int32 sx = ScreenInfo ? (ScreenInfo->position.x << 16) : 0;
    int32 sy = ScreenInfo ? (ScreenInfo->position.y << 16) : 0;
    for (int i = 0; i < 3; ++i) {
        verts[i].x -= sx;
        verts[i].y -= sy;
    }

    rsdk_draw_face(verts, 3, (uint8_t)red, (uint8_t)green, (uint8_t)blue,
                   0xFF, ink);
}

/* Decomp .c L202-246 — DrawParallelogram. Four-vertex polygon via
 * RSDK.DrawFace. The edgeSize parameter skews the top/bottom edges. */
void UIWidgets_DrawParallelogram(int32 x, int32 y, int32 width, int32 height,
                                 int32 edgeSize,
                                 int32 red, int32 green, int32 blue)
{
    rsdk_face_vertex_t verts[4];

    verts[0].x = x - (width  << 15);
    verts[0].y = y - (height << 15);
    verts[1].x = x + (width  << 15);
    verts[1].y = y - (height << 15);
    verts[2].x = verts[1].x;
    verts[2].y = y + (height << 15);
    verts[3].x = verts[0].x;
    verts[3].y = y + (height << 15);

    if ((edgeSize << 16) <= 0) {
        verts[0].x -= edgeSize << 16;
        verts[2].x += edgeSize << 16;
    } else {
        verts[1].x += edgeSize << 16;
        verts[3].x -= edgeSize << 16;
    }

    int32 sx = ScreenInfo ? (ScreenInfo->position.x << 16) : 0;
    int32 sy = ScreenInfo ? (ScreenInfo->position.y << 16) : 0;
    for (int i = 0; i < 4; ++i) {
        verts[i].x -= sx;
        verts[i].y -= sy;
    }

    rsdk_draw_face(verts, 4, (uint8_t)red, (uint8_t)green, (uint8_t)blue,
                   0xFF, INK_NONE);
}

/* Decomp .c L247-260 — DrawUpDownArrows. Draws two arrow sprites from
 * UIWidgets->arrowsAnimator (frameID 2 up, frameID 3 down) at +/-
 * arrowDist around (x, y). */
void UIWidgets_DrawUpDownArrows(int32 x, int32 y, int32 arrowDist)
{
    if (!UIWidgets) return;
    /* Decomp L249-251: drawPos initialised to (x, y); arrowsAnimator
     * frameID swapped to 2 (up) then 3 (down); DrawSprite each. */
    UIWidgets->arrowsAnimator.frame_id = 2;
    int32 up_y = y - (arrowDist << 15);
    rsdk_draw_sprite(&UIWidgets->arrowsAnimator, x, up_y,
                     0 /* FLIP_NONE */, false);

    UIWidgets->arrowsAnimator.frame_id = 3;
    int32 dn_y = up_y + (arrowDist << 16);
    rsdk_draw_sprite(&UIWidgets->arrowsAnimator, x, dn_y,
                     0 /* FLIP_NONE */, false);
}

/* Decomp .c L261-275 — DrawLeftRightArrows. Mirrors DrawUpDownArrows on
 * the horizontal axis (arrowsAnimator frameID 0 left, 1 right). */
void UIWidgets_DrawLeftRightArrows(int32 x, int32 y, int32 arrowDist)
{
    if (!UIWidgets) return;
    UIWidgets->arrowsAnimator.frame_id = 0;
    int32 left_x = x - (arrowDist >> 1);
    rsdk_draw_sprite(&UIWidgets->arrowsAnimator, left_x, y,
                     0 /* FLIP_NONE */, false);

    UIWidgets->arrowsAnimator.frame_id = 1;
    int32 right_x = left_x + arrowDist;
    rsdk_draw_sprite(&UIWidgets->arrowsAnimator, right_x, y,
                     0 /* FLIP_NONE */, false);
}

/* Decomp .c L276-286 — DrawTriJoinRect. Composes two opposing right-
 * triangles to form the diagonal-edged rectangle used by save-slot /
 * shift buttons. Returns the next draw position (x + TO_FIXED(14), y). */
Vector2 UIWidgets_DrawTriJoinRect(int32 x, int32 y,
                                  color leftColor, color rightColor)
{
    Vector2 newPos;
    newPos.x = x + (14 << 16);   /* TO_FIXED(14) */
    newPos.y = y;

    UIWidgets_DrawRightTriangle(x, newPos.y, 13,
                                (int32)((leftColor >> 16) & 0xFF),
                                (int32)((leftColor >>  8) & 0xFF),
                                (int32)((leftColor)       & 0xFF));
    UIWidgets_DrawRightTriangle(newPos.x, newPos.y + (12 << 16), -13,
                                (int32)((rightColor >> 16) & 0xFF),
                                (int32)((rightColor >>  8) & 0xFF),
                                (int32)((rightColor)       & 0xFF));
    return newPos;
}
