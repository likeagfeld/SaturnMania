// =============================================================================
// p5_main.cpp -- engine-headers driver TU for the P5 first-object proof (#204).
//
// The OTHER half of the two-TU split (see p5_ring.cpp): this TU uses the REAL
// engine headers (WITH `namespace RSDK`) and owns
//   (a) main() -- registers a blank class @0 + the Ring class @1, spawns ONE Ring
//       entity into objectEntityList, then drives 40 ticks of the REAL engine
//       dispatch (RSDK::ProcessObjects + RSDK::ProcessObjectDrawLists) and
//       captures the witnesses; and
//   (b) the extern "C" bridge thunks the flat Ring TU calls through its `RSDK`
//       function table + RSDK_THIS macro.
//
// Why this proves P5: ProcessObjects (Scene/Object.cpp) is the REAL engine entity
// loop. It sets sceneInfo.entity then calls objectClassList[...].update() == the
// real Ring_Update, which runs Ring_State_LostFX, which calls RSDK.ProcessAnimation
// == p5_bridge_proc_anim == the REAL RSDK::ProcessAnimation (Animation.cpp). So a
// real decomp object, registered + spawned + ticked + animated by the real core,
// advances its animator at the decomp cadence -- the cadence the gate recomputes
// independently and compares. The draw is routed to a witness counter (not the
// Saturn-incompatible Drawing.cpp) and the tile/parallax paths are avoided by
// putting the Ring alone in drawGroup 3 with only drawGroupVisible[3] set.
//
// Bounded-run safety (all verified against Object.cpp this session):
//   - cameraCount == 0 (zero-init) -> the camera loop and every ACTIVE_BOUNDS
//     branch are skipped; the Ring uses ACTIVE_NORMAL so inRange is set directly.
//   - interaction == 0 on the entity -> the typeGroups indexing pass is skipped
//     (no out-of-range typeGroups[classID]/[group] writes).
//   - tileLayers all zero-init -> every layer maps to drawGroup 0, so the VISIBLE
//     drawGroup 3 has 0 layers -> ProcessParallax / DrawLayer* never run.
//   - sceneInfo.state = ENGINESTATE_REGULAR (1, != ENGINESTATE_LOAD 0) so the
//     draw-list pass is not gated off.
// Saturn-only P5 scaffolding (P6 decides its fate).
// =============================================================================
#include "RSDK/Core/RetroEngine.hpp"

using namespace RSDK;

// ---- Witnesses written here (defined in p5_ring.cpp) -----------------------
extern "C" {
extern int32 p5_w_ticks;
extern int32 p5_w_draw_calls;
extern int32 p5_w_last_frameid;
}

// ---- Flat-TU entry points / data this driver consumes ----------------------
extern "C" {
extern void  *p5_ring_staticvars_slot; // &Ring (Object** view)
extern unsigned p5_ring_entity_size;   // sizeof(EntityRing)
void Ring_Update(void);                // flat decomp body
void Ring_Draw(void);                  // flat decomp body
void p5_ring_spawn(void *slot, void *frames);
void p5_ring_capture(void *slot);
}

// ---- Bridges the flat Ring TU calls through (extern "C" -> namespace RSDK) --
extern "C" void *p5_scene_entity(void)
{
    return (void *)sceneInfo.entity;
}

extern "C" void p5_bridge_proc_anim(void *animator)
{
    // The cadence proof: the REAL engine ProcessAnimation (Animation.cpp:150-177).
    ProcessAnimation((Animator *)animator);
}

extern "C" void p5_bridge_draw_sprite(void *animator, void *position, int32 screenRelative)
{
    (void)position;
    (void)screenRelative;
    ++p5_w_draw_calls;
    p5_w_last_frameid = ((Animator *)animator)->frameID;
}

// ---- A valid static Object for the blank class @0 --------------------------
static Object  s_blankObject;
static Object *s_blankStaticVars = &s_blankObject;

// ---- 8 real-engine SpriteFrames so ProcessAnimation reads a matching stride -
static SpriteFrame s_ringFrames[8];

int main(void)
{
    // Class 0: a blank with a valid static Object so the ProcessObjects static-
    // update loop can deref (*staticVars)->active for every class in [0,classCount).
    RegisterObject(&s_blankStaticVars, "Blank Object", sizeof(Entity), sizeof(Object),
                   NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

    // Class 1: the real Ring. Saturn RegisterObject guard accepts it because
    // sizeof(EntityRing) (172) <= sizeof(EntityBase) (344, Task #203).
    RegisterObject((Object **)p5_ring_staticvars_slot, "Ring", p5_ring_entity_size, 20,
                   Ring_Update, NULL, NULL, Ring_Draw, NULL, NULL, NULL, NULL, NULL);

    stageObjectIDs[0]    = 0;
    stageObjectIDs[1]    = 1;
    sceneInfo.classCount = 2;

    for (int32 i = 0; i < 8; ++i) s_ringFrames[i].duration = 0x100;

    // Spawn ONE Ring at slot 0x40 (the first non-reserved entity slot); slots
    // below it stay classID==0 and are skipped by ProcessObjects.
    p5_ring_spawn(&objectEntityList[0x40], (void *)s_ringFrames);

    sceneInfo.state            = ENGINESTATE_REGULAR;
    videoSettings.screenCount  = 1;
    engine.drawGroupVisible[3] = true;

    for (int32 i = 0; i < 40; ++i) {
        ProcessObjects();
        ProcessObjectDrawLists();
        ++p5_w_ticks;
    }

    p5_ring_capture(&objectEntityList[0x40]);

    // crt0 hangs if main returns; park here so the savestate finds us in .text.
    for (;;) {
    }
    return 0;
}
