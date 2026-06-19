#include "RSDK/Core/RetroEngine.hpp"

using namespace RSDK;

#if RETRO_REV0U
#include "Legacy/ObjectLegacy.cpp"
#endif

#if !defined(P6_SCENE_TEST)
// P6.7a: under the P6 pack these relocate -- objectClassList/objectEntityList/
// typeGroups are pointer-form with backings in p6_io_main.cpp (Animation.cpp:9
// pattern); globalObjectIDs/stageObjectIDs are WRAM-L .equ absolutes; the
// scalar counters live in p6_io_main's engine-scalars block.
ObjectClass RSDK::objectClassList[OBJECT_COUNT];
int32 RSDK::objectClassCount = 0;

int32 RSDK::globalObjectCount = 0;
int32 RSDK::globalObjectIDs[OBJECT_COUNT];
int32 RSDK::stageObjectIDs[OBJECT_COUNT];

EntityBase RSDK::objectEntityList[ENTITY_COUNT];

EditableVarInfo *RSDK::editableVarList;
int32 RSDK::editableVarCount = 0;

TypeGroupList RSDK::typeGroups[TYPEGROUP_COUNT];
#endif

bool32 RSDK::validDraw = false;

ForeachStackInfo RSDK::foreachStackList[FOREACH_STACK_COUNT];
ForeachStackInfo *RSDK::foreachStackPtr = NULL;

#if RETRO_REV0U
#if RETRO_USE_MOD_LOADER
void RSDK::RegisterObject(Object **staticVars, const char *name, uint32 entityClassSize, uint32 staticClassSize, void (*update)(),
                          void (*lateUpdate)(), void (*staticUpdate)(), void (*draw)(), void (*create)(void *), void (*stageLoad)(),
                          void (*editorLoad)(), void (*editorDraw)(), void (*serialize)(), void (*staticLoad)(Object *))
{
    return RegisterObject_STD(staticVars, name, entityClassSize, staticClassSize, update, lateUpdate, staticUpdate, draw, create, stageLoad,
                              editorLoad, editorDraw, serialize, staticLoad);
}

void RSDK::RegisterObject_STD(Object **staticVars, const char *name, uint32 entityClassSize, uint32 staticClassSize, std::function<void()> update,
                              std::function<void()> lateUpdate, std::function<void()> staticUpdate, std::function<void()> draw,
                              std::function<void(void *)> create, std::function<void()> stageLoad, std::function<void()> editorLoad,
                              std::function<void()> editorDraw, std::function<void()> serialize, std::function<void(Object *)> staticLoad)
#else
void RSDK::RegisterObject(Object **staticVars, const char *name, uint32 entityClassSize, uint32 staticClassSize, void (*update)(),
                          void (*lateUpdate)(), void (*staticUpdate)(), void (*draw)(), void (*create)(void *), void (*stageLoad)(),
                          void (*editorLoad)(), void (*editorDraw)(), void (*serialize)(), void (*staticLoad)(Object *))
#endif
#else
#if RETRO_USE_MOD_LOADER
void RSDK::RegisterObject(Object **staticVars, const char *name, uint32 entityClassSize, uint32 staticClassSize, void (*update)(),
                          void (*lateUpdate)(), void (*staticUpdate)(), void (*draw)(), void (*create)(void *), void (*stageLoad)(),
                          void (*editorLoad)(), void (*editorDraw)(), void (*serialize)())
{
    return RegisterObject_STD(staticVars, name, entityClassSize, staticClassSize, update, lateUpdate, staticUpdate, draw, create, stageLoad,
                              editorLoad, editorDraw, serialize);
}

void RSDK::RegisterObject_STD(Object **staticVars, const char *name, uint32 entityClassSize, uint32 staticClassSize, std::function<void()> update,
                              std::function<void()> lateUpdate, std::function<void()> staticUpdate, std::function<void()> draw,
                              std::function<void(void *)> create, std::function<void()> stageLoad, std::function<void()> editorLoad,
                              std::function<void()> editorDraw, std::function<void()> serialize)
#else
void RSDK::RegisterObject(Object **staticVars, const char *name, uint32 entityClassSize, uint32 staticClassSize, void (*update)(),
                          void (*lateUpdate)(), void (*staticUpdate)(), void (*draw)(), void (*create)(void *), void (*stageLoad)(),
                          void (*editorLoad)(), void (*editorDraw)(), void (*serialize)())
#endif
#endif
{
    if (objectClassCount < OBJECT_COUNT) {
        if (entityClassSize > sizeof(EntityBase))
            PrintLog(PRINT_NORMAL, "Class exceeds max entity memory: %s", name);
#if RETRO_PLATFORM == RETRO_SATURN
        // P4 Task #203 / P6.7 step B (Task #227): the refusal threshold is
        // now the WIDE slot (556 B -- reserve/temp regions of the dual-stride
        // pool, Object.hpp). Classes in (344, 556] register normally; their
        // entities are reserve/temp-resident by decomp construction and
        // ResetEntitySlot refuses narrow-slot placement (witnessed). Classes
        // beyond 556 (Platform 724, TitleCard 864 -- future waves) still
        // refuse here: cleanly absent beats silent slot corruption (the
        // Phase 1.4-1.15 class).
        if (entityClassSize > ENTITY_WIDE_SIZE)
            return;
#endif

        ObjectClass *classInfo = &objectClassList[objectClassCount];
        GEN_HASH_MD5(name, classInfo->hash);
        classInfo->staticVars      = staticVars;
        classInfo->entityClassSize = entityClassSize;
        classInfo->staticClassSize = staticClassSize;
        classInfo->update          = update;
        classInfo->lateUpdate      = lateUpdate;
        classInfo->staticUpdate    = staticUpdate;
        classInfo->draw            = draw;
        classInfo->create          = create;
        classInfo->stageLoad       = stageLoad;
        classInfo->editorLoad      = editorLoad;
        classInfo->editorDraw      = editorDraw;
        classInfo->serialize       = serialize;
#if RETRO_REV0U
        classInfo->staticLoad = staticLoad;
#endif

#if !RETRO_USE_ORIGINAL_CODE
        classInfo->name = name;
#endif

        ++objectClassCount;
    }
}

#if RETRO_REV02 || RETRO_USE_MOD_LOADER
void RSDK::RegisterStaticVariables(void **staticVars, const char *name, uint32 classSize)
{
    RETRO_HASH_MD5(hash);
    GEN_HASH_MD5(name, hash);
    AllocateStorage((void **)staticVars, classSize, DATASET_STG, true);
    LoadStaticVariables((uint8 *)*staticVars, hash, 0);
}
#endif

#define ALIGN_TO(type)                                                                                                                               \
    aligned = dataPos & -(int32)sizeof(type);                                                                                                        \
    if (aligned < dataPos)                                                                                                                           \
        dataPos = aligned + sizeof(type);

