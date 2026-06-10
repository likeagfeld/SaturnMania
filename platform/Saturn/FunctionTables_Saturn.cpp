// =============================================================================
// FunctionTables_Saturn.cpp -- P0 backend-contract shim (engine true-port,#199).
//
// Mirrors Link.cpp: the RSDK/API function-table storage, the table-setup entry
// point, and the game version info. The core indexes RSDKFunctionTable to
// dispatch every RSDK.* API call, so SetupFunctionTables() gets its REAL body
// at P3 (it just memsets + populates the tables -- no Saturn hardware). P0
// keeps it a stub so the contract closes; the populated table lands with the
// core boot. ABI-exact matching enforced via RSDK/Core/RetroEngine.hpp.
// =============================================================================
#include "RSDK/Core/RetroEngine.hpp"

using namespace RSDK;

// Link.hpp:327,329 / Link.cpp:6,9 -- dispatch tables (real storage).
void *RSDK::RSDKFunctionTable[FunctionTable_Count];
#if RETRO_REV02
void *RSDK::APIFunctionTable[APITable_Count];
#endif

// Link.cpp:65 -- populates the tables above; real body at P3.
void RSDK::SetupFunctionTables() {}

// Text.hpp:18 -- game title/subtitle/version/filter (real storage).
RSDK::GameVersionInfo RSDK::gameVerInfo;
