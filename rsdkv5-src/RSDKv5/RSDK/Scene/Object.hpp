#ifndef OBJECT_H
#define OBJECT_H

#if RETRO_USE_MOD_LOADER
#include <functional>
#endif

namespace RSDK
{

#define RSDK_SIGNATURE_OBJ (0x4A424F) // "OBJ"

#if RETRO_PLATFORM == RETRO_SATURN
// P4 data retarget (Task #203): the Saturn has 2 MB of main RAM (two separate
// 1 MB banks: WRAM-H @0x06000000 + WRAM-L @0x00200000), not the PC's effectively
// unbounded heap. These counts size the engine's three largest .bss arrays:
//   objectEntityList = ENTITY_COUNT  * sizeof(EntityBase)
//   typeGroups       = TYPEGROUP_COUNT * (uint16[ENTITY_COUNT] + int32)
//   objectClassList  = OBJECT_COUNT  * sizeof(ObjectClass)
// At the stock counts these alone are 2.57 MB + 1.20 MB + 68 KB = 3.83 MB --
// they overflow main RAM on their own. The values below are sized for the
// bounded P5 proof (one Ring on a minimal scene). MEASURED CAVEAT: full GHZ1
// has 1041 placed entities (tools/parse_title_entities.py on GHZ Scene1.bin),
// so shipping GHZ1 needs SCENEENTITY_COUNT >= ~1088 -> that does NOT fit a flat
// single-bank image and is a P3 bank-placement / P6 entity-streaming decision,
// NOT a #define. Every non-Saturn build keeps the stock values byte-identical.
#define OBJECT_COUNT (0x100)

#define RESERVE_ENTITY_COUNT (0x40)
// P6.7 W11b (Task #226) GHZ-SCALE entity flip, per the P6.7b contract
// (SaturnMemoryMap.h P68_RESERVE/SCENE/TEMP_ENTITIES): GHZ1 places 1,041
// scene entities (max raw slot 1,040, live-parsed -- qa_p6_memmap M2), so
// SCENEENTITY covers 1,041 + headroom at 0x440 (1088) and TEMP doubles to
// 0x80 for runtime spawns (ring scatter etc.). ENTITY_COUNT = 0x500 (1280)
// x 344 B EntityBase = 440,320 B -- the objectEntityList window in WRAM-L
// map v7 (p6_io_main.cpp). The group lists do NOT scale with this: their
// entries are capped per-list below (the engine only writes inRange
// entities into them, Object.cpp:462-493; camera-local peak << caps).
// P6.7 Player wave step B (Task #227): TEMP halves 0x80 -> 0x40 to fund the
// DUAL-STRIDE pool below (TEMPENTITY_START stays 0x480; CreateEntity reuses
// temp slots circularly, and the GHZ runtime peak -- ring scatter ~32 +
// dust/trails -- fits 64 with headroom).
#define TEMPENTITY_COUNT     (0x40)
#define SCENEENTITY_COUNT    (0x440)
#define ENTITY_COUNT         (RESERVE_ENTITY_COUNT + SCENEENTITY_COUNT + TEMPENTITY_COUNT)
#define TEMPENTITY_START     (ENTITY_COUNT - TEMPENTITY_COUNT)

// P6.7 Player wave step B (Task #227): DUAL-STRIDE entity pool. The 344 B
// uniform slot refused EntityPlayer (556) / GameOver (452) / ImageTrail
// (440), and a uniform 556 stride costs +276 KB -- unfundable in WRAM-L.
// MEASURED decomp truth that makes a split pool sound: oversize entities
// only ever LIVE in reserve or temp slots -- the scene Player entity is a
// spawn MARKER that Player_LoadSprites CopyEntity's into SLOT_PLAYER1 and
// destroys before any Create touches its scene slot (Player.c:781-815), and
// CreateEntity allocates from the temp region. So:
//   slots [0, RESERVE)            WIDE   (ENTITY_WIDE_SIZE = 556 >= Player)
//   slots [RESERVE, TEMP_START)   NARROW (sizeof(EntityBase) = 344)
//   slots [TEMP_START, COUNT)     WIDE
// Pool bytes: 64*556 + 1088*344 + 64*556 = 445,440 (0x6CC00) at
// P6_LW_ENTITYLIST (map v8, p6_io_main.cpp). Slot indexing goes through
// RSDK_ENTITY_AT / RSDK_ENTITY_SLOT below; RegisterObject's refusal
// threshold moves to ENTITY_WIDE_SIZE, and ResetEntitySlot refuses an
// oversize class aimed at a narrow slot (witnessed).
#define ENTITY_WIDE_SIZE (556)
#define ENTITYLIST_SIZE_BYTES                                                                                                                        \
    ((uint32)RESERVE_ENTITY_COUNT * ENTITY_WIDE_SIZE + (uint32)SCENEENTITY_COUNT * sizeof(EntityBase)                                                \
     + (uint32)TEMPENTITY_COUNT * ENTITY_WIDE_SIZE)

// P6.7 W11b group-list entry caps (P68_TYPEGROUP/DRAWGROUP_ENTRY_CAP):
// stock TypeGroupList/DrawList embed entries[ENTITY_COUNT] -- at 0x500
// that is 0x84 x 2,564 = 338,448 B of typeGroups alone. Group lists are
// rebuilt EVERY frame from inRange entities only, so the live population
// is camera-local (measured Title peak < 0x40; GHZ inRange peak bounded
// by what fits a 426x240 maxView). Caps below + the witnessed append
// clamp keep overflow SAFE (drop + count) instead of the .bss-corruption
// class. PC arms of the append macros expand to the original expression.
#define TYPEGROUP_ENTRY_CAP (0x100)
#define DRAWGROUP_ENTRY_CAP (0x100)
// defined in p6_io_main.cpp (diag) -- the p6_saturn_tempentity_skips
// block-scope-extern pattern; counts EVERY dropped group append.
#define RSDK_TYPEGROUP_APPEND(list, value)                                                                                                           \
    do {                                                                                                                                             \
        extern int32 p6_saturn_group_skips;                                                                                                          \
        if ((list).entryCount < TYPEGROUP_ENTRY_CAP)                                                                                                 \
            (list).entries[(list).entryCount++] = (value);                                                                                           \
        else                                                                                                                                         \
            ++p6_saturn_group_skips;                                                                                                                 \
    } while (0)
#define RSDK_DRAWGROUP_APPEND(list, value)                                                                                                           \
    do {                                                                                                                                             \
        extern int32 p6_saturn_group_skips;                                                                                                          \
        if ((list).entityCount < DRAWGROUP_ENTRY_CAP)                                                                                                \
            (list).entries[(list).entityCount++] = (value);                                                                                          \
        else                                                                                                                                         \
            ++p6_saturn_group_skips;                                                                                                                 \
    } while (0)

#define TYPE_COUNT        (0x80)
#define EDITABLEVAR_COUNT (0x100)
#define TYPEGROUP_COUNT   (0x84)

// EntityBase per-slot data[] overlay width. data[] exists ONLY so that
// sizeof(EntityBase) >= sizeof(the largest registered EntityXxx); it is the
// DOMINANT term in objectEntityList = ENTITY_COUNT * sizeof(EntityBase). At the
// stock 0x100 (1024 B) each of the 448 Saturn slots costs 1112 B (486.5 KB total);
// 0x40 (256 B) drops that to 344 B/slot (150.5 KB), reclaiming ~336 KB. SAFETY:
// shrinking below the largest EntityXxx silently corrupts the adjacent slot (the
// Phase 1.4-1.15 .bss-overflow class), so EntityDevOutput (message[1012]) is
// Saturn-shrunk to fit, each engine object TU carries a compile-time slot-fit
// static_assert, and RegisterObject refuses an oversize class on Saturn.
// P6 RESTORATION: drop the Saturn branch -> data[] returns to 0x100.
#define OBJECT_DATA_COUNT (0x40)
#else
#define OBJECT_COUNT (0x400)

// 0x800 scene objects, 0x40 reserved ones, and 0x100 spare slots for creation
#define RESERVE_ENTITY_COUNT (0x40)
#define TEMPENTITY_COUNT     (0x100)
#define SCENEENTITY_COUNT    (0x800)
#define ENTITY_COUNT         (RESERVE_ENTITY_COUNT + SCENEENTITY_COUNT + TEMPENTITY_COUNT)
#define TEMPENTITY_START     (ENTITY_COUNT - TEMPENTITY_COUNT)

// PC arms of the W11b group-append seam: the exact original expressions
// (byte-identical upstream behavior); caps = the full entry arrays.
#define TYPEGROUP_ENTRY_CAP (ENTITY_COUNT)
#define DRAWGROUP_ENTRY_CAP (ENTITY_COUNT)
#define RSDK_TYPEGROUP_APPEND(list, value) ((list).entries[(list).entryCount++] = (value))
#define RSDK_DRAWGROUP_APPEND(list, value) ((list).entries[(list).entityCount++] = (value))

#define TYPE_COUNT        (0x100)
#define EDITABLEVAR_COUNT (0x100)
#define TYPEGROUP_COUNT   (0x104)

#define OBJECT_DATA_COUNT (0x100)

// PC arm of the Saturn dual-stride pool (uniform slots; byte-identical).
#define ENTITYLIST_SIZE_BYTES ((uint32)ENTITY_COUNT * sizeof(EntityBase))
#endif

#define FOREACH_STACK_COUNT (0x400)

// Used for DefaultObject & DevOutput
#define RSDK_THIS(class) Entity##class *self = (Entity##class *)sceneInfo.entity

enum StaticVariableTypes {
    SVAR_UINT8,
    SVAR_UINT16,
    SVAR_UINT32,
    SVAR_INT8,
    SVAR_INT16,
    SVAR_INT32,
    SVAR_BOOL,
    SVAR_POINTER,
    SVAR_VECTOR2,
    SVAR_STRING,
    SVAR_ANIMATOR,
    SVAR_HITBOX,
    SVAR_SPRITEFRAME,
};

enum TypeGroups {
    GROUP_ALL = 0,

