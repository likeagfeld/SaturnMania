#!/usr/bin/env python3
# =============================================================================
# build_include_tree.py -- P6.7d (Task #210): regenerate the sizing include
# tree from the flat decomp cache. Maps tools/_decomp_raw flat names onto the
# include paths the decomp sources use (root headers + "Cat/Obj.h").
# The obj/ + obj_os/ output dirs and this include/ tree are generated --
# gitignored; tools/_decomp_raw is the committed source of truth.
#
# Usage: python tools/_portspike/_p67d_sizing/build_include_tree.py
# Then:  docker run ... bash tools/_portspike/_p67d_sizing/compile_os.sh
# =============================================================================
import glob
import os
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
RAW = os.path.join(ROOT, "tools", "_decomp_raw")
INC = os.path.join(HERE, "include")

ROOT_HEADERS = [("SonicMania_Game.h", "Game.h"),
                ("SonicMania_GameLink.h", "GameLink.h"),
                ("SonicMania_GameVariables.h", "GameVariables.h"),
                ("SonicMania_GameObjects.h", "GameObjects.h"),
                ("SonicMania_All.h", "All.h")]

# W14b (Task #227, MEASURED p6_ff 2026-06-12): the game-side RSDKScreenInfo
# MUST mirror the engine's Saturn ScreenInfo retarget (Drawing.hpp:110-122,
# frameBuffer[1]) -- both sides share the ONE screens[] instance through the
# EngineInfo link, so the full PC frameBuffer puts every game-side
# position/size/center access 143,356 B past the engine struct
# (Camera_Create read a garbage center; Camera_SetCameraBounds wrote
# screens[0].position into pack .bss 143 KB away; the Player never went
# onScreen). Gated on SATURN_GLOBALS_RETARGET (the game-TU census knob).
# No TU in the verbatim closure touches frameBuffer (grep-verified).
GAMELINK_FB_STOCK = """    // uint16 *frameBuffer;
    uint16 frameBuffer[SCREEN_XMAX * SCREEN_YSIZE];
"""
GAMELINK_FB_SATURN = """#if defined(SATURN_GLOBALS_RETARGET)
    // Saturn retarget (W14b): mirrors engine Drawing.hpp frameBuffer[1] --
    // see build_include_tree.py for the measured rationale.
    uint16 frameBuffer[1];
#else
    // uint16 *frameBuffer;
    uint16 frameBuffer[SCREEN_XMAX * SCREEN_YSIZE];
#endif
"""


def patch_gamelink(path):
    with open(path, "r", newline="") as f:
        text = f.read()
    if GAMELINK_FB_STOCK not in text:
        raise SystemExit("GameLink.h frameBuffer block not found -- "
                         "decomp header shape changed; re-derive the W14b patch")
    with open(path, "w", newline="") as f:
        f.write(text.replace(GAMELINK_FB_STOCK, GAMELINK_FB_SATURN, 1))


def main():
    os.makedirs(INC, exist_ok=True)
    for flat, name in ROOT_HEADERS:
        shutil.copy(os.path.join(RAW, flat), os.path.join(INC, name))
    patch_gamelink(os.path.join(INC, "GameLink.h"))
    n = 0
    for p in glob.glob(os.path.join(RAW, "SonicMania_Objects_*.h")):
        base = os.path.basename(p)[len("SonicMania_Objects_"):-2]
        cat, obj = base.split("_", 1)
        d = os.path.join(INC, cat)
        os.makedirs(d, exist_ok=True)
        shutil.copy(p, os.path.join(d, obj + ".h"))
        n += 1
    print("placed", len(ROOT_HEADERS), "root headers +", n, "object headers")


if __name__ == "__main__":
    main()
