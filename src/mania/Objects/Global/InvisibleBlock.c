/* ---------------------------------------------------------------------
 * Phase 2.4g.1 (Task #153) — InvisibleBlock object.
 *
 * Mechanical port of
 * `tools/_decomp_raw/SonicMania_Objects_Global_InvisibleBlock.c`.
 * Original: Christian Whitehead / Simon Thomley / Hunter Bridges;
 * decomp by Rubberduckycooly & RMGRich.
 *
 * FIRST GHZ entity on the RSDK entity engine per
 * memory/ghz-pivot-to-rsdk-engine.md. Translation notes vs decomp:
 *
 *   - `foreach_active(Player, player)` (decomp L16): the Saturn build
 *     has exactly one player (the bespoke `player_t g_ghz_player`,
 *     pinned via `g_ghz_player_addr`, valid once `g_ghz_player_ready`).
 *     There is no RSDK Player entity / Player slot group yet, so the
 *     loop collapses to a single guarded reference. When the full
 *     EntityPlayer migration lands (later phase) this becomes a real
 *     foreach_active over the Player group.
 *
 *   - `player->isChibi` (decomp L18): no Encore/chibi mode on Saturn —
 *     treated as 0, so the `!noChibi || !isChibi` clause is always true.
 *
 *   - `Player_CheckCollisionBox(player, self, &self->hitbox)` (decomp
 *     L19): the Saturn signature is
 *     `Player_CheckCollisionBox(player_t*, int box_x_px, int box_y_px,
 *     const hitbox_t*)` — integer-pixel box center (Player.h:241). The
 *     decomp passes the entity `self`; we pass `self->position` shifted
 *     from Q16.16 to pixels. The contact side enum (C_TOP/LEFT/RIGHT/
 *     BOTTOM) and the snap-back + axis-velocity-zero are identical to
 *     decomp (Player.c:2267-2341 / Collision.cpp:276-487 setValues).
 *
 *   - `self->visible = DebugMode->debugActive` (decomp L45): no debug
 *     overlay on Saturn -> visible stays false (the block is, by
 *     definition, invisible in normal play; the decomp only draws the
 *     ItemBox debug sprite when DebugMode is active).
 *
 *   - `Zone->objectDrawGroup[1]` (decomp L72) -> Saturn has no Zone
 *     object yet (lands at 2.4g.2); drawGroup defaults to 0. Since
 *     visible is always false on Saturn the drawGroup is inert.
 * ------------------------------------------------------------------- */

#include "InvisibleBlock.h"

ObjectInvisibleBlock *InvisibleBlock = NULL;

/* Bespoke single-player handle (Game.c). `g_ghz_player_addr` is a
 * link-time-constant pointer to the static `g_ghz_player` (never reseated),
 * so reading it cross-TU is safe under LTO.
 *
 * We deliberately do NOT read the `volatile bool g_ghz_player_ready` flag
 * here: per memory/sync-load-eliminates-cross-tu-volatile.md (BINDING,
 * Phase 2.3j), a cross-TU `volatile bool g_xxx_ready` gate is unreliable
 * under GCC 8.2 whole-program LTO (the reader CSE's a stale 0x00). The
 * rule's prescribed signal is the same-TU state-machine variable that
 * advances PAST the load — exposed here via `mania_is_ghz_active()`
 * (Game.h:250 -> reads `s_ts_state == TS_GHZ_ACTIVE` inside Game.c's own
 * TU). InvisibleBlock entities only exist after `mania_load_ghz_scene`
 * has fully run (which initialises `g_ghz_player`), so gating on the
 * GHZ-active window is sufficient and LTO-safe. */
extern player_t *const g_ghz_player_addr;

/* QA landmarks (Phase 2.4g.1 gate P4). `used` + non-static so LTO keeps
 * a stable linker-map address the savestate gate can peek:
 *   g_ghz_invblock_spawned = count of InvisibleBlock entities created
 *                            into RSDK slots this scene.
 *   g_ghz_invblock_collv   = snapshot of the player's collisionFlagV the
 *                            last frame any InvisibleBlock raised a bit.
 * Never read by gameplay — diagnostics only. */