void RSDK::LoadStaticVariables(uint8 *classPtr, uint32 *hash, int32 readOffset)
{
    char fullFilePath[0x40];

    const char *hexChars = "0123456789ABCDEF";
    char classHash[]     = "00000000000000000000000000000000";

    int32 strPos = 0;
    for (int32 i = 0; i < 32; i += 4) classHash[strPos++] = hexChars[(hash[0] >> i) & 0xF];
    for (int32 i = 0; i < 32; i += 4) classHash[strPos++] = hexChars[(hash[1] >> i) & 0xF];
    for (int32 i = 0; i < 32; i += 4) classHash[strPos++] = hexChars[(hash[2] >> i) & 0xF];
    for (int32 i = 0; i < 32; i += 4) classHash[strPos++] = hexChars[(hash[3] >> i) & 0xF];

    sprintf_s(fullFilePath, sizeof(fullFilePath), "Data/Objects/Static/%s.bin", classHash);

    FileInfo info;
    InitFileInfo(&info);
    if (LoadFile(&info, fullFilePath, FMODE_RB)) {
        uint32 sig = ReadInt32(&info, false);

        if (sig != RSDK_SIGNATURE_OBJ) {
            CloseFile(&info);
            return;
        }

        int32 dataPos = readOffset;
        while (info.readPos < info.fileSize) {
            int32 type      = ReadInt8(&info);
            int32 arraySize = ReadInt32(&info, false);

            bool32 hasValues = (type & 0x80) != 0;
            type &= 0x7F;

            int32 aligned = 0;
            if (hasValues) {
                uint32 count = ReadInt32(&info, false);

                switch (type) {
                    default:
#if !RETRO_USE_ORIGINAL_CODE
                        PrintLog(PRINT_NORMAL, "Invalid static variable type: %d", type);
#endif
                        break;

                    case SVAR_UINT8:
                    case SVAR_INT8:
                        if (info.readPos + (count * sizeof(uint8)) <= info.fileSize && &classPtr[dataPos]) {
                            for (int32 i = 0; i < count * sizeof(uint8); i += sizeof(uint8)) ReadBytes(&info, &classPtr[dataPos + i], sizeof(uint8));
                        }
                        else {
                            info.readPos += count * sizeof(uint8);
                        }

                        dataPos += count * sizeof(uint8);
                        break;

                    case SVAR_UINT16:
                    case SVAR_INT16: {
                        ALIGN_TO(int16);

                        if (info.readPos + (count * sizeof(int16)) <= info.fileSize && &classPtr[dataPos]) {
                            for (int32 i = 0; i < count * sizeof(int16); i += sizeof(int16)) {
#if !RETRO_USE_ORIGINAL_CODE
                                *(int16 *)&classPtr[dataPos + i] = ReadInt16(&info);
#else
                                // This only works as intended on little-endian CPUs.
                                ReadBytes(&info, &classPtr[dataPos + i], sizeof(int16));
#endif
                            }
                        }
                        else {
                            info.readPos += count * sizeof(int16);
                        }

                        dataPos += sizeof(int16) * count;
                        break;
                    }

                    case SVAR_UINT32:
                    case SVAR_INT32: {
                        ALIGN_TO(int32);

                        if (info.readPos + (count * sizeof(int32)) <= info.fileSize && &classPtr[dataPos]) {
                            for (int32 i = 0; i < count * sizeof(int32); i += sizeof(int32)) {
#if !RETRO_USE_ORIGINAL_CODE
                                *(int32 *)&classPtr[dataPos + i] = ReadInt32(&info, false);
#else
                                // This only works as intended on little-endian CPUs.
                                ReadBytes(&info, &classPtr[dataPos + i], sizeof(int32));
#endif
                            }
                        }
                        else {
                            info.readPos += count * sizeof(int32);
                        }

                        dataPos += sizeof(int32) * count;
                        break;
                    }

                    case SVAR_BOOL: {
                        ALIGN_TO(bool32);

                        if (info.readPos + (count * sizeof(bool32)) <= info.fileSize && &classPtr[dataPos]) {
                            for (int32 i = 0; i < count * sizeof(bool32); i += sizeof(bool32)) {
#if !RETRO_USE_ORIGINAL_CODE
                                *(bool32 *)&classPtr[dataPos + i] = (bool32)ReadInt32(&info, false);
#else
                                // This only works as intended on little-endian CPUs.
                                ReadBytes(&info, &classPtr[dataPos + i], sizeof(bool32));
#endif
                            }
                        }
                        else {
                            info.readPos += count * sizeof(bool32);
                        }

                        dataPos += sizeof(bool32) * count;
                        break;
                    }
                }
            }
            else {
                switch (type) {
                    case SVAR_UINT8:
                    case SVAR_INT8: dataPos += sizeof(uint8) * arraySize; break;

                    case SVAR_UINT16:
                    case SVAR_INT16:
                        ALIGN_TO(int16);

                        dataPos += sizeof(int16) * arraySize;
                        break;

                    case SVAR_UINT32:
                    case SVAR_INT32:
                        ALIGN_TO(int32);

                        dataPos += sizeof(int32) * arraySize;
                        break;

                    case SVAR_BOOL:
                        ALIGN_TO(bool32);

                        dataPos += sizeof(bool32) * arraySize;
                        break;

                    case SVAR_POINTER:
                        ALIGN_TO(void *);

                        dataPos += sizeof(void *) * arraySize;
                        break;

                    case SVAR_VECTOR2:
                        ALIGN_TO(int32);

                        dataPos += sizeof(Vector2) * arraySize;
                        break;

                    case SVAR_STRING:
                        ALIGN_TO(void *);

                        dataPos += sizeof(String) * arraySize;
                        break;

                    case SVAR_ANIMATOR:
                        ALIGN_TO(void *);

                        dataPos += sizeof(Animator) * arraySize;
                        break;

                    case SVAR_HITBOX:
                        ALIGN_TO(int16);

                        dataPos += sizeof(Hitbox) * arraySize;
                        break;

                    case SVAR_SPRITEFRAME:
                        ALIGN_TO(int16);

                        dataPos += sizeof(GameSpriteFrame) * arraySize;
                        break;

                    default:
#if !RETRO_USE_ORIGINAL_CODE
                        PrintLog(PRINT_NORMAL, "Invalid data type: %d", type);
#endif
                        break;
                }
            }
        }

        CloseFile(&info);
    }
}

void RSDK::InitObjects()
{
    sceneInfo.entitySlot = 0;
    // P6.7c (Task #210): the stock literal `ENTITY_COUNT - 0x100` IS
    // TEMPENTITY_START on PC (TEMPENTITY_COUNT == 0x100) but createSlot is
    // uint16 (Scene.hpp), so at the Saturn ENTITY_COUNT retarget (0xC0) the
    // subtraction wraps to 0xFFC0 -- a WILD CreateEntity index addressing
    // 0x017CE800 (outside WRAM-L). TEMPENTITY_START is byte-identical on PC
    // and correct on every retarget. MEASURED by the P6.7c verification pass.
    sceneInfo.createSlot = TEMPENTITY_START;
    cameraCount          = 0;

    for (int32 o = 0; o < sceneInfo.classCount; ++o) {
#if RETRO_USE_MOD_LOADER
        currentObjectID = o;
#endif

#if defined(P6_SCENE_TEST)
        // Task #227 hang bisect: breadcrumb BEFORE each dispatch -- after a
        // wedge, the savestate peek names the exact class. Top bit set =
        // StageLoad phase; low halves = (stage index << 16) | class id.
        {
            extern int32 p6_w_initobj_step;
            p6_w_initobj_step = (int32)(0x10000000u | ((uint32)o << 16) | stageObjectIDs[o]);
        }
#endif
        if (objectClassList[stageObjectIDs[o]].stageLoad)
            objectClassList[stageObjectIDs[o]].stageLoad();
    }

#if RETRO_USE_MOD_LOADER
    RunModCallbacks(MODCB_ONSTAGELOAD, NULL);
#endif

    for (int32 e = 0; e < ENTITY_COUNT; ++e) {
        sceneInfo.entitySlot = e;
        sceneInfo.entity     = RSDK_ENTITY_AT(e);

        if (sceneInfo.entity->classID) {
            if (objectClassList[stageObjectIDs[sceneInfo.entity->classID]].create) {
#if defined(P6_SCENE_TEST)
                {
                    extern int32 p6_w_initobj_step;
                    p6_w_initobj_step = (int32)(0x20000000u | ((uint32)sceneInfo.entity->classID << 16) | (uint32)e);
                }
#endif
                sceneInfo.entity->interaction = true;
                objectClassList[stageObjectIDs[sceneInfo.entity->classID]].create(NULL);
            }
        }
    }
#if defined(P6_SCENE_TEST)
    {
        extern int32 p6_w_initobj_step;
        p6_w_initobj_step = 0x7FFFFFFF; // InitObjects completed
    }
#endif

    sceneInfo.state = ENGINESTATE_REGULAR;

    if (!cameraCount)
        AddCamera(&screens[0].position, TO_FIXED(screens[0].center.x), TO_FIXED(screens[0].center.y), false);
}
#if RETRO_PLATFORM == RETRO_SATURN && defined(P6_PERF_OBJPROF)
// Perf Phase 2d (Task #211): file-scope decls for the per-classID Update-timing
// diagnostic (the globals live in p6_perf.c / p6_io_main.cpp; extern "C" escapes
// the RSDK namespace to bind the global symbols).
extern "C" volatile unsigned int p6_perf_vbl_count;
extern "C" int p6_w_objupd_vbl[64];
extern "C" int p6_w_objupd_n[64];
// Phase 2h: FRT microsecond-grade per-Update timing (the vbl counter is too
// coarse once the frame fits ~1 vblank). p6_perf_frt_get() is the coherent
// interrupt-masked 16-bit FRC read; a single Update is < the 78ms /32 wrap so
// (unsigned short)(t1 - t0) is the exact tick delta even across one wrap.
extern "C" unsigned short p6_perf_frt_get(void);
extern "C" int p6_w_objupd_us[64];
// Phase 2i (Task #245): per-loop ProcessObjects sub-phase FRT timing.
extern "C" int p6_w_objsec_loop1;
extern "C" int p6_w_objsec_loop2;
extern "C" int p6_w_objsec_loop3;
// LOCKED-60 (#243): DrawLists sub-attribution -- bubble sort vs draw() callbacks.
extern "C" int p6_w_draw_sort;
extern "C" int p6_w_draw_cb;
extern "C" int p6_w_draw_maxgrp;
extern "C" int p6_w_draw_nents;
// LOCKED-60 (#243): loop1 scan occupancy -- sizes the trim + explains the growth.
extern "C" int p6_w_scan_pop;
extern "C" int p6_w_scan_maxslot;
extern "C" int p6_w_scan_bounds;
#endif
#if RETRO_PLATFORM == RETRO_SATURN && defined(P6_SHADOW_COMPARE)
// LOCKED-60 (#243) scan-split PARITY PROOF -- decls (defs in p6_io_main.cpp).
extern "C" {
    extern int g_p6_shadow_enable;
    extern unsigned char s_p6_shadow_inrange[];
    extern int p6_w_scan_divergence;
    extern int p6_w_scan_divmax;
}
#endif

