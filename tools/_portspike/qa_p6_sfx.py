#!/usr/bin/env python3
# =============================================================================
# qa_p6_sfx.py -- P6.6a gate (Task #209): ENGINE AUDIO LOAD + CHANNEL LOGIC.
# The UNMODIFIED engine loads the REAL Global/ScoreAdd.wav out of the ORIGINAL
# Data.rsdk (LoadSfx -> LoadSfxToSlot WAV parse + S16->F32 convert,
# Audio.cpp:305-424), the Saturn AudioDevice::Init backend runs the engine's
# own InitAudioChannels (Audio.cpp:164-182: channel reset, interpolation
# lookup, stream-slot reservation from DATASET_MUS), and the engine's own
# PlaySfx channel allocator (Audio.cpp:441-507) arms channel 0 with the
# canonical state every port mixes from. SCSP-audible playback is P6.6b; this
# step proves the platform-independent audio core end-to-end byte-exact.
#
# Witness contract (offline model = this file's WAV parse mirroring
# Audio.cpp:321-402 EXACTLY, incl. the Seek_Set(20+chunkSize) data scan and
# the S16->F32 formula -- bit-exact: /0x8000 is a pure exponent shift and
# *0.75f keeps <=17 mantissa bits):
#   S1 p6_w_sfx_inited  == 1     AudioDeviceBase::initializedAudioChannels
#   S2 p6_w_sfx_musbuf  == 1     stream-slot buffer alloc'd (MUS pool, 8 KB)
#   S3 p6_w_sfx_id      == 0     GetSfx hash-lookup roundtrip (first slot)
#   S4 p6_w_sfx_len     == 1469  samples (data 2938 B / 2, Audio.cpp:374-376)
#   S5 p6_w_sfx_hash    == djb2-xor over the F32 buffer BYTES (big-endian,
#                          length*4 = 5876 B) -- byte-exact engine convert
#   S6 p6_w_sfx_channel == 0     PlaySfx slot pick (all channels idle)
#   S7 p6_w_sfx_chstate == (CHANNEL_SFX<<24)|soundID == 0x01000000
#   S8 p6_w_sfx_chspeed == 0x10000 (TO_FIXED(1), Audio.cpp:495)
#   S9 p6_w_sfx_chloop  == 0xFFFFFFFF (loopPoint 0 -> loop = -1, :497-500)
#
# Usage: python tools/_portspike/qa_p6_sfx.py [savestate.mcs] [map]
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
WAV = os.path.join(ROOT, "extracted", "Data", "SoundFX", "Global", "ScoreAdd.wav")

SYMS = ["_p6_w_sfx_inited", "_p6_w_sfx_musbuf", "_p6_w_sfx_id", "_p6_w_sfx_len",
        "_p6_w_sfx_hash", "_p6_w_sfx_channel", "_p6_w_sfx_chstate",
        "_p6_w_sfx_chspeed", "_p6_w_sfx_chloop"]

CHANNEL_SFX = 1  # Audio.hpp:39