__attribute__((used)) volatile int g_ghz_invblock_spawned = 0;
__attribute__((used)) volatile int g_ghz_invblock_collv   = 0;

/* __attribute__((used)) defeats GCC 8.2 whole-program LTO symbol
 * elimination so the Phase 2.4g.1 gate (tools/qa_phase2_4g1_gate.py P1)
 * can locate the registered Update/Create callbacks by name in game.map.
 * Same precedent as memory/entity-atlas-loader-pattern.md (the entity
 * atlas globals are `used` for the very same reason — every object
 * callback is otherwise reached only via a function pointer stored in the
 * RSDK class table, which LTO internalises/renames out of the map). */
__attribute__((used)) void InvisibleBlock_Update(void)
{
    RSDK_THIS(InvisibleBlock);

    /* foreach_active(Player, player) -> single bespoke player. Gated on the
     * GHZ-active window (same-TU state var via mania_is_ghz_active), NOT a
     * cross-TU volatile ready flag (see header note + sync-load memory). */
    if (mania_is_ghz_active()) {
        player_t *player = g_ghz_player_addr;

        if ((self->planeFilter <= 0
             || player->collisionPlane == (uint8)(((uint8)self->planeFilter - 1) & 1))
            /* (!noChibi || !isChibi): isChibi==0 on Saturn -> always true */) {

            int box_x = self->position.x >> 16;
            int box_y = self->position.y >> 16;

            switch (Player_CheckCollisionBox(player, box_x, box_y, &self->hitbox)) {
                case C_TOP:
                    if (!self->noCrush)
                        player->collisionFlagV |= 1;
                    break;

                case C_LEFT:
                    if (!self->noCrush)
                        player->collisionFlagH |= 1;
                    break;

                case C_RIGHT:
                    if (!self->noCrush)
                        player->collisionFlagH |= 2;
                    break;

                case C_BOTTOM:
                    if (!self->noCrush)
                        player->collisionFlagV |= 2;
                    break;

                default: break;
            }

            g_ghz_invblock_collv = player->collisionFlagV;
        }
    }

    /* DebugMode->debugActive is always false on Saturn. */
    self->visible = false;
}

void InvisibleBlock_LateUpdate(void) {}

void InvisibleBlock_StaticUpdate(void) {}

void InvisibleBlock_Draw(void)
{
    /* Decomp draws the ItemBox debug sprite only when DebugMode is
     * active; no debug overlay on Saturn, so this is a no-op. */
}

__attribute__((used)) void InvisibleBlock_Create(void *data)
{
    RSDK_THIS(InvisibleBlock);
    (void)data;

    /* SceneInfo->inEditor is always false on Saturn (Game.h ManiaSceneInfo). */

    /* timeAttackOnly destroy path: no Time Attack mode on Saturn -> a
     * timeAttackOnly block simply never participates. We honour the
     * decomp intent by leaving it inactive rather than destroying the
     * slot (the slot is owned by the scene loader). */
    self->visible = false;
    self->active  = self->activeNormal ? ACTIVE_NORMAL : ACTIVE_BOUNDS;

    self->updateRange.x = (self->width + 5) << 19;
    self->updateRange.y = (self->height + 5) << 19;

    self->hitbox.right  = 8 * self->width + 8;
    self->hitbox.left   = -self->hitbox.right;
    self->hitbox.bottom = 8 * self->height + 8;
    self->hitbox.top    = -self->hitbox.bottom;

    self->drawGroup = 0; /* Zone->objectDrawGroup[1] lands at 2.4g.2 */

    ++g_ghz_invblock_spawned;
}

void InvisibleBlock_StageLoad(void)
{
    /* Decomp loads "Global/ItemBox.bin" (frameID 10) purely as the
     * DebugMode visualisation sprite. With no debug overlay on Saturn
     * there is nothing to load — keeping this a no-op also means the
     * all-class StageLoad pass cannot trigger any GHZ/title asset I/O
     * from this class (Phase 2.4g.1 zero-boot-cost guarantee). */
}
