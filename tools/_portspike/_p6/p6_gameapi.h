// =============================================================================
// p6_gameapi.h -- FLAT game-API view for the P6.1 engine-dispatch proof (#205).
//
// Byte-identical surface to p5_gameapi.h (Task #204): p6_ring.cpp compiles the
// VERBATIM decomp Ring bodies against this FLAT view (no `namespace RSDK`) so the
// Ring TU never sees the engine headers. The ONE behavioural change from P5 lives
// entirely in p6_main.cpp's bridges: instead of calling RSDK::ProcessAnimation /
// the witness counter DIRECTLY, the P6 bridges dispatch THROUGH the engine's own
// RSDKFunctionTable[] (populated by the REAL RSDK::SetupFunctionTables in
// Core/Link.cpp). The Ring's `RSDK.ProcessAnimation(...)` / `RSDK.DrawSprite(...)`
// source is unchanged; the proof is that the engine table now carries the call.
//
// Struct layouts are the SAME as p5_gameapi.h (RETRO_REVISION=2 -> REV02=1):
//   - Entity 88 B, Animator 24 B, Object base classID@0 active@2, EntityRing 172 B.
// See p5_gameapi.h for the per-field offset rationale; nothing here diverges.
// =============================================================================
#ifndef P6_GAMEAPI_H
#define P6_GAMEAPI_H

typedef signed char    int8;
typedef unsigned char  uint8;
typedef short          int16;
typedef unsigned short uint16;
typedef int            int32;
typedef unsigned int   uint32;
typedef int            bool32; // RSDK bool32 is a 32-bit int

typedef struct {
    int32 x;
    int32 y;
} Vector2;

// ---- Animator : 24 bytes (Graphics/Animation.hpp) --------------------------
typedef struct {
    void *frames;         // @0  SpriteFrame*  (real engine SpriteFrame in p6_main.cpp)
    int32 frameID;        // @4
    int16 animationID;    // @8
    int16 prevAnimationID;// @10
    int16 speed;          // @12
    int16 timer;          // @14
    int16 frameDuration;  // @16
    int16 frameCount;     // @18
    uint8 loopIndex;      // @20
    uint8 rotationStyle;  // @21
    // 2 bytes tail padding -> sizeof == 24
} Animator;

// ---- Entity : 88 bytes (Scene/Object.hpp:140-174, REV02 on / REV0U off) ----
struct Entity {
    Vector2 position;     // @0
    Vector2 scale;        // @8
    Vector2 velocity;     // @16
    Vector2 updateRange;  // @24
    int32 angle;          // @32
    int32 alpha;          // @36
    int32 rotation;       // @40
    int32 groundVel;      // @44
    int32 zdepth;         // @48
    uint16 group;         // @52
    uint16 classID;       // @54
    bool32 inRange;       // @56
    bool32 isPermanent;   // @60
    bool32 tileCollisions;// @64
    bool32 interaction;   // @68
    bool32 onGround;      // @72
    uint8 active;         // @76
    uint8 filter;         // @77  (RETRO_REV02)
    uint8 direction;      // @78
    uint8 drawGroup;      // @79
    uint8 collisionLayers;// @80
    uint8 collisionPlane; // @81
    uint8 collisionMode;  // @82
    uint8 drawFX;         // @83
    uint8 inkEffect;      // @84
    uint8 visible;        // @85
    uint8 onScreen;       // @86
    // 1 byte tail padding -> sizeof == 88
};

// ---- Object base : classID@0 active@2 (Scene/Object.hpp:135-138) -----------
typedef struct {
    int16 left;
    int16 top;
    int16 right;
    int16 bottom;
} Hitbox;

struct ObjectRing {
    int16 classID;   // @0  (Object base)
    uint8 active;    // @2  (Object base)
    Hitbox hitbox;   // @4
    int32 pan;       // @12
    uint16 aniFrames;// @16
    uint16 sfxRing;  // @18
};

// ---- EntityRing : 172 bytes (Objects/Global/Ring.h:32-48) ------------------
// `: Entity` puts the 88-byte base at offset 0; `state` follows at @88.
typedef void (*RingStateMachine)(void);

struct EntityRing : Entity {
    RingStateMachine state;     // @88
    RingStateMachine stateDraw; // @92
    int32 type;                 // @96   (RingTypes)
    int32 planeFilter;          // @100  (PlaneFilterTypes)
    int32 ringAmount;           // @104
    int32 timer;                // @108
    int32 maxFrameCount;        // @112
    int32 sparkleType;          // @116
    void *storedPlayer;         // @120  (EntityPlayer*)
    int32 moveType;             // @124  (RingMoveTypes)
    Vector2 amplitude;          // @128
    int32 speed;                // @136
    Vector2 drawPos;            // @140
    Animator animator;          // @148  -> ends @172
};

// ---- Game-API macros (mirror RSDKv5 Object.hpp / Animation.hpp) -------------
#define StateMachine(name)      void (*name)(void)
#define StateMachine_Run(state) do { if (state) (state)(); } while (0)
#define RSDK_THIS(type)         Entity##type *self = (Entity##type *)p6_scene_entity()
#define destroyEntity(e)        p6_destroy_entity((void *)(e))

// ---- The game-side `RSDK.` function table (subset the Ring bodies call) -----
typedef struct {
    void (*ProcessAnimation)(void *animator);
    void (*DrawSprite)(void *animator, void *position, int32 screenRelative);
} RSDKFunctionTable;

#ifdef __cplusplus
extern "C" {
#endif

// Resolved in p6_main.cpp (the engine-headers TU) -- pointer ABI is namespace-
// agnostic, so these extern "C" bridges cross the flat<->namespaced TU boundary.
// In P6 the two *_bridge_* thunks dispatch THROUGH the engine RSDKFunctionTable[].
void *p6_scene_entity(void);                                   // returns sceneInfo.entity
void  p6_destroy_entity(void *entity);
void  p6_bridge_proc_anim(void *animator);                     // -> RSDKFunctionTable[ProcessAnimation]
void  p6_bridge_draw_sprite(void *animator, void *position, int32 screenRelative);

#ifdef __cplusplus
}
#endif

#endif // P6_GAMEAPI_H