    GROUP_CUSTOM0 = TYPE_COUNT,
    GROUP_CUSTOM1,
    GROUP_CUSTOM2,
    GROUP_CUSTOM3,
};

enum VariableTypes {
    VAR_UINT8,
    VAR_UINT16,
    VAR_UINT32,
    VAR_INT8,
    VAR_INT16,
    VAR_INT32,
    VAR_ENUM,
    VAR_BOOL,
    VAR_STRING,
    VAR_VECTOR2,
    VAR_FLOAT, // Not actually used in Sonic Mania so it's just an assumption, but this is the only thing that'd fit the 32 bit limit and make sense
    VAR_COLOR,
};

enum ActiveFlags {
    ACTIVE_NEVER,   // never update
    ACTIVE_ALWAYS,  // always update (even if paused/frozen)
    ACTIVE_NORMAL,  // always update (unless paused/frozen)
    ACTIVE_PAUSED,  // update only when paused/frozen
    ACTIVE_BOUNDS,  // update if in x & y bounds
    ACTIVE_XBOUNDS, // update only if in x bounds (y bounds dont matter)
    ACTIVE_YBOUNDS, // update only if in y bounds (x bounds dont matter)
    ACTIVE_RBOUNDS, // update based on radius boundaries (updateRange.x == radius)

