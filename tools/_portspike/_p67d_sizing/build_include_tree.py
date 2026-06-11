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


def main():
    os.makedirs(INC, exist_ok=True)
    for flat, name in ROOT_HEADERS:
        shutil.copy(os.path.join(RAW, flat), os.path.join(INC, name))
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
