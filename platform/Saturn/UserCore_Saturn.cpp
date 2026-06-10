// =============================================================================
// UserCore_Saturn.cpp -- P0 backend-contract shim (engine true-port, #199).
//
// Defines the 12 SKU user-services + settings-INI contract symbols the
// platform-INDEPENDENT core leaves undefined. The real engine spreads these
// across Link.cpp / UserCore.cpp / UserStorage.cpp / UserAchievements.cpp; the
// Saturn port collapses the user backend into one TU. ABI-exact matching is
// enforced by including RSDK/Core/RetroEngine.hpp. P0 = real storage for the
// state structs + link-only stubs; the Saturn SKU (NULL user, no achievements,
// backup-RAM-backed settings) is wired up at P3.
// =============================================================================
#include "RSDK/Core/RetroEngine.hpp"

using namespace RSDK;

// --- SKU services (UserCore.hpp:47-48,108; UserStorage.hpp:402) ---------------
// curSKU + unknownInfo are ALSO defined by the REAL Core/Link.cpp (Link.cpp:11-12).
// The P5 link has no Core_Link.o so this TU must own them; the P6.1 GREEN link
// includes Core_Link.o, so it recompiles this TU with -DRSDK_SKU_GLOBALS_IN_LINK
// to cede ownership to Core_Link.o and avoid a multiple-definition collision.
#ifndef RSDK_SKU_GLOBALS_IN_LINK
RSDK::SKU::SKUInfo     RSDK::SKU::curSKU;
RSDK::SKU::UnknownInfo RSDK::SKU::unknownInfo;
#endif
RSDK::SKU::UserCore   *RSDK::SKU::userCore = NULL;
char                   RSDK::SKU::userFileDir[0x100];

// --- P3 (#202): concrete Saturn UserCore so the core actually BOOTS ----------
// RunRetroEngine derefs userCore at :40 (GetUserPlatform), :81 (CheckAPIInitialized,
// must be true or RRE returns 0 before InitEngine), and every frame at :122
// (FrameInit), :124 (CheckEnginePause), :131/:133 (CheckFocusLost). The base
// RSDK::SKU::UserCore (UserCore.hpp:61-106, RETRO_REV02) already returns the
// boot-correct defaults (CheckAPIInitialized->true, CheckEnginePause->false,
// CheckFocusLost->false, GetUserPlatform->PLATFORM_PC). Only StageLoad/FrameInit/
// OnUnknownEvent are out-of-line, so instantiating the base emits a vtable that
// references those 3 symbols -- define them as no-ops (the boot-to-ENGINESTATE_NONE
// witness needs no user-services work) and publish a static instance. The static
// instance has a vtable, so its vptr is set by a global constructor that crt0's
// .ctors pass runs before main (P3Witness/crt0 scaffolding). P6: a real Saturn SKU
// (backup-RAM settings, NULL achievements) replaces these no-ops.
void RSDK::SKU::UserCore::StageLoad() {}
void RSDK::SKU::UserCore::FrameInit() {}
void RSDK::SKU::UserCore::OnUnknownEvent() {}

static RSDK::SKU::UserCore saturnUserCore;

void RSDK::SKU::InitUserCore() { userCore = &saturnUserCore; }
void RSDK::SKU::ReleaseUserCore() { userCore = NULL; }

// UserAchievements.hpp:115-117 -- no achievement backend on Saturn.
void RSDK::SKU::LoadAchievementAssets() {}
void RSDK::SKU::ProcessAchievements() {}
void RSDK::SKU::DrawAchievements() {}

// --- Settings INI (UserCore.hpp:190,193-194) ---------------------------------
RSDK::CustomSettings RSDK::customSettings;
void RSDK::LoadSettingsINI() {}
void RSDK::SaveSettingsINI(bool32 writeToFile) {}
