// =============================================================================
// RenderDevice_Saturn.cpp -- P0 backend-contract shim (engine true-port, #199).
//
// Defines the RenderDeviceBase device API, the Draw* primitives, and the
// render/scene globals that the platform-INDEPENDENT RSDKv5 logic core leaves
// undefined (Probe 6 enumerated them). Every definition below is the engine
// header's OWN declaration turned into a definition: because this TU
// #includes RSDK/Core/RetroEngine.hpp (which transitively declares each of
// these), the C++ compiler enforces ABI-EXACT mangled-name matching. A wrong
// type or signature is a COMPILE error here, never a silent link mismatch.
//
// P0 scope: the render/scene globals get REAL POD storage (permanent); the
// functions get minimal link-only stubs. The VDP1/VDP2 hardware bodies arrive
// at P3 (core boot) and P5 (first object render). The symbols, TU, and
// signatures defined here are permanent -- only the function bodies grow.
// =============================================================================
#include "RSDK/Core/RetroEngine.hpp"

using namespace RSDK;

// --- RenderDeviceBase device API (11 contract symbols) -----------------------
// Drawing.hpp:189-243. Saturn RenderDevice is `class RenderDevice :
// public RenderDeviceBase {}` (SaturnRenderDevice.hpp); the core odr-uses the
// base-class statics below.
bool RSDK::RenderDeviceBase::Init() { return true; }
void RSDK::RenderDeviceBase::CopyFrameBuffer() {}
void RSDK::RenderDeviceBase::ProcessDimming() {}
void RSDK::RenderDeviceBase::FlipScreen() {}
void RSDK::RenderDeviceBase::Release(bool32 isRefresh) {}
void RSDK::RenderDeviceBase::SetupImageTexture(int32 width, int32 height, uint8 *imagePixels) {}
bool RSDK::RenderDeviceBase::ProcessEvents() { return true; }
void RSDK::RenderDeviceBase::InitFPSCap() {}
bool RSDK::RenderDeviceBase::CheckFPSCap() { return true; }
void RSDK::RenderDeviceBase::UpdateFPSCap() {}

bool32 RSDK::RenderDeviceBase::isRunning;

// --- Draw primitives (7 contract symbols) ------------------------------------
// Drawing.hpp:379-385,410. Translate to VDP1 polygons/sprites at P5.
void RSDK::DrawLine(int32 x1, int32 y1, int32 x2, int32 y2, uint32 color, int32 alpha, int32 inkEffect, bool32 screenRelative) {}
void RSDK::DrawRectangle(int32 x, int32 y, int32 width, int32 height, uint32 color, int32 alpha, int32 inkEffect, bool32 screenRelative) {}
void RSDK::DrawCircle(int32 x, int32 y, int32 radius, uint32 color, int32 alpha, int32 inkEffect, bool32 screenRelative) {}
void RSDK::DrawCircleOutline(int32 x, int32 y, int32 innerRadius, int32 outerRadius, uint32 color, int32 alpha, int32 inkEffect, bool32 screenRelative) {}
void RSDK::DrawFace(Vector2 *vertices, int32 vertCount, int32 r, int32 g, int32 b, int32 alpha, int32 inkEffect) {}
void RSDK::DrawBlendedFace(Vector2 *vertices, uint32 *colors, int32 vertCount, int32 alpha, int32 inkEffect) {}
void RSDK::DrawDevString(const char *string, int32 x, int32 y, int32 align, uint32 color) {}

// --- Render/scene globals (10 storage symbols + UpdateGameWindow) ------------
// Drawing.hpp:261-288. Defined as real BSS storage so the core's references
// resolve; sized/typed by the engine header (compile-checked).
DrawList      RSDK::drawGroups[DRAWGROUP_COUNT];
char          RSDK::drawGroupNames[0x10][0x10];
GFXSurface    RSDK::gfxSurface[SURFACE_COUNT];
int32         RSDK::cameraCount;
ScreenInfo    RSDK::screens[SCREEN_COUNT];
CameraInfo    RSDK::cameras[CAMERA_COUNT];
ScreenInfo   *RSDK::currentScreen;
ShaderEntry   RSDK::shaderList[SHADER_COUNT];
VideoSettings RSDK::videoSettings;
bool32        RSDK::changedVideoSettings;

void RSDK::UpdateGameWindow() {}