#if RETRO_PLATFORM == RETRO_SATURN
// Phase 2h (Task #230): the in-range slot list. ProcessObjects ran THREE full
// ENTITY_COUNT (2368) scans/frame through SaturnEntityAt + slow-LWRAM entity
// reads -- but only ~12 entities are in range, so the typeGroup-build and
// lateUpdate passes wasted ~2356 reads each. Loop 1 already computes inRange
// for every slot; we record the in-range slots (in ascending slot order) and
// drive passes 2 + 3 off the list, turning two 2368-scans into two ~12-walks.
// SAFE: inRange is engine-private -- no Mania object writes another entity's
// inRange (verified: every object-level inRange write is a local bool32), so
// the value recorded at each slot's loop-1 visit is the same value the stock
// separate passes would re-read. The full diag sweep (player/continuous/
// ghzlive/collision/entdraw) is the behavioural RED gate on this.
// Phase 2i (Task #245): the in-range slot list is bounded by the camera-local
// population the draw/type groups already cap at 0x100 (TYPEGROUP/DRAWGROUP_
// ENTRY_CAP); a 426x240 maxView holds far fewer than 256 (GHZ peak ~12-100).
// Capping shrinks the array from ENTITY_COUNT int16 AND frees room for the
// _prev buffer below; appends clamp (drop > cap) like the group appends.
#define SATURN_INRANGE_CAP (0x100)
static int16 s_p6_inrange[SATURN_INRANGE_CAP];
static int32 s_p6_inrange_n = 0;
// loop3: last frame's in-range list. onScreen is set during Draw ONLY for
// entities that were in a draw group (== in range), so clearing it over the
// PREVIOUS frame's in-range set is parity-exact with the stock clear-all (every
// other slot is already 0). Carried forward at the end of ProcessObjects.
static int16 s_p6_inrange_prev[SATURN_INRANGE_CAP];
static int32 s_p6_inrange_prev_n = 0;
#endif

