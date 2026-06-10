// =============================================================================
// p6_ring2.cpp -- P6.7a (Task #210): the VERBATIM decomp Ring for the
// jo-hosted P6SCENE pack (the P6.1-era p6_ring.cpp is the STANDALONE-image
// flavor and defines io/magic witnesses that would multiple-define against
// p6_io_main.o; this TU carries ONLY the object + fresh p6_w_obj_* names).
//
// Bodies are byte-for-byte Objects/Global/Ring.c (Ring_Update, Ring_Draw,
// Ring_State_LostFX, Ring_Draw_Normal) against the flat game-API in
// p6_gameapi.h -- the same two-TU split as P5/P6.1: this TU never sees an
// engine header, and its file-scope `RSDK` table dispatches through the
// extern "C" bridges p6_io_main.cpp defines, which route through the
// engine's OWN RSDKFunctionTable[] (populated by the real
// SetupFunctionTables, Core/Link.cpp -- the P6.1-proven path). DrawSprite
// is no longer a witness no-op: the table slot now carries the REAL Saturn
// RSDK::DrawSprite backend (P6.5b3) -> the falling ring is VISIBLE.
//
// P6.7a's new increment over P6.1: this object is REGISTERED through the
// engine's RegisterObject and ticked by the engine's ProcessObjects
// (Object.cpp:357-475) -- update dispatch, ACTIVE_NORMAL inRange, draw-group
// enqueue -- with Ring_Draw dispatched from the draw-group walk. The
// LostFX state is pure verbatim decomp physics (gravity +0x1800/tick,
// timer, scale), so every witness below has a closed-form model.
// =============================================================================
#include "p6_gameapi.h"
#include <string.h>

void p6_bridge_proc_anim(void *animator);
void p6_bridge_draw_sprite(void *animator, void *position, int32 screenRelative);

// ---- The game-side function table (internal linkage; bridges in p6_io_main) -
static RSDKFunctionTable RSDK = { p6_bridge_proc_anim, p6_bridge_draw_sprite };

// ---- The Ring static-object instance + the `Ring` pointer the bodies use ----
static ObjectRing g_ringStatic;
ObjectRing *Ring = &g_ringStatic;

// ---- P6.7a witnesses (C linkage; read by qa_p6_obj.py) ----------------------
extern "C" {
__attribute__((used)) int32 p6_w_obj_classcount = 0;  // engine objectClassCount after registration
__attribute__((used)) int32 p6_w_obj_classid    = 0;  // entity classID at capture (1 alive, 0 dead phase)
__attribute__((used)) int32 p6_w_obj_timer      = 0;  // verbatim LostFX ++timer
__attribute__((used)) int32 p6_w_obj_vely       = 0;  // velocity.y == 0x1800 * timer
__attribute__((used)) int32 p6_w_obj_posy       = 0;  // y0 + 0x1800 * timer*(timer+1)/2
__attribute__((used)) int32 p6_w_obj_scalex     = 0;  // 0x10 * timer (memset start)
__attribute__((used)) int32 p6_w_obj_frameid    = 0;  // entity animator frame (engine ProcessAnimation)
__attribute__((used)) int32 p6_w_obj_draws      = 0;  // Ring_Draw dispatches from the draw-group walk
__attribute__((used)) int32 p6_w_obj_spawns     = 0;  // engine CreateEntity respawns
}

// =============================================================================
// VERBATIM decomp bodies (Objects/Global/Ring.c). Wrapped extern "C" for naming.
// =============================================================================
extern "C" {

void Ring_Update(void)
{
    RSDK_THIS(Ring);
    StateMachine_Run(self->state);
}

void Ring_LateUpdate(void) {}

void Ring_StaticUpdate(void) {}

void Ring_Draw(void)
{
    RSDK_THIS(Ring);
    StateMachine_Run(self->stateDraw);
}

void Ring_State_LostFX(void)
{
    RSDK_THIS(Ring);
    self->velocity.y += 0x1800;
    self->position.x += self->velocity.x;
    self->position.y += self->velocity.y;
    RSDK.ProcessAnimation(&self->animator);
    self->scale.x += 0x10;
    self->scale.y += 0x10;
    if (++self->timer > 64)
        destroyEntity(self);
}

void Ring_Draw_Normal(void)
{
    RSDK_THIS(Ring);
    self->direction = self->animator.frameID > 8;
    RSDK.DrawSprite(&self->animator, NULL, false);
}

} // extern "C"

