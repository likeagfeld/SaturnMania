#ifndef DRAWING_H
#define DRAWING_H

namespace RSDK
{

#define SURFACE_COUNT (0x40)

#ifndef SCREEN_COUNT
#if RETRO_PLATFORM == RETRO_SATURN
// P4 data retarget (Task #203): screens[SCREEN_COUNT] is 143408 bytes each
// (ScreenInfo carries the uint16[SCREEN_XMAX*SCREEN_YSIZE] software frameBuffer).
// At the stock REV02 count of 4 that is 560 KB of .bss for a SOFTWARE render
// target the Saturn never uses (VDP1/VDP2 render in hardware). One screen holds
// the engine's clip/camera bookkeeping; the dead frameBuffer is collapsed when
// the render backend is wired (P3/P5). Non-Saturn builds keep the stock count.
#define SCREEN_COUNT (1)
#elif RETRO_REV02
#define SCREEN_COUNT (4)
#else
#define SCREEN_COUNT (2)
#endif
#endif
#define CAMERA_COUNT (4)

#define DEFAULT_PIXWIDTH (424)

// P6.7 W11 census (Task #210, 2026-06-11): the P4 "a real GHZ scene uses <=4
// layers" assumption is FALSIFIED by the whole-game Scene.bin census --
// GHZ1 uses 5 (BG Outside + BG Cave 1/2 + FG Low/High) and the maximum is 8
// (FBZ/Scene2, TMZ1/Scene1). The stock LAYER_COUNT is restored on Saturn;
// the 8 x 13,384 B tileLayers cost is a SaturnMemoryMap.h W11 ledger item.
#define LAYER_COUNT     (8)
#define DRAWGROUP_COUNT (16)

#define SHADER_COUNT (0x20)

// Also for "Images" but it's a cleaner name as is
#define RETRO_VIDEO_TEXTURE_W (1024)
#define RETRO_VIDEO_TEXTURE_H (512)

enum InkEffects {
    INK_NONE,
    INK_BLEND,
    INK_ALPHA,
    INK_ADD,
    INK_SUB,
    INK_TINT,
    INK_MASKED,
    INK_UNMASKED,
};

enum DrawFX { FX_NONE = 0, FX_FLIP = 1, FX_ROTATE = 2, FX_SCALE = 4 };

enum FlipFlags { FLIP_NONE, FLIP_X, FLIP_Y, FLIP_XY };

enum Alignments {
    ALIGN_LEFT,
    ALIGN_RIGHT,
    ALIGN_CENTER,
};

enum WindowStates {
    WINDOWSTATE_UNINITIALIZED,
    WINDOWSTATE_ACTIVE,
    WINDOWSTATE_INACTIVE,
};

enum ShaderIDs {
    SHADER_NONE,
    SHADER_CLEAN,
    SHADER_CRT_YEETRON,
    SHADER_CRT_YEE64,
    SHADER_YUV_420,
    SHADER_YUV_422,
    SHADER_YUV_444,
    SHADER_RGB_IMAGE,
};

struct GFXSurface {
    RETRO_HASH_MD5(hash);
    uint8 *pixels;
    int32 height;
    int32 width;
    int32 lineSize;
    uint8 scope;
#if RETRO_PLATFORM == RETRO_SATURN
    // P6.7 W12b (Task #227): banded-sheet binding. Sheets above
    // P68_SHEET_RESIDENT_MAX keep NO resident pixels (pixels == NULL);
    // reads go through SaturnSheet_FetchRect on this slot (the VDP1
    // slot-cache miss path). -1 = resident sheet, stock behavior.
    // Appended LAST so every prior field offset is unchanged.
    int8 saturnSheetSlot;
#endif
};

#if RETRO_PLATFORM == RETRO_SATURN
// P6.7 W12 residency threshold (mirrors the SaturnMemoryMap.h contract --
// engine TUs do not include the map header): sheets <= 64 KB decoded stay
// DATASET_STG-resident (Items.gif class, the P6.5b2-proven path); larger
// sheets bind an offline band store (platform/Saturn/SaturnSheet.cpp).
#define P68_SHEET_RESIDENT_MAX (0x10000)
extern "C" int32 SaturnSheet_FindSlot(const uint32 *hash);
extern "C" int32 SaturnSheet_FetchRect(int32 slot, int32 sx, int32 sy,
                                       int32 w, int32 h, uint8 *dst);
#endif

struct ScreenInfo {
#if RETRO_PLATFORM == RETRO_SATURN
    // P4 data retarget (Task #203): the software frameBuffer[SCREEN_XMAX*SCREEN_YSIZE]
    // (uint16 320*224 = 143360 B) is the per-screen blit target the Saturn NEVER uses --
    // VDP1 sprites + VDP2 NBG scrolls render in hardware. Its ONLY writers are the
    // DrawLayer* software rasterizers (Object.cpp), which are Saturn-gated off. Stub it
    // to [1] to reclaim ~140 KB of .bss. The struct's other fields (position/size/clip)
    // stay live (DevOutput, clip bounds, camera bookkeeping all read them).
    // P6 RESTORATION: drop the Saturn branch -> frameBuffer returns to full size.
    uint16 frameBuffer[1];
#else
    // uint16 *frameBuffer;
    uint16 frameBuffer[SCREEN_XMAX * SCREEN_YSIZE];
#endif
    Vector2 position;
    Vector2 size;
    Vector2 center;
    int32 pitch;
    int32 clipBound_X1;
    int32 clipBound_Y1;
    int32 clipBound_X2;
    int32 clipBound_Y2;
    int32 waterDrawPos;
};

struct CameraInfo {
    Vector2 *targetPos;
    Vector2 position;
    Vector2 offset;
    bool32 worldRelative;
};

struct DrawList {
#if RETRO_PLATFORM == RETRO_SATURN
    // W11b (Task #226): capped entries (DRAWGROUP_ENTRY_CAP, Object.hpp) --
    // appends go through RSDK_DRAWGROUP_APPEND; reads bounded by entityCount.
    uint16 entries[DRAWGROUP_ENTRY_CAP];
#else
    uint16 entries[ENTITY_COUNT];
#endif
    uint16 layerDrawList[LAYER_COUNT];
    void (*hookCB)();
    bool32 sorted;
    int32 entityCount;
    int32 layerCount;
};

struct VideoSettings {
    uint8 windowed;
    uint8 bordered;
    uint8 exclusiveFS;
    uint8 vsync;
    uint8 tripleBuffered;
    uint8 shaderSupport;
    int32 fsWidth;
    int32 fsHeight;
    int32 refreshRate;
    int32 windowWidth;
    int32 windowHeight;
    int32 pixWidth;
    int32 pixHeight;
    int32 windowState;
    int32 shaderID;
    int32 screenCount;
    uint32 dimTimer;
    uint32 dimLimit;
    float dimMax;
    float dimPercent;
    float viewportW;
    float viewportH;
    float viewportX;
    float viewportY;
};

enum VideoSettingsValues {
    VIDEOSETTING_WINDOWED,
    VIDEOSETTING_BORDERED,
    VIDEOSETTING_EXCLUSIVEFS,
    VIDEOSETTING_VSYNC,
    VIDEOSETTING_TRIPLEBUFFERED,
    VIDEOSETTING_WINDOW_WIDTH,
    VIDEOSETTING_WINDOW_HEIGHT,
    VIDEOSETTING_FSWIDTH,
    VIDEOSETTING_FSHEIGHT,
    VIDEOSETTING_REFRESHRATE,
    VIDEOSETTING_SHADERSUPPORT,
    VIDEOSETTING_SHADERID,
    VIDEOSETTING_SCREENCOUNT,
#if RETRO_REV02
    VIDEOSETTING_DIMTIMER,
#endif
    VIDEOSETTING_STREAMSENABLED,
    VIDEOSETTING_STREAM_VOL,
    VIDEOSETTING_SFX_VOL,
    VIDEOSETTING_LANGUAGE,
    VIDEOSETTING_STORE,
    VIDEOSETTING_RELOAD,
    VIDEOSETTING_CHANGED,
    VIDEOSETTING_WRITE,
};

struct float4 {
    float x;
    float y;
    float z;
    float w;
};

struct float3 {
    float x;
    float y;
    float z;
};

struct float2 {
    float x;
    float y;
};

struct RenderVertex {
    float3 pos;
    uint32 color;
    float2 tex;
};

struct ShaderEntryBase {
    uint8 linear;
    char name[0x20];
};

class RenderDeviceBase
{
public:
    static bool Init();
    static void CopyFrameBuffer();
    static void ProcessDimming();
    static void FlipScreen();
    static void Release(bool32 isRefresh);

