#!/usr/bin/env python3
# P4 guard gate (Task #203) -- Levers 3+4: screens.frameBuffer stub, LAYER_COUNT cap,
# DATAFILE_COUNT cap. The remaining ~336 KB needed to clear the 2 MB Saturn physical
# line after Lever 1 (collisionMasks/tileInfo x4->x1) and Lever 2 (data[0x100]->0x40).
#
# WHAT THESE LEVERS DO (measured this session from _p2_link.elf .bss):
#   L3  screens[1].frameBuffer[SCREEN_XMAX*SCREEN_YSIZE] (uint16 320*224 = 143360 B) is
#       the SOFTWARE blit target the Saturn never uses -- VDP1 sprites + VDP2 NBG render
#       in hardware. Its only writers are the DrawLayer* software rasterizers
#       (Object.cpp:799-805). Stub frameBuffer -> [1] AND Saturn-gate the DrawLayer*
#       switch off (so the stub can never be written out-of-bounds). Reclaims ~140 KB.
#   L4a dataFileList[DATAFILE_COUNT] (RSDKFileInfo 32 B/entry = 128 KB at 0x1000) is the
#       Data.rsdk pack registry. The Saturn CD ships Saturn-native assets, not a pack,
#       so it registers ~0 files. Cap 0x1000 -> 0x100 (8 KB) AND Saturn-clamp the
#       LoadDataPack fill loop. Reclaims ~120 KB.
#   L4b tileLayers[LAYER_COUNT] (13384 B/layer = 104.6 KB at 8). Proof scene has 0 tile
#       layers; cap 8 -> 4 (53.5 KB) AND Saturn-clamp LoadScene's layerCount. Reclaims
#       ~53 KB.
#
# THE BUG CLASS THIS GATE CATCHES (RED-first)
#   A bare cap WITHOUT a loader clamp just relocates the Phase 1.4-1.15 .bss-overflow:
#   a scene/pack declaring more layers/files than the (now smaller) array would write
#   past it into the adjacent .bss. So each cap REQUIRES its Saturn loader-clamp, and a
#   stubbed frameBuffer REQUIRES the DrawLayer* dispatch gated off. This gate is RED
#   until cap+clamp (and stub+gate) are BOTH present for every lever.
#
# RED on the current tree (full-size frameBuffer, ungated DrawLayer*, DATAFILE_COUNT
# 0x1000, LAYER_COUNT 8, no clamps). GREEN once every check below holds. PC build stays
# byte-identical (every Saturn branch is #if'd out off-Saturn).
#
# The AUTHORITATIVE fit number comes from qa_p4_memfit.py on the re-linked ELF; the
# arithmetic projection here (L7) is a source-side sanity check against the measured
# 2326104 B baseline.
#
# P6 RESTORATION: delete the Saturn branches + the three loader clamps; frameBuffer,
# DATAFILE_COUNT and LAYER_COUNT return to stock -- exactly like SCENEENTITY_COUNT,
# COLLISION_FLIPCOUNT and OBJECT_DATA_COUNT.

import os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DRAW_HPP  = os.path.join(ROOT, "rsdkv5-src", "RSDKv5", "RSDK", "Graphics", "Drawing.hpp")
OBJ_CPP   = os.path.join(ROOT, "rsdkv5-src", "RSDKv5", "RSDK", "Scene", "Object.cpp")
READ_HPP  = os.path.join(ROOT, "rsdkv5-src", "RSDKv5", "RSDK", "Core", "Reader.hpp")
READ_CPP  = os.path.join(ROOT, "rsdkv5-src", "RSDKv5", "RSDK", "Core", "Reader.cpp")
SCENE_CPP = os.path.join(ROOT, "rsdkv5-src", "RSDKv5", "RSDK", "Scene", "Scene.cpp")

# Measured baseline (this session, qa_p4_memfit on the post-Lever-1/2 _p2_link.elf):
BASELINE_TOTAL = 2326104          # bytes (image + heap), pre Lever-3/4
PHYSICAL       = 2 * 1024 * 1024  # 2 MB hard Saturn ceiling (WRAM-H + WRAM-L)
# Per-lever .bss deltas (exact, from the measured array sizes):
DELTA_FRAMEBUF = 143360 - 2                 # 320*224 uint16 -> [1] uint16
DELTA_DATAFILE = (0x1000 - 0x100) * 32      # 3840 entries * 32 B/RSDKFileInfo
DELTA_TILELYR  = (8 - 4) * 13384            # 4 layers * 13384 B/TileLayer

def read(p):
    with open(p, "r", encoding="utf-8", errors="replace") as f:
        return f.read()

def norm(s):
    return re.sub(r"[ \t]+", " ", s)

dhpp = norm(read(DRAW_HPP))
ocpp = norm(read(OBJ_CPP))
rhpp = norm(read(READ_HPP))
rcpp = norm(read(READ_CPP))
scpp = norm(read(SCENE_CPP))

checks = []  # (name, ok, detail)

