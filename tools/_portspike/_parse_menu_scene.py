#!/usr/bin/env python3
"""One-off M2 audit: parse extracted/Data/Stages/Menu/Scene1.bin object table.
Resolve class hashes against md5(name) for the Mania Menu class corpus, and for
every UIControl entity extract its `tag` (VAR_STRING) + position. Reports which
of UISaveSlot / UITransition / UIDialog / the "Save Select" UIControl are placed,
and the per-UIControl tag list (the navigation graph)."""
import hashlib, struct, sys, os

SCENE = r"D:\sonicmaniasaturn\extracted\Data\Stages\Menu\Scene1.bin"

# The Menu StageConfig object list (per build_gameconfig_hash_table) + global.
# We hash a broad corpus so every placed class resolves by name.
NAMES = """DefaultObject DevOutput APICallback Announcer FXFade UIControl UIButton
UIButtonLabel UIButtonPrompt UIBackground UIWidgets UIText UIHeading UISubHeading
UIChoice UISlider UIVsRoundPicker UIResPicker UIWinSize UIKeyBinder UIPicture
UIModeButton UISaveSlot UICharButton UITAZoneModule UIVsCharSelector UIVsZoneButton
UIVsScoreboard UIVsResults UILeaderboard UIInfoLabel UIRankButton UIMedallionPanel
UIOptionPanel UIDialog UITransition UIWaitSpinner UIUsernamePopup UICreditsText
UIDiorama UICarousel UIShifter UIReplayCarousel UIPopover MenuSetup MenuParam
ManiaModeMenu MainMenu TimeAttackMenu CompetitionMenu OptionsMenu ExtrasMenu
LevelSelect DemoMenu ThanksSetup TimeAttackData GameProgress SaveGame Localization
Music Player Camera HUD Zone TitleCard PauseMenu Options Soundboard
CompetitionSession Logo TextOverlay""".split()

HASHMAP = {hashlib.md5(n.encode()).digest(): n for n in NAMES}

with open(SCENE, "rb") as f:
    d = f.read()
assert d[:4] == b"SCN\x00", "not SCN"

p = 4 + 0x10  # signature + 16-byte editor metadata head (per verify_scene)
nl = d[p]; p += 1 + nl + 1            # name pstring + trailing byte
layer_count = d[p]; p += 1
for _ in range(layer_count):
    p += 1                            # visibleInEditor
    nm = d[p]; p += 1 + nm            # layer name
    p += 1 + 1 + 2 + 2 + 2 + 2        # type+drawGroup+xs+ys+px+sc
    sic = struct.unpack_from("<H", d, p)[0]; p += 2
    p += sic * 6                      # scrollInfo
    for _z in range(2):               # line-scroll + layout compressed blobs
        total = struct.unpack_from("<I", d, p)[0]; p += 4
        struct.unpack_from(">I", d, p)[0]; p += 4
        p += total - 4

obj_count = d[p]; p += 1
print(f"objectCount = {obj_count}")

VAR_SIZE = {0:1,1:2,2:4,3:1,4:2,5:4,6:4,7:4,9:8,11:4}  # 8 string, 10? handled below
present = {}
controls = []   # (tag, x>>16, y>>16, slot)
for oi in range(obj_count):
    nhash = d[p:p+16]; p += 16
    name = HASHMAP.get(nhash, "??"+nhash[:4].hex())
    var_count = d[p]; p += 1
    # attribs: (var_count-1) of (16 hash + 1 type). We need the type + know if
    # the active var is 'tag' for UIControl. We don't have var names here, so we
    # record the type sequence and, for UIControl, the FIRST VAR_STRING is `tag`
    # (UIControl_Serialize order: tag is the first editable var).
    attrib_types = []
    for _ in range(max(0, var_count-1)):
        p += 16
        at = d[p]; p += 1
        attrib_types.append(at)
    entity_count = struct.unpack_from("<H", d, p)[0]; p += 2
    present[name] = present.get(name, 0) + entity_count
    for _e in range(entity_count):
        slot = struct.unpack_from("<H", d, p)[0]; p += 2
        x = struct.unpack_from("<i", d, p)[0]; p += 4
        y = struct.unpack_from("<i", d, p)[0]; p += 4
        first_string = None
        for at in attrib_types:
            if at == 8:
                ln = struct.unpack_from("<H", d, p)[0]; p += 2
                raw = d[p:p+ln*2]; p += ln*2
                s = raw[0::2].decode("latin-1", "replace")  # UTF-16LE -> ascii low bytes
                if first_string is None:
                    first_string = s
            else:
                p += VAR_SIZE.get(at, 4)
        if name == "UIControl":
            controls.append((first_string, x >> 16, y >> 16, slot))

print("\n=== object classes present (name: entityCount) ===")
for n in sorted(present):
    print(f"  {n:24s} {present[n]}")

print("\n=== UIControl entities (tag, worldX, worldY, slot) ===")
for tag, x, y, slot in controls:
    print(f"  tag={tag!r:30s} pos=({x},{y}) slot={slot}")

print("\n=== M2 key-class presence ===")
for k in ["UISaveSlot", "UITransition", "UIDialog", "UIModeButton", "MenuSetup", "FXFade"]:
    print(f"  {k:16s} {'PRESENT x'+str(present[k]) if k in present else 'ABSENT'}")
saveselect = [c for c in controls if c[0] == "Save Select"]
print(f"  'Save Select' UIControl: {'PRESENT '+str(saveselect) if saveselect else 'ABSENT'}")
