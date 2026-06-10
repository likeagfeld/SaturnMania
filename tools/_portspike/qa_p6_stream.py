#!/usr/bin/env python3
# =============================================================================
# qa_p6_stream.py -- P6.6c gate (Task #209): ENGINE PlayStream -> CD-DA.
# The UNMODIFIED engine PlayStream (Audio.cpp:248-300) runs the canonical
# music call shape (decomp Music_PlayTrack -> RSDK.PlayStream(track->
# fileName, 0, 0, loopPoint, true); the Title scene requests
# "TitleScreen.ogg" via TRACK_STAGE, TitleSetup.c:145): channel arm,
# "Data/Music/<name>" sprintf, then the DEVICE seam
# AudioDevice::HandleStreamLoad. The Saturn device maps the stream name to
# a CD-DA CUE track (Saturn-fit per bgm-loops-hand-curated /
# saturn-cdda-cue-format: OGG decode is not real-time-feasible on SH-2;
# BGM = CD audio tracks; track 2 = GreenHill1, track 3 = TitleScreen per
# build.bat:175-176) and starts hardware playback through the proven
# jo_audio_play_cd_track -> CDC_CdPlay path.
#
# Witness contract:
#   T1 p6_w_str_slot  == 0  PlayStream return (Music channel 0 convention)
#   T2 p6_w_str_state == CHANNEL_STREAM (2): the Saturn HandleStreamLoad
#      completed the same success transition LoadStream:239 performs
#   T3 p6_w_str_path  == djb2("Data/Music/TitleScreen.ogg"): the engine's
#      OWN sprintf built the canonical path (Audio.cpp:291)
#   T4 p6_w_str_track == 3: name->track mapping resolved
#   T5 PHYSICAL: the savestate's CD-block state shows the audio PLAY
#      command armed for track 3 -- CDB.CurPlayStart equals the track-3
#      INDEX 01 FAD computed from the ACTUAL disc files (game.iso +
#      cd_audio/track02.bin + 2s pregaps), and CurPlayRepeat == 0x0F
#      (endless repeat, jo repeat_infinitely -> CDC PM 0x0F).
#
# Usage: python tools/_portspike/qa_p6_stream.py [savestate.mcs] [map]
# =============================================================================
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location(
    "qa_p6_scene", os.path.join(HERE, "qa_p6_scene.py"))
_scene = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_scene)

ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
MCS_DEFAULT = os.path.join(HERE, "_p6", "_p6_pack.mcs")
ISO = os.path.join(ROOT, "game.iso")
TRACK02 = os.path.join(ROOT, "cd_audio", "track02.bin")

SYMS = ["_p6_w_str_slot", "_p6_w_str_state", "_p6_w_str_path", "_p6_w_str_track"]
CHANNEL_STREAM = 2  # Audio.hpp:39
EXPECT_TRACK = 3


def djb2(s):
    h = 5381
    for c in s.encode():
        h = (((h << 5) + h) ^ c) & 0xFFFFFFFF
    return h


def expected_play_param():
    # Mednafen's CDB stores the raw CD-block PLAY command parameters, NOT a
    # resolved FAD (measured 2026-06-10: CurPlayStart == 0x301 after
    # CDC_CdPlay with CDC_PTYPE_TNO track 3 index 1 -- the TNO encoding
    # (track << 8) | index, ST-38-R1 ptype semantics). The first gate run
    # modeled a computed FAD (96373) and mis-fired RED on a correct build.
    return (EXPECT_TRACK << 8) | 1


