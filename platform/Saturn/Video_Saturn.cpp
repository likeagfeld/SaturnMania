// =============================================================================
// Video_Saturn.cpp -- P0 backend-contract shim (engine true-port, #199).
//
// Defines the single video contract symbol: ProcessVideo (Video.hpp:26), the
// per-frame FMV pump the core calls unconditionally. Saturn has no Theora; the
// real body (Cinepak/CD-stream, or a no-op when no video is playing) lands at
// P3/P5. ABI-exact matching enforced via RSDK/Core/RetroEngine.hpp.
// =============================================================================
#include "RSDK/Core/RetroEngine.hpp"

using namespace RSDK;

void RSDK::ProcessVideo() {}
