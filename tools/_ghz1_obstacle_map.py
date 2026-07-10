#!/usr/bin/env python3
# _ghz1_obstacle_map.py -- AUTHORITATIVE GHZ1 obstacle/waypoint dump for the
# scripted-autorun input table (signpost campaign). Parses Scene1.bin per
# Scene.cpp:558-780 (same walk as _parse_ghz_scene.py) and emits EVERY entity
# of the traversal-relevant classes sorted by x, plus Player spawn + SignPost.
# Data source: extracted/Data/Stages/GHZ/Scene1.bin (decomp-authoritative).
import importlib.util, struct, hashlib, sys
from pathlib import Path

spec = importlib.util.spec_from_file_location("rs", "tools/render_scene.py")
rs = importlib.util.module_from_spec(spec); spec.loader.exec_module(rs)
R = rs.R
d = open("extracted/Data/Stages/GHZ/Scene1.bin", "rb").read()
r = R(d)
assert d[:4] == b"SCN\x00"
r.p = 4
r.skip(0x10)
sl = r.u8(); r.skip(sl + 1)
layer_count = r.u8()
for _ in range(layer_count):
    r.u8(); r.s(); r.u8(); r.u8()
    r.u16(); r.u16(); r.u16(); r.u16()
    sic = r.u16()
    for _ in range(sic):
        r.u16(); r.u16(); r.u8(); r.u8()
    r.compressed(); r.compressed()

NAMES = """Player Ring RingSparkle ItemBox Monitor Spikes Spring SpringBoard
Bumper Platform CollapsingPlatform Bridge Barrier BreakableWall CheckerBall
Decoration Motobug BuzzBomber Crabmeat Newtron Splats Chopper Batbrain
ForceSpin ForceUnstick SpinBooster CorkscrewPath ZipLine SpikeLog BurningLog
Water BGSwitch PlaneSwitch BoundsMarker InvisibleBlock Camera Music HUD SignPost
GHZSetup GHZ2Outro CutsceneSeq DDWrecker DERobot Eggman TimeAttackGate WoodChipper
DustDevil TitleCard ActClear ScoreBonus Explosion Animals Dust ImageTrail
Shield GameOver PauseMenu SaveGame DebugMode Soundboard StarPost""".split()
hmap = {}
for nm in NAMES:
    for enc in ("ascii", "utf-16-le"):
        hmap[hashlib.md5(nm.encode(enc)).digest()] = nm

VARSIZE = {0: 1, 1: 2, 2: 4, 3: 1, 4: 2, 5: 4, 6: 4, 7: 4, 9: 8, 10: 4, 11: 4}
objcount = r.u8()
WANT = {"Player", "SignPost", "Motobug", "BuzzBomber", "Crabmeat", "Newtron",
        "Chopper", "Batbrain", "Spikes", "SpikeLog", "Spring", "Bridge",
        "CollapsingPlatform", "Platform", "StarPost", "ForceSpin", "SpinBooster",
        "PlaneSwitch", "ItemBox", "BreakableWall", "InvisibleBlock", "Water",
        "BoundsMarker", "DDWrecker", "CorkscrewPath"}
rows = []
for i in range(objcount):
    h = bytes(d[r.p:r.p+16]); r.p += 16
    name = hmap.get(h, "?%s" % h.hex()[:12])
    varcount = d[r.p]; r.p += 1
    types = [None]
    for e in range(1, varcount):
        r.p += 16
        types.append(d[r.p]); r.p += 1
    ecount = struct.unpack_from("<H", d, r.p)[0]; r.p += 2
    for e in range(ecount):
        slot = struct.unpack_from("<H", d, r.p)[0]; r.p += 2
        px = struct.unpack_from("<i", d, r.p)[0]; r.p += 4
        py = struct.unpack_from("<i", d, r.p)[0]; r.p += 4
        for v in range(1, varcount):
            t = types[v]
            if t == 8:
                ln = struct.unpack_from("<H", d, r.p)[0]; r.p += 2 + ln*2
            else:
                r.p += VARSIZE.get(t, 4)
        if name in WANT:
            rows.append((px >> 16, py >> 16, name, slot))

rows.sort()
if "--json" in sys.argv:
    import json
    print(json.dumps([{"x": x, "y": y, "cls": n, "slot": s} for x, y, n, s in rows]))
else:
    for x, y, n, s in rows:
        print("%6d %6d  %-20s slot%4d" % (x, y, n, s))
