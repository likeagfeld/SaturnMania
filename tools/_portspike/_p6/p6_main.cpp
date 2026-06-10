// =============================================================================
// p6_main.cpp -- engine-headers driver TU for the P6.1 engine-dispatch proof
// (Task #205). The OTHER half of the two-TU split (see p6_ring.cpp).
//
// This TU uses the REAL engine headers (WITH `namespace RSDK`) and owns:
//   (a) main() -- calls the REAL RSDK::SetupFunctionTables() (Core/Link.cpp:65)
//       to populate the engine's own `void *RSDKFunctionTable[]` array, then
//       registers a blank class @0 + the Ring class @1, spawns ONE Ring entity,
//       drives 40 ticks of the REAL engine dispatch (ProcessObjects +
//       ProcessObjectDrawLists), and captures the witnesses; and
//   (b) the extern "C" bridge thunks the flat Ring TU calls through its `RSDK`
//       function table -- but in P6 those thunks dispatch THROUGH the engine's
//       RSDKFunctionTable[] (not directly, as P5 did).
//
// The P6.1 milestone vs P5 (#204): P5's p5_bridge_proc_anim called
// RSDK::ProcessAnimation DIRECTLY. P6's p6_bridge_proc_anim instead casts
// RSDKFunctionTable[FunctionTable_ProcessAnimation] (the slot the REAL
// SetupFunctionTables wrote) and calls THAT. Likewise p6_bridge_draw_sprite
// routes through RSDKFunctionTable[FunctionTable_DrawSprite] (a no-op DrawSprite
// stub in p6_stubs.cpp) AFTER bumping the witnesses -- so the 9 Ring witnesses
// stay byte-identical to P5 while the call now provably travels through the
// engine's own table. Two extra witnesses record the two slot .text addresses.
//
// RED build (empty FunctionTables_Saturn.o, no Core_Link.o/p6_stubs.o): the
// empty SetupFunctionTables leaves the table all-zero -> both slot witnesses 0
// (not in .text), the bridges' `if (fn)` guards skip, ProcessAnimation never
// runs -> last_frameid stays 0. GREEN build (Core_Link.o + p6_stubs.o, exclude
// FunctionTables_Saturn.o): real table -> slots land in .text, ProcessAnimation
// dispatches -> last_frameid 6 and all 9 Ring witnesses match P5.
//
// Bounded-run safety is identical to p5_main.cpp (verified against Object.cpp):
//   cameraCount==0, interaction==0, tileLayers zero-init, state ENGINESTATE_REGULAR.
// Saturn-only P6 scaffolding (its fate is decided as the P6 climb proceeds).
// =============================================================================
#include "RSDK/Core/RetroEngine.hpp"

using namespace RSDK;

// ---- Witnesses written here (defined in p6_ring.cpp) -----------------------
extern "C" {
extern int32 p6_w_ticks;
extern int32 p6_w_draw_calls;
extern int32 p6_w_last_frameid;
extern int32 p6_w_slot_procanim;   // RSDKFunctionTable[ProcessAnimation] address
extern int32 p6_w_slot_drawsprite; // RSDKFunctionTable[DrawSprite] address
}

// ---- P6.2 file-I/O witnesses + GFS backend init (Task #206) -----------------
#ifdef P6_IO_TEST
extern "C" {
extern int32 p6_w_io_loaded;     // RSDK::LoadFile returned true
extern int32 p6_w_io_filesize;   // info.fileSize after LoadFile
extern int32 p6_w_io_firstbytes; // first 4 file bytes, big-endian packed
#ifdef RETRO_SATURN_FILEIO
int  p6_gfs_init(void);          // GFS_Init (p6_gfs.c), io-green only
#endif
}
#endif

// ---- Flat-TU entry points / data this driver consumes ----------------------
extern "C" {
extern void  *p6_ring_staticvars_slot; // &Ring (Object** view)
extern unsigned p6_ring_entity_size;   // sizeof(EntityRing)
void Ring_Update(void);                // flat decomp body
void Ring_Draw(void);                  // flat decomp body
void p6_ring_spawn(void *slot, void *frames);
void p6_ring_capture(void *slot);
}

// ---- Bridges the flat Ring TU calls through (extern "C" -> namespace RSDK) --
// In P6 these dispatch THROUGH the engine's RSDKFunctionTable[] (populated by the
// REAL SetupFunctionTables). The guarded `if (fn)` is what makes the RED build
// (zero table) skip dispatch instead of jumping to a NULL slot.
extern "C" void *p6_scene_entity(void)
{
    return (void *)sceneInfo.entity;
}

extern "C" void p6_bridge_proc_anim(void *animator)
{
    // The cadence proof now routes through the engine table slot that
    // SetupFunctionTables wrote with the REAL ProcessAnimation (Animation.cpp).
    typedef void (*ProcAnimFn)(Animator *);
    ProcAnimFn fn = (ProcAnimFn)RSDKFunctionTable[FunctionTable_ProcessAnimation];
    if (fn)
        fn((Animator *)animator);
}

