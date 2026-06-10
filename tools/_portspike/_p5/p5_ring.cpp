// =============================================================================
// p5_ring.cpp -- FLAT Ring TU for the P5 first-object proof (Task #204).
//
// Compiles the VERBATIM decomp bodies of Objects/Global/Ring.c (Ring_Update,
// Ring_Draw, Ring_State_LostFX, Ring_Draw_Normal) against the flat game-API in
// p5_gameapi.h. The bodies are byte-for-byte the decomp (only the surrounding
// `extern "C"` linkage spec is added so p5_main.cpp can name the two registered
// entry points Ring_Update / Ring_Draw). NOTHING here includes an engine header
// -- the engine's `namespace RSDK` and this TU's `RSDK` function-table object
// never meet, which is the whole point of the two-TU split.
//
// The `RSDK` table is file-scope static (internal linkage): the only caller is
// the Ring bodies in THIS TU, so static both prevents any link collision with an
// engine symbol and lets the bodies write `RSDK.ProcessAnimation(...)` verbatim.
// Its slots point at the extern "C" bridges in p5_main.cpp -- ProcessAnimation
// routes to the REAL Animation.cpp (the cadence proof); DrawSprite routes to the
// witness shim (the draw is COUNTED, not rasterized -- Drawing.cpp is bypassed).
//
// Witnesses: 9 C-linkage int32 VALUE globals the gate peeks from a Mednafen
// savestate via the .map. p5_w_magic is a const sentinel the gate uses to self-
// calibrate WRAM byte order; p5.linker pins this TU's .bss to WRAM-H so every
// numeric witness shares the magic's byte order. Saturn-only P5 scaffolding.
// =============================================================================
#include "p5_gameapi.h"
#include <string.h>

// ---- The game-side function table (internal linkage; bridges live in main) --
static RSDKFunctionTable RSDK = { p5_bridge_proc_anim, p5_bridge_draw_sprite };

// ---- The Ring static-object instance + the `Ring` pointer the bodies use ----
static ObjectRing g_ringStatic;
ObjectRing *Ring = &g_ringStatic;

// ---- 9 witnesses (C linkage; names match qa_p5_ring.py exactly) ------------
extern "C" {
__attribute__((used)) extern const int32 p5_w_magic = 0x12345678;
__attribute__((used)) int32 p5_w_ticks         = 0;
__attribute__((used)) int32 p5_w_draw_calls    = 0; // written by p5_main bridge
__attribute__((used)) int32 p5_w_last_frameid  = 0; // written by p5_main bridge
__attribute__((used)) int32 p5_w_ring_classid  = 0;
__attribute__((used)) int32 p5_w_ring_timer    = 0;
__attribute__((used)) int32 p5_w_ring_scalex   = 0;
__attribute__((used)) int32 p5_w_ring_vely     = 0;
__attribute__((used)) int32 p5_w_ring_posy     = 0;
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
// P5 harness entry points (extern "C", consumed by p5_main.cpp).
// =============================================================================
extern "C" {

// The staticVars slot the engine RegisterObject stores: a pointer to the `Ring`
// pointer (Object** view). The staticUpdate loop derefs (*staticVars)->active ==
// g_ringStatic.active (Object base @2, zero-init -> ACTIVE_NEVER -> skipped).
void *p5_ring_staticvars_slot = (void *)&Ring;

// EntityRing slot size the engine guard checks (<= sizeof(EntityBase)=344).
unsigned p5_ring_entity_size = (unsigned)sizeof(EntityRing);

// destroyEntity target: unreachable at N=40 (timer reaches 40, never > 64), but
// must compile/link. Faithfully clears the engine-visible classID.
void p5_destroy_entity(void *entity)
{
    ((EntityRing *)entity)->classID = 0;
}

// Spawn one Ring entity into the given objectEntityList slot, in Ring_State_LostFX
// with a live 8-frame animator (speed 0x60, duration 0x100) -- the exact initial
// conditions qa_p5_ring.py's witness model was computed from.
void p5_ring_spawn(void *slot, void *frames)
{
    EntityRing *e = (EntityRing *)slot;
    memset(e, 0, sizeof(EntityRing));
    e->classID   = 1;          // @54  -> stageObjectIDs[1] -> Ring class
    e->active    = 2;          // @76  ACTIVE_NORMAL (Object.hpp:115) -> inRange
    e->visible   = 1;          // @85
    e->drawGroup = 3;          // @79  bounded draw group (no tile layers)
    // interaction left 0 -> ProcessObjects skips the typeGroups indexing pass.
    e->state     = Ring_State_LostFX;
    e->stateDraw = Ring_Draw_Normal;
    e->animator.frames        = frames;
    e->animator.frameID       = 0;
    e->animator.speed         = 0x60;
    e->animator.timer         = 0;
    e->animator.frameDuration = 0x100;
    e->animator.frameCount    = 8;
    e->animator.loopIndex     = 0;
}

// Read the post-loop entity state into the witnesses the gate compares.
void p5_ring_capture(void *slot)
{
    EntityRing *e   = (EntityRing *)slot;
    p5_w_ring_classid = (int32)e->classID;
    p5_w_ring_timer   = e->timer;
    p5_w_ring_scalex  = e->scale.x;
    p5_w_ring_vely    = e->velocity.y;
    p5_w_ring_posy    = e->position.y;
}

} // extern "C"