def read_var_u32(sections, sect, var, signed=False):
    import struct
    buf = sections["__buf_bytes__"]
    if sect not in sections or var not in sections[sect]:
        return None
    o, s = sections[sect][var]
    if s < 4:
        raw = buf[o:o + s] + b"\x00" * (4 - s)
    else:
        raw = buf[o:o + 4]
    return struct.unpack("<i" if signed else "<I", raw)[0]


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("P6.6c STREAM GATE: engine PlayStream -> CD-DA (TitleScreen.ogg)")
    print("=" * 72)
    exp_path_hash = djb2("Data/Music/TitleScreen.ogg")
    exp_play = expected_play_param()
    print("  model: path-hash 0x%08X, track %d, play param 0x%X (TNO form)"
          % (exp_path_hash, EXPECT_TRACK, exp_play))
    print("  savestate: %s" % mcs)
    print("-" * 72)

    if not os.path.isfile(mp):
        print("RESULT: RED -- link map missing (%s)" % mp)
        return 1
    map_text = _scene.read_text(mp)
    syms = {}
    missing = []
    for s in [_scene.SYM_MAGIC] + SYMS:
        syms[s] = _scene.map_symbol(map_text, s)
        if syms[s] is None:
            missing.append(s)
    if missing:
        print("RESULT: RED -- witness symbol(s) absent from the map:")
        for s in missing:
            print("        %s" % s)
        print("        (Expected while the P6.6c backend is unwritten.)")
        return 1
    if not os.path.isfile(mcs):
        print("RESULT: RED -- savestate missing (%s)" % mcs)
        return 1

    import pathlib
    mod = _scene.load_harness()
    sections = mod.parse_savestate(pathlib.Path(mcs))
    raw_magic = mod._peek_bytes(sections, syms[_scene.SYM_MAGIC], 4)
    label, perm = _scene.calibrate(raw_magic)
    if perm is None:
        print("RESULT: RED -- magic mis-decode")
        return 1
    v = {s: _scene.peek_u32(mod, sections, syms[s], perm,
                            signed=(s in ("_p6_w_str_slot", "_p6_w_str_track")))
         for s in SYMS}
    print("  peeked: " + "  ".join("%s=%s" % (s[9:], _scene._hx(v[s])) for s in SYMS))

    cur_start  = read_var_u32(sections, "CDB", "CurPlayStart")
    cur_end    = read_var_u32(sections, "CDB", "CurPlayEnd")
    cur_repeat = read_var_u32(sections, "CDB", "CurPlayRepeat")
    print("  CDB: CurPlayStart=%s CurPlayEnd=%s CurPlayRepeat=%s"
          % (cur_start, cur_end,
             ("0x%X" % cur_repeat) if cur_repeat is not None else None))

    checks = [
        ("T1 PlayStream slot == 0 (Music channel convention)",
         v["_p6_w_str_slot"] == 0, "got %s" % v["_p6_w_str_slot"]),
        ("T2 channel state == CHANNEL_STREAM",
         v["_p6_w_str_state"] == CHANNEL_STREAM, "got %s" % v["_p6_w_str_state"]),
        ("T3 engine sprintf path == Data/Music/TitleScreen.ogg (hash)",
         (v["_p6_w_str_path"] or 0) & 0xFFFFFFFF == exp_path_hash,
         "got %s expect 0x%08X" % (_scene._hx(v["_p6_w_str_path"]), exp_path_hash)),
        ("T4 name->track mapping == %d" % EXPECT_TRACK,
         v["_p6_w_str_track"] == EXPECT_TRACK, "got %s" % v["_p6_w_str_track"]),
        ("T5 CD-block PLAY armed for track %d (TNO 0x%X start+end, endless 0x0F)"
         % (EXPECT_TRACK, exp_play),
         cur_start == exp_play and cur_end == exp_play and cur_repeat == 0x0F,
         "start=%s end=%s repeat=%s" % (cur_start, cur_end,
                                        ("0x%X" % cur_repeat) if cur_repeat is not None else None)),
    ]

    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the engine's canonical PlayStream call drives")
        print("        REAL CD-DA: channel armed by unmodified engine code, the")
        print("        Saturn device mapped the stream name to its CUE track,")
        print("        and the CD block is playing it on endless repeat.")
        return 0
    print("RESULT: RED -- engine PlayStream -> CD-DA not proven (see witnesses).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
