// =============================================================================
// AudioDevice_Saturn.cpp -- P0 backend-contract shim (engine true-port, #199).
//
// Defines the 4 audio contract symbols the platform-INDEPENDENT core leaves
// undefined: AudioDevice::Release (SaturnAudioDevice.hpp), the `channels`
// mixer array, and the SFX loaders (Audio.hpp). ABI-exact matching is enforced
// by including RSDK/Core/RetroEngine.hpp -- the same header the core compiled
// against. P0 = link-only stubs + real `channels` storage; SCSP/CD-DA bodies
// arrive at P3/P5.
// =============================================================================
#include "RSDK/Core/RetroEngine.hpp"

using namespace RSDK;

// AudioDevice : public AudioDeviceBase (SaturnAudioDevice.hpp). Only Release()
// is odr-used by the core (Init/InitAudioChannels/FrameInit are inline or
// unreferenced); the rest of the device is brought up at P3.
void RSDK::AudioDevice::Release() {}

// Audio.hpp:42 -- the SFX/stream channel mixer state (real storage).
ChannelInfo RSDK::channels[CHANNEL_COUNT];

// Audio.hpp:69,199 -- SFX bank management (CD-streamed at P5).
void RSDK::ClearStageSfx() {}
void RSDK::LoadSfx(char *filePath, uint8 plays, uint8 scope) {}
