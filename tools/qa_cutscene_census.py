#!/usr/bin/env python3
"""
qa_cutscene_census.py -- Task #328 cutscene-parity census + RED gate.

Parses the two intro-cutscene scene binaries
  extracted/Data/Stages/AIZ/Scene1.bin          (Cutscenes / Angel Island Zone)
  extracted/Data/Stages/GHZCutscene/Scene1.bin  (Cutscenes / Green Hill Zone)
per RSDKv5-Decompilation Scene.cpp::LoadSceneFile (object hash + attrib table +
entity records) and emits the DECOMP-EXPECTED drawn-actor list per scene:
every placed entity of every visible cutscene class, with position and the
attribute values that select its sprite (e.g. CutsceneHBH.characterID,
Platform.frameID, Decoration.type).

Gate mode (--gate <sheet-manifest>): compares the expected sheet set per scene
against the sheets the Saturn legs stage (hardcoded from the staging census of
tools/_portspike/_p6/p6_io_main.cpp -- p6_aiz_reload + the AIZ->GHZCut seam).
Exits 1 (RED) when an expected actor's sheet is not staged on that leg.

Decomp citations for the per-class sheet requirement are inline in EXPECT_*.
"""
import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Broad class-name table: all globals + cutscene actors + attribute names.
KNOWN_NAMES = """
AIZSetup FXRuby AIZTornado AIZTornadoPath FXFade CutsceneSeq Platform
Decoration AIZEggRobo AIZKingClaw PhantomRuby RubyPortal
GHZSetup BGSwitch GHZCutsceneST GHZCutsceneK CutsceneHBH FXTrail GHZ2Outro
DERobot Eggman
Player Ring HUD Music SaveGame Localization APICallback DemoMenu Options
Camera WindowInfo StarPost Animals Dust Explosion Debris ScoreBonus
PauseMenu ImageTrail ActClear TitleCard GameOver DebugMode Soundboard
PlaneSwitch BoundsMarker InvisibleBlock ForcePlate Motobug BuzzBomber
Crabmeat Chopper Newtron Splats Batbrain SpikeLog BurningLog CheckerBall
type frame frameID track trackFile channel isLooping desc filter tag size
characterID direction oscillate hiddenAtStart outerRadius fadeWhite fadeBlack
waitForTrigger sfx bgID rotSpeed speed collapseDelay targetPos amplitude
angle childCount speedOut speedIn wait state color onlyOnLoad activated
""".split()

HMAP = {hashlib.md5(n.encode("ascii")).digest(): n for n in KNOWN_NAMES}

VARSIZE = {0: 1, 1: 2, 2: 4, 3: 1, 4: 2, 5: 4, 6: 4, 7: 4, 9: 8, 10: 4, 11: 4}


class R:
    def __init__(self, d):
        self.d = d
        self.p = 0

    def u8(self):
        v = self.d[self.p]; self.p += 1; return v

    def u16(self):
        v = struct.unpack_from("<H", self.d, self.p)[0]; self.p += 2; return v

    def i32(self):
        v = struct.unpack_from("<i", self.d, self.p)[0]; self.p += 4; return v

    def s(self):
        n = self.u8()
        v = self.d[self.p:self.p + n].decode("latin-1"); self.p += n
        return v

    def skip(self, n):
        self.p += n

    def compressed(self):
        csz = struct.unpack_from("<I", self.d, self.p)[0]; self.p += 4
        self.p += csz  # u32 uncompressed size + zlib blob live inside csz


def parse_scene(path):
    d = path.read_bytes()
    assert d[:4] == b"SCN\x00", path
    r = R(d)
    r.p = 4
    r.skip(0x10)
    sl = r.u8(); r.skip(sl + 1)
    for _ in range(r.u8()):        # layers
        r.u8(); r.s(); r.u8(); r.u8()
        r.u16(); r.u16(); r.u16(); r.u16()
        for _ in range(r.u16()):
            r.u16(); r.u16(); r.u8(); r.u8()
        r.compressed()
        r.compressed()
    objs = []
    for _ in range(r.u8()):
        h = bytes(d[r.p:r.p + 16]); r.p += 16
        name = HMAP.get(h, "?" + h.hex()[:12])
        varcount = r.u8()
        vnames, vtypes = [None], [None]
        for _ in range(1, varcount):
            vh = bytes(d[r.p:r.p + 16]); r.p += 16
            vnames.append(HMAP.get(vh, "?" + vh.hex()[:8]))
            vtypes.append(r.u8())
        ents = []
        for _ in range(r.u16()):
            slot = r.u16()
            px, py = r.i32() >> 16, r.i32() >> 16
            attrs = {}
            for v in range(1, varcount):
                t = vtypes[v]
                if t == 8:
                    ln = r.u16(); r.skip(ln * 2); attrs[vnames[v]] = "<str>"
                elif t == 9:
                    attrs[vnames[v]] = (r.i32() >> 16, r.i32() >> 16)
                else:
                    raw = d[r.p:r.p + VARSIZE.get(t, 4)]
                    r.skip(VARSIZE.get(t, 4))
                    attrs[vnames[v]] = int.from_bytes(raw, "little")
            ents.append((slot, px, py, attrs))
        objs.append((name, ents))
    return objs