    // Not really even a real active value, but some objects set their active states to this so here it is I suppose
    ACTIVE_DISABLED = 0xFF,
};

enum DefaultObjects {
    TYPE_DEFAULTOBJECT = 0,
#if RETRO_REV02
    TYPE_DEVOUTPUT,
#endif

    TYPE_DEFAULT_COUNT, // max
};

struct Object {
    int16 classID;
    uint8 active;
};

struct Entity {
#if RETRO_REV0U
    // used for languages such as beeflang that always have vfTables in classes
    void *vfTable;
#endif
    Vector2 position;
    Vector2 scale;
    Vector2 velocity;
    Vector2 updateRange;
    int32 angle;
    int32 alpha;
    int32 rotation;
    int32 groundVel;
    int32 zdepth;
    uint16 group;
    uint16 classID;
    bool32 inRange;
    bool32 isPermanent;
    bool32 tileCollisions;
    bool32 interaction;
    bool32 onGround;
    uint8 active;
#if RETRO_REV02
    uint8 filter;
#endif
    uint8 direction;
    uint8 drawGroup;
    uint8 collisionLayers;
    uint8 collisionPlane;
    uint8 collisionMode;
    uint8 drawFX;
    uint8 inkEffect;
    uint8 visible;
    uint8 onScreen;
};

struct EntityBase : Entity {
    void *data[OBJECT_DATA_COUNT]; // 0x100 PC / 0x40 Saturn (Task #203); sized to the largest EntityXxx
#if RETRO_REV0U
    void *unknown;
#endif
};

struct ObjectClass {
    RETRO_HASH_MD5(hash);

