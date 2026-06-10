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
int32 GetVideoSetting(int32 id) { return 0; }
void SetVideoSetting(int32 id, int32 value) {}
void SwapDrawListEntries(uint8 drawGroup, uint16 slot1, uint16 slot2, uint16 count) {}
void FillScreen(uint32 color, int32 alphaR, int32 alphaG, int32 alphaB) {}
void DrawSprite(Animator *animator, Vector2 *position, bool32 screenRelative) {}
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
SFXInfo sfxList[SFX_COUNT];
int32 PlayStream(const char *filename, uint32 slot, uint32 startPos, uint32 loopPoint, bool32 loadASync) { return -1; }
int32 PlaySfx(uint16 sfx, uint32 loopPoint, uint32 priority) { return -1; }
void SetChannelAttributes(uint8 channel, float volume, float panning, float speed) {}
uint32 GetChannelPos(uint32 channel) { return 0; }

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