void RSDK::ProcessObjects()
{
    for (int32 i = 0; i < DRAWGROUP_COUNT; ++i) drawGroups[i].entityCount = 0;

    for (int32 o = 0; o < sceneInfo.classCount; ++o) {
#if RETRO_USE_MOD_LOADER
        currentObjectID = o;
#endif

        ObjectClass *classInfo = &objectClassList[stageObjectIDs[o]];
        if ((*classInfo->staticVars)->active == ACTIVE_ALWAYS || (*classInfo->staticVars)->active == ACTIVE_NORMAL) {
            if (classInfo->staticUpdate)
                classInfo->staticUpdate();
        }
    }

#if RETRO_USE_MOD_LOADER
    RunModCallbacks(MODCB_ONSTATICUPDATE, INT_TO_VOID(ENGINESTATE_REGULAR));
#endif

    for (int32 s = 0; s < cameraCount; ++s) {
        CameraInfo *camera = &cameras[s];

        if (camera->targetPos) {
            if (camera->worldRelative) {
                camera->position.x = camera->targetPos->x;
                camera->position.y = camera->targetPos->y;
            }
            else {
                camera->position.x = TO_FIXED(camera->targetPos->x);
                camera->position.y = TO_FIXED(camera->targetPos->y);
            }
        }
    }

#if RETRO_PLATFORM == RETRO_SATURN && defined(P6_SHADOW_COMPARE)
    // SCAN-SPLIT PARITY PROOF (#243): classify ALL entities at frame-start (the
    // dual-SH2 scan-split model -- no Update has run; the camera is already updated
    // above so it matches loop1) into s_p6_shadow_inrange[], using the engine's EXACT
    // bounds checks (copied verbatim from the switch below). The real loop1 then
    // classifies interleaved; the compare after it counts where inRange differs --
    // i.e. an entity a mid-frame reposition pushed across its bound (the ONLY way the
    // split could diverge from serial). 0 over real gameplay => split is parity-exact.
    if (g_p6_shadow_enable) {
        uint8 *_shep = (uint8 *)objectEntityList;
        for (int32 e = 0; e < ENTITY_COUNT; ++e) {
            EntityBase *se = (EntityBase *)_shep;
            _shep += (e < RESERVE_ENTITY_COUNT || e >= TEMPENTITY_START)
                         ? (uint32)ENTITY_WIDE_SIZE : (uint32)sizeof(EntityBase);
            uint8 ir = 0;
            if (se->classID) {
                switch (se->active) {
                    case ACTIVE_ALWAYS:
                    case ACTIVE_NORMAL: ir = 1; break;
                    case ACTIVE_BOUNDS:
                        for (int32 s = 0; s < cameraCount; ++s) {
                            int32 sx = abs(se->position.x - cameras[s].position.x);
                            int32 sy = abs(se->position.y - cameras[s].position.y);
                            if (sx <= se->updateRange.x + cameras[s].offset.x
                                && sy <= se->updateRange.y + cameras[s].offset.y) { ir = 1; break; }
                        }
                        break;
                    case ACTIVE_XBOUNDS:
                        for (int32 s = 0; s < cameraCount; ++s) {
                            int32 sx = abs(se->position.x - cameras[s].position.x);
                            if (sx <= se->updateRange.x + cameras[s].offset.x) { ir = 1; break; }
                        }
                        break;
                    case ACTIVE_YBOUNDS:
                        for (int32 s = 0; s < cameraCount; ++s) {
                            int32 sy = abs(se->position.y - cameras[s].position.y);
                            if (sy <= se->updateRange.y + cameras[s].offset.y) { ir = 1; break; }
                        }
                        break;
                    case ACTIVE_RBOUNDS:
                        for (int32 s = 0; s < cameraCount; ++s) {
                            int32 sx = FROM_FIXED(abs(se->position.x - cameras[s].position.x));
                            int32 sy = FROM_FIXED(abs(se->position.y - cameras[s].position.y));
                            if (sx * sx + sy * sy <= se->updateRange.x + cameras[s].offset.x) { ir = 1; break; }
                        }
                        break;
                    default: ir = 0; break;
                }
            }
            s_p6_shadow_inrange[e] = ir;
        }
    }
#endif
    sceneInfo.entitySlot = 0;
#if RETRO_PLATFORM == RETRO_SATURN
    s_p6_inrange_n = 0; // Phase 2h: rebuild the in-range slot list this frame
    // Phase 2i (Task #245): advance the entity pointer by the dual-stride pool's
    // region size (wide reserve/temp, narrow scene) instead of recomputing
    // RSDK_ENTITY_AT(e) (the SaturnEntityAt branch+multiply) every slot. _ep ==
    // RSDK_ENTITY_AT(e) at each slot -> parity-exact, just cheaper addressing on
    // the hottest ENTITY_COUNT scan.
    uint8 *_ep = (uint8 *)objectEntityList;
#endif
#if RETRO_PLATFORM == RETRO_SATURN && defined(P6_PERF_OBJPROF)
    // Phase 2i (Task #245): bracket the three internal loops to split the
    // ProcessObjects FRT cost (loop1 inRange-scan+Update / loop2 typeGroup /
    // loop3 lateUpdate-scan). Diagnostic only.
    unsigned short _ps_tA = p6_perf_frt_get(), _ps_tB = _ps_tA, _ps_tC = _ps_tA, _ps_tD = _ps_tA;
    p6_w_scan_pop = 0; p6_w_scan_maxslot = 0; p6_w_scan_bounds = 0;
#endif
    for (int32 e = 0; e < ENTITY_COUNT; ++e) {
#if RETRO_PLATFORM == RETRO_SATURN
        sceneInfo.entity = (EntityBase *)_ep;
        _ep += (e < RESERVE_ENTITY_COUNT || e >= TEMPENTITY_START)
                   ? (uint32)ENTITY_WIDE_SIZE : (uint32)sizeof(EntityBase);
#else
        sceneInfo.entity = RSDK_ENTITY_AT(e);
#endif
        if (sceneInfo.entity->classID) {
#if RETRO_PLATFORM == RETRO_SATURN && defined(P6_PERF_OBJPROF)
            ++p6_w_scan_pop; p6_w_scan_maxslot = e;
            { uint8 _av = (uint8)sceneInfo.entity->active;
              if (_av >= ACTIVE_BOUNDS && _av <= ACTIVE_RBOUNDS) ++p6_w_scan_bounds; }
#endif
            switch (sceneInfo.entity->active) {
                default:
                case ACTIVE_DISABLED: break;

                case ACTIVE_NEVER:
                case ACTIVE_PAUSED: sceneInfo.entity->inRange = false; break;

                case ACTIVE_ALWAYS:
                case ACTIVE_NORMAL: sceneInfo.entity->inRange = true; break;

                case ACTIVE_BOUNDS:
                    sceneInfo.entity->inRange = false;

                    for (int32 s = 0; s < cameraCount; ++s) {
                        int32 sx = abs(sceneInfo.entity->position.x - cameras[s].position.x);
                        int32 sy = abs(sceneInfo.entity->position.y - cameras[s].position.y);

                        if (sx <= sceneInfo.entity->updateRange.x + cameras[s].offset.x
                            && sy <= sceneInfo.entity->updateRange.y + cameras[s].offset.y) {
                            sceneInfo.entity->inRange = true;
                            break;
                        }
                    }
                    break;

                case ACTIVE_XBOUNDS:
                    sceneInfo.entity->inRange = false;

                    for (int32 s = 0; s < cameraCount; ++s) {
                        int32 sx = abs(sceneInfo.entity->position.x - cameras[s].position.x);

                        if (sx <= sceneInfo.entity->updateRange.x + cameras[s].offset.x) {
                            sceneInfo.entity->inRange = true;
                            break;
                        }
                    }
                    break;

                case ACTIVE_YBOUNDS:
                    sceneInfo.entity->inRange = false;

                    for (int32 s = 0; s < cameraCount; ++s) {
                        int32 sy = abs(sceneInfo.entity->position.y - cameras[s].position.y);

                        if (sy <= sceneInfo.entity->updateRange.y + cameras[s].offset.y) {
                            sceneInfo.entity->inRange = true;
                            break;
                        }
                    }
                    break;

                case ACTIVE_RBOUNDS:
                    sceneInfo.entity->inRange = false;

                    for (int32 s = 0; s < cameraCount; ++s) {
                        int32 sx = FROM_FIXED(abs(sceneInfo.entity->position.x - cameras[s].position.x));
                        int32 sy = FROM_FIXED(abs(sceneInfo.entity->position.y - cameras[s].position.y));

                        if (sx * sx + sy * sy <= sceneInfo.entity->updateRange.x + cameras[s].offset.x) {
                            sceneInfo.entity->inRange = true;
                            break;
                        }
                    }
                    break;
            }

            if (sceneInfo.entity->inRange) {
#if RETRO_PLATFORM == RETRO_SATURN && defined(P6_PERF_OBJPROF)
                // Perf Phase 2d (Task #211): per-classID Update timing via the
                // overflow-immune 60Hz vblank counter (an Update can exceed the
                // FRT 78ms range). Diagnostic only -- gated by P6_PERF_OBJPROF.
                // (decls = the file-scope extern "C" block above ProcessObjects)
                unsigned int _ov0 = p6_perf_vbl_count;
                unsigned short _of0 = p6_perf_frt_get();
                if (objectClassList[stageObjectIDs[sceneInfo.entity->classID]].update)
                    objectClassList[stageObjectIDs[sceneInfo.entity->classID]].update();
                {
                    unsigned short _of1 = p6_perf_frt_get();
                    int _oc = sceneInfo.entity->classID & 0x3F;
                    p6_w_objupd_vbl[_oc] += (int)(p6_perf_vbl_count - _ov0);
                    p6_w_objupd_us[_oc]  += (int)(unsigned short)(_of1 - _of0);
                    p6_w_objupd_n[_oc]++;
                }
#else
                if (objectClassList[stageObjectIDs[sceneInfo.entity->classID]].update)
                    objectClassList[stageObjectIDs[sceneInfo.entity->classID]].update();
#endif

#if RETRO_PLATFORM == RETRO_SATURN
                // P6.7b (Task #210): the Saturn group lists are entry-capped
                // (SaturnMemoryMap.h P68_DRAWGROUP_ENTRY_CAP at the P6.8
                // flip); an uncapped write past entries[] is the silent
                // .bss-corruption class (Phase 1.4-1.15). Clamp -- a full
                // list drops the entity from DRAW this frame (visible,
                // diagnosable) instead of corrupting the adjacent group.
                if (sceneInfo.entity->drawGroup < DRAWGROUP_COUNT
                    && drawGroups[sceneInfo.entity->drawGroup].entityCount < ENTITY_COUNT)
#else
                if (sceneInfo.entity->drawGroup < DRAWGROUP_COUNT)
#endif
                    RSDK_DRAWGROUP_APPEND(drawGroups[sceneInfo.entity->drawGroup], sceneInfo.entitySlot);
            }
        }
        else {
            sceneInfo.entity->inRange = false;
        }

#if RETRO_PLATFORM == RETRO_SATURN
        // Phase 2h: record this slot if it ended the pass in range (read AFTER
        // its update() so a self-deactivation is honoured -- same value the
        // stock separate passes re-read). Drives passes 2 + 3 off the list.
        if (sceneInfo.entity->classID && sceneInfo.entity->inRange
            && s_p6_inrange_n < SATURN_INRANGE_CAP)
            s_p6_inrange[s_p6_inrange_n++] = (int16)e;
#endif

        sceneInfo.entitySlot++;
    }
#if RETRO_PLATFORM == RETRO_SATURN && defined(P6_SHADOW_COMPARE)
    // SCAN-SPLIT PARITY: compare the real interleaved inRange (loop1 sets it ONCE in
    // the switch, NOT re-evaluated after the own-update move) to the frame-start
    // shadow. A mismatch == an entity an EARLIER entity's update repositioned across
    // its bound before this entity's classification -> the only split divergence.
    if (g_p6_shadow_enable) {
        int32 _div = 0; uint8 *_cep = (uint8 *)objectEntityList;
        for (int32 e = 0; e < ENTITY_COUNT; ++e) {
            EntityBase *ce = (EntityBase *)_cep;
            _cep += (e < RESERVE_ENTITY_COUNT || e >= TEMPENTITY_START)
                        ? (uint32)ENTITY_WIDE_SIZE : (uint32)sizeof(EntityBase);
            if (ce->classID && (uint8)(ce->inRange ? 1 : 0) != s_p6_shadow_inrange[e]) ++_div;
        }
        p6_w_scan_divergence = _div;
        if (_div > p6_w_scan_divmax) p6_w_scan_divmax = _div;
    }
#endif
#if RETRO_PLATFORM == RETRO_SATURN && defined(P6_PERF_OBJPROF)
    _ps_tB = p6_perf_frt_get(); // end loop1 (inRange scan + Update + drawgroup)
#endif

#if RETRO_USE_MOD_LOADER
    RunModCallbacks(MODCB_ONUPDATE, INT_TO_VOID(ENGINESTATE_REGULAR));
#endif

    for (int32 i = 0; i < TYPEGROUP_COUNT; ++i) typeGroups[i].entryCount = 0;

#if RETRO_PLATFORM == RETRO_SATURN
    // Phase 2h: drive the typeGroup rebuild off the in-range list (ascending
    // slot order == the stock full-scan order) instead of re-scanning all 2368
    // slots to act on ~12. Entry-cap clamps per SaturnMemoryMap.h (P6.7b).
    for (int32 li = 0; li < s_p6_inrange_n; ++li) {
        int32 e = s_p6_inrange[li];
        sceneInfo.entitySlot = e;
        sceneInfo.entity = RSDK_ENTITY_AT(e);

        if (sceneInfo.entity->interaction) { // inRange guaranteed by list membership
            if (typeGroups[GROUP_ALL].entryCount < ENTITY_COUNT)
                RSDK_TYPEGROUP_APPEND(typeGroups[GROUP_ALL], e);

            if (typeGroups[sceneInfo.entity->classID].entryCount < ENTITY_COUNT)
                RSDK_TYPEGROUP_APPEND(typeGroups[sceneInfo.entity->classID], e);

            if (sceneInfo.entity->group >= TYPE_COUNT
                && typeGroups[sceneInfo.entity->group].entryCount < ENTITY_COUNT)
                RSDK_TYPEGROUP_APPEND(typeGroups[sceneInfo.entity->group], e);
        }
    }
#else
    sceneInfo.entitySlot = 0;
    for (int32 e = 0; e < ENTITY_COUNT; ++e) {
        sceneInfo.entity = RSDK_ENTITY_AT(e);

        if (sceneInfo.entity->inRange && sceneInfo.entity->interaction) {
            RSDK_TYPEGROUP_APPEND(typeGroups[GROUP_ALL], e); // All active objects

            RSDK_TYPEGROUP_APPEND(typeGroups[sceneInfo.entity->classID], e); // class-based groups

            if (sceneInfo.entity->group >= TYPE_COUNT)
                RSDK_TYPEGROUP_APPEND(typeGroups[sceneInfo.entity->group], e); // extra groups
        }

        sceneInfo.entitySlot++;
    }
#endif
#if RETRO_PLATFORM == RETRO_SATURN && defined(P6_PERF_OBJPROF)
    _ps_tC = p6_perf_frt_get(); // end loop2 (typeGroup build)
#endif

#if RETRO_PLATFORM == RETRO_SATURN
    // Phase 2i (Task #245): loop3 off the in-range lists, not a full ENTITY_COUNT
    // scan. lateUpdate over THIS frame's in-range (post-all-updates; inRange is
    // engine-private so the loop1 snapshot == the live set, ascending-slot order
    // == the stock scan order). onScreen was set during LAST frame's Draw ONLY
    // for entities then in a draw group == last frame's in-range, so clearing it
    // over s_p6_inrange_prev is parity-exact with the stock clear-all (every
    // other slot is already 0). Carry this frame's list forward for next frame.
    for (int32 li = 0; li < s_p6_inrange_n; ++li) {
        int32 e         = s_p6_inrange[li];
        sceneInfo.entitySlot = e;
        sceneInfo.entity = RSDK_ENTITY_AT(e);
        if (objectClassList[stageObjectIDs[sceneInfo.entity->classID]].lateUpdate)
            objectClassList[stageObjectIDs[sceneInfo.entity->classID]].lateUpdate();
    }
    for (int32 li = 0; li < s_p6_inrange_prev_n; ++li)
        RSDK_ENTITY_AT(s_p6_inrange_prev[li])->onScreen = 0;
    for (int32 li = 0; li < s_p6_inrange_n; ++li) s_p6_inrange_prev[li] = s_p6_inrange[li];
    s_p6_inrange_prev_n = s_p6_inrange_n;
#else
    sceneInfo.entitySlot = 0;
    for (int32 e = 0; e < ENTITY_COUNT; ++e) {
        sceneInfo.entity = RSDK_ENTITY_AT(e);

        if (sceneInfo.entity->inRange) {
            if (objectClassList[stageObjectIDs[sceneInfo.entity->classID]].lateUpdate)
                objectClassList[stageObjectIDs[sceneInfo.entity->classID]].lateUpdate();
        }

        sceneInfo.entity->onScreen = 0;
        sceneInfo.entitySlot++;
    }
#endif
#if RETRO_PLATFORM == RETRO_SATURN && defined(P6_PERF_OBJPROF)
    _ps_tD = p6_perf_frt_get(); // end loop3 (list-driven lateUpdate + onScreen)
    p6_w_objsec_loop1 = (int)(unsigned short)(_ps_tB - _ps_tA);
    p6_w_objsec_loop2 = (int)(unsigned short)(_ps_tC - _ps_tB);
    p6_w_objsec_loop3 = (int)(unsigned short)(_ps_tD - _ps_tC);
#endif

#if RETRO_USE_MOD_LOADER
    RunModCallbacks(MODCB_ONLATEUPDATE, INT_TO_VOID(ENGINESTATE_REGULAR));
#endif
}
void RSDK::ProcessPausedObjects()
{
    for (int32 i = 0; i < DRAWGROUP_COUNT; ++i) drawGroups[i].entityCount = 0;

    for (int32 o = 0; o < sceneInfo.classCount; ++o) {
#if RETRO_USE_MOD_LOADER
        currentObjectID = o;
#endif

        ObjectClass *classInfo = &objectClassList[stageObjectIDs[o]];
        if ((*classInfo->staticVars)->active == ACTIVE_ALWAYS || (*classInfo->staticVars)->active == ACTIVE_PAUSED) {
            if (classInfo->staticUpdate)
                classInfo->staticUpdate();
        }
    }

#if RETRO_USE_MOD_LOADER
    RunModCallbacks(MODCB_ONSTATICUPDATE, INT_TO_VOID(ENGINESTATE_PAUSED));
#endif

    sceneInfo.entitySlot = 0;
    for (int32 e = 0; e < ENTITY_COUNT; ++e) {
        sceneInfo.entity = RSDK_ENTITY_AT(e);

        if (sceneInfo.entity->classID) {
            if (sceneInfo.entity->active == ACTIVE_ALWAYS || sceneInfo.entity->active == ACTIVE_PAUSED) {
                if (objectClassList[stageObjectIDs[sceneInfo.entity->classID]].update)
                    objectClassList[stageObjectIDs[sceneInfo.entity->classID]].update();

                if (sceneInfo.entity->drawGroup < DRAWGROUP_COUNT)
                    RSDK_DRAWGROUP_APPEND(drawGroups[sceneInfo.entity->drawGroup], sceneInfo.entitySlot);
            }
        }
        else {
            sceneInfo.entity->inRange = false;
        }

        sceneInfo.entitySlot++;
    }

#if RETRO_USE_MOD_LOADER
    RunModCallbacks(MODCB_ONUPDATE, INT_TO_VOID(ENGINESTATE_PAUSED));
#endif

    sceneInfo.entitySlot = 0;
    for (int32 e = 0; e < ENTITY_COUNT; ++e) {
        sceneInfo.entity = RSDK_ENTITY_AT(e);

        if (sceneInfo.entity->active == ACTIVE_ALWAYS || sceneInfo.entity->active == ACTIVE_PAUSED) {
            if (objectClassList[stageObjectIDs[sceneInfo.entity->classID]].lateUpdate)
                objectClassList[stageObjectIDs[sceneInfo.entity->classID]].lateUpdate();
        }

        sceneInfo.entity->onScreen = 0;
        sceneInfo.entitySlot++;
    }

#if RETRO_USE_MOD_LOADER
    RunModCallbacks(MODCB_ONLATEUPDATE, INT_TO_VOID(ENGINESTATE_PAUSED));
#endif
}
void RSDK::ProcessFrozenObjects()
{
    for (int32 i = 0; i < DRAWGROUP_COUNT; ++i) drawGroups[i].entityCount = 0;

    for (int32 o = 0; o < sceneInfo.classCount; ++o) {
#if RETRO_USE_MOD_LOADER
        currentObjectID = o;
#endif

        ObjectClass *classInfo = &objectClassList[stageObjectIDs[o]];
        if ((*classInfo->staticVars)->active == ACTIVE_ALWAYS || (*classInfo->staticVars)->active == ACTIVE_PAUSED) {
            if (classInfo->staticUpdate)
                classInfo->staticUpdate();
        }
    }

#if RETRO_USE_MOD_LOADER
    RunModCallbacks(MODCB_ONSTATICUPDATE, INT_TO_VOID(ENGINESTATE_FROZEN));
#endif

    for (int32 s = 0; s < cameraCount; ++s) {
        CameraInfo *camera = &cameras[s];

        if (camera->targetPos) {
            if (camera->worldRelative) {
                camera->position.x = camera->targetPos->x;
                camera->position.y = camera->targetPos->y;
            }
            else {
                camera->position.x = TO_FIXED(camera->targetPos->x);
                camera->position.y = TO_FIXED(camera->targetPos->y);
            }
        }
    }

    sceneInfo.entitySlot = 0;
    for (int32 e = 0; e < ENTITY_COUNT; ++e) {
        sceneInfo.entity = RSDK_ENTITY_AT(e);

        if (sceneInfo.entity->classID) {
            switch (sceneInfo.entity->active) {
                default:
                case ACTIVE_DISABLED: break;

                case ACTIVE_NEVER:
                case ACTIVE_PAUSED: sceneInfo.entity->inRange = false; break;

                case ACTIVE_ALWAYS:
                case ACTIVE_NORMAL: sceneInfo.entity->inRange = true; break;

                case ACTIVE_BOUNDS:
                    sceneInfo.entity->inRange = false;

                    for (int32 s = 0; s < cameraCount; ++s) {
                        int32 sx = abs(sceneInfo.entity->position.x - cameras[s].position.x);
                        int32 sy = abs(sceneInfo.entity->position.y - cameras[s].position.y);

                        if (sx <= sceneInfo.entity->updateRange.x + cameras[s].offset.x
                            && sy <= sceneInfo.entity->updateRange.y + cameras[s].offset.y) {
                            sceneInfo.entity->inRange = true;
                            break;
                        }
                    }
                    break;

                case ACTIVE_XBOUNDS:
                    sceneInfo.entity->inRange = false;

                    for (int32 s = 0; s < cameraCount; ++s) {
                        int32 sx = abs(sceneInfo.entity->position.x - cameras[s].position.x);

                        if (sx <= sceneInfo.entity->updateRange.x + cameras[s].offset.x) {
                            sceneInfo.entity->inRange = true;
                            break;
                        }
                    }
                    break;

                case ACTIVE_YBOUNDS:
                    sceneInfo.entity->inRange = false;

                    for (int32 s = 0; s < cameraCount; ++s) {
                        int32 sy = abs(sceneInfo.entity->position.y - cameras[s].position.y);

                        if (sy <= sceneInfo.entity->updateRange.y + cameras[s].offset.y) {
                            sceneInfo.entity->inRange = true;
                            break;
                        }
                    }
                    break;

                case ACTIVE_RBOUNDS:
                    sceneInfo.entity->inRange = false;

                    for (int32 s = 0; s < cameraCount; ++s) {
                        int32 sx = FROM_FIXED(abs(sceneInfo.entity->position.x - cameras[s].position.x));
                        int32 sy = FROM_FIXED(abs(sceneInfo.entity->position.y - cameras[s].position.y));

                        if (sx * sx + sy * sy <= sceneInfo.entity->updateRange.x + cameras[s].offset.x) {
                            sceneInfo.entity->inRange = true;
                            break;
                        }
                    }
                    break;
            }

            if (sceneInfo.entity->inRange) {
                if (sceneInfo.entity->active == ACTIVE_ALWAYS || sceneInfo.entity->active == ACTIVE_PAUSED) {
                    if (objectClassList[stageObjectIDs[sceneInfo.entity->classID]].update)
                        objectClassList[stageObjectIDs[sceneInfo.entity->classID]].update();
                }

                if (sceneInfo.entity->drawGroup < DRAWGROUP_COUNT)
                    RSDK_DRAWGROUP_APPEND(drawGroups[sceneInfo.entity->drawGroup], sceneInfo.entitySlot);
            }
        }
        else {
            sceneInfo.entity->inRange = false;
        }

        sceneInfo.entitySlot++;
    }

#if RETRO_USE_MOD_LOADER
    RunModCallbacks(MODCB_ONUPDATE, INT_TO_VOID(ENGINESTATE_FROZEN));
#endif

    for (int32 i = 0; i < TYPEGROUP_COUNT; ++i) typeGroups[i].entryCount = 0;

    sceneInfo.entitySlot = 0;
    for (int32 e = 0; e < ENTITY_COUNT; ++e) {
        sceneInfo.entity = RSDK_ENTITY_AT(e);

        if (sceneInfo.entity->inRange) {
            if (sceneInfo.entity->active == ACTIVE_ALWAYS || sceneInfo.entity->active == ACTIVE_PAUSED) {
                if (objectClassList[stageObjectIDs[sceneInfo.entity->classID]].lateUpdate)
                    objectClassList[stageObjectIDs[sceneInfo.entity->classID]].lateUpdate();
            }

            if (sceneInfo.entity->interaction) {
                RSDK_TYPEGROUP_APPEND(typeGroups[GROUP_ALL], e); // All active entities

                RSDK_TYPEGROUP_APPEND(typeGroups[sceneInfo.entity->classID], e); // type-based groups

                if (sceneInfo.entity->group >= TYPE_COUNT)
                    RSDK_TYPEGROUP_APPEND(typeGroups[sceneInfo.entity->group], e); // extra groups
            }
        }

        sceneInfo.entity->onScreen = 0;
        sceneInfo.entitySlot++;
    }

#if RETRO_USE_MOD_LOADER
    RunModCallbacks(MODCB_ONLATEUPDATE, INT_TO_VOID(ENGINESTATE_FROZEN));
#endif
}
void RSDK::ProcessObjectDrawLists()
{
    if (sceneInfo.state != ENGINESTATE_LOAD && sceneInfo.state != (ENGINESTATE_LOAD | ENGINESTATE_STEPOVER)) {
#if RETRO_PLATFORM == RETRO_SATURN && defined(P6_PERF_OBJPROF)
        // LOCKED-60 (#243): per-frame reset of the DrawLists sub-attribution witnesses.
        p6_w_draw_sort = 0; p6_w_draw_cb = 0; p6_w_draw_maxgrp = 0; p6_w_draw_nents = 0;
#endif
        for (int32 s = 0; s < videoSettings.screenCount; ++s) {
            currentScreen             = &screens[s];
            sceneInfo.currentScreenID = s;

            for (int32 l = 0; l < DRAWGROUP_COUNT; ++l) drawGroups[l].layerCount = 0;

            for (int32 t = 0; t < LAYER_COUNT; ++t) {
                uint8 drawGroup = tileLayers[t].drawGroup[s];

                if (drawGroup < DRAWGROUP_COUNT)
                    drawGroups[drawGroup].layerDrawList[drawGroups[drawGroup].layerCount++] = t;
            }

            sceneInfo.currentDrawGroup = 0;
            for (int32 l = 0; l < DRAWGROUP_COUNT; ++l) {
                if (engine.drawGroupVisible[l]) {
                    DrawList *list = &drawGroups[l];

                    if (list->hookCB)
                        list->hookCB();

#if RETRO_PLATFORM == RETRO_SATURN && defined(P6_PERF_OBJPROF)
                    if (list->entityCount > p6_w_draw_maxgrp) p6_w_draw_maxgrp = list->entityCount;
                    p6_w_draw_nents += list->entityCount;
                    unsigned short _ds0 = p6_perf_frt_get();
#endif
                    if (list->sorted) {
                        for (int32 e = 0; e < list->entityCount; ++e) {
                            for (int32 i = list->entityCount - 1; i > e; --i) {
                                int32 slot1 = list->entries[i - 1];
                                int32 slot2 = list->entries[i];
                                if (RSDK_ENTITY_AT(slot2)->zdepth > RSDK_ENTITY_AT(slot1)->zdepth) {
                                    list->entries[i - 1] = slot2;
                                    list->entries[i]     = slot1;
                                }
                            }
                        }
                    }
#if RETRO_PLATFORM == RETRO_SATURN && defined(P6_PERF_OBJPROF)
                    { unsigned short _ds1 = p6_perf_frt_get();
                      p6_w_draw_sort += (int)(unsigned short)(_ds1 - _ds0); _ds0 = _ds1; }
#endif

                    for (int32 i = 0; i < list->entityCount; ++i) {
                        sceneInfo.entitySlot = list->entries[i];
                        validDraw            = false;
                        sceneInfo.entity     = RSDK_ENTITY_AT(list->entries[i]);
                        if (sceneInfo.entity->visible) {
                            if (objectClassList[stageObjectIDs[sceneInfo.entity->classID]].draw)
                                objectClassList[stageObjectIDs[sceneInfo.entity->classID]].draw();

#if RETRO_VER_EGS || RETRO_USE_DUMMY_ACHIEVEMENTS
                            if (i == list->entityCount - 1)
                                SKU::DrawAchievements();
#endif

                            sceneInfo.entity->onScreen |= validDraw << sceneInfo.currentScreenID;
                        }
                    }
#if RETRO_PLATFORM == RETRO_SATURN && defined(P6_PERF_OBJPROF)
                    p6_w_draw_cb += (int)(unsigned short)(p6_perf_frt_get() - _ds0);
#endif

                    for (int32 i = 0; i < list->layerCount; ++i) {
                        TileLayer *layer = &tileLayers[list->layerDrawList[i]];

#if RETRO_USE_MOD_LOADER
                        RunModCallbacks(MODCB_ONSCANLINECB, (void *)layer->scanlineCallback);
#endif
                        if (layer->scanlineCallback)
                            layer->scanlineCallback(scanlines);
                        else
                            ProcessParallax(layer);

#if RETRO_PLATFORM != RETRO_SATURN
                        // P4 data retarget (Task #203): the DrawLayer* software rasterizers
                        // are the ONLY writers of ScreenInfo.frameBuffer, which is Saturn-
                        // stubbed to [1] (Drawing.hpp). On Saturn, tile layers render via
                        // VDP2 NBG hardware, so this software path is dead -- gate it off so
                        // it can never write past the stubbed buffer (the Phase 1.4-1.15
                        // .bss-corruption class). ProcessParallax above still runs (it writes
                        // scanlines[], not frameBuffer). P6 RESTORATION: drop the guard.
                        switch (layer->type) {
                            case LAYER_HSCROLL: DrawLayerHScroll(layer); break;
                            case LAYER_VSCROLL: DrawLayerVScroll(layer); break;
                            case LAYER_ROTOZOOM: DrawLayerRotozoom(layer); break;
                            case LAYER_BASIC: DrawLayerBasic(layer); break;
                            default: break;
                        }
#endif
                    }

#if RETRO_USE_MOD_LOADER
                    RunModCallbacks(MODCB_ONDRAW, INT_TO_VOID(l));
#endif

                    if (currentScreen->clipBound_X1 > 0)
                        currentScreen->clipBound_X1 = 0;

                    if (currentScreen->clipBound_Y1 > 0)
                        currentScreen->clipBound_Y1 = 0;

                    if (currentScreen->size.x >= 0) {
                        if (currentScreen->clipBound_X2 < currentScreen->size.x)
                            currentScreen->clipBound_X2 = currentScreen->size.x;
                    }
                    else {
                        currentScreen->clipBound_X2 = 0;
                    }

                    if (currentScreen->size.y >= 0) {
                        if (currentScreen->clipBound_Y2 < currentScreen->size.y)
                            currentScreen->clipBound_Y2 = currentScreen->size.y;
                    }
                    else {
                        currentScreen->clipBound_Y2 = 0;
                    }
                }

                sceneInfo.currentDrawGroup++;
            }

#if !RETRO_USE_ORIGINAL_CODE
            if (engine.showUpdateRanges) {
                for (int32 l = 0; l < DRAWGROUP_COUNT; ++l) {
                    if (engine.drawGroupVisible[l]) {
                        DrawList *list = &drawGroups[l];
                        for (int32 i = 0; i < list->entityCount; ++i) {
                            Entity *entity     = RSDK_ENTITY_AT(list->entries[i]);

                            if (entity->visible || (engine.showUpdateRanges & 2)) {
                                switch (entity->active) {
                                    default:
                                    case ACTIVE_DISABLED:
                                    case ACTIVE_NEVER: break;

                                    case ACTIVE_ALWAYS:
                                    case ACTIVE_NORMAL: 
                                    case ACTIVE_PAUSED:
                                        DrawRectangle(entity->position.x, entity->position.y, TO_FIXED(1), TO_FIXED(1), 0x0000FF, 0xFF, INK_NONE,
                                                      false);
                                        break;

                                    case ACTIVE_BOUNDS:
                                        DrawLine(entity->position.x - entity->updateRange.x, entity->position.y - entity->updateRange.y,
                                                 entity->position.x + entity->updateRange.x, entity->position.y - entity->updateRange.y, 0x0000FF,
                                                 0xFF, INK_NONE, false);

                                        DrawLine(entity->position.x - entity->updateRange.x, entity->position.y + entity->updateRange.y,
                                                 entity->position.x + entity->updateRange.x, entity->position.y + entity->updateRange.y, 0x0000FF,
                                                 0xFF, INK_NONE, false);

                                        DrawLine(entity->position.x - entity->updateRange.x, entity->position.y - entity->updateRange.y,
                                                 entity->position.x - entity->updateRange.x, entity->position.y + entity->updateRange.y, 0x0000FF,
                                                 0xFF, INK_NONE, false);

                                        DrawLine(entity->position.x + entity->updateRange.x, entity->position.y - entity->updateRange.y,
                                                 entity->position.x + entity->updateRange.x, entity->position.y + entity->updateRange.y, 0x0000FF,
                                                 0xFF, INK_NONE, false);
                                        break;

                                    case ACTIVE_XBOUNDS:
                                        DrawLine(entity->position.x - entity->updateRange.x, TO_FIXED(currentScreen->position.y),
                                                 entity->position.x - entity->updateRange.x,
                                                 TO_FIXED(currentScreen->position.y + currentScreen->size.y), 0x0000FF, 0xFF, INK_NONE, false);

                                        DrawLine(entity->position.x + entity->updateRange.x, TO_FIXED(currentScreen->position.y),
                                                 entity->position.x + entity->updateRange.x,
                                                 TO_FIXED(currentScreen->position.y + currentScreen->size.y), 0x0000FF, 0xFF, INK_NONE, false);
                                        break;

                                    case ACTIVE_YBOUNDS:
                                        DrawLine(TO_FIXED(currentScreen->position.x), entity->position.y - entity->updateRange.y,
                                                 TO_FIXED(currentScreen->position.x + currentScreen->size.x),
                                                 entity->position.y - entity->updateRange.y, 0x0000FF, 0xFF, INK_NONE, false);

                                        DrawLine(TO_FIXED(currentScreen->position.x), entity->position.y + entity->updateRange.y,
                                                 TO_FIXED(currentScreen->position.x + currentScreen->size.x),
                                                 entity->position.y + entity->updateRange.y, 0x0000FF, 0xFF, INK_NONE, false);
                                        break;

                                    case ACTIVE_RBOUNDS:
                                        DrawCircleOutline(entity->position.x, entity->position.y, FROM_FIXED(entity->updateRange.x),
                                                          FROM_FIXED(entity->updateRange.x) + 1, 0x0000FF, 0xFF, INK_NONE, false);
                                        break;
                                }
                            }
                        }
                    }
                }
            }

            if (engine.showEntityInfo) {
                for (int32 l = 0; l < DRAWGROUP_COUNT; ++l) {
                    if (engine.drawGroupVisible[l]) {
                        DrawList *list = &drawGroups[l];
                        for (int32 i = 0; i < list->entityCount; ++i) {
                            Entity *entity = RSDK_ENTITY_AT(list->entries[i]);

                            if (entity->visible || (engine.showEntityInfo & 2)) {
                                char buffer[0x100];
                                sprintf_s(buffer, sizeof(buffer), "%s\nx: %g\ny: %g", objectClassList[stageObjectIDs[entity->classID]].name,
                                          entity->position.x / 65536.0f, entity->position.y / 65536.0f);

                                DrawDevString(buffer, FROM_FIXED(entity->position.x) - currentScreen->position.x,
                                              FROM_FIXED(entity->position.y) - currentScreen->position.y, ALIGN_LEFT, 0xF0F0F0);
                            }
                        }
                    }
                }
            }

            if (showHitboxes) {
                for (int32 i = 0; i < debugHitboxCount; ++i) {
                    DebugHitboxInfo *info = &debugHitboxList[i];
                    int32 x               = info->pos.x + TO_FIXED(info->hitbox.left);
                    int32 y               = info->pos.y + TO_FIXED(info->hitbox.top);
                    int32 w               = abs((info->pos.x + TO_FIXED(info->hitbox.right)) - x);
                    int32 h               = abs((info->pos.y + TO_FIXED(info->hitbox.bottom)) - y);

                    switch (info->type) {
                        case H_TYPE_TOUCH: DrawRectangle(x, y, w, h, info->collision ? 0x808000 : 0xFF0000, 0x60, INK_ALPHA, false); break;

                        case H_TYPE_CIRCLE:
                            DrawCircle(info->pos.x, info->pos.y, info->hitbox.left, info->collision ? 0x808000 : 0xFF0000, 0x60, INK_ALPHA, false);
                            break;

                        case H_TYPE_BOX:
                            DrawRectangle(x, y, w, h, 0x0000FF, 0x60, INK_ALPHA, false);

                            if (info->collision & 1) // top
                                DrawRectangle(x, y, w, TO_FIXED(1), 0xFFFF00, 0xC0, INK_ALPHA, false);

                            if (info->collision & 8) // bottom
                                DrawRectangle(x, y + h, w, TO_FIXED(1), 0xFFFF00, 0xC0, INK_ALPHA, false);

                            if (info->collision & 2) { // left
                                int32 sy = y;
                                int32 sh = h;

                                if (info->collision & 1) {
                                    sy += TO_FIXED(1);
                                    sh -= TO_FIXED(1);
                                }

                                if (info->collision & 8)
                                    sh -= TO_FIXED(1);

                                DrawRectangle(x, sy, TO_FIXED(1), sh, 0xFFFF00, 0xC0, INK_ALPHA, false);
                            }

                            if (info->collision & 4) { // right
                                int32 sy = y;
                                int32 sh = h;

                                if (info->collision & 1) {
                                    sy += TO_FIXED(1);
                                    sh -= TO_FIXED(1);
                                }

                                if (info->collision & 8)
                                    sh -= TO_FIXED(1);

                                DrawRectangle(x + w, sy, TO_FIXED(1), sh, 0xFFFF00, 0xC0, INK_ALPHA, false);
                            }
                            break;

                        case H_TYPE_PLAT:
                            DrawRectangle(x, y, w, h, 0x00FF00, 0x60, INK_ALPHA, false);

                            if (info->collision & 1) // top
                                DrawRectangle(x, y, w, TO_FIXED(1), 0xFFFF00, 0xC0, INK_ALPHA, false);

                            if (info->collision & 8) // bottom
                                DrawRectangle(x, y + h, w, TO_FIXED(1), 0xFFFF00, 0xC0, INK_ALPHA, false);
                            break;
                    }
                }
            }

            if (engine.showPaletteOverlay) {
                for (int32 p = 0; p < PALETTE_BANK_COUNT; ++p) {
                    int32 x = (videoSettings.pixWidth - (0x10 << 3));
                    int32 y = (SCREEN_YSIZE - (0x10 << 2));

                    for (int32 c = 0; c < PALETTE_BANK_SIZE; ++c) {
                        uint32 clr = GetPaletteEntry(p, c);

                        DrawRectangle(x + ((c & 0xF) << 1) + ((p % (PALETTE_BANK_COUNT / 2)) * (2 * 16)),
                                      y + ((c >> 4) << 1) + ((p / (PALETTE_BANK_COUNT / 2)) * (2 * 16)), 2, 2, clr, 0xFF, INK_NONE, true);
                    }
                }
            }

#endif

            currentScreen++;
            sceneInfo.currentScreenID++;
        }
    }
}

