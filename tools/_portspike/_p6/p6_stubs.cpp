// =============================================================================
// p6_stubs.cpp -- the 51-symbol link-closure TU for P6.1 (Task #205).
//
// The REAL Core/Link.cpp SetupFunctionTables() (Core_Link.o) takes the ADDRESS
// of every backend entry point as it populates RSDKFunctionTable[]/APIFunction-
// Table[]. 51 of those entry points are NOT defined by any object in the current
// true-port set (the authoritative worklist is _p6/_undef_symbols.txt, produced
// by probe_undef.sh). This TU closes that gap with minimal out-of-line
// definitions so the GREEN link resolves.
//
// The hand-stub-via-real-headers technique: this TU #includes the REAL engine
// headers, so the compiler checks each definition below against the real
// declaration. A wrong signature is a COMPILE error here, not a silent link
// mismatch -- mangling is therefore guaranteed correct. Every stub body is a
// trivial no-op / return-0; the real hardware backends land later:
//   - Drawing (14) + RenderDeviceBase vertex arrays -> P6.4 (VDP1/VDP2)
//   - Video (1) -> P6.5
//   - Audio (5) -> P6.5 (SCSP + CD-DA)
//   - User/SKU (29) -> P6.5 (Saturn has no achievements/leaderboards/cloud DB;
//     these stay return-0 permanently -- the Ring never calls them, they only
//     need to LINK so SetupFunctionTables can address them).
//
// NOTE: channels[CHANNEL_COUNT] is intentionally NOT defined here -- it is
// already provided by AudioDevice_Saturn.cpp (defining it again = collision).
// Saturn-only P6 scaffolding.
// =============================================================================
#include "RSDK/Core/RetroEngine.hpp"
#include <stdarg.h>

