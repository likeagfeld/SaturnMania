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
bool32 CheckObjectCollisionTouch(Entity *thisEntity, Hitbox *thisHitbox, Entity *otherEntity, Hitbox *otherHitbox) { return false; }
uint8 CheckObjectCollisionBox(Entity *thisEntity, Hitbox *thisHitbox, Entity *otherEntity, Hitbox *otherHitbox, bool32 setPos) { return 0; }
bool32 CheckObjectCollisionPlatform(Entity *thisEntity, Hitbox *thisHitbox, Entity *otherEntity, Hitbox *otherHitbox, bool32 setPos) { return false; }
bool32 ObjectTileCollision(Entity *entity, uint16 collisionLayers, uint8 collisionMode, uint8 collisionPlane, int32 xOffset, int32 yOffset, bool32 setPos) { return false; }
bool32 ObjectTileGrip(Entity *entity, uint16 collisionLayers, uint8 collisionMode, uint8 collisionPlane, int32 xOffset, int32 yOffset, int32 tolerance) { return false; }
void ProcessObjectMovement(Entity *entity, Hitbox *outerBox, Hitbox *innerBox) {}

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
void DrawFace(Vector2 *vertices, int32 vertCount, int32 r, int32 g, int32 b, int32 alpha, int32 inkEffect) {}
void DrawBlendedFace(Vector2 *vertices, color *vertColors, int32 vertCount, int32 alpha, int32 inkEffect) {}
void BlendColors(uint8 paletteID, color *srcColorsA, color *srcColorsB, int32 blendAmount, int32 startIndex, int32 count) {}
int32 maskColor = 0; // Palette.hpp:28

// ---- Palette (Graphics/Palette.cpp) ------------------------------------------
void LoadPalette(uint8 paletteID, const char *filename, uint16 disabledRows) {}
void SetPaletteFade(uint8 destBankID, uint8 srcBankA, uint8 srcBankB, int16 blendAmount, int32 startIndex, int32 endIndex) {}

// ---- Input (Input/Input.cpp) --------------------------------------------------
InputDevice *inputDeviceList[INPUTDEVICE_COUNT];
int32 inputDeviceCount = 0;
int32 inputSlots[PLAYER_COUNT];
InputDevice *inputSlotDevices[PLAYER_COUNT];
int32 GetInputDeviceType(uint32 deviceID) { return 0; }

// ---- Dev / misc ---------------------------------------------------------------
int32 debugHitboxCount = 0;
DebugHitboxInfo debugHitboxList[DEBUG_HITBOX_COUNT];
int32 AddDebugHitbox(uint8 type, uint8 dir, Entity *entity, Hitbox *hitbox) { return -1; } // Collision.hpp:46
void AddViewableVariable(const char *name, void *value, int32 type, int32 min, int32 max) {}
void UpdateGameWindow() {}
bool32 useEndLine = true;
int32 *globalVarsPtr = NULL; // RetroEngine.hpp:731

namespace SKU {
UserCore *userCore = NULL;
} // namespace SKU

} // namespace RSDK