uint16 RSDK::FindObject(const char *name)
{
    RETRO_HASH_MD5(hash);
    GEN_HASH_MD5(name, hash);

    for (int32 o = 0; o < sceneInfo.classCount; ++o) {
        if (HASH_MATCH_MD5(hash, objectClassList[stageObjectIDs[o]].hash))
            return o;
    }

    return TYPE_DEFAULTOBJECT;
}

int32 RSDK::GetEntityCount(uint16 classID, bool32 isActive)
{
    if (classID >= TYPE_COUNT)
        return 0;
    if (isActive)
        return typeGroups[classID].entryCount;

    int32 entityCount = 0;
    for (int32 i = 0; i < ENTITY_COUNT; ++i) {
        if (RSDK_ENTITY_AT(i)->classID == classID)
            entityCount++;
    }

    return entityCount;
}

void RSDK::ResetEntity(Entity *entity, uint16 classID, void *data)
{
    if (entity) {
        ObjectClass *info = &objectClassList[stageObjectIDs[classID]];
        memset(entity, 0, info->entityClassSize);

        if (info->create) {
            Entity *curEnt = sceneInfo.entity;

            sceneInfo.entity              = entity;
            sceneInfo.entity->interaction = true;
#if RETRO_USE_MOD_LOADER
            int32 superStore          = superLevels[inheritLevel];
            superLevels[inheritLevel] = 0;
#endif
            info->create(data);
#if RETRO_USE_MOD_LOADER
            superLevels[inheritLevel] = superStore;
#endif
            sceneInfo.entity->classID = classID;

            sceneInfo.entity = curEnt;
        }

        entity->classID = classID;
    }
}

