#!/usr/bin/env python3
# P4 guard gate (Task #203) -- collisionMasks/tileInfo x4 -> x1 on Saturn.
#
# WHAT THIS LEVER DOES
#   The stock engine sizes collisionMasks[CPATH_COUNT][TILE_COUNT*4] and
#   tileInfo[CPATH_COUNT][TILE_COUNT*4]. The x4 holds three software pre-built
#   flipped copies (FLIP_X/Y/XY) of every tile's collision geometry, generated
#   in LoadTileConfig (Scene.cpp:869-949). On Saturn the VDP collision backend
#   (P5/P6) derives flipped geometry from per-cell flip bits, so the x3 expansion
#   is dead .bss. Dropping it reclaims ~414 KB (collisionMasks 512->128 KB,
#   tileInfo 40->10 KB).
#
# THE BUG CLASS THIS GATE CATCHES (RED-first)
#   The collision SENSORS read collisionMasks[cPlane][tile & 0xFFF] and
#   tileInfo[..][tile & 0xFFF] where tile&0xFFF == 0..4095 indexes the x4 region
#   (Collision.cpp 24 sites). With a x1 array, any flipped tile (index >= 1024)
#   is an OUT-OF-BOUNDS read that silently corrupts adjacent WRAM .bss -- the
#   exact Phase 1.4-1.15 BSS-overflow class (15 iterations). Relying only on
#   "the proof scene runs no tile collision" repeats that failed assumption.
#   So the safe lever ALSO masks every Saturn collision read to the base region
#   via COLLISION_TILE_MASK (0x3FF on Saturn / 0xFFF else): a flipped tile reads
#   the FLIP_NONE mask -- a bounded, in-bounds geometric approximation retired at
#   P6, never OOB. This gate asserts BOTH the shrink AND that zero unmasked
#   `tile & 0xFFF]` collision reads remain on the Saturn path.
#
# RED on the current tree (macros undefined, arrays *4, reads unmasked, flip
# write-blocks ungated). GREEN once every check below holds. PC build stays
# byte-identical (COLLISION_FLIPCOUNT==4, COLLISION_TILE_MASK==0xFFF off Saturn).
#
# P6 RESTORATION: delete the two macros + the `#if RETRO_PLATFORM != RETRO_SATURN`
# guards; the arrays return to *4 and reads to 0xFFF -- exactly like SCENEENTITY_COUNT.

import os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCENE_HPP = os.path.join(ROOT, "rsdkv5-src", "RSDKv5", "RSDK", "Scene", "Scene.hpp")
SCENE_CPP = os.path.join(ROOT, "rsdkv5-src", "RSDKv5", "RSDK", "Scene", "Scene.cpp")
COLL_CPP  = os.path.join(ROOT, "rsdkv5-src", "RSDKv5", "RSDK", "Scene", "Collision.cpp")
BUILD_SH  = os.path.join(ROOT, "tools", "_portspike", "build_trueport.sh")

def read(p):
    with open(p, "r", encoding="utf-8", errors="replace") as f:
        return f.read()

def norm(s):
    # collapse runs of whitespace so `*  COLLISION_FLIPCOUNT` matches `* COLLISION_FLIPCOUNT`
    return re.sub(r"[ \t]+", " ", s)

checks = []  # (name, ok, detail)

hpp = read(SCENE_HPP)
cpp = read(SCENE_CPP)
coll = read(COLL_CPP)
bsh = read(BUILD_SH) if os.path.exists(BUILD_SH) else ""

nhpp = norm(hpp)
ncpp = norm(cpp)

# C1: COLLISION_FLIPCOUNT macro present, Saturn=1 / else=4
c1 = (re.search(r"#define COLLISION_FLIPCOUNT \(1\)", nhpp) is not None and
      re.search(r"#define COLLISION_FLIPCOUNT \(4\)", nhpp) is not None)
checks.append(("C1 COLLISION_FLIPCOUNT macro (Saturn 1 / else 4)", c1,
               "found both #define forms" if c1 else "missing 1-and-4 #define pair"))

# C2: COLLISION_TILE_MASK macro present, Saturn=0x3FF / else=0xFFF
c2 = (re.search(r"#define COLLISION_TILE_MASK \(0x3FF\)", nhpp) is not None and
      re.search(r"#define COLLISION_TILE_MASK \(0xFFF\)", nhpp) is not None)
checks.append(("C2 COLLISION_TILE_MASK macro (Saturn 0x3FF / else 0xFFF)", c2,
               "found both #define forms" if c2 else "missing 0x3FF-and-0xFFF #define pair"))