# DECOMP-EXPECTED sheet per drawn cutscene class (pre-plus, ST path).
# class -> (sheet path used by its anim .bin, decomp citation)
SHEET_OF = {
    "AIZTornado":  ("AIZ/Objects.gif",           "AIZTornado_StageLoad AIZ/AIZTornado.bin"),
    "Platform":    ("<zone>/Objects.gif",        "Platform_StageLoad <folder>/Platform.bin"),
    "Decoration":  ("AIZ/Objects.gif",           "Decoration_StageLoad AIZ/Decoration.bin"),
    "AIZEggRobo":  ("AIZ/Objects.gif",           "AIZEggRobo_StageLoad AIZ/AIZEggRobo.bin"),
    "AIZKingClaw": ("<zone>/Objects.gif",        "AIZKingClaw_StageLoad <folder>/Claw.bin"),
    "PhantomRuby": ("Global/PhantomRuby.gif",    "PhantomRuby_StageLoad Global/PhantomRuby.bin"),
    "RubyPortal":  ("AIZ/Portal.gif",            "RubyPortal_StageLoad AIZ/Portal.bin (PLUS only)"),
    "CutsceneHBH": ("Cutscene/HBH.gif (Saturn HBHOBJ.SHT rewrite)",
                    "CutsceneHBH_LoadSprites SPZ1/Boss.bin+PSZ2/Shinobi.bin+MSZ/HeavyMystic.bin+LRZ3/HeavyRider.bin+LRZ3/HeavyKing.bin"),
    "Ring":        ("Global/Items.gif",          "Ring_StageLoad Global/Ring.bin"),
    "HUD":         ("Global/Display.gif",        "HUD_StageLoad Global/HUD.bin"),
    "Player":      ("Players/Sonic1-3.gif + Players/Tails1.gif", "Player_StageLoad"),
}

# Sheets staged per Saturn leg (census of tools/_portspike/_p6/p6_io_main.cpp).
STAGED = {
    "AIZ": [  # p6_aiz_reload (:7457-7681) + p6_scene_load_and_arm AIZ branch
        "Players/Sonic1.gif", "Players/Sonic2.gif", "Players/Sonic3.gif",
        "Players/Tails1.gif", "AIZ/Objects.gif",
    ],
    "GHZCutscene": [  # AIZ->GHZCut seam (:8398-8428) / p6_ghzcut_reload
        "GHZCutscene/Objects.gif", "Global/PhantomRuby.gif",
        "Global/Items.gif", "Global/Display.gif",
        "Cutscene/HBH.gif (Saturn HBHOBJ.SHT rewrite)",
        "Players/Sonic1.gif", "Players/Sonic2.gif", "Players/Sonic3.gif",
        "Players/Tails1.gif",
    ],
}

DRAWN_CLASSES = set(SHEET_OF)


def main():
    gate = "--gate" in sys.argv
    red = []
    for folder in ("AIZ", "GHZCutscene"):
        p = ROOT / "extracted/Data/Stages" / folder / "Scene1.bin"
        objs = parse_scene(p)
        print("=" * 72)
        print("SCENE %s/Scene1.bin -- placed entities per class" % folder)
        for name, ents in objs:
            if not ents:
                continue
            print("  %-14s x%-3d" % (name, len(ents)))
            for slot, px, py, attrs in ents:
                sel = {k: v for k, v in attrs.items()
                       if k in ("characterID", "frameID", "type", "sfx",
                                "direction", "oscillate", "hiddenAtStart")}
                print("     slot%4d (%6d,%6d) %s" % (slot, px, py, sel or ""))
            if name in DRAWN_CLASSES:
                sheet, cite = SHEET_OF[name]
                sheet = sheet.replace("<zone>", folder)
                staged = STAGED[folder]
                ok = any(sheet == s or sheet.split(" ")[0] == s for s in staged)
                mark = "OK   " if ok else "RED  "
                if not ok:
                    red.append((folder, name, sheet, cite))
                print("     %s sheet %-28s [%s]" % (mark, sheet, cite))
    print("=" * 72)
    if red:
        print("RED: %d expected drawn actors have NO staged sheet on their Saturn leg:" % len(red))
        for folder, name, sheet, cite in red:
            print("  %-12s %-14s needs %-30s (%s)" % (folder, name, sheet, cite))
    else:
        print("GREEN: every placed drawn cutscene actor's sheet is staged.")
    if gate:
        sys.exit(1 if red else 0)


if __name__ == "__main__":
    main()