#if RETRO_PLATFORM == RETRO_SATURN
// P6.7 Player wave step B: oversize-class-at-narrow-slot refusals (would
// overrun into the adjacent scene slot -- the Phase 1.4-1.15 class). The
// decomp never does this (oversize entities live in reserve/temp by
// construction, see Object.hpp pool comment); nonzero = a wave violated it.
int32 p6_saturn_entity_slot_refusals = 0;
// W14b (Task #227): reserve-slot reset call log -- discriminates "Camera's
// ResetEntitySlot(SLOT_CAMERA1) never issued" from "issued, then the slot
// was clobbered" (MEASURED p6_f9: slot 60 classID 0 at tick time while the
// Camera staticVars are live). Ring of (slot<<16)|classID, reserve slots only.
int32 p6_w_rslot_log[16]  = { 0 };
int32 p6_w_rslot_step[16] = { 0 }; // p6_w_initobj_step at call time (names the caller)
int32 p6_w_rslot_logn     = 0;
#endif

void RSDK::ResetEntitySlot(uint16 slot, uint16 classID, void *data)
{
    ObjectClass *object = &objectClassList[stageObjectIDs[classID]];
    slot                = slot < ENTITY_COUNT ? slot : (ENTITY_COUNT - 1);

#if RETRO_PLATFORM == RETRO_SATURN
    if (object->entityClassSize > sizeof(EntityBase) && slot >= RESERVE_ENTITY_COUNT && slot < TEMPENTITY_START) {
        ++p6_saturn_entity_slot_refusals;
        return;
    }
    if (slot < RESERVE_ENTITY_COUNT && p6_w_rslot_logn < 16) {
        extern int32 p6_w_initobj_step;
        p6_w_rslot_step[p6_w_rslot_logn] = p6_w_initobj_step; // caller phase/class
        p6_w_rslot_log[p6_w_rslot_logn++] = ((int32)slot << 16) | (int32)classID;
    }
#endif

    Entity *entity = RSDK_ENTITY_AT(slot);
    memset(RSDK_ENTITY_AT(slot), 0, object->entityClassSize);

    if (object->create) {
        Entity *curEnt = sceneInfo.entity;

        sceneInfo.entity    = entity;
        entity->interaction = true;
#if RETRO_USE_MOD_LOADER
        int32 superStore          = superLevels[inheritLevel];
        superLevels[inheritLevel] = 0;
#endif
        object->create(data);
#if RETRO_USE_MOD_LOADER
        superLevels[inheritLevel] = superStore;
#endif
        entity->classID = classID;

        sceneInfo.entity = curEnt;
    }
    else {
        entity->classID = classID;
    }
}