    static void RefreshWindow();

    static void SetupImageTexture(int32 width, int32 height, uint8 *imagePixels);
    static void SetupVideoTexture_YUV420(int32 width, int32 height, uint8 *imagePixels);
    static void SetupVideoTexture_YUV422(int32 width, int32 height, uint8 *imagePixels);
    static void SetupVideoTexture_YUV424(int32 width, int32 height, uint8 *imagePixels);

    static bool ProcessEvents();

    static void InitFPSCap();
    static bool CheckFPSCap();
    static void UpdateFPSCap();

    // Public since it's needed for the ModAPI
    static bool InitShaders();
    static void LoadShader(const char *fileName, bool32 linear);

    // ====================
    // RSDK COMMON START
    // ====================

    static bool32 isRunning;
    static int32 windowRefreshDelay;

    static int32 displayWidth[16];
    static int32 displayHeight[16];
    static int32 displayCount;

    static int32 lastShaderID;

#if RETRO_REV02
    static uint8 startVertex_2P[];
    static uint8 startVertex_3P[];
#endif

    static float2 pixelSize;
    static float2 textureSize;
    static float2 viewSize;

private:
    static bool SetupRendering();
    static void InitVertexBuffer();
    static bool InitGraphicsAPI();

    static void GetDisplays();
};

#if RETRO_RENDERDEVICE_DIRECTX9
#include "DX9/DX9RenderDevice.hpp"
#elif RETRO_RENDERDEVICE_DIRECTX11
#include "DX11/DX11RenderDevice.hpp"
#elif RETRO_RENDERDEVICE_SDL2
#include "SDL2/SDL2RenderDevice.hpp"
#elif RETRO_RENDERDEVICE_GLFW
#include "GLFW/GLFWRenderDevice.hpp"
#elif RETRO_RENDERDEVICE_VK
#include "Vulkan/VulkanRenderDevice.hpp"
#elif RETRO_RENDERDEVICE_EGL
#include "EGL/EGLRenderDevice.hpp"
#elif RETRO_RENDERDEVICE_SATURN
#include "SaturnRenderDevice.hpp" // platform/Saturn/ via -I (Task #196)
#endif

#if defined(P6_SCENE_TEST)
extern DrawList *drawGroups; // P6.3: relocated (DEAD), defined in p6_io_main.cpp
#else
extern DrawList drawGroups[DRAWGROUP_COUNT];
#endif
extern char drawGroupNames[0x10][0x10];

extern uint16 blendLookupTable[0x20 * 0x100];
extern uint16 subtractLookupTable[0x20 * 0x100];

extern GFXSurface gfxSurface[SURFACE_COUNT];

extern float dpi;
extern int32 cameraCount;
extern ScreenInfo screens[SCREEN_COUNT];
extern CameraInfo cameras[CAMERA_COUNT];
extern ScreenInfo *currentScreen;

extern int32 shaderCount;
extern ShaderEntry shaderList[SHADER_COUNT];

extern VideoSettings videoSettings;
extern VideoSettings videoSettingsBackup;
extern bool32 changedVideoSettings;

#if RETRO_USE_MOD_LOADER
extern int32 userShaderCount;
#else
#define userShaderCount (4)
#endif

void UpdateGameWindow();

void GenerateBlendLookupTable();

void InitSystemSurfaces();

void GetDisplayInfo(int32 *displayID, int32 *width, int32 *height, int32 *refreshRate, char *text);
void GetWindowSize(int32 *width, int32 *height);

#if RETRO_REV02
inline void SetScreenVertices(uint8 startVert2P_S1, uint8 startVert2P_S2, uint8 startVert3P_S1, uint8 startVert3P_S2, uint8 startVert3P_S3)
{
    RenderDevice::startVertex_2P[0] = startVert2P_S1;
    RenderDevice::startVertex_2P[1] = startVert2P_S2;

    RenderDevice::startVertex_3P[0] = startVert3P_S1;
    RenderDevice::startVertex_3P[1] = startVert3P_S2;
    RenderDevice::startVertex_3P[2] = startVert3P_S3;
}
#endif

void SetScreenSize(uint8 screenID, uint16 width, uint16 height);

inline void AddCamera(Vector2 *targetPos, int32 offsetX, int32 offsetY, bool32 worldRelative)
{
    if (cameraCount < CAMERA_COUNT) {
        cameras[cameraCount].targetPos     = targetPos;
        cameras[cameraCount].offset.x      = offsetX;
        cameras[cameraCount].offset.y      = offsetY;
        cameras[cameraCount].worldRelative = worldRelative;

        ++cameraCount;
    }
}

inline void ClearCameras() { cameraCount = 0; }

inline void SetClipBounds(uint8 screenID, int32 x1, int32 y1, int32 x2, int32 y2)
{
    ScreenInfo *screen;

    if (screenID < SCREEN_COUNT) {
        screen = &screens[screenID];

        screen->clipBound_X1 = CLAMP(x1, 0, screen->size.x);
        screen->clipBound_Y1 = CLAMP(y1, 0, screen->size.y);
        screen->clipBound_X2 = CLAMP(x2, 0, screen->size.x);
        screen->clipBound_Y2 = CLAMP(y2, 0, screen->size.y);
    }
}

int32 GetVideoSetting(int32 id);
void SetVideoSetting(int32 id, int32 value);

inline void AddDrawListRef(uint8 drawGroup, uint16 entityID)
{
    if (drawGroup < DRAWGROUP_COUNT)
        RSDK_DRAWGROUP_APPEND(drawGroups[drawGroup], entityID);
}

inline uint16 GetDrawListRefSlot(uint8 drawGroup, uint16 listPos)
{
    DrawList *list = &drawGroups[drawGroup];
    if (drawGroup < DRAWGROUP_COUNT && listPos < list->entityCount)
        return list->entries[listPos];

    return 0;
}

inline Entity *GetDrawListRef(uint8 drawGroup, uint16 listPos)
{
    DrawList *listPtr = &drawGroups[drawGroup];
    if (drawGroup < DRAWGROUP_COUNT && listPos < listPtr->entityCount)
        return &objectEntityList[listPtr->entries[listPos]];

    return NULL;
}

inline void SetDrawGroupProperties(uint8 drawGroup, bool32 sorted, void (*hookCB)())
{
    if (drawGroup < DRAWGROUP_COUNT) {
        DrawList *list = &drawGroups[drawGroup];
        list->sorted   = sorted;
        list->hookCB   = hookCB;
    }
}

void SwapDrawListEntries(uint8 drawGroup, uint16 slot1, uint16 slot2, uint16 count);

void FillScreen(uint32 color, int32 alphaR, int32 alphaG, int32 alphaB);

void DrawLine(int32 x1, int32 y1, int32 x2, int32 y2, uint32 color, int32 alpha, int32 inkEffect, bool32 screenRelative);
void DrawRectangle(int32 x, int32 y, int32 width, int32 height, uint32 color, int32 alpha, int32 inkEffect, bool32 screenRelative);
void DrawCircle(int32 x, int32 y, int32 radius, uint32 color, int32 alpha, int32 inkEffect, bool32 screenRelative);
void DrawCircleOutline(int32 x, int32 y, int32 innerRadius, int32 outerRadius, uint32 color, int32 alpha, int32 inkEffect, bool32 screenRelative);

void DrawFace(Vector2 *vertices, int32 vertCount, int32 r, int32 g, int32 b, int32 alpha, int32 inkEffect);
void DrawBlendedFace(Vector2 *vertices, uint32 *colors, int32 vertCount, int32 alpha, int32 inkEffect);

void DrawSprite(Animator *animator, Vector2 *position, bool32 screenRelative);
void DrawSpriteFlipped(int32 x, int32 y, int32 width, int32 height, int32 sprX, int32 sprY, int32 direction, int32 inkEffect, int32 alpha,
                       int32 sheetID);
void DrawSpriteRotozoom(int32 x, int32 y, int32 pivotX, int32 pivotY, int32 width, int32 height, int32 sprX, int32 sprY, int32 scaleX, int32 scaleY,
                        int32 direction, int16 Rotation, int32 inkEffect, int32 alpha, int32 sheetID);

void DrawDeformedSprite(uint16 sheetID, int32 inkEffect, int32 alpha);

void DrawTile(uint16 *tileInfo, int32 countX, int32 countY, Vector2 *position, Vector2 *offset, bool32 screenRelative);
void DrawAniTile(uint16 sheetID, uint16 tileIndex, uint16 srcX, uint16 srcY, uint16 width, uint16 height);

#if RETRO_REV0U || RETRO_USE_MOD_LOADER
inline void DrawDynamicAniTile(Animator *animator, uint16 tileIndex)
{
    if (animator->frames) {
        SpriteFrame *frame = &animator->frames[animator->frameID];
        DrawAniTile(frame->sheetID, tileIndex, frame->sprX, frame->sprY, frame->width, frame->height);
    }
}
#endif

void DrawString(Animator *animator, Vector2 *position, String *string, int32 endFrame, int32 textLength, int32 align, int32 spacing, void *unused,
                Vector2 *charPositions, bool32 screenRelative);
void DrawDevString(const char *string, int32 x, int32 y, int32 align, uint32 color);

inline void ClearGfxSurfaces()
{
    // Unload sprite sheets
    for (int32 s = 0; s < SURFACE_COUNT; ++s) {
        if (gfxSurface[s].scope != SCOPE_GLOBAL) {
            MEM_ZERO(gfxSurface[s]);
            gfxSurface[s].scope = SCOPE_NONE;
        }
    }
}

#if RETRO_REV0U
#include "Legacy/DrawingLegacy.hpp"
#endif

} // namespace RSDK

#endif // !DRAWING_H