// =============================================================================
// Drawing (14) + RenderDeviceBase static vertex arrays  (Graphics/Drawing.hpp)
// =============================================================================
namespace RSDK {

void GenerateBlendLookupTable() {}
void InitSystemSurfaces() {}
void GetDisplayInfo(int32 *displayID, int32 *width, int32 *height, int32 *refreshRate, char *text) {}
void GetWindowSize(int32 *width, int32 *height) {}
void SetScreenSize(uint8 screenID, uint16 width, uint16 height) {}
// W14b ROOT CAUSE (Task #227, MEASURED p6_fd 2026-06-12): this pair were
// return-0/no-op P6.1 link-closure stubs, but Camera_StageLoad gates its
// SLOT_CAMERA1 ResetEntitySlot loop on GetVideoSetting(VIDEOSETTING_
// SCREENCOUNT) (Camera.c:95) -- the stub's 0 meant NO Camera entity was
// ever created, Camera_LateUpdate never wrote screens[0].position
// (Camera.c:105-107), and the Player never went onScreen (qa_p6_player P8
// RED: drawflags onScreen=0, slotdelta=0 with binds/draws all live).
// Discriminator chain: p6_w_eng_vs_count=1 vs p6_w_eng_screencount=0 in the
// same function proved the call -- not the data -- was broken.
// Bodies below are verbatim Drawing.cpp:414-448 and :450-559; Drawing.cpp
// itself is not in the pack, so its file-scope changedVideoSettings /
// videoSettingsBackup (extern, Drawing.hpp:333-334) are DEFINED here.
// VIDEOSETTING_WRITE keeps no-op: SaveSettingsINI is the PC ini writer.
VideoSettings videoSettingsBackup;
bool32 changedVideoSettings = false;

int32 GetVideoSetting(int32 id)
{
    switch (id) {
        case VIDEOSETTING_WINDOWED: return videoSettings.windowed;
        case VIDEOSETTING_BORDERED: return videoSettings.bordered;
        case VIDEOSETTING_EXCLUSIVEFS: return videoSettings.exclusiveFS;
        case VIDEOSETTING_VSYNC: return videoSettings.vsync;
        case VIDEOSETTING_TRIPLEBUFFERED: return videoSettings.tripleBuffered;
        case VIDEOSETTING_WINDOW_WIDTH: return videoSettings.windowWidth;
        case VIDEOSETTING_WINDOW_HEIGHT: return videoSettings.windowHeight;
        case VIDEOSETTING_FSWIDTH: return videoSettings.fsWidth;
        case VIDEOSETTING_FSHEIGHT: return videoSettings.fsHeight;
        case VIDEOSETTING_REFRESHRATE: return videoSettings.refreshRate;
        case VIDEOSETTING_SHADERSUPPORT: return videoSettings.shaderSupport;
        case VIDEOSETTING_SHADERID: return videoSettings.shaderID;
        case VIDEOSETTING_SCREENCOUNT: return videoSettings.screenCount;
#if RETRO_REV02
        case VIDEOSETTING_DIMTIMER: return videoSettings.dimTimer;
#endif
        case VIDEOSETTING_STREAMSENABLED: return engine.streamsEnabled;
        case VIDEOSETTING_STREAM_VOL: return (int32)(engine.streamVolume * 1024.0);
        case VIDEOSETTING_SFX_VOL: return (int32)(engine.soundFXVolume * 1024.0);
        case VIDEOSETTING_LANGUAGE:
#if RETRO_REV02
            return SKU::curSKU.language;
#else
            return gameVerInfo.language;
#endif
        case VIDEOSETTING_CHANGED: return changedVideoSettings;

        default: break;
    }

    return 0;
}

void SetVideoSetting(int32 id, int32 value)
{
    bool32 boolVal = value;
    switch (id) {
        case VIDEOSETTING_WINDOWED:
            if (videoSettings.windowed != boolVal) {
                videoSettings.windowed = boolVal;
                changedVideoSettings   = true;
            }
            break;

        case VIDEOSETTING_BORDERED:
            if (videoSettings.bordered != boolVal) {
                videoSettings.bordered = boolVal;
                changedVideoSettings   = true;
            }
            break;

        case VIDEOSETTING_EXCLUSIVEFS:
            if (videoSettings.exclusiveFS != boolVal) {
                videoSettings.exclusiveFS = boolVal;
                changedVideoSettings      = true;
            }
            break;

        case VIDEOSETTING_VSYNC:
            if (videoSettings.vsync != boolVal) {
                videoSettings.vsync  = boolVal;
                changedVideoSettings = true;
            }
            break;

        case VIDEOSETTING_TRIPLEBUFFERED:
            if (videoSettings.tripleBuffered != boolVal) {
                videoSettings.tripleBuffered = boolVal;
                changedVideoSettings         = true;
            }
            break;

        case VIDEOSETTING_WINDOW_WIDTH:
            if (videoSettings.windowWidth != value) {
                videoSettings.windowWidth = value;
                changedVideoSettings      = true;
            }
            break;

        case VIDEOSETTING_WINDOW_HEIGHT:
            if (videoSettings.windowHeight != value) {
                videoSettings.windowHeight = value;
                changedVideoSettings       = true;
            }
            break;

        case VIDEOSETTING_FSWIDTH: videoSettings.fsWidth = value; break;
        case VIDEOSETTING_FSHEIGHT: videoSettings.fsHeight = value; break;
        case VIDEOSETTING_REFRESHRATE: videoSettings.refreshRate = value; break;
        case VIDEOSETTING_SHADERSUPPORT: videoSettings.shaderSupport = value; break;
        case VIDEOSETTING_SHADERID:
            if (videoSettings.shaderID != value) {
                videoSettings.shaderID = value;
                changedVideoSettings   = true;
            }
            break;

        case VIDEOSETTING_SCREENCOUNT: videoSettings.screenCount = value; break;
#if RETRO_REV02
        case VIDEOSETTING_DIMTIMER: videoSettings.dimLimit = value; break;
#endif
        case VIDEOSETTING_STREAMSENABLED:
            if (engine.streamsEnabled != boolVal)
                changedVideoSettings = true;

            engine.streamsEnabled = boolVal;
            break;

        case VIDEOSETTING_STREAM_VOL:
            if (engine.streamVolume != (value / 1024.0f)) {
                engine.streamVolume  = (float)value / 1024.0f;
                changedVideoSettings = true;
            }
            break;

        case VIDEOSETTING_SFX_VOL:
            if (engine.soundFXVolume != ((float)value / 1024.0f)) {
                engine.soundFXVolume = (float)value / 1024.0f;
                changedVideoSettings = true;
            }
            break;

        case VIDEOSETTING_LANGUAGE:
#if RETRO_REV02
            SKU::curSKU.language = value;
#else
            gameVerInfo.language = value;
#endif
            break;

        case VIDEOSETTING_STORE: memcpy(&videoSettingsBackup, &videoSettings, sizeof(videoSettings)); break;

        case VIDEOSETTING_RELOAD:
            changedVideoSettings = true;
            memcpy(&videoSettings, &videoSettingsBackup, sizeof(videoSettingsBackup));
            break;

        case VIDEOSETTING_CHANGED: changedVideoSettings = boolVal; break;

        case VIDEOSETTING_WRITE: break; // SaveSettingsINI is PC-only (no ini on disc)

        default: break;
    }
}
void SwapDrawListEntries(uint8 drawGroup, uint16 slot1, uint16 slot2, uint16 count) {}
#if defined(P6_FRONTEND_TITLE)
// TASK 2 (this session): the title INTRO fade/flash. The PC FillScreen blends
// `color` over the software framebuffer, which on Saturn is the frameBuffer[1]
// stub = a no-op. Forward to the real Saturn VDP1 full-screen-overlay impl in
// p6_vdp1.c (extern "C"; that TU owns <jo/jo.h>). Front-end-only -- the GHZ flavor
// keeps the no-op below (it has no intro). See p6_vdp1.c p6_fillscreen_saturn.
extern "C" void p6_fillscreen_saturn(unsigned int color, int alphaR, int alphaG, int alphaB);
void FillScreen(uint32 color, int32 alphaR, int32 alphaG, int32 alphaB)
{
    p6_fillscreen_saturn((unsigned int)color, (int)alphaR, (int)alphaG, (int)alphaB);
}
#else
void FillScreen(uint32 color, int32 alphaR, int32 alphaG, int32 alphaB) {}
#endif
#if !defined(P6_SCENE_TEST) // P6.5b3: real Saturn DrawSprite backend in p6_io_main.cpp
void DrawSprite(Animator *animator, Vector2 *position, bool32 screenRelative) {}
#endif
void DrawDeformedSprite(uint16 sheetID, int32 inkEffect, int32 alpha) {}
void DrawTile(uint16 *tileInfo, int32 countX, int32 countY, Vector2 *position, Vector2 *offset, bool32 screenRelative) {}
void DrawAniTile(uint16 sheetID, uint16 tileIndex, uint16 srcX, uint16 srcY, uint16 width, uint16 height) {}
void DrawString(Animator *animator, Vector2 *position, String *string, int32 endFrame, int32 textLength, int32 align, int32 spacing,
                void *unused, Vector2 *charPositions, bool32 screenRelative) {}

#if RETRO_REV02
// Declared `static uint8 startVertex_2P[];` (incomplete) in RenderDeviceBase.
// Sized to the max index used by Drawing.hpp:330-335 (2P -> [0..1], 3P -> [0..2]).
uint8 RenderDeviceBase::startVertex_2P[2];
uint8 RenderDeviceBase::startVertex_3P[3];
#endif

// ===========================================================================
// Video (1)  (Graphics/Video.hpp)
// ===========================================================================
bool32 LoadVideo(const char *filename, double startDelay, bool32 (*skipCallback)()) { return false; }

// ===========================================================================
// Audio (5)  (Audio/Audio.hpp)  -- channels[] stays in AudioDevice_Saturn.cpp
// ===========================================================================
#if !defined(P6_SCENE_TEST) // P6.6: real Audio.cpp (Audio_Audio.o) owns these in the pack
SFXInfo sfxList[SFX_COUNT];
int32 PlayStream(const char *filename, uint32 slot, uint32 startPos, uint32 loopPoint, bool32 loadASync) { return -1; }
int32 PlaySfx(uint16 sfx, uint32 loopPoint, uint32 priority) { return -1; }
void SetChannelAttributes(uint8 channel, float volume, float panning, float speed) {}
uint32 GetChannelPos(uint32 channel) { return 0; }
#endif

} // namespace RSDK

