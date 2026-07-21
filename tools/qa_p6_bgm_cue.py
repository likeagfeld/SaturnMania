#!/usr/bin/env python3
"""qa_p6_bgm_cue.py -- RED-first gate for the wrong-BGM defect (measured 2026-07-20).

MEASURED ROOT CAUSE: the engine's AudioDevice::HandleStreamLoad maps each zone's
PlayStream to a distinct CD-DA track -- GreenHill1=2, TitleScreen=3, AngelIsland=4,
RubyPresence=5, HBHMischief=6, BossEggman1=7. But game.cue shipped with only TWO
audio tracks pointing at _arc.wav / _arc_sound.wav (a stale video-capture layout).
So GHZ's "track 2" plays _arc.wav (not GreenHill music) and requests for tracks
4-7 (AIZ/Ruby/HBH/Eggman) hit NON-EXISTENT tracks -> wrong / missing music (the
user's "sega saturn boot sound instead of game music"). The per-zone tracks exist
(cd_audio/track02..07.wav) but were never wired into the CUE -- build_shipping.sh
DOCUMENTS the build_cdda.py multi-track step but never runs it.

This gate asserts game.cue is the correct multi-track BGM cuesheet: TRACK 01 data
(game.iso) + AUDIO tracks 02..07 referencing the per-zone track0N.bin (NOT _arc).
RED on the stale CUE; GREEN after build_cdda.py wires the 6 per-zone tracks.

Exit 0 = GREEN, 1 = RED (defect present), 2 = game.cue missing.
"""
import re
import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parent.parent
CUE = _ROOT / "game.cue"
NEED_AUDIO = 6   # tracks 02..07 = GreenHill/Title/AIZ/Ruby/HBH/Eggman


def main():
    if not CUE.exists():
        sys.stderr.write("qa_p6_bgm_cue: game.cue missing -- build first\n")
        return 2
    text = CUE.read_text(errors="replace")
    audio = re.findall(r"TRACK\s+(\d+)\s+AUDIO", text)
    files = re.findall(r'FILE\s+"([^"]+)"', text)
    arc = [f for f in files if "_arc" in f.lower()]
    perzone = [f for f in files if re.search(r"track0[2-7]", f, re.I)]

    print("=" * 66)
    print("qa_p6_bgm_cue  (wrong-BGM gate: per-zone CD-DA tracks in game.cue)")
    print("=" * 66)
    print(f"  AUDIO tracks in CUE   : {len(audio)}  (need >= {NEED_AUDIO}: engine maps zones->tracks 2-7)")
    print(f"  per-zone track files  : {sorted(set(perzone))}")
    print(f"  stale _arc files      : {sorted(set(arc))}")

    fail = []
    if len(audio) < NEED_AUDIO:
        fail.append(f"only {len(audio)} AUDIO tracks (< {NEED_AUDIO}) -- zones 4-7 (AIZ/Ruby/"
                    f"HBH/Eggman) have NO CD-DA track -> silent/wrong BGM")
    if arc:
        fail.append(f"CUE references stale arc audio {sorted(set(arc))} instead of the per-zone "
                    f"BGM tracks -- GHZ 'track 2' plays _arc.wav, not GreenHill music")
    if not perzone:
        fail.append("no per-zone track0N.bin referenced -- BGM track map is broken")
    if fail:
        for f in fail:
            print(f"  RED  {f}")
        print("qa_p6_bgm_cue: RED -- BGM CUE mismapped (run build_cdda.py with the 6 per-zone tracks).")
        return 1
    print("qa_p6_bgm_cue: GREEN -- game.cue wires the per-zone BGM tracks 2-7.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
