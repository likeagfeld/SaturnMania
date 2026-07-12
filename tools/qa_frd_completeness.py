#!/usr/bin/env python3
# =============================================================================
# qa_frd_completeness.py -- FRD-inflate-class gate (C1 / InvincibleStars class).
#
# THE BUG CLASS (measured 2026-07-11, InvincibleStars sparkle): an anim .bin that
#   (a) IS in Data.rsdk (loads at runtime, aniFrames valid), AND
#   (b) references an FRD-COVERED sheet GIF, BUT
#   (c) was never EXTRACTED to extracted/ (wrong / missing path in maniafilelist.txt),
# is INVISIBLE to the offline tools/build_frame_dir.py (it walks extracted/*.bin only)
# -> its rects are absent from that sheet's .FRD -> every runtime draw of that anim
# MISSES the frame directory -> falls to a per-frame banded miniz inflate. Measured
# cost: ~8.6 inflates/frame at GHZ steady motion (InvincibleStars, Global/Invincible.bin
# -> Global/Shields.gif). Fix = extract the .bin + rebuild the FRD; runtime rects then HIT.
#
# THIS GATE: for every LoadSpriteAnimation("...") path the COMPILED objects reference,
# flag any that is (in-pack) AND (NOT extracted) AND (references an FRD-covered sheet).
# Any such = RED (a latent FRD inflate). InvincibleStars pre-fix would fire RED here;
# post-fix (Global/Invincible.bin extracted) it is GREEN.
#
# Sources of referenced anim paths: the object TUs actually compiled into the Saturn
# build (src/mania, the P6 overlay, the cached decomp set). Editor/* anims are skipped
# (GAME_INCLUDE_EDITOR is never compiled for Saturn).
#
#   python tools/qa_frd_completeness.py            # RED/GREEN + exit code
# =============================================================================
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
import rsdk_extract as R          # noqa: E402
from frame_census import parse_bin  # noqa: E402

# The 12 sheet GIFs build_frame_dir.py emits an .FRD for (its SHEETS list, lowercased).
FRD_GIFS = {
    "players/sonic1.gif", "players/sonic2.gif", "players/sonic3.gif", "players/tails1.gif",
    "global/items.gif", "global/display.gif", "global/shields.gif", "global/objects.gif",
    "global/phantomruby.gif", "ghz/objects.gif", "aiz/objects.gif", "ghzcutscene/objects.gif",
}
SRC_DIRS = ["src/mania", "tools/_portspike/_p6", "tools/_decomp_raw", "rsdkv5-src"]
PAT = re.compile(r'LoadSprite(?:Animation|Sheet)\("([^"]+)"')


def collect_paths():
    paths = set()
    for d in SRC_DIRS:
        base = os.path.join(ROOT, d)
        for root, _dirs, files in os.walk(base):
            for fn in files:
                if not fn.lower().endswith((".c", ".cpp", ".h", ".hpp")):
                    continue
                try:
                    txt = open(os.path.join(root, fn), errors="replace").read()
                except OSError:
                    continue
                for m in PAT.finditer(txt):
                    paths.add(m.group(1))
    return paths


def main():
    datapack = os.path.join(ROOT, "Data.rsdk")
    if not os.path.exists(datapack):
        print("qa_frd_completeness: SKIP (no Data.rsdk; run setup)")
        return 0
    raw, _fc, entries = R.parse_datapack(datapack)
    stored = {e["hash"] for e in entries}
    scratch = os.path.join(HERE, "_frd_gate_scratch")
    os.makedirs(scratch, exist_ok=True)
    by_hash = {e["hash"]: e for e in entries}

    red = []
    for p in sorted(collect_paths()):
        if not p.lower().endswith(".bin"):
            continue                       # sheets (.gif) are handled by staging, not FRD walk
        if p.lower().startswith("editor/"):
            continue                       # editor-only, never compiled on Saturn
        full = "Data/Sprites/" + p
        h = R.lookup_hash(full)
        if h not in stored:
            continue                       # not in pack -> runtime aniFrames=-1, DrawSprite guards it
        ondisk = os.path.join(ROOT, "extracted", full.replace("/", os.sep))
        if os.path.exists(ondisk):
            continue                       # extracted -> build_frame_dir already folds its rects
        # in-pack + NOT extracted: does it reference an FRD-covered sheet? (extract to check)
        e = by_hash[h]
        blob = raw[e["offset"]:e["offset"] + e["size"]]
        if e["encrypted"]:
            blob = R.decrypt(blob, full, e["size"])
        tmp = os.path.join(scratch, os.path.basename(p))
        open(tmp, "wb").write(blob)
        parsed = parse_bin(tmp)
        if parsed is None:
            continue
        sheets = [s.lower() for s in parsed[0]]
        hit = [s for s in sheets if s in FRD_GIFS]
        if hit:
            red.append((p, hit))
    # cleanup scratch
    for f in os.listdir(scratch):
        os.remove(os.path.join(scratch, f))
    os.rmdir(scratch)

    if red:
        print("qa_frd_completeness: RED -- FRD-inflate-class gaps (in-pack, NOT extracted, "
              "references an FRD sheet):")
        for p, hit in red:
            print("  %s -> %s  (add to tools/maniafilelist.txt + rebuild build_frame_dir)" % (p, hit))
        return 1
    print("qa_frd_completeness: GREEN -- every compiled anim referencing an FRD sheet is "
          "extracted (no latent banded-inflate class).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
