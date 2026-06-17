#!/usr/bin/env python3
# Task #182 -- GHZ stage-BGM gate (RED-first). The engine-shipping GHZ build must
# play the GHZ stage music (GreenHill1.ogg -> CUE audio track 2 via
# AudioDevice::HandleStreamLoad), NOT the title track / silence.
#
# MEASURED root cause (2026-06-17): the shipping continuous-GHZ boot
# (p6_ghz_reload -> p6_scene_load_and_arm) NEVER calls PlayStream -- the witness
# p6_w_str_track stays at its -1 init (no BGM requested). AND build_shipping.sh
# masters a single-track (data-only) CUE -- no CD-DA tracks exist to play. The
# decomp plays the stage BGM via Music_PlayTrack(TRACK_STAGE) ->
# RSDK.PlayStream(stageMusicName,...) (Music.c:259-276) the moment a stage loads.
#
# TWO measurable conditions (both must hold for GHZ to play GHZ music):
#   M1  p6_w_str_track == 2  in a GHZ-live capture (the engine REQUESTED track 2,
#       i.e. PlayStream("GreenHill1.ogg") -> HandleStreamLoad -> track 2). The
#       request is the only savestate-visible half; the actual CD-DA audio is
#       real-time (not serialized) and is confirmed by ear.
#   M2  game.cue is MULTI-TRACK with an AUDIO track (TRACK 02 AUDIO) -- the CD-DA
#       the request plays. A single data-only TRACK 01 cue = silence regardless.
#
#   python tools/_portspike/qa_p6_music.py [savestate.mcs] [map]
# RED on the current build (str_track=-1, single-track cue); GREEN once the GHZ
# load triggers the BGM and the shipping build masters the CD-DA track.

import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
CUE  = os.path.join(ROOT, "game.cue")
GHZ_STAGE_TRACK = 2  # GreenHill1.ogg -> CUE audio track 2 (HandleStreamLoad)


def cue_has_audio_track(path):
    try:
        txt = Q.read_text(path)
    except Exception:
        return False, "no game.cue"
    naud = sum(1 for ln in txt.splitlines() if "AUDIO" in ln.upper() and "TRACK" in ln.upper())
    return naud >= 1, ("%d audio track(s)" % naud)


def main(argv):
    mcs = argv[0] if argv else os.path.join(HERE, "p6_music.mcs")
    mp  = argv[1] if len(argv) > 1 else Q.MAP_DEFAULT
    mod = Q.load_harness()
    map_text = Q.read_text(mp)
    sec = mod.parse_savestate(Q._as_path(mcs))
    ma = Q.map_symbol(map_text, "_p6_w_magic"); raw = mod._peek_bytes(sec, ma, 4) if ma else None
    _, perm = Q.calibrate(raw)
    if perm is None:
        print("RED: magic uncalibrated"); return 1

    def peek(n, signed=True):
        a = Q.map_symbol(map_text, n)
        return Q.peek_u32(mod, sec, a, perm, signed=signed) if a else None

    cont = peek("_p6_w_cont_frames")
    strk = peek("_p6_w_str_track")
    audio_ok, audio_msg = cue_has_audio_track(CUE)

    print("=== qa_p6_music (GHZ stage BGM, #182) ===")
    print("  mcs              = %s" % os.path.basename(mcs))
    print("  cont_frames      = %s (GHZ live)" % Q._dv(cont))
    print("  p6_w_str_track   = %s   (expect %d = GreenHill1->track2)" % (Q._dv(strk), GHZ_STAGE_TRACK))
    print("  game.cue audio   = %s (%s)" % ("yes" if audio_ok else "NO", audio_msg))

    m1 = (strk == GHZ_STAGE_TRACK)
    m2 = audio_ok
    print("  [%s] M1 GHZ requested its stage BGM (str_track==%d)" % ("GREEN" if m1 else " RED ", GHZ_STAGE_TRACK))
    print("  [%s] M2 CUE has the CD-DA audio track to play" % ("GREEN" if m2 else " RED "))
    if m1 and m2:
        print("RESULT: GREEN -- GHZ requests GreenHill1 + the CD-DA track exists (confirm audible by ear)")
        return 0
    print("RESULT: RED -- GHZ stage music not wired (see M1/M2)")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
