/* Phase 3.2.a (Task #145) — UIBackground class body.
 *
 * Mechanical port of `tools/_decomp_raw/SonicMania_Objects_Menu_UIBackground.c`
 * (91 LOC; Christian Whitehead/Simon Thomley/Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich). Each function below cites the decomp
 * line range it mirrors.
 *
 * Per-line decomp citations:
 *   .c L12-17    UIBackground_Update         -> UIBackground_Update
 *   .c L19       UIBackground_LateUpdate     -> UIBackground_LateUpdate
 *   .c L21       UIBackground_StaticUpdate   -> UIBackground_StaticUpdate
 *   .c L23-28    UIBackground_Draw           -> UIBackground_Draw
 *   .c L30-40    UIBackground_Create         -> UIBackground_Create
 *   .c L42       UIBackground_StageLoad      -> UIBackground_StageLoad
 *   .c L44-70    UIBackground_DrawNormal     -> UIBackground_DrawNormal
 *
 * Saturn-side stubs (Phase 3.2.a foundation):
 *   * `RSDK.FillScreen` / `RSDK.DrawCircle` / `RSDK.DrawCircleOutline` map
 *     to Saturn-side rsdk_fill_screen / rsdk_draw_circle / rsdk_draw_
 *     circle_outline once the Phase A4 Drawing layer lands those
 *     primitives (Phase 3.2.c). Until then DrawNormal is a no-op so the
 *     entity's per-frame Draw doesn't crash with an undefined RSDK
 *     symbol. The menu scene's prior MVP overlay (slPrint via
 *     MenuSetup.c) continues to render on top.
 *   * `RSDK.Sin512` / `Cos512` / `Sin256` / `Cos256` map to upstream
 *     trig tables; not yet ported. Animated circle motion deferred.
 *
 * Phase 3.2.a scope:  this class is REGISTERED so Menu/Scene1.bin's 27
 * UIBackground entity hashes resolve cleanly (vs the prior "skip
 * unresolved" path that silently dropped 36 of 41 classes). Drawing
 * fidelity lands in 3.2.b/c per docs/menusetup_decomposition_plan.md. */

#include "UIBackground.h"
#include "../../../rsdk/object.h"
#include "../../../rsdk/drawing.h"
#include "../../../rsdk/math.h"

ObjectUIBackground *UIBackground = NULL;

/* Decomp .c L12-17 — Update. Increments the per-entity tick counter that
 * UIBackground_DrawNormal uses as the phase for its Sin/Cos circle
 * motion. We mirror verbatim. */
void UIBackground_Update(void)
{
    RSDK_THIS(UIBackground);
    ++self->timer;
}

/* Decomp .c L19. */
void UIBackground_LateUpdate(void) {}

/* Decomp .c L21. */
void UIBackground_StaticUpdate(void) {}

/* Decomp .c L23-28 — Draw. */
void UIBackground_Draw(void)
{
    RSDK_THIS(UIBackground);
    StateMachine_Run(self->stateDraw);
}

/* Decomp .c L30-40 — Create. */
void UIBackground_Create(void *data)
{
    (void)data;
    RSDK_THIS(UIBackground);

    if (!SceneInfo->inEditor) {
        self->active    = ACTIVE_NORMAL;
        self->visible   = 1;
        self->drawGroup = 0;
        self->stateDraw = UIBackground_DrawNormal;
    }
}

/* Decomp .c L42 — StageLoad.
 *   void UIBackground_StageLoad(void) {
 *       UIBackground->activeColors = UIBackground->bgColors;
 *   } */
void UIBackground_StageLoad(void)
{
    if (!UIBackground) return;
    UIBackground->activeColors = UIBackground->bgColors;
}