Entity *RSDK::CreateEntity(uint16 classID, void *data, int32 x, int32 y)
{
    ObjectClass *object = &objectClassList[stageObjectIDs[classID]];
    Entity *entity      = RSDK_ENTITY_AT(sceneInfo.createSlot);

    int32 permCnt = 0, loopCnt = 0;
    while (entity->classID) {
        // after 16 loops, the game says fuck it and will start overwriting non-temp objects
        if (!entity->isPermanent && loopCnt >= 16)
            break;

        if (entity->isPermanent)
            ++permCnt;

        sceneInfo.createSlot++;
        if (sceneInfo.createSlot == ENTITY_COUNT) {
            sceneInfo.createSlot = TEMPENTITY_START;
            entity               = RSDK_ENTITY_AT(sceneInfo.createSlot);
        }
        else {
            entity = RSDK_ENTITY_AT(sceneInfo.createSlot);
        }

        if (permCnt >= TEMPENTITY_COUNT)
            break;

        ++loopCnt;
    }

    memset(entity, 0, object->entityClassSize);
    entity->position.x  = x;
    entity->position.y  = y;
    entity->interaction = true;

    if (object->create) {
        Entity *curEnt = sceneInfo.entity;

        sceneInfo.entity = entity;
#if RETRO_USE_MOD_LOADER
        int32 superStore          = superLevels[inheritLevel];
        superLevels[inheritLevel] = 0;
#endif
        object->create(data);
#if RETRO_USE_MOD_LOADER
        superLevels[inheritLevel] = superStore;
#endif
        entity->classID = classID;

        sceneInfo.entity = curEnt;
    }
    else {
        entity->classID = classID;
        entity->active  = ACTIVE_NORMAL;
        entity->visible = true;
    }

    return entity;
}