# C3: extern decls in Scene.hpp use TILE_COUNT * COLLISION_FLIPCOUNT (no bare *4)
c3a = re.search(r"extern CollisionMask collisionMasks\[CPATH_COUNT\]\[TILE_COUNT \* COLLISION_FLIPCOUNT\]", nhpp) is not None
c3b = re.search(r"extern TileInfo tileInfo\[CPATH_COUNT\]\[TILE_COUNT \* COLLISION_FLIPCOUNT\]", nhpp) is not None
c3 = c3a and c3b
checks.append(("C3 Scene.hpp extern dims use COLLISION_FLIPCOUNT", c3,
               "both externs migrated" if c3 else f"collisionMasks={c3a} tileInfo={c3b}"))

# C4: definitions in Scene.cpp use TILE_COUNT * COLLISION_FLIPCOUNT
c4a = re.search(r"CollisionMask RSDK::collisionMasks\[CPATH_COUNT\]\[TILE_COUNT \* COLLISION_FLIPCOUNT\]", ncpp) is not None
c4b = re.search(r"TileInfo RSDK::tileInfo\[CPATH_COUNT\]\[TILE_COUNT \* COLLISION_FLIPCOUNT\]", ncpp) is not None
c4 = c4a and c4b
checks.append(("C4 Scene.cpp definition dims use COLLISION_FLIPCOUNT", c4,
               "both defs migrated" if c4 else f"collisionMasks={c4a} tileInfo={c4b}"))

# C5: the three flip write-blocks in LoadTileConfig are guarded out on Saturn.
# Find the LoadTileConfig body and confirm each `// FlipX/Y/XY` loop sits inside a
# `#if RETRO_PLATFORM != RETRO_SATURN ... #endif` region.
def guarded_region_indices(text):
    """Return list of (start,end) char spans where RETRO_PLATFORM != RETRO_SATURN holds."""
    spans = []
    stack = []  # (kind, active_when_true)
    i = 0
    # crude but adequate single-level scanner for the != RETRO_SATURN guard
    for m in re.finditer(r"#if[ \t]+RETRO_PLATFORM[ \t]*!=[ \t]*RETRO_SATURN|#endif", text):
        tok = m.group(0)
        if tok.startswith("#if"):
            stack.append(m.end())
        else:
            if stack:
                start = stack.pop()
                spans.append((start, m.start()))
    return spans

spans = guarded_region_indices(cpp)
def in_guard(pos):
    return any(s <= pos < e for (s, e) in spans)

c5 = True
c5_detail = []
for tag in ("// FlipX", "// FlipY", "// FlipXY"):
    m = re.search(re.escape(tag), cpp)
    if not m:
        c5 = False; c5_detail.append(f"{tag} not found"); continue
    if not in_guard(m.start()):
        c5 = False; c5_detail.append(f"{tag} NOT guarded")
    else:
        c5_detail.append(f"{tag} guarded")
checks.append(("C5 LoadTileConfig FlipX/Y/XY blocks guarded != RETRO_SATURN", c5,
               "; ".join(c5_detail)))

# C6: zero unmasked `tile & 0xFFF]` collision reads remain (all -> COLLISION_TILE_MASK).
unmasked = re.findall(r"tile & 0xFFF\]", coll)
c6 = len(unmasked) == 0
# sanity: confirm the migrated form is actually present (so we didn't just delete reads)
migrated = re.findall(r"tile & COLLISION_TILE_MASK\]", coll)
c6 = c6 and len(migrated) >= 20
checks.append(("C6 Collision.cpp reads masked to base (no `tile & 0xFFF]`)", c6,
               f"unmasked={len(unmasked)} migrated={len(migrated)}"))

# C7: CopyCollisionMask is dead in the Saturn build (its flip writes use *FLIP_X/Y/XY
# which would be OOB on a x1 array, but it is gated `#if RETRO_REV0U || RETRO_USE_MOD_LOADER`
# and build_trueport.sh forces both off).
c7 = ("-DRETRO_USE_MOD_LOADER=0" in bsh) and ("-DRETRO_REVISION=2" in bsh)
checks.append(("C7 build_trueport.sh keeps CopyCollisionMask uncompiled", c7,
               "MOD_LOADER=0 & REVISION=2 set" if c7 else "build flags missing"))

print("=" * 72)
print("P4 GUARD GATE: collisionMasks/tileInfo x4 -> x1 (Saturn)")
print("=" * 72)
allok = True
for name, ok, detail in checks:
    allok = allok and ok
    print(f"  [{'GREEN' if ok else ' RED '}] {name}")
    print(f"          {detail}")
print("-" * 72)
if allok:
    print("RESULT: GREEN -- lever applied, x1 shrink is OOB-safe on Saturn.")
    sys.exit(0)
else:
    print("RESULT: RED -- lever not (fully) applied; x1 shrink would be unsafe.")
    sys.exit(1)
