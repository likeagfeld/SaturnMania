#ifndef SCENE_H
#define SCENE_H

namespace RSDK
{

#define TILE_COUNT    (0x400)
#define TILE_SIZE     (0x10)
#define TILE_DATASIZE (TILE_SIZE * TILE_SIZE)
#define TILESET_SIZE  (TILE_COUNT * TILE_DATASIZE)

#if RETRO_PLATFORM == RETRO_SATURN
// P4 data retarget (Task #203): on Saturn tilesetPixels stores ONLY the base
// (FLIP_NONE) tile region. The stock x4 holds software pre-flipped copies
// (FLIP_X/Y/XY) consumed by the C software tile renderer (DrawLayer*, Scene.cpp
// :1294-2069). On Saturn the VDP2 cell/scroll hardware performs H/V tile flips
// from its own per-cell flip bits, so the pre-expansion is dead weight; dropping
// it reclaims 3 * TILESET_SIZE = 768 KB of WRAM .bss. The DrawLayer* readers that
// index the FLIP regions are retired by the P5/P6 VDP2 tile-render backend and are
// NOT reached by the P3 boot or the P5 Ring-only proof scene (no tile layer drawn
// -> ProcessObjectDrawLists' layer loop is empty). collisionMasks/tileInfo get the
// same x4->x1 treatment via COLLISION_FLIPCOUNT below (their flipped copies are
// pre-built collision GEOMETRY, distinct from pixels, but equally dead on Saturn).
#define TILESET_FLIPCOUNT (1)
#else
#define TILESET_FLIPCOUNT (4)
#endif

#define CPATH_COUNT (2)

#if RETRO_PLATFORM == RETRO_SATURN
// P4 data retarget (Task #203): collisionMasks/tileInfo store ONLY the base
// (FLIP_NONE) region on Saturn. The stock x4 holds three software pre-built
// flipped copies (FLIP_X/Y/XY) of each tile's collision geometry, generated in
// LoadTileConfig (Scene.cpp:869-949) and read by the collision sensors at
// collisionMasks[cPlane][tile & 0xFFF] / tileInfo[..][tile & 0xFFF]
// (Collision.cpp, 26 sites). On Saturn the VDP collision backend (P5/P6) derives
// flipped geometry from per-cell flip bits, so the x3 expansion is dead .bss;
// dropping it reclaims ~414 KB (collisionMasks 512->128 KB, tileInfo 40->10 KB).
//   Because the sensors index with `tile & 0xFFF` (0..4095) -- which addresses the
// x4 region -- a x1 array would be an OUT-OF-BOUNDS read for any flipped tile
// (>=1024): the silent WRAM-.bss corruption class that cost 15 iterations in
// Phase 1.4-1.15. So COLLISION_TILE_MASK clamps every Saturn collision read to the
// base region (0x3FF): a flipped tile reads the FLIP_NONE mask -- a bounded,
// in-bounds geometric approximation retired by the P6 backend, never OOB. The P5
// Ring-only proof scene runs NO tile collision (no Player, no collidable layer), so
// even the approximation is never exercised there.
//   P6 RESTORATION: set both macros back to the stock (4) / (0xFFF) and remove the
// `#if RETRO_PLATFORM != RETRO_SATURN` guards on the LoadTileConfig flip blocks.
#define COLLISION_FLIPCOUNT (1)
#define COLLISION_TILE_MASK (0x3FF)
#else
#define COLLISION_FLIPCOUNT (4)
#define COLLISION_TILE_MASK (0xFFF)
#endif

#define RSDK_SIGNATURE_CFG (0x474643) // "CFG"
#define RSDK_SIGNATURE_SCN (0x4E4353) // "SCN"
#define RSDK_SIGNATURE_TIL (0x4C4954) // "TIL"

enum LayerTypes {
    LAYER_HSCROLL,
    LAYER_VSCROLL,
    LAYER_ROTOZOOM,
    LAYER_BASIC,
};

struct SceneListInfo {
    RETRO_HASH_MD5(hash);
    char name[0x20];
    uint16 sceneOffsetStart;
    uint16 sceneOffsetEnd;
    uint8 sceneCount;
};

struct SceneListEntry {
    RETRO_HASH_MD5(hash);
    char name[0x20];
    char folder[0x10];
    char id[0x08];
#if RETRO_REV02
    uint8 filter;
#endif
};

enum CollisionModes {
    CMODE_FLOOR,
    CMODE_LWALL,
    CMODE_ROOF,
    CMODE_RWALL,
};

enum EngineStates {
    ENGINESTATE_LOAD,
    ENGINESTATE_REGULAR,
    ENGINESTATE_PAUSED,
    ENGINESTATE_FROZEN,
    ENGINESTATE_STEPOVER = 4,
    ENGINESTATE_DEVMENU  = 8,
    ENGINESTATE_VIDEOPLAYBACK,
    ENGINESTATE_SHOWIMAGE,
#if RETRO_REV02
    ENGINESTATE_ERRORMSG,
    ENGINESTATE_ERRORMSG_FATAL,
#endif
    ENGINESTATE_NONE,
#if RETRO_REV0U
    // Prolly origins-only, called by the ending so I assume this handles playing ending movies and returning to menu
    ENGINESTATE_GAME_FINISHED,
#endif
};

struct SceneInfo {
    Entity *entity;
    SceneListEntry *listData;
    SceneListInfo *listCategory;
    int32 timeCounter;
    int32 currentDrawGroup;
    int32 currentScreenID;
    uint16 listPos;
    uint16 entitySlot;
    uint16 createSlot;
    uint16 classCount;
    bool32 inEditor;
    bool32 effectGizmo;
    bool32 debugMode;
    bool32 useGlobalObjects;
    bool32 timeEnabled;
    uint8 activeCategory;
    uint8 categoryCount;
    uint8 state;
#if RETRO_REV02
    uint8 filter;
#endif
    uint8 milliseconds;
    uint8 seconds;
    uint8 minutes;
};

struct ScrollInfo {
    int32 tilePos;
    int32 parallaxFactor;
    int32 scrollSpeed;
    int32 scrollPos;
    uint8 deform;
    uint8 unknown; // stored in the scene, but always 0, never referenced in-engine either...
};

struct ScanlineInfo {
    Vector2 position; // position of the scanline
    Vector2 deform;   // deformation that should be applied (only applies to RotoZoom type)
};

struct TileLayer {
    uint8 type;
    uint8 drawGroup[CAMERA_COUNT];
    uint8 widthShift;
    uint8 heightShift;
    uint16 xsize;
    uint16 ysize;
    Vector2 position;
    int32 parallaxFactor;
    int32 scrollSpeed;
    int32 scrollPos;
    int32 deformationOffset;
    int32 deformationOffsetW;
    int32 deformationData[0x400];
    int32 deformationDataW[0x400];
    void (*scanlineCallback)(ScanlineInfo *scanlines);
    uint16 scrollInfoCount;
    ScrollInfo scrollInfo[0x100];
    RETRO_HASH_MD5(name);
    uint16 *layout;
    uint8 *lineScroll;
};

struct CollisionMask {
    uint8 floorMasks[TILE_SIZE];
    uint8 lWallMasks[TILE_SIZE];
    uint8 rWallMasks[TILE_SIZE];
    uint8 roofMasks[TILE_SIZE];
};

struct TileInfo {
    uint8 floorAngle;
    uint8 lWallAngle;
    uint8 rWallAngle;
    uint8 roofAngle;
    uint8 flag;
};

extern ScanlineInfo *scanlines;
#if defined(P6_SCENE_TEST)
extern TileLayer *tileLayers; // P6.3: relocated to WRAM-L (pointer form), defined in p6_io_main.cpp
extern CollisionMask (*collisionMasks)[TILE_COUNT * COLLISION_FLIPCOUNT]; // P6.3: relocated (DEAD), defined in p6_io_main.cpp
extern TileInfo (*tileInfo)[TILE_COUNT * COLLISION_FLIPCOUNT];            // P6.3: relocated (DEAD), defined in p6_io_main.cpp
#else
extern TileLayer tileLayers[LAYER_COUNT];

extern CollisionMask collisionMasks[CPATH_COUNT][TILE_COUNT * COLLISION_FLIPCOUNT]; // x4 PC (FLIP copies) / x1 Saturn
extern TileInfo tileInfo[CPATH_COUNT][TILE_COUNT * COLLISION_FLIPCOUNT];            // x4 PC (FLIP copies) / x1 Saturn
#endif

#if RETRO_PLATFORM == RETRO_SATURN
// =============================================================================
// P6.7 PACKED COLLISION RESIDENCY (Task #210): the EXACT LoadTileConfig
// geometry in 16 bits/column -- 32 B/tile, 2 x 1024 x 32 = 65,536 B (the
// SaturnMemoryMap P68_HWRAM_COLL_BYTES contract; raw x1 is 131,072 B and fits
// no bank). Format (offline model + C1 whole-file round-trip proof in
// tools/_portspike/_p6/gen_collision_model.py):
//   bits 0-3   rich height (floorMasks for regular tiles, roofMasks for yFlip
//              tiles -- the OTHER of the pair is determined: regular roof is
//              0x0F-or-0xFF, yFlip floor is 0x00-or-0xFF, Scene.cpp:770-834)
//   bit  4     column sentinel (floor AND roof read 0xFF)
//   bits 5-8   lWallMasks height     bit 9   lWall sentinel (0xFF)
//   bits 10-13 rWallMasks height     bit 14  rWall sentinel (0xFF)
//   bit  15    tile yFlip flag (replicated per column)
// The accessors below also derive the FLIP_X/Y/XY variants of masks AND
// tileInfo angles ON THE FLY via the exact PC pre-expansion formulas
// (Scene.cpp:889-963) -- retiring the Task #203 COLLISION_TILE_MASK
// base-mask approximation: a flipped tile now reads its EXACT flipped
// geometry from x1 storage. (Also fixes GetTileAngle's latent x1 OOB: its
// stock `tile & 0xFFF` indexes the x4 region.)
// Gate: qa_p6_collision K1-K5 (packed hash + 128 flip probes on SH-2).
extern uint16 (*packedCollisionMasks)[TILE_COUNT * TILE_SIZE]; // [CPATH][0x4000], defined in p6_io_main.cpp

enum PackedMaskDirs { PACKEDDIR_FLOOR, PACKEDDIR_LWALL, PACKEDDIR_RWALL, PACKEDDIR_ROOF };

// dir-of-truth + column + invert mapping per flip variant (derived from the
// PC pre-expansion: FlipX mirrors columns for floor/roof and swaps+inverts
// lWall/rWall; FlipY swaps+inverts floor/roof and mirrors lWall/rWall
// columns; FlipXY composes both).
inline uint8 PackedCollisionMask(uint8 plane, uint16 tile, uint8 dir, uint8 col)
{
    uint8 flip = (tile >> 10) & 3;
    uint16 base = tile & 0x3FF;
    static const uint8 srcDir[4][4] = {
        // FLOOR             LWALL            RWALL            ROOF
        { PACKEDDIR_FLOOR, PACKEDDIR_LWALL, PACKEDDIR_RWALL, PACKEDDIR_ROOF }, // FLIP_NONE
        { PACKEDDIR_FLOOR, PACKEDDIR_RWALL, PACKEDDIR_LWALL, PACKEDDIR_ROOF }, // FLIP_X
        { PACKEDDIR_ROOF, PACKEDDIR_LWALL, PACKEDDIR_RWALL, PACKEDDIR_FLOOR }, // FLIP_Y
        { PACKEDDIR_ROOF, PACKEDDIR_RWALL, PACKEDDIR_LWALL, PACKEDDIR_FLOOR }, // FLIP_XY
    };
    // column mirrored for floor/roof under FLIP_X|XY, for lWall/rWall under
    // FLIP_Y|XY; value inverted (0xF - v) for floor/roof under FLIP_Y|XY,
    // for lWall/rWall under FLIP_X|XY.
    bool32 vertDir = (dir == PACKEDDIR_FLOOR || dir == PACKEDDIR_ROOF);
    bool32 mirrorCol = vertDir ? (flip & FLIP_X) : (flip & FLIP_Y);
    bool32 invertVal = vertDir ? (flip & FLIP_Y) : (flip & FLIP_X);
    uint8 c = mirrorCol ? (0xF - col) : col;
    uint16 w = packedCollisionMasks[plane & 1][(base << 4) | c];
    uint8 v;
    switch (srcDir[flip][dir]) {
        default:
        case PACKEDDIR_FLOOR:
            if (w & 0x10)
                return 0xFF;
            v = (w & 0x8000) ? 0x00 : (w & 0xF);
            break;
        case PACKEDDIR_ROOF:
            if (w & 0x10)
                return 0xFF;
            v = (w & 0x8000) ? (w & 0xF) : 0x0F;
            break;
        case PACKEDDIR_LWALL:
            if (w & 0x200)
                return 0xFF;
            v = (w >> 5) & 0xF;
            break;
        case PACKEDDIR_RWALL:
            if (w & 0x4000)
                return 0xFF;
            v = (w >> 10) & 0xF;
            break;
    }
    return invertVal ? (0xF - v) : v;
}

// tileInfo angles: x1 stores the base angles; flips derive per
// Scene.cpp:893-896 (X: negate + lWall<->rWall swap), :920-923
// (Y: -0x80 - a + floor<->roof swap), :948-951 (XY composes both:
// 0x80 + a with both swaps). which: 0 floor, 1 lWall, 2 rWall, 3 roof.
inline uint8 PackedTileAngle(uint8 plane, uint16 tile, uint8 which)
{
    uint8 flip = (tile >> 10) & 3;
    const TileInfo *ti = &tileInfo[plane & 1][tile & 0x3FF];
    static const uint8 srcWhich[4][4] = {
        { 0, 1, 2, 3 }, // FLIP_NONE
        { 0, 2, 1, 3 }, // FLIP_X: lWall' = -rWall, rWall' = -lWall
        { 3, 1, 2, 0 }, // FLIP_Y: floor' = -0x80 - roof, ...
        { 3, 2, 1, 0 }, // FLIP_XY
    };
    const uint8 *angles = &ti->floorAngle; // floor, lWall, rWall, roof contiguous
    uint8 a = angles[srcWhich[flip][which]];
    switch (flip) {
        default:
        case FLIP_NONE: return a;
        case FLIP_X: return (uint8)(-(int8)a);
        case FLIP_Y: return (uint8)(-0x80 - (int8)a);
        case FLIP_XY: return (uint8)(0x80 + (int8)a);
    }
}

#define RSDK_FLOOR_MASK(plane, tile, col) PackedCollisionMask(plane, tile, PACKEDDIR_FLOOR, col)
#define RSDK_LWALL_MASK(plane, tile, col) PackedCollisionMask(plane, tile, PACKEDDIR_LWALL, col)
#define RSDK_RWALL_MASK(plane, tile, col) PackedCollisionMask(plane, tile, PACKEDDIR_RWALL, col)
#define RSDK_ROOF_MASK(plane, tile, col)  PackedCollisionMask(plane, tile, PACKEDDIR_ROOF, col)
#define RSDK_FLOOR_ANGLE(plane, tile) PackedTileAngle(plane, tile, 0)
#define RSDK_LWALL_ANGLE(plane, tile) PackedTileAngle(plane, tile, 1)
#define RSDK_RWALL_ANGLE(plane, tile) PackedTileAngle(plane, tile, 2)
#define RSDK_ROOF_ANGLE(plane, tile)  PackedTileAngle(plane, tile, 3)
#else
// Stock expansion: the macros are the EXACT original site expressions
// (Collision.cpp adopts them mechanically; PC builds are byte-identical).
#define RSDK_FLOOR_MASK(plane, tile, col) collisionMasks[plane][(tile) & COLLISION_TILE_MASK].floorMasks[col]
#define RSDK_LWALL_MASK(plane, tile, col) collisionMasks[plane][(tile) & COLLISION_TILE_MASK].lWallMasks[col]
#define RSDK_RWALL_MASK(plane, tile, col) collisionMasks[plane][(tile) & COLLISION_TILE_MASK].rWallMasks[col]
#define RSDK_ROOF_MASK(plane, tile, col)  collisionMasks[plane][(tile) & COLLISION_TILE_MASK].roofMasks[col]
#define RSDK_FLOOR_ANGLE(plane, tile) tileInfo[plane][(tile) & COLLISION_TILE_MASK].floorAngle
#define RSDK_LWALL_ANGLE(plane, tile) tileInfo[plane][(tile) & COLLISION_TILE_MASK].lWallAngle
#define RSDK_RWALL_ANGLE(plane, tile) tileInfo[plane][(tile) & COLLISION_TILE_MASK].rWallAngle
#define RSDK_ROOF_ANGLE(plane, tile)  tileInfo[plane][(tile) & COLLISION_TILE_MASK].roofAngle
#endif

#if RETRO_REV02
extern bool32 forceHardReset;
#endif
extern char currentSceneFolder[0x10];
extern char currentSceneID[0x10];
#if RETRO_REV02
extern uint8 currentSceneFilter;
#endif

extern SceneInfo sceneInfo;

#if defined(P6_SCENE_TEST)
extern uint8 *tilesetPixels; // P6.3: relocated (DEAD), defined in p6_io_main.cpp
#else
extern uint8 tilesetPixels[TILESET_SIZE * TILESET_FLIPCOUNT];
#endif

void LoadSceneFolder();
void LoadSceneAssets();
void LoadTileConfig(char *filepath);
void LoadStageGIF(char *filepath);

void ProcessParallaxAutoScroll();
void ProcessParallax(TileLayer *layer);
void ProcessSceneTimer();

void SetScene(const char *categoryName, const char *sceneName);
inline void LoadScene()
{
    if ((sceneInfo.state & ENGINESTATE_STEPOVER) == ENGINESTATE_STEPOVER)
        sceneInfo.state = ENGINESTATE_LOAD | ENGINESTATE_STEPOVER;
    else
        sceneInfo.state = ENGINESTATE_LOAD;
}

#if RETRO_REV02
inline void ForceHardReset(bool32 shouldHardReset) { forceHardReset = shouldHardReset; }
#endif

inline bool32 CheckValidScene()
{
    if (sceneInfo.activeCategory >= sceneInfo.categoryCount)
        return false;

    SceneListInfo *list = &sceneInfo.listCategory[sceneInfo.activeCategory];
    return sceneInfo.listPos >= list->sceneOffsetStart && sceneInfo.listPos <= list->sceneOffsetEnd;
}

inline bool32 CheckSceneFolder(const char *folderName) { return strcmp(folderName, sceneInfo.listData[sceneInfo.listPos].folder) == 0; }

inline uint16 GetTileLayerID(const char *name)
{
    RETRO_HASH_MD5(hash);
    GEN_HASH_MD5(name, hash);

    for (int32 i = 0; i < LAYER_COUNT; ++i) {
        if (HASH_MATCH_MD5(tileLayers[i].name, hash))
            return i;
    }

    return (uint16)-1;
}

inline TileLayer *GetTileLayer(uint16 layerID) { return layerID < LAYER_COUNT ? &tileLayers[layerID] : NULL; }

inline void GetLayerSize(uint16 layerID, Vector2 *size, bool32 usePixelUnits)
{
    if (layerID < LAYER_COUNT && size) {
        TileLayer *layer = &tileLayers[layerID];

        if (usePixelUnits) {
            size->x = TILE_SIZE * layer->xsize;
            size->y = TILE_SIZE * layer->ysize;
        }
        else {
            size->x = layer->xsize;
            size->y = layer->ysize;
        }
    }
}

inline uint16 GetTile(uint16 layerID, int32 tileX, int32 tileY)
{
    if (layerID < LAYER_COUNT) {
        TileLayer *layer = &tileLayers[layerID];
        if (tileX >= 0 && tileX < layer->xsize && tileY >= 0 && tileY < layer->ysize)
            return layer->layout[tileX + (tileY << layer->widthShift)];
    }

    return (uint16)-1;
}

inline void SetTile(uint16 layerID, int32 tileX, int32 tileY, uint16 tile)
{
    if (layerID < LAYER_COUNT) {
        TileLayer *layer = &tileLayers[layerID];
        if (tileX >= 0 && tileX < layer->xsize && tileY >= 0 && tileY < layer->ysize)
            layer->layout[tileX + (tileY << layer->widthShift)] = tile;
    }
}

inline int32 GetTileAngle(uint16 tile, uint8 cPlane, uint8 cMode)
{
    // P6.7 (Task #210): the RSDK_*_ANGLE macros expand to the EXACT stock
    // expressions on PC (COLLISION_TILE_MASK == 0xFFF) and to the on-the-fly
    // flip derivation on Saturn -- which also fixes the stock x1 OOB this
    // getter had (tile & 0xFFF indexes the x4 region).
    switch (cMode) {
        default: return 0;
        case CMODE_FLOOR: return RSDK_FLOOR_ANGLE(cPlane & 1, tile);
        case CMODE_LWALL: return RSDK_LWALL_ANGLE(cPlane & 1, tile);
        case CMODE_ROOF: return RSDK_ROOF_ANGLE(cPlane & 1, tile);
        case CMODE_RWALL: return RSDK_RWALL_ANGLE(cPlane & 1, tile);
    }
}
inline void SetTileAngle(uint16 tile, uint8 cPlane, uint8 cMode, uint8 angle)
{
    switch (cMode) {
        default: break;
        case CMODE_FLOOR: tileInfo[cPlane & 1][tile & 0x3FF].floorAngle = angle; break;
        case CMODE_LWALL: tileInfo[cPlane & 1][tile & 0x3FF].lWallAngle = angle; break;
        case CMODE_ROOF: tileInfo[cPlane & 1][tile & 0x3FF].roofAngle = angle; break;
        case CMODE_RWALL: tileInfo[cPlane & 1][tile & 0x3FF].rWallAngle = angle; break;
    }
}

inline uint8 GetTileFlags(uint16 tile, uint8 cPlane) { return tileInfo[cPlane & 1][tile & 0x3FF].flag; }
inline void SetTileFlags(uint16 tile, uint8 cPlane, uint8 flag) { tileInfo[cPlane & 1][tile & 0x3FF].flag = flag; }

void CopyTileLayer(uint16 dstLayerID, int32 dstStartX, int32 dstStartY, uint16 srcLayerID, int32 srcStartX, int32 srcStartY, int32 countX,
                   int32 countY);

inline void CopyTile(uint16 dest, uint16 src, uint16 count)
{
    if (dest > TILE_COUNT)
        dest = TILE_COUNT - 1;

    if (src > TILE_COUNT)
        src = TILE_COUNT - 1;

    if (count > TILE_COUNT)
        count = TILE_COUNT - 1;

    uint8 *destPixels = &tilesetPixels[TILE_DATASIZE * dest];
    uint8 *srcPixels  = &tilesetPixels[TILE_DATASIZE * src];

#if RETRO_PLATFORM != RETRO_SATURN
    // FLIP_X/Y/XY copies skipped on Saturn -- tilesetPixels is x1 (base only);
    // VDP2 does tile flips in hardware. See TILESET_FLIPCOUNT (Scene.hpp:12).
    uint8 *destPixelsX = &tilesetPixels[(TILE_DATASIZE * dest) + ((TILE_COUNT * TILE_DATASIZE) * FLIP_X)];
    uint8 *srcPixelsX  = &tilesetPixels[(TILE_DATASIZE * src) + ((TILE_COUNT * TILE_DATASIZE) * FLIP_X)];

    uint8 *destPixelsY = &tilesetPixels[(TILE_DATASIZE * dest) + ((TILE_COUNT * TILE_DATASIZE) * FLIP_Y)];
    uint8 *srcPixelsY  = &tilesetPixels[(TILE_DATASIZE * src) + ((TILE_COUNT * TILE_DATASIZE) * FLIP_Y)];

    uint8 *destPixelsXY = &tilesetPixels[(TILE_DATASIZE * dest) + ((TILE_COUNT * TILE_DATASIZE) * FLIP_XY)];
    uint8 *srcPixelsXY  = &tilesetPixels[(TILE_DATASIZE * src) + ((TILE_COUNT * TILE_DATASIZE) * FLIP_XY)];
#endif

    for (int32 t = 0; t < count; ++t) {
        for (int32 p = 0; p < TILE_DATASIZE; ++p) {
            *destPixels++   = *srcPixels++;
#if RETRO_PLATFORM != RETRO_SATURN
            *destPixelsX++  = *srcPixelsX++;
            *destPixelsY++  = *srcPixelsY++;
            *destPixelsXY++ = *srcPixelsXY++;
#endif
        }
    }
}

inline ScanlineInfo *GetScanlines() { return scanlines; }

// Draw a layer with horizonal scrolling capabilities
void DrawLayerHScroll(TileLayer *layer);
// Draw a layer with vertical scrolling capabilities
void DrawLayerVScroll(TileLayer *layer);
// Draw a layer with rotozoom (via scanline callback) capabilities
void DrawLayerRotozoom(TileLayer *layer);
// Draw a "basic" layer, no special capabilities, but it's the fastest to draw
void DrawLayerBasic(TileLayer *layer);

#if RETRO_REV0U
#include "Legacy/SceneLegacy.hpp"
#endif

} // namespace RSDK

#endif
