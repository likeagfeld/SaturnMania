// =============================================================================
// P3Witness_Saturn.cpp -- P3 boot-witness exports (engine true-port, #202).
//
// The P3 gate (tools/_portspike/qa_p3_core_boots.py) proves the true-ported
// RSDKv5 logic core BOOTS on Saturn by peeking three runtime witnesses from a
// Mednafen savestate. Rather than hand-compute SH-2 struct offsets, this TU
// exports four C-linkage globals whose initialisers the LINKER resolves; the
// gate reads each from its .map address (mcs_extract double-indirection).
//
// The sh-none-elf-8.2.0 toolchain prefixes a leading underscore to every C
// symbol, so the extern "C" names here (p3_w_*) become the ELF names the gate
// searches for (_p3_w_*).
//
//   p3_w_engine_init = &engine.initialized   The compiler bakes the struct
//   p3_w_scene_state = &sceneInfo.state       offset; the linker bakes the base
//                                             address. Gate peeks the pointer,
//                                             then peeks the field it targets.
//   p3_w_expect_none = ENGINESTATE_NONE      The build's OWN enum value (13
//                                             under RETRO_REVISION=2). Lets the
//                                             gate compare against the compiled
//                                             constant, catching a mis-set REV.
//   p3_w_magic       = 0x12345678            WRAM-H byte-order calibration
//                                             anchor (Task #136 pair-swap).
//
// All four are plain (non-const => external linkage) globals in .data, which is
// part of the loaded image at 0x06004000 (WRAM-H). The two-bank P3 linker
// script (p3.linker) keeps engine + sceneInfo .bss in WRAM-H too, so the magic,
// the pointers, and the fields they target all share ONE byte-order
// calibration. Saturn-only scaffolding; the PC build never compiles this TU.
// P6 retires it alongside crt0.s + p3.linker if the true-port does not ship.
// =============================================================================
#include "RSDK/Core/RetroEngine.hpp"

using namespace RSDK;

extern "C" {
bool32 *p3_w_engine_init = &engine.initialized;
uint8  *p3_w_scene_state = &sceneInfo.state;
uint32  p3_w_expect_none = (uint32)ENGINESTATE_NONE;
uint32  p3_w_magic       = 0x12345678u;
}