// =============================================================================
// Pack harness exports (extern "C", consumed by p6_io_main.cpp).
// =============================================================================
extern "C" {

// staticVars slot RegisterObject stores (Object** view; Object base zero-init
// -> active ACTIVE_NEVER -> the staticUpdate pass skips it).
void *p6_ring2_staticvars_slot = (void *)&Ring;
unsigned p6_ring2_entity_size  = (unsigned)sizeof(EntityRing);

// destroyEntity target (p6_gameapi.h:122 routes the macro here): Saturn-fit
// flat shim -- clears the engine-visible classID so ProcessObjects stops
// dispatching the slot and the pack tick's respawn check sees it free.
// (The engine's own destroyEntity macro routes to ResetEntity(
// TYPE_DEFAULTOBJECT); the flat TU cannot name engine functions -- same
// P5/P6.1 deviation, documented. The standalone p6_ring.cpp's definition
// is NOT in this pack, so no collision.)
void p6_destroy_entity(void *entity)
{
    ((EntityRing *)entity)->classID = 0;
}

// Post-CreateEntity init: the engine CreateEntity (Object.cpp:1114) memsets
// the slot + sets position/interaction/classID; Ring's create callback is
// registered NULL because the verbatim Ring_Create needs the Zone object
// (out of P6.7a scope), so the spawn-relevant lines of Ring_Create are
// applied here with citations:
//   active  ACTIVE_NORMAL  (Ring_Create: self->active = ACTIVE_BOUNDS for
//                           placed rings; LostFX rings run ACTIVE_NORMAL via
//                           Ring_State_Lost setup, Ring.c)
//   visible/drawGroup      (Ring_Create: self->visible = true; drawGroup =
//                           Zone->objectDrawGroup[1] -> 3 here, no Zone)
//   state/stateDraw        (Ring.c lost-ring setup: state = Ring_State_LostFX,
//                           stateDraw = Ring_Draw_Normal)
//   animator               points at the ENGINE-loaded Global/Ring.bin anim 0
//                           frames (SetSpriteAnimation equivalent initial
//                           conditions: speed 0x40, duration 0x100, 16 frames).
void p6_ring2_arm(void *slot, void *frames)
{
    EntityRing *e = (EntityRing *)slot;
    e->active    = 2; // ACTIVE_NORMAL (Object.hpp ActiveFlags)
    e->visible   = 1;
    e->drawGroup = 3;
    e->state     = Ring_State_LostFX;
    e->stateDraw = Ring_Draw_Normal;
    e->animator.frames        = frames;
    e->animator.frameID       = 0;
    e->animator.speed         = 0x40;  // Ring.bin anim 0 speed (qa model)
    e->animator.timer         = 0;
    e->animator.frameDuration = 0x100; // anim 0 per-frame duration
    e->animator.frameCount    = 16;
    e->animator.loopIndex     = 0;
}

// Witness copy, called from the pack tick AFTER ProcessObjects each frame.
void p6_ring2_witness(const void *slot)
{
    const EntityRing *e = (const EntityRing *)slot;
    p6_w_obj_classid = (int32)e->classID;
    if (e->classID) {
        p6_w_obj_timer   = e->timer;
        p6_w_obj_vely    = e->velocity.y;
        p6_w_obj_posy    = e->position.y;
        p6_w_obj_scalex  = e->scale.x;
        p6_w_obj_frameid = (int32)e->animator.frameID;
    }
}

} // extern "C"
