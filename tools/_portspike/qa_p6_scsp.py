#!/usr/bin/env python3
# =============================================================================
# qa_p6_scsp.py -- P6.6b gate (Task #209): ENGINE SFX AUDIBLE ON SCSP.
# The Saturn audio backend converts the engine's float channel buffer
# (armed by the engine's own PlaySfx, proven byte-exact in P6.6a) to S16
# PCM and plays it through the PROVEN jo/SGL path (jo_audio_play_sound ->
# slPCMOn; the SGL sound driver streams the samples from work RAM into
# sound-RAM ring buffers and keys SCSP slots). The proof re-triggers the
# engine PlaySfx + backend play every 256 ticks so the bleep is audible
# and the SCSP ring holds recent sample data at any capture moment.
#
# Witness contract:
#   A1 p6_w_snd_plays == 1 + floor(ticks/256): boot play + every tick
#      re-trigger went through BOTH the engine PlaySfx AND the backend.
#   A2 p6_w_snd_s16hash == model: the F32->S16 device conversion is
#      byte-exact. Bit-reproducible: engine F32 = (s*0.75)/0x8000 exactly,
#      so f*32768.0f = 0.75*s exactly (<=17 mantissa bits) and the C
#      (int16) cast truncates toward zero -- np.trunc(0.75*s) matches.
#   A3 PHYSICAL EVIDENCE: the savestate's 512 KB SCSP sound RAM contains
#      at least one 64-byte window of the model S16 stream (windows are
#      entropy-filtered >=24 distinct bytes; searched raw AND 16-bit
#      pair-swapped to self-calibrate Mednafen's serialization order).
#      A 64-byte exact match cannot occur by chance -- the engine's
#      samples physically reached the sound hardware's memory.
#
# Usage: python tools/_portspike/qa_p6_scsp.py [savestate.mcs] [map]
# =============================================================================
import importlib.util
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location(
    "qa_p6_scene", os.path.join(HERE, "qa_p6_scene.py"))
_scene = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_scene)

ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
MCS_DEFAULT = os.path.join(HERE, "_p6", "_p6_pack.mcs")
# MenuBleep, NOT ScoreAdd: measured 2026-06-10, ScoreAdd's S16 stream holds
# only THREE distinct byte values total (square-wave bleep) -- no 64-byte
# window is conclusive against sound RAM. MenuBleep windows carry 31-56
# distinct bytes. The proof loads it as a SECOND engine SFX (slot 1) so the
# committed qa_p6_sfx.py ScoreAdd contract (slot 0) is untouched.
WAV = os.path.join(ROOT, "extracted", "Data", "SoundFX", "Global", "MenuBleep.wav")

SYMS = ["_p6_w_snd_plays", "_p6_w_snd_s16hash", "_p6_w_spr_ticks"]
RETRIGGER_TICKS = 256
# Direct-SCSP backend (Coup reference): the engine S16 buffer is uploaded
# ONCE to Sound RAM +0x6C000 and slots 28-31 key it -- A3 is an
# exact-region compare at the fixed address, capture-timing independent.
PCM_SRAM_ADDR = 0x05A00000 + 0x6C000


def model():
    import numpy as np
    d = open(WAV, "rb").read()
    chunk_size = struct.unpack("<I", d[16:20])[0]
    pos = 20 + chunk_size
    while d[pos:pos + 4] != b"data":
        pos += 4
    length = struct.unpack("<I", d[pos + 4:pos + 8])[0] // 2
    raw = np.frombuffer(d[pos + 8:pos + 8 + length * 2], dtype="<i2").astype(np.int32)
    # Engine F32 (Audio.cpp:400) -> backend S16: trunc(0.75 * s) exactly.
    s16 = np.trunc(raw * 0.75).astype(np.int16)
    be = s16.astype(">i2").tobytes()
    h = 5381
    for b in be:
        h = (((h << 5) + h) ^ b) & 0xFFFFFFFF
    return {"length": int(length), "hash": h, "be": be}


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("P6.6b SCSP GATE: engine SFX audible on the sound hardware")
    print("=" * 72)
    m = model()
    print("  model: %d samples -> S16 %d B hash 0x%08X @ Sound RAM 0x%08X"
          % (m["length"], len(m["be"]), m["hash"], PCM_SRAM_ADDR))
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
        print("        (Expected while the P6.6b backend is unwritten.)")
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
                            signed=(s in ("_p6_w_snd_plays", "_p6_w_spr_ticks")))
         for s in SYMS}
    print("  peeked: " + "  ".join("%s=%s" % (s[6:], _scene._hx(v[s])) for s in SYMS))

    t = v["_p6_w_spr_ticks"] or 0
    exp_plays = 1 + t // RETRIGGER_TICKS

    # Sound RAM at the fixed upload address (SCSP/RAM region row in
    # mcs_extract). Compare raw and 16-bit pair-swapped serializations.
    sram = mod._peek_bytes(sections, PCM_SRAM_ADDR, len(m["be"]))
    swapped = bytearray(len(sram))
    swapped[0::2] = sram[1::2]
    swapped[1::2] = sram[0::2]
    if bytes(sram) == m["be"]:
        found = "raw"
    elif bytes(swapped) == m["be"]:
        found = "pair-swapped"
    else:
        found = None
        nmatch = sum(1 for i in range(len(m["be"])) if sram[i] == m["be"][i])
        detail_a3 = "exact-compare FAILED (%d/%d bytes match raw form)" % (nmatch, len(m["be"]))

    checks = [
        ("A1 boot play + tick re-triggers (expect %d at ticks=%d)" % (exp_plays, t),
         v["_p6_w_snd_plays"] == exp_plays and (v["_p6_w_snd_plays"] or 0) >= 2,
         "plays=%s" % v["_p6_w_snd_plays"]),
        ("A2 F32->S16 device conversion byte-exact via hash",
         (v["_p6_w_snd_s16hash"] or 0) & 0xFFFFFFFF == m["hash"],
         "got %s expect 0x%08X" % (_scene._hx(v["_p6_w_snd_s16hash"]), m["hash"])),
        ("A3 FULL S16 buffer byte-exact in SCSP sound RAM @ 0x%08X" % PCM_SRAM_ADDR,
         found is not None,
         ("%s serialization" % found) if found else detail_a3),
    ]

    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the engine-loaded, engine-played SFX is reaching")
        print("        the SCSP: device conversion byte-exact and the samples are")
        print("        physically present in sound RAM. Engine audio is AUDIBLE.")
        return 0
    print("RESULT: RED -- SCSP-audible path not proven (see witnesses).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