    // Events
#if RETRO_USE_MOD_LOADER // using std::function makes it easier to use stuff like lambdas
    std::function<void()> update;
    std::function<void()> lateUpdate;
    std::function<void()> staticUpdate;
    std::function<void()> draw;
    std::function<void(void *)> create;
    std::function<void()> stageLoad;
    std::function<void()> editorLoad;
    std::function<void()> editorDraw;
    std::function<void()> serialize;
#if RETRO_REV0U
    std::function<void(Object *)> staticLoad;
#endif
#else
    void (*update)();
    void (*lateUpdate)();
    void (*staticUpdate)();
    void (*draw)();
    void (*create)(void *);
    void (*stageLoad)();
    void (*editorLoad)();
    void (*editorDraw)();
    void (*serialize)();
#if RETRO_REV0U
    void (*staticLoad)(Object *);
#endif
#endif

    // Classes
    Object **staticVars;
    int32 entityClassSize;
    int32 staticClassSize;

#if RETRO_USE_MOD_LOADER
    ObjectClass *inherited;
#endif

#if !RETRO_USE_ORIGINAL_CODE
    const char *name; // for debugging purposes
#endif
};

struct EditableVarInfo {
    RETRO_HASH_MD5(hash);
    int32 offset;
    int32 active;
    uint8 type;
};

struct ForeachStackInfo {
    int32 id;
};

struct TypeGroupList {
#if RETRO_PLATFORM == RETRO_SATURN
    // W11b: capped entries (see TYPEGROUP_ENTRY_CAP above) -- every append
    // goes through RSDK_TYPEGROUP_APPEND; reads are bounded by entryCount.
    uint16 entries[TYPEGROUP_ENTRY_CAP];
#else
    uint16 entries[ENTITY_COUNT];
#endif
    int32 entryCount;
};

#if defined(P6_SCENE_TEST)
extern ObjectClass *objectClassList; // P6.3: relocated to WRAM-L (pointer form), defined in p6_io_main.cpp
#else
extern ObjectClass objectClassList[OBJECT_COUNT];
#endif
extern int32 objectClassCount;

// Loaded Global Objects
extern int32 globalObjectCount;
extern int32 globalObjectIDs[OBJECT_COUNT];

// Loaded Stage Objects (includes Globals if "loadGlobals" is enabled)
extern int32 stageObjectIDs[OBJECT_COUNT];

#if defined(P6_SCENE_TEST)
extern EntityBase *objectEntityList; // P6.3: relocated to WRAM-L (pointer form), defined in p6_io_main.cpp
#else
extern EntityBase objectEntityList[ENTITY_COUNT];
#endif

extern EditableVarInfo *editableVarList;
extern int32 editableVarCount;

extern ForeachStackInfo foreachStackList[FOREACH_STACK_COUNT];
extern ForeachStackInfo *foreachStackPtr;

#if defined(P6_SCENE_TEST)
extern TypeGroupList *typeGroups; // P6.3: relocated to WRAM-L (pointer form), defined in p6_io_main.cpp
#else
extern TypeGroupList typeGroups[TYPEGROUP_COUNT];
#endif

extern bool32 validDraw;

#if RETRO_REV0U
void RegisterObject(Object **staticVars, const char *name, uint32 entityClassSize, uint32 staticClassSize, void (*update)(), void (*lateUpdate)(),
                    void (*staticUpdate)(), void (*draw)(), void (*create)(void *), void (*stageLoad)(), void (*editorLoad)(), void (*editorDraw)(),
                    void (*serialize)(), void (*staticLoad)(Object *));

#if RETRO_USE_MOD_LOADER
void RegisterObject_STD(Object **staticVars, const char *name, uint32 entityClassSize, uint32 staticClassSize, std::function<void()> update,
                        std::function<void()> lateUpdate, std::function<void()> staticUpdate, std::function<void()> draw,
                        std::function<void(void *)> create, std::function<void()> stageLoad, std::function<void()> editorLoad,
                        std::function<void()> editorDraw, std::function<void()> serialize, std::function<void(Object *)> staticLoad);
#endif
#else
void RegisterObject(Object **staticVars, const char *name, uint32 entityClassSize, uint32 staticClassSize, void (*update)(), void (*lateUpdate)(),
                    void (*staticUpdate)(), void (*draw)(), void (*create)(void *), void (*stageLoad)(), void (*editorLoad)(), void (*editorDraw)(),
                    void (*serialize)());

#if RETRO_USE_MOD_LOADER
void RegisterObject_STD(Object **staticVars, const char *name, uint32 entityClassSize, uint32 staticClassSize, std::function<void()> update,
                        std::function<void()> lateUpdate, std::function<void()> staticUpdate, std::function<void()> draw,
                        std::function<void(void *)> create, std::function<void()> stageLoad, std::function<void()> editorLoad,
                        std::function<void()> editorDraw, std::function<void()> serialize);
#endif
#endif

#if RETRO_REV02 || RETRO_USE_MOD_LOADER
void RegisterStaticVariables(void **varClass, const char *name, uint32 classSize);
#endif

void LoadStaticVariables(uint8 *classPtr, uint32 *hash, int32 readOffset);

#define RSDK_EDITABLE_VAR(object, type, var) RSDK.SetEditableVar(type, #var, (uint8)object->classID, offsetof(Entity##object, var))

// Bug Details(?):
// classID isn't used AND is a uint8, how strange
// assuming classID would be used in-editor (it is in RetroED2, but not sure about official RSDK SDK)
// not sure why it's a uint8, given the original value is a uint16, so there's some small warnings about that
inline void SetEditableVar(uint8 type, const char *name, uint8 classID, int32 offset)
{
    if (editableVarCount < EDITABLEVAR_COUNT - 1) {
        EditableVarInfo *var = &editableVarList[editableVarCount];

        GEN_HASH_MD5(name, var->hash);
        var->type   = type;
        var->offset = offset;
        var->active = true;

        editableVarCount++;
    }
}

inline void SetActiveVariable(int32 classID, const char *name)
{
    // Editor-Only function
}
inline void AddEnumVariable(const char *name)
{
    // Editor-Only function
}

void InitObjects();
void ProcessObjects();
void ProcessPausedObjects();
void ProcessFrozenObjects();
void ProcessObjectDrawLists();

uint16 FindObject(const char *name);

#if RETRO_PLATFORM == RETRO_SATURN
// P6.7 Player wave step B: dual-stride slot <-> pointer mapping (regions per
// the pool comment at ENTITY_WIDE_SIZE above). Every objectEntityList[slot]
// site routes through these.
// P6.8 I2 (camera-local pool): the slot -> pool-slot INDIRECTION point. IDENTITY in
// I2 -> byte-identical to the pre-I2 direct mapping (the load-time self-check
// p6_i2_selfcheck asserts every slot in [0,ENTITY_COUNT) still resolves to its
// original address, latching p6_w_i2_resolve_ok). I3 replaces this body with a
// table lookup (table placed in the pool-shrink-freed home) that remaps far scene
// slots to dormant records -- routing SaturnEntityAt through this ONE function is
// what lets I3 do that WITHOUT touching the 211 RSDK_GET_ENTITY call sites. ZERO
// new allocation in I2.
inline int32 SaturnSlotToPoolSlot(int32 slot) { return slot; }
// I3 (2026-06-19, WRAM-H funding): noinline forces ONE COMDAT-folded out-of-line copy
// instead of inlining the dual-stride arithmetic at the ~69 RSDK_ENTITY_AT sites
// (MEASURED ~2.4 KB of WRAM-H duplicates; the -fkeep-inline-functions copy was gc'd as
// unreferenced -> 0 symbols in the map). De-inlining funds the per-access remap the
// camera-local pool shrink needs (only ~64 B was free under P6_HW_ANIMPAK). Gate
// tools/_portspike/qa_p6_deinline.py. SaturnSlotToPoolSlot STAYS inline -- a `return slot`
// identity costs nothing inlined; it becomes a table lookup at the actual pool shrink.
inline __attribute__((noinline)) EntityBase *SaturnEntityAt(int32 slot)
{
    int32 ps    = SaturnSlotToPoolSlot(slot); // I2 indirection (identity now; I3 -> table)
    uint8 *base = (uint8 *)objectEntityList;
    if (ps < RESERVE_ENTITY_COUNT)
        return (EntityBase *)(base + (uint32)ps * ENTITY_WIDE_SIZE);
    if (ps < TEMPENTITY_START)
        return (EntityBase *)(base + (uint32)RESERVE_ENTITY_COUNT * ENTITY_WIDE_SIZE
                              + (uint32)(ps - RESERVE_ENTITY_COUNT) * sizeof(EntityBase));
    return (EntityBase *)(base + (uint32)RESERVE_ENTITY_COUNT * ENTITY_WIDE_SIZE + (uint32)SCENEENTITY_COUNT * sizeof(EntityBase)
                          + (uint32)(ps - TEMPENTITY_START) * ENTITY_WIDE_SIZE);
}
inline int32 SaturnEntitySlot(EntityBase *entity)
{
    uint32 off       = (uint32)((uint8 *)entity - (uint8 *)objectEntityList);
    uint32 narrowOff = (uint32)RESERVE_ENTITY_COUNT * ENTITY_WIDE_SIZE;
    uint32 tempOff   = narrowOff + (uint32)SCENEENTITY_COUNT * sizeof(EntityBase);
    if (off >= ENTITYLIST_SIZE_BYTES)
        return 0;
    if (off < narrowOff)
        return (int32)(off / ENTITY_WIDE_SIZE);
    if (off < tempOff)
        return RESERVE_ENTITY_COUNT + (int32)((off - narrowOff) / sizeof(EntityBase));
    return TEMPENTITY_START + (int32)((off - tempOff) / ENTITY_WIDE_SIZE);
}
#define RSDK_ENTITY_AT(slot) (RSDK::SaturnEntityAt((int32)(slot)))
#define RSDK_ENTITY_SLOT(e)  (RSDK::SaturnEntitySlot((RSDK::EntityBase *)(e)))
#else
#define RSDK_ENTITY_AT(slot) (&objectEntityList[(slot)])
#define RSDK_ENTITY_SLOT(e)  ((int32)((EntityBase *)(e) - objectEntityList))
#endif

inline Entity *GetEntity(uint16 slot) { return (Entity *)RSDK_ENTITY_AT(slot < ENTITY_COUNT ? slot : (ENTITY_COUNT - 1)); }
inline int32 GetEntitySlot(EntityBase *entity)
{
    int32 slot = RSDK_ENTITY_SLOT(entity);
    return (uint32)slot < ENTITY_COUNT ? slot : 0;
}
int32 GetEntityCount(uint16 classID, bool32 isActive);

void ResetEntity(Entity *entity, uint16 classID, void *data);
void ResetEntitySlot(uint16 slot, uint16 classID, void *data);
Entity *CreateEntity(uint16 classID, void *data, int32 x, int32 y);

inline void CopyEntity(void *destEntity, void *srcEntity, bool32 clearSrcEntity)
{
    if (destEntity && srcEntity) {
        memcpy(destEntity, srcEntity, sizeof(EntityBase));

#if RETRO_PLATFORM == RETRO_SATURN
        // Step B (Task #227): a WIDE destination slot (reserve/temp) keeps
        // ENTITY_WIDE_SIZE - 344 bytes beyond the copied EntityBase; PC
        // copies the source's zeros there (uniform 1112 B slots), so zero
        // the tail for exact parity (e.g. Player_LoadSprites' CopyEntity of
        // the narrow scene spawn marker into wide SLOT_PLAYER1).
        {
            int32 dslot = RSDK_ENTITY_SLOT(destEntity);
            if (dslot < RESERVE_ENTITY_COUNT || dslot >= TEMPENTITY_START)
                memset((uint8 *)destEntity + sizeof(EntityBase), 0, ENTITY_WIDE_SIZE - sizeof(EntityBase));
        }
#endif

        if (clearSrcEntity)
            memset(srcEntity, 0, sizeof(EntityBase));
    }
}

bool32 GetActiveEntities(uint16 group, Entity **entity);
bool32 GetAllEntities(uint16 classID, Entity **entity);

inline void BreakForeachLoop() { --foreachStackPtr; }

// CheckPosOnScreen but if range is NULL it'll use entity->updateRange
bool32 CheckOnScreen(Entity *entity, Vector2 *range);
// Checks if a position is on screen & within range
bool32 CheckPosOnScreen(Vector2 *position, Vector2 *range);

void ClearStageObjects();

#if RETRO_REV0U
#include "Legacy/ObjectLegacy.hpp"
#endif

} // namespace RSDK

#endif // !OBJECT_H
