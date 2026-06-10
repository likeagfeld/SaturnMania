// SaturnAudioDevice.hpp -- Sega Saturn audio device-backend DECLARATION surface.
// True-port engine pivot (Task #196). Included from Audio/Audio.hpp via the
// RETRO_AUDIODEVICE_SATURN #elif branch, at GLOBAL scope (Audio.hpp closes
// namespace RSDK before the device include), so this header opens its own
// namespace RSDK -- mirrors Audio/NX/NXAudioDevice.hpp (the proven no-STL,
// no-pthread console audio template; the Switch port is the closest analogue).
//
// LockAudioDevice/UnlockAudioDevice are no-ops here: RSDK's audio mixing runs off
// the host's audio callback on desktop/console, but the Saturn mixes on the 68000
// SCSP path (the existing hand-port src/rsdk/audio layer + CD-DA). The core TUs
// reference these macros only through Audio.hpp inline StopSfx/StopAllSfx/etc.
// AudioDevice::Init/Release/InitAudioChannels are defined in the Saturn audio
// backend .cpp (added in the audio-wiring phase); they are extern references for
// the core compile, which is all the logic core needs to codegen to SH-2 .o.

#define LockAudioDevice()   ;
#define UnlockAudioDevice() ;

namespace RSDK
{
class AudioDevice : public AudioDeviceBase
{
public:
    static bool32 Init();
    static void Release();

    static void FrameInit() {}

    // P6.6c (Task #209): DECLARATION only -- the Saturn device body maps the
    // engine's stream request (streamFilePath, set by PlayStream's sprintf,
    // Audio.cpp:291) to a CD-DA CUE track instead of running the PC vorbis
    // LoadStream: OGG decode is not real-time-feasible on SH-2 and the
    // Saturn-fit BGM transport is CD audio (bgm-loops-hand-curated /
    // saturn-cdda-cue-format memory rules). Defined in the P6 proof body
    // (p6_io_main.cpp) until the integrated backend lands at P6.8.
    static void HandleStreamLoad(ChannelInfo *channel, bool32 async);

private:
    static uint8 contextInitialized;

    static void InitAudioChannels();
    static void InitMixBuffer() {}
};
} // namespace RSDK