bool32 RSDK::GetActiveEntities(uint16 group, Entity **entity)
{
    if (group >= TYPEGROUP_COUNT)
        return false;

    if (!entity)
        return false;

    if (*entity) {
        ++foreachStackPtr->id;
    }
    else {
        foreachStackPtr++;
        foreachStackPtr->id = 0;
    }

    for (Entity *nextEntity = RSDK_ENTITY_AT(typeGroups[group].entries[foreachStackPtr->id]); foreachStackPtr->id < typeGroups[group].entryCount;
         ++foreachStackPtr->id, nextEntity = RSDK_ENTITY_AT(typeGroups[group].entries[foreachStackPtr->id])) {
        if (nextEntity->classID == group) {
            *entity = nextEntity;
            return true;
        }
    }

    foreachStackPtr--;

    return false;
}
bool32 RSDK::GetAllEntities(uint16 classID, Entity **entity)
{
    if (classID >= OBJECT_COUNT)
        return false;

    if (!entity)
        return false;

    if (*entity) {
        ++foreachStackPtr->id;
    }
    else {
        foreachStackPtr++;
        foreachStackPtr->id = 0;
    }

    for (; foreachStackPtr->id < ENTITY_COUNT; ++foreachStackPtr->id) {
        Entity *nextEntity = RSDK_ENTITY_AT(foreachStackPtr->id);
        if (nextEntity->classID == classID) {
            *entity = nextEntity;
            return true;
        }
    }

    foreachStackPtr--;

    return false;
}

bool32 RSDK::CheckOnScreen(Entity *entity, Vector2 *range)
{
    if (!entity)
        return false;

    if (!range)
        range = &entity->updateRange;

    return CheckPosOnScreen(&entity->position, range);
}
bool32 RSDK::CheckPosOnScreen(Vector2 *position, Vector2 *range)
{
    if (!position || !range)
        return false;

    for (int32 s = 0; s < cameraCount; ++s) {
        int32 sx = abs(position->x - cameras[s].position.x);
        int32 sy = abs(position->y - cameras[s].position.y);

        if (sx <= range->x + cameras[s].offset.x && sy <= range->y + cameras[s].offset.y)
            return true;
    }

    return false;
}

void RSDK::ClearStageObjects()
{
    // Unload static object classes
    for (int32 o = 0; o < sceneInfo.classCount; ++o) {
        if (objectClassList[stageObjectIDs[o]].staticVars) {
            *objectClassList[stageObjectIDs[o]].staticVars = NULL;
        }
    }
}
