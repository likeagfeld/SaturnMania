// =============================================================================
// p6_pack_stubs.cpp -- P6.7a (Task #210): the function-table closure stubs.
//
// Core/Link.cpp's SetupFunctionTables references the ENTIRE RSDK API surface;
// the standalone P6.1 image satisfied it with the full 16-TU logic core, but
// the jo-hosted pack carries only the proven subset (Reader/Scene/Storage/
// Text/Sprite/Animation/Audio/Object). This TU defines the MEASURED 43-symbol
// remainder (build iteration 4 undefined list, 2026-06-10) as link-only
// stubs: Collision (real TU lands with Player-scale work), Scene3D/Model,
// Drawing primitives (DrawLine/Face/Circle -> the existing src/rsdk polygon
// emitter pattern when needed), Matrix math, Palette ops, Input, and the
// dev/debug surface. Each is replaced by its REAL engine TU as P6.7b+ scale
// the pack out; the headers type-check every signature against the engine
// declarations (a mismatched stub is a compile error, not a silent wrong
// overload).
// =============================================================================
#include "RSDK/Core/RetroEngine.hpp"

namespace RSDK {

// ---- Collision (Scene/Collision.cpp) ----------------------------------------
// P6.7 W15b (Task #227): the six collision stubs are RETIRED -- the real
// engine Scene/Collision.cpp is a pack TU now (build_p6scene_objs.sh [7q]).
// Every tile read in it routes through the RSDK_*_MASK / RSDK_*_ANGLE seam
// (Scene.hpp:360-378 -> PackedCollisionMask/PackedTileAngle on Saturn); the
// only raw collisionMasks/tileInfo mutation site, CopyCollisionMask
// (Collision.cpp:110-206), is preprocessed out at RETRO_REVISION=2 +
// RETRO_USE_MOD_LOADER=0 (its #if guard), so the read-only packed store is
// never written.

// ---- Scene3D / Model (Graphics/Scene3D.cpp) ----------------------------------
uint16 LoadMesh(const char *filename, uint8 scope) { return (uint16)-1; }
uint16 Create3DScene(const char *name, uint16 vertexCount, uint8 scope) { return (uint16)-1; }
void Draw3DScene(uint16 sceneID) {}
void AddModelToScene(uint16 modelFrames, uint16 sceneIndex, uint8 drawMode, Matrix *matWorld, Matrix *matNormal, color color) {}
void AddMeshFrameToScene(uint16 modelFrames, uint16 sceneIndex, Animator *animator, uint8 drawMode, Matrix *matWorld, Matrix *matNormal, color color) {}

// ---- Matrix math (Core/Math.cpp's matrix half is gc'd; table refs them) ------
void SetIdentityMatrix(Matrix *matrix) {}
void MatrixMultiply(Matrix *dest, Matrix *matrixA, Matrix *matrixB) {}
void MatrixTranslateXYZ(Matrix *matrix, int32 x, int32 y, int32 z, bool32 setIdentity) {}
void MatrixScaleXYZ(Matrix *matrix, int32 x, int32 y, int32 z) {}
void MatrixRotateX(Matrix *matrix, int16 angle) {}
void MatrixRotateY(Matrix *matrix, int16 angle) {}
void MatrixRotateZ(Matrix *matrix, int16 angle) {}
void MatrixRotateXYZ(Matrix *matrix, int16 x, int16 y, int16 z) {}
void MatrixInverse(Matrix *dest, Matrix *matrix) {}

// ---- Drawing primitives (Drawing.cpp not in pack; FIXME P6.7b+: route to
// ---- the proven src/rsdk polygon-emitter pattern) ----------------------------
void DrawLine(int32 x1, int32 y1, int32 x2, int32 y2, uint32 color, int32 alpha, int32 inkEffect, bool32 screenRelative) {}
void DrawRectangle(int32 x, int32 y, int32 width, int32 height, uint32 color, int32 alpha, int32 inkEffect, bool32 screenRelative) {}
void DrawCircle(int32 x, int32 y, int32 radius, uint32 color, int32 alpha, int32 inkEffect, bool32 screenRelative) {}
void DrawCircleOutline(int32 x, int32 y, int32 innerRadius, int32 outerRadius, uint32 color, int32 alpha, int32 inkEffect, bool32 screenRelative) {}
#if defined(P6_FRONTEND_MENU)
// M3 (Task #295): forward DrawFace to the jo-side VDP1 flat-colour polygon emitter
// (p6_vdp1.c:p6_drawface_saturn -- a slPutPolygon flat quad at Z=455, behind the row
// icon/text Z=450 + above the gold backdrop Z=460). The pack C++ TU cannot call
// slPutPolygon (jo/SGL namespace clash), so the jo emitter is reached via this extern
// "C" seam. UIWidgets_DrawParallelogram already subtracted ScreenInfo->position, so the
// verts arrive screen-relative 16.16 (the emitter does >>16 then screen-centre). The
// pointer is non-NULL once p6_vdp1.o links (always, in the MENU flavor). vertCount 3/4.
extern "C" void p6_drawface_saturn(const int *vx, const int *vy, int count,
                                   int r8, int g8, int b8);
void DrawFace(Vector2 *vertices, int32 vertCount, int32 r, int32 g, int32 b, int32 alpha, int32 inkEffect)
{
#if !defined(P6_MENU_LAYOUT320)
    // BISECT (recovery): the 320-agent DrawFace->p6_drawface_saturn plate emitter is a
    // suspect for the first-frame crash (cont_frames=0). No-op it (the working layout-
    // diagnosis state had DrawFace calls=0) until the emitter is re-verified under
    // P6_MENU_LAYOUT320.
    (void)vertices; (void)vertCount; (void)r; (void)g; (void)b; (void)alpha; (void)inkEffect;
    return;
#endif
    (void)alpha; (void)inkEffect; // INK_NONE: opaque replace; partial-alpha = Phase Z
    if (!vertices || vertCount < 3 || vertCount > 4)
        return;
    int vx[4], vy[4];
    for (int32 i = 0; i < vertCount; ++i) { vx[i] = vertices[i].x; vy[i] = vertices[i].y; }
    p6_drawface_saturn(vx, vy, (int)vertCount, (int)r, (int)g, (int)b);
}
#else
void DrawFace(Vector2 *vertices, int32 vertCount, int32 r, int32 g, int32 b, int32 alpha, int32 inkEffect) {}
#endif
void DrawBlendedFace(Vector2 *vertices, color *vertColors, int32 vertCount, int32 alpha, int32 inkEffect) {}
void BlendColors(uint8 paletteID, color *srcColorsA, color *srcColorsB, int32 blendAmount, int32 startIndex, int32 count) {}
int32 maskColor = 0; // Palette.hpp:28

// ---- Palette (Graphics/Palette.cpp) ------------------------------------------
void LoadPalette(uint8 paletteID, const char *filename, uint16 disabledRows) {}
void SetPaletteFade(uint8 destBankID, uint8 srcBankA, uint8 srcBankB, int16 blendAmount, int32 startIndex, int32 endIndex) {}

// ---- Input (Input/Input.cpp) --------------------------------------------------
// P6.7 W7 (Task #227): the Input stubs are RETIRED -- the VERBATIM engine
// Input/Input.cpp is a pack TU now (build_p6scene_objs.sh [7r]); it defines
// inputDeviceList/inputDeviceCount/inputSlots/inputSlotDevices (Input.cpp:5-9),
// the pad-state arrays controller/stickL/stickR/triggerL/triggerR/touchInfo
// (Input.cpp:11-18), and the real GetInputDeviceType (Input.cpp:323). The
// Saturn SMPC device backend is platform/Saturn/InputDevice_Saturn.cpp ([7s]),
// the AudioDevice_Saturn precedent applied to input.

// ---- Dev / misc ---------------------------------------------------------------
// P6.7 W15b: debugHitboxCount/debugHitboxList/AddDebugHitbox stubs RETIRED --
// the real Collision.cpp:61-106 defines them (DEBUG_HITBOX_COUNT=8 Saturn
// bound per Collision.hpp W13 comment; AddDebugHitbox's `i < DEBUG_HITBOX_-
// COUNT` append check bounds it -- verified, unchanged).
void AddViewableVariable(const char *name, void *value, int32 type, int32 min, int32 max) {}
void UpdateGameWindow() {}
bool32 useEndLine = true;
// P6.7c: globalVarsPtr stub REMOVED -- Core_RetroEngine.o (real TU, in the
// pack for LoadGameConfig) defines it at RetroEngine.cpp:18. NULL is the
// verified-safe state: the GameConfig var loop breaks immediately at
// RetroEngine.cpp:1172-1173 (the Mania globals buffer needs ~262 KB and is a
// P6.8 design item -- the var offsets reach index 66,995).

namespace SKU {
UserCore *userCore = NULL;
} // namespace SKU

} // namespace RSDK