extern "C" void p6_bridge_draw_sprite(void *animator, void *position, int32 screenRelative)
{
    // Witnesses bumped BEFORE dispatch so they stay byte-identical to P5 even
    // though the call now travels through the engine's DrawSprite table slot
    // (a no-op stub in p6_stubs.cpp -- the rasterizer lands in P6.4).
    ++p6_w_draw_calls;
    p6_w_last_frameid = ((Animator *)animator)->frameID;

    typedef void (*DrawSpriteFn)(Animator *, Vector2 *, bool32);
    DrawSpriteFn fn = (DrawSpriteFn)RSDKFunctionTable[FunctionTable_DrawSprite];
    if (fn)
        fn((Animator *)animator, (Vector2 *)position, (bool32)screenRelative);
}

// ---- A valid static Object for the blank class @0 --------------------------
static Object  s_blankObject;
static Object *s_blankStaticVars = &s_blankObject;

// ---- 8 real-engine SpriteFrames so ProcessAnimation reads a matching stride -
static SpriteFrame s_ringFrames[8];

// ENTRY: all modes (red/green/io-red/io-green) boot through crt0.s, whose _start
// calls _main directly AND performs the C-runtime init (BSS-zero + ctors). The proof
// body IS main(); crt0 has already prepared the runtime. P6.2 file I/O needs no SGL,
// so io-green uses the same crt0 entry as every other mode -- no boot shim.
int main(void)
{

    // P6.1 core step: run the REAL engine table-population path. After this the
    // engine's own RSDKFunctionTable[] carries live .text addresses (GREEN) or
    // stays all-zero (RED, empty stub). The two slot witnesses capture the proof.
    RSDK::SetupFunctionTables();
    p6_w_slot_procanim   = (int32)(uintptr_t)RSDKFunctionTable[FunctionTable_ProcessAnimation];
    p6_w_slot_drawsprite = (int32)(uintptr_t)RSDKFunctionTable[FunctionTable_DrawSprite];

    // Class 0: a blank with a valid static Object so the ProcessObjects static-
    // update loop can deref (*staticVars)->active for every class in [0,classCount).
    RegisterObject(&s_blankStaticVars, "Blank Object", sizeof(Entity), sizeof(Object),
                   NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

    // Class 1: the real Ring. Saturn RegisterObject guard accepts it because
    // sizeof(EntityRing) (172) <= sizeof(EntityBase) (344, Task #203).
    RegisterObject((Object **)p6_ring_staticvars_slot, "Ring", p6_ring_entity_size, 20,
                   Ring_Update, NULL, NULL, Ring_Draw, NULL, NULL, NULL, NULL, NULL);

    stageObjectIDs[0]    = 0;
    stageObjectIDs[1]    = 1;
    sceneInfo.classCount = 2;

    for (int32 i = 0; i < 8; ++i) s_ringFrames[i].duration = 0x100;

    // Spawn ONE Ring at slot 0x40 (the first non-reserved entity slot); slots
    // below it stay classID==0 and are skipped by ProcessObjects.
    p6_ring_spawn(&objectEntityList[0x40], (void *)s_ringFrames);

    sceneInfo.state            = ENGINESTATE_REGULAR;
    videoSettings.screenCount  = 1;
    engine.drawGroupVisible[3] = true;

    for (int32 i = 0; i < 40; ++i) {
        ProcessObjects();
        ProcessObjectDrawLists();
        ++p6_w_ticks;
    }

    p6_ring_capture(&objectEntityList[0x40]);

#ifdef P6_IO_TEST
    // P6.2 (Task #206): exercise the UNMODIFIED engine RSDK::LoadFile against a
    // real on-disc file. io-green defines RETRO_SATURN_FILEIO -> Reader.hpp routes
    // fOpen/fRead/fSeek/fTell/fClose to the Saturn GFS backend (p6_gfs.c), which we
    // initialise here first. io-red leaves it undefined -> LoadFile uses newlib
    // stdio -> the _open=-1 stub (p6_syscalls.c) -> LoadFile false -> loaded==0.
#ifdef RETRO_SATURN_FILEIO
    p6_gfs_init();
#endif
    FileInfo finfo;
    InitFileInfo(&finfo);
    if (LoadFile(&finfo, "P6IO.BIN", FMODE_RB)) {
        p6_w_io_loaded   = 1;
        p6_w_io_filesize = finfo.fileSize;
        uint8 hdr[4]     = { 0, 0, 0, 0 };
        ReadBytes(&finfo, hdr, 4);
        p6_w_io_firstbytes = ((int32)hdr[0] << 24) | ((int32)hdr[1] << 16)
                           | ((int32)hdr[2] << 8) | (int32)hdr[3];
        CloseFile(&finfo);
    }
#endif

    // Neither entry expects a return (crt0's _start caller and SGL's `jmp @main`
    // both fall through to nothing); park here so the savestate finds PC in .text.
    for (;;) {
    }
    return 0;
}