// =============================================================================
// User / SKU (29)  (User/Core/UserStorage.hpp + UserLeaderboards/Achievements/
// Presence/Stats.hpp). All return-0/NULL: Saturn has no online backend; the
// Ring never calls these -- they only need to LINK.
// =============================================================================
namespace RSDK {
namespace SKU {

// ---- Backend singleton pointers (base-class views; never instantiated) -----
UserStorage      *userStorage   = NULL;
UserDBStorage    *userDBStorage = NULL;
UserAchievements *achievements  = NULL;
UserLeaderboards *leaderboards  = NULL;
UserRichPresence *richPresence  = NULL;
UserStats        *stats         = NULL;

// ---- UserDB row-sorting / value free functions -----------------------------
uint16 SetupUserDBRowSorting(uint16 tableID) { return (uint16)-1; }
int32  AddUserDBRowSortFilter(uint16 tableID, int32 type, const char *name, void *value) { return 0; }
int32  SortUserDBRows(uint16 tableID, int32 type, const char *name, bool32 sortAscending) { return 0; }
int32  GetSortedUserDBRowCount(uint16 tableID) { return 0; }
int32  GetSortedUserDBRowID(uint16 tableID, uint16 entryID) { return -1; }
bool32 GetUserDBValue(uint16 tableID, uint32 rowID, int32 type, char *name, void *value) { return false; }
bool32 SetUserDBValue(uint16 tableID, uint32 rowID, int32 type, char *name, void *value) { return false; }
bool32 GetUserDBRowsChanged(uint16 tableID) { return false; }
void   GetUserDBRowCreationTime(uint16 tableID, uint16 rowID, char *buf, size_t size, char *format) {}

// ---- User file I/O ---------------------------------------------------------
bool32 LoadUserFile(const char *filename, void *buffer, uint32 bufSize) { return false; }
bool32 SaveUserFile(const char *filename, void *buffer, uint32 bufSize) { return false; }

// ---- Leaderboards ----------------------------------------------------------
void ResetLeaderboardInfo() {}
LeaderboardEntry *ReadLeaderboardEntry(int32 entryID) { return NULL; }
void LeaderboardEntryInfo::LoadLeaderboardEntries(int32 start, uint32 length, int32 type) {}

// ---- UserDB member functions -----------------------------------------------
int32  UserDB::AddRow() { return -1; }
uint16 UserDB::GetRowByID(uint32 uuid) { return (uint16)-1; }
bool32 UserDB::RemoveRow(uint32 row) { return false; }
bool32 UserDB::RemoveAllRows() { return false; }

// ---- UserDBStorage member functions ----------------------------------------
uint16 UserDBStorage::InitUserDB(const char *name, va_list list) { return (uint16)-1; }
uint16 UserDBStorage::LoadUserDB(const char *filename, void (*callback)(int32 status)) { return (uint16)-1; }
bool32 UserDBStorage::SaveUserDB(uint16 tableID, void (*callback)(int32 status)) { return false; }
void   UserDBStorage::ClearUserDB(uint16 tableID) {}
void   UserDBStorage::ClearAllUserDBs() {}

} // namespace SKU
} // namespace RSDK