def model():
    import numpy as np
    d = open(WAV, "rb").read()
    assert d[:4] == b"RIFF" and d[8:12] == b"WAVE"
    # Mirror Audio.cpp:330 chunkSize then :341 Seek_Set(34) sampleBits then
    # :346 Seek_Set(20+chunkSize) and the 'data' scan (:351-372).
    chunk_size = struct.unpack("<I", d[16:20])[0]
    sample_bits = struct.unpack("<H", d[34:36])[0]
    pos = 20 + chunk_size
    loop = 0
    while True:
        sig = d[pos:pos + 4]
        pos += 4
        if sig == b"data":
            break
        loop += 4
        assert loop < 0x40, "data chunk not found the way the engine finds it"
    length = struct.unpack("<I", d[pos:pos + 4])[0]
    pos += 4
    if sample_bits == 16:
        length //= 2
        raw = np.frombuffer(d[pos:pos + length * 2], dtype="<i2").astype(np.int32)
        # Audio.cpp:392-400: manual sign fix on the uint16 read, then
        # (s / 32768.0f) * 0.75f -- all exact in float32.
        f = (raw.astype(np.float32) / np.float32(0x8000)) * np.float32(0.75)
    else:
        raw = np.frombuffer(d[pos:pos + length], dtype=np.uint8).astype(np.int32)
        f = (raw - 0x80).astype(np.float32) / np.float32(0x80)
    be = f.astype(">f4").tobytes()  # SH-2 stores the floats big-endian
    h = 5381
    for b in be:
        h = (((h << 5) + h) ^ b) & 0xFFFFFFFF
    return {"length": int(length), "hash": h, "bits": int(sample_bits),
            "bytes": len(be)}


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("P6.6a SFX GATE: engine LoadSfx + PlaySfx channel core (Data.rsdk)")
    print("=" * 72)
    m = model()
    print("  model: ScoreAdd.wav %d-bit, %d samples, f32 %d B, hash 0x%08X"
          % (m["bits"], m["length"], m["bytes"], m["hash"]))
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
        print("        (Expected while the P6.6a body is unwritten.)")
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
                            signed=(s in ("_p6_w_sfx_id", "_p6_w_sfx_channel",
                                          "_p6_w_sfx_len")))
         for s in SYMS}
    print("  peeked: " + "  ".join("%s=%s" % (s[9:], _scene._hx(v[s])) for s in SYMS))

    checks = [
        ("S1 InitAudioChannels ran (engine init path)",
         v["_p6_w_sfx_inited"] == 1, "got %s" % v["_p6_w_sfx_inited"]),
        ("S2 stream-slot buffer alloc'd from DATASET_MUS",
         v["_p6_w_sfx_musbuf"] == 1, "got %s" % v["_p6_w_sfx_musbuf"]),
        ("S3 GetSfx hash roundtrip -> slot 0",
         v["_p6_w_sfx_id"] == 0, "got %s" % v["_p6_w_sfx_id"]),
        ("S4 sample length == %d" % m["length"],
         v["_p6_w_sfx_len"] == m["length"], "got %s" % v["_p6_w_sfx_len"]),
        ("S5 F32 buffer (%d B) byte-exact via hash" % m["bytes"],
         (v["_p6_w_sfx_hash"] or 0) & 0xFFFFFFFF == m["hash"],
         "got %s expect 0x%08X" % (_scene._hx(v["_p6_w_sfx_hash"]), m["hash"])),
        ("S6 PlaySfx channel pick == 0",
         v["_p6_w_sfx_channel"] == 0, "got %s" % v["_p6_w_sfx_channel"]),
        ("S7 channel state == CHANNEL_SFX, soundID == 0",
         (v["_p6_w_sfx_chstate"] or 0) & 0xFFFFFFFF == (CHANNEL_SFX << 24),
         "got %s expect 0x%08X" % (_scene._hx(v["_p6_w_sfx_chstate"]),
                                   CHANNEL_SFX << 24)),
        ("S8 channel speed == TO_FIXED(1)",
         (v["_p6_w_sfx_chspeed"] or 0) & 0xFFFFFFFF == 0x10000,
         "got %s" % _scene._hx(v["_p6_w_sfx_chspeed"])),
        ("S9 loop == loopPoint-1 == 0xFFFFFFFF",
         (v["_p6_w_sfx_chloop"] or 0) & 0xFFFFFFFF == 0xFFFFFFFF,
         "got %s" % _scene._hx(v["_p6_w_sfx_chloop"])),
    ]

    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the engine loaded the ORIGINAL ScoreAdd.wav from")
        print("        Data.rsdk byte-exact (WAV parse + S16->F32 convert) and")
        print("        its own PlaySfx armed the canonical channel state. The")
        print("        platform-independent audio core is proven; P6.6b wires")
        print("        the SCSP-audible half.")
        return 0
    print("RESULT: RED -- engine audio core not proven (see witnesses).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