/* Decomp .c L44-70 — DrawNormal. Renders the animated circular backdrop
 * (FillScreen + two concentric DrawCircleOutline + six DrawCircle calls
 * driven by Sin512/Cos512 phase on self->timer).
 *
 * Phase 3.2.b port: mechanical translation of decomp UIBackground.c:44-70
 * onto the Saturn-side rsdk_fill_screen + rsdk_draw_circle +
 * rsdk_draw_circle_outline primitives that land in this same iteration
 * (src/rsdk/drawing.{c,h} extensions).
 *
 * Per-line citations against tools/_decomp_raw/SonicMania_Objects_Menu_
 * UIBackground.c:
 *   L48-49 — colorPtrs alias of UIBackground->activeColors
 *   L51    — RSDK.FillScreen(colorPtrs[0], 0xFF, 0xFF, 0xFF)
 *   L53-54 — drawPos from Sin512/Sin256/Cos256 phase
 *   L55-56 — DrawCircleOutline (centre, +108..+116 radius band)
 *   L58    — DrawCircle (drawPos + centre, r=32, colorPtrs[1])
 *   L59    — DrawCircle (-drawPos + centre, r=16, colorPtrs[1])
 *   L60    — DrawCircle (drawPos + centre, r=26, colorPtrs[1])
 *   L62-63 — drawPos from Cos512/Cos256/Sin256 phase
 *   L64-65 — DrawCircleOutline (centre, +140..+148 radius band)
 *   L67    — DrawCircle (drawPos + centre, r=32, colorPtrs[2])
 *   L68    — DrawCircle (-drawPos + centre, r=16, colorPtrs[2])
 *   L69    — DrawCircle (drawPos + centre, r=26, colorPtrs[0])
 *
 * Saturn-side simplifications:
 *   * `screenRelative=true` -> primitives interpret coords as screen-
 *     pixel ints directly (no fixed-point conversion). The decomp's
 *     `ScreenInfo->center.x/y` are already pixel ints (decomp Drawing.cpp
 *     §3.1 constants); Saturn-side ScreenInfo mirror uses the same.
 *   * INK_NONE + alpha=0xFF route through the solid path on the Saturn
 *     primitives — no blend lookup pass needed (Phase Z follow-up if
 *     blended menus ever ship). */
void UIBackground_DrawNormal(void)
{
    RSDK_THIS(UIBackground);

    if (!UIBackground || !UIBackground->activeColors || !ScreenInfo) return;

    color *colorPtrs = UIBackground->activeColors;

    /* decomp L51 — full-screen back-colour fill. */
    rsdk_fill_screen(colorPtrs[0], 0xFF, 0xFF, 0xFF);

    int32 cx = ScreenInfo->center.x;
    int32 cy = ScreenInfo->center.y;
    int32 t  = self->timer;

    /* decomp L53-54: drawPos.x = ((Sin512(t)>>3) + 112) * Sin256(t) >> 8.
     * Saturn trig tables already match the upstream range. The decomp
     * Sin512 returns a value in [-0x200..+0x200]; >>3 narrows to
     * [-64..+64]; +112 keeps the radius in [48..176]; * Sin256 (range
     * [-0x100..+0x100]) >>8 normalises back to roughly pixel scale. */
    int32 amp1 = ((rsdk_sin512(t) >> 3) + 112);
    int32 dx1  = (amp1 * rsdk_sin256(t)) >> 8;
    int32 dy1  = (amp1 * rsdk_cos256(t)) >> 8;

    /* decomp L55-56 — inner annular ring around the centre. */
    int32 r1_inner = (rsdk_sin512(t) >> 3) + 108;
    int32 r1_outer = (rsdk_sin512(t) >> 3) + 116;
    rsdk_draw_circle_outline(cx, cy, r1_inner, r1_outer, colorPtrs[1]);

    /* decomp L58-60 — three filled satellites at +drawPos / -drawPos. */
    rsdk_draw_circle(cx + dx1, cy + dy1, 32, colorPtrs[1]);
    rsdk_draw_circle(cx - dx1, cy - dy1, 16, colorPtrs[1]);
    rsdk_draw_circle(cx + dx1, cy + dy1, 26, colorPtrs[1]);

    /* decomp L62-63: drawPos.x = ((Cos512(t)>>3) + 144) * Cos256(t) >> 8. */
    int32 amp2 = ((rsdk_cos512(t) >> 3) + 144);
    int32 dx2  = (amp2 * rsdk_cos256(t)) >> 8;
    int32 dy2  = (amp2 * rsdk_sin256(t)) >> 8;

    /* decomp L64-65 — outer annular ring. */
    int32 r2_inner = (rsdk_cos512(t) >> 3) + 140;
    int32 r2_outer = (rsdk_cos512(t) >> 3) + 148;
    rsdk_draw_circle_outline(cx, cy, r2_inner, r2_outer, colorPtrs[2]);

    /* decomp L67-69 — three more satellites; last uses colorPtrs[0]. */
    rsdk_draw_circle(cx + dx2, cy + dy2, 32, colorPtrs[2]);
    rsdk_draw_circle(cx - dx2, cy - dy2, 16, colorPtrs[2]);
    rsdk_draw_circle(cx + dx2, cy + dy2, 26, colorPtrs[0]);
}