# L1: Drawing.hpp ScreenInfo.frameBuffer Saturn-stubbed to [1], full-size off-Saturn.
l1a = re.search(r"uint16 frameBuffer\[1\];", dhpp) is not None
l1b = re.search(r"uint16 frameBuffer\[SCREEN_XMAX \* SCREEN_YSIZE\];", dhpp) is not None
l1 = l1a and l1b
checks.append(("L1 ScreenInfo.frameBuffer Saturn-stub [1] / else full-size", l1,
               "both forms present" if l1 else f"saturn[1]={l1a} else_full={l1b}"))

# L2: Object.cpp DrawLayer* software dispatch Saturn-gated OFF (VDP2 does layers; the
#     stubbed frameBuffer must never be written by the rasterizers).
l2 = re.search(
    r"#if RETRO_PLATFORM != RETRO_SATURN.{0,800}?DrawLayerHScroll.{0,400}?DrawLayerBasic.{0,200}?#endif",
    ocpp, re.DOTALL) is not None
checks.append(("L2 Object.cpp DrawLayer* switch Saturn-gated off", l2,
               "guard present" if l2 else "no `#if RETRO_PLATFORM != RETRO_SATURN ... DrawLayer* ... #endif`"))

# L3: Reader.hpp DATAFILE_COUNT macro Saturn=0x100 / else=0x1000.
l3a = re.search(r"#define DATAFILE_COUNT \(0x100\)", rhpp) is not None
l3b = re.search(r"#define DATAFILE_COUNT \(0x1000\)", rhpp) is not None
l3 = l3a and l3b
checks.append(("L3 DATAFILE_COUNT macro (Saturn 0x100 / else 0x1000)", l3,
               "both forms present" if l3 else f"saturn0x100={l3a} else0x1000={l3b}"))

# L4: Reader.cpp LoadDataPack Saturn-clamps the pack fileCount to DATAFILE_COUNT.
l4 = re.search(
    r"#if RETRO_PLATFORM == RETRO_SATURN.{0,500}?fileCount > DATAFILE_COUNT.{0,300}?#endif",
    rcpp, re.DOTALL) is not None
checks.append(("L4 Reader.cpp LoadDataPack clamps fileCount to DATAFILE_COUNT", l4,
               "clamp present" if l4 else "no Saturn `fileCount > DATAFILE_COUNT` clamp"))

# L5: Drawing.hpp LAYER_COUNT macro Saturn=4 / else=8.
l5a = re.search(r"#define LAYER_COUNT\s+\(4\)", dhpp) is not None
l5b = re.search(r"#define LAYER_COUNT\s+\(8\)", dhpp) is not None
l5 = l5a and l5b
checks.append(("L5 LAYER_COUNT macro (Saturn 4 / else 8)", l5,
               "both forms present" if l5 else f"saturn4={l5a} else8={l5b}"))

# L6: Scene.cpp LoadScene Saturn-clamps the scene layerCount to LAYER_COUNT.
l6 = re.search(
    r"#if RETRO_PLATFORM == RETRO_SATURN.{0,500}?layerCount > LAYER_COUNT.{0,300}?#endif",
    scpp, re.DOTALL) is not None
checks.append(("L6 Scene.cpp LoadScene clamps layerCount to LAYER_COUNT", l6,
               "clamp present" if l6 else "no Saturn `layerCount > LAYER_COUNT` clamp"))

# L7: arithmetic projection -- the three levers must take the measured baseline UNDER
#     the 2 MB physical line with a comfortable margin (>= 64 KB).
projected = BASELINE_TOTAL - (DELTA_FRAMEBUF + DELTA_DATAFILE + DELTA_TILELYR)
margin = PHYSICAL - projected
l7 = projected < PHYSICAL and margin >= 64 * 1024
checks.append(("L7 projection: baseline - levers < 2 MB physical (>=64 KB margin)", l7,
               f"baseline={BASELINE_TOTAL} - (fb {DELTA_FRAMEBUF} + file {DELTA_DATAFILE} + "
               f"layer {DELTA_TILELYR}) = {projected} ; physical {PHYSICAL} ; margin {margin} "
               f"({margin/1024:.1f} KB)"))

print("=" * 72)
print("P4 GUARD GATE: frameBuffer stub + DATAFILE_COUNT cap + LAYER_COUNT cap (Saturn)")
print("=" * 72)
allok = True
for name, ok, detail in checks:
    allok = allok and ok
    print(f"  [{'GREEN' if ok else ' RED '}] {name}")
    print(f"          {detail}")
print("-" * 72)
if allok:
    print("RESULT: GREEN -- frameBuffer stubbed + both caps applied with loader clamps;")
    print("        no silent overflow replaces the reclaimed .bss. Re-link + qa_p4_memfit")
    print("        for the authoritative fit number.")
    sys.exit(0)
else:
    print("RESULT: RED -- a lever's cap/stub or its loader clamp/dispatch-gate is missing;")
    print("        applying the cap without the clamp would reintroduce the .bss overflow.")
    sys.exit(1)
