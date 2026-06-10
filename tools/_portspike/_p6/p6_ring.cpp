// =============================================================================
// p6_ring.cpp -- FLAT Ring TU for the P6.1 engine-dispatch proof (Task #205).
//
// Byte-for-byte the P5 Ring TU (#204): the VERBATIM decomp bodies of
// Objects/Global/Ring.c (Ring_Update, Ring_Draw, Ring_State_LostFX,
// Ring_Draw_Normal) compiled against the FLAT p6_gameapi.h. NOTHING here includes
// an engine header -- the engine's `namespace RSDK` and this TU's `RSDK`
// function-table object never meet (the two-TU split documented in p6_main.cpp).
//
// The ONLY P6 change vs p5_ring.cpp is the witness set: every numeric witness is
// renamed p5_w_* -> p6_w_* (so qa_p6_dispatch.py reads an unambiguous P6 symbol
// set) and TWO new slot-address witnesses are added -- p6_w_slot_procanim and
// p6_w_slot_drawsprite -- which p6_main.cpp fills with the .text addresses the
// REAL RSDK::SetupFunctionTables() wrote into RSDKFunctionTable[ProcessAnimation]
// and [DrawSprite]. Those two are the P6.1 milestone witnesses: they prove the
// engine's own table is populated with live code, and the 9 carried-over Ring
// witnesses prove the Ring still runs end-to-end while its RSDK.* calls now route
// THROUGH that engine table (via the table-dispatching bridges in p6_main.cpp).
//
// The `RSDK` table here stays file-scope static (internal linkage); its two slots
// point at the extern "C" table-dispatch bridges in p6_main.cpp. Saturn-only P6
// scaffolding (its fate is decided as the P6 climb proceeds).
// =============================================================================
#include "p6_gameapi.h"
#include <string.h>

// ---- The game-side function table (internal linkage; bridges live in main) --
static RSDKFunctionTable RSDK = { p6_bridge_proc_anim, p6_bridge_draw_sprite };

// ---- The Ring static-object instance + the `Ring` pointer the bodies use ----
static ObjectRing g_ringStatic;
ObjectRing *Ring = &g_ringStatic;

// ---- Witnesses (C linkage; names match qa_p6_dispatch.py exactly) ----------
extern "C" {
__attribute__((used)) extern const int32 p6_w_magic = 0x12345678;
__attribute__((used)) int32 p6_w_ticks         = 0;
__attribute__((used)) int32 p6_w_draw_calls    = 0; // written by p6_main bridge
__attribute__((used)) int32 p6_w_last_frameid  = 0; // written by p6_main bridge
__attribute__((used)) int32 p6_w_ring_classid  = 0;
__attribute__((used)) int32 p6_w_ring_timer    = 0;
__attribute__((used)) int32 p6_w_ring_scalex   = 0;
__attribute__((used)) int32 p6_w_ring_vely     = 0;
__attribute__((used)) int32 p6_w_ring_posy     = 0;
// P6.1 slot-address witnesses: the .text addresses SetupFunctionTables() wrote.
__attribute__((used)) int32 p6_w_slot_procanim  = 0; // RSDKFunctionTable[ProcessAnimation]
__attribute__((used)) int32 p6_w_slot_drawsprite = 0; // RSDKFunctionTable[DrawSprite]
// P6.2 file-I/O witnesses (Task #206). Zero-init -> .bss -> p6_ring.o(.bss) is
// pinned to WRAM-H by p6.linker, so these share ONE byte order with p6_w_magic.
// p6_main fills loaded/filesize/firstbytes from the engine RSDK::LoadFile run;
// p6_gfs fills gfsinit/fid (io-green only -- diagnostics for a failed GREEN).
__attribute__((used)) int32 p6_w_io_loaded     = 0; // RSDK::LoadFile("P6IO.BIN") true
__attribute__((used)) int32 p6_w_io_filesize   = 0; // info.fileSize after LoadFile
__attribute__((used)) int32 p6_w_io_firstbytes = 0; // first 4 bytes, big-endian packed
__attribute__((used)) int32 p6_w_io_gfsinit    = 0; // GFS_Init() return (io-green)
__attribute__((used)) int32 p6_w_io_fid        = 0; // GFS_NameToId() return (io-green)
__attribute__((used)) int32 p6_w_io_step       = 0; // p6_gfs_init progress 1..4 (localises a hang)
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
// P6 harness entry points (extern "C", consumed by p6_main.cpp).
// =============================================================================
extern "C" {

// The staticVars slot the engine RegisterObject stores: a pointer to the `Ring`
// pointer (Object** view). The staticUpdate loop derefs (*staticVars)->active ==
// g_ringStatic.active (Object base @2, zero-init -> ACTIVE_NEVER -> skipped).
void *p6_ring_staticvars_slot = (void *)&Ring;

// EntityRing slot size the engine guard checks (<= sizeof(EntityBase)=344).
unsigned p6_ring_entity_size = (unsigned)sizeof(EntityRing);

// destroyEntity target: unreachable at N=40 (timer reaches 40, never > 64), but
// must compile/link. Faithfully clears the engine-visible classID.
void p6_destroy_entity(void *entity)
{
    ((EntityRing *)entity)->classID = 0;
}

// Spawn one Ring entity into the given objectEntityList slot, in Ring_State_LostFX
// with a live 8-frame animator (speed 0x60, duration 0x100) -- the exact initial
// conditions qa_p6_dispatch.py's witness model was computed from.
void p6_ring_spawn(void *slot, void *frames)
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
void p6_ring_capture(void *slot)
{
    EntityRing *e   = (EntityRing *)slot;
    p6_w_ring_classid = (int32)e->classID;
    p6_w_ring_timer   = e->timer;
    p6_w_ring_scalex  = e->scale.x;
    p6_w_ring_vely    = e->velocity.y;
    p6_w_ring_posy    = e->position.y;
}

} // extern "C"
