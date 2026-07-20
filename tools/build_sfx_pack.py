#!/usr/bin/env python3
"""build_sfx_pack.py -- offline Green Hill gameplay-SFX pack for the SCSP.

MEASURED CONTEXT (2026-07-20, memory/dead-sfx-rootcause-f32-pool-exhaustion):
gameplay SFX are silent because Mania decodes them to F32 in the ~32 KB Saturn
DATASET_SFX WRAM pool (pool exhausts after 1-2 -> only 3/256 load). The fix
(user-chosen path, "8-bit + downsample repack") is to hold the SFX as native
SCSP samples in sound RAM instead. But sound RAM is 512 KB, SGL-shared, with only
~76 KB reliably free above p6_snd.c's PCM window. The full core set is 1.58 MB as
S16 @44100 -> physically impossible. So: downsample 44100->22050 (SCSP replays at
OCT=-1) AND convert S16->S8 (SCSP PCM8B=1) = 1/4 size, then greedily pack a
PRIORITY-ordered curated set until the budget is hit.

All Green Hill SFX measured mono/44100/16-bit, so the conversion is uniform:
  * downsample: average sample pairs (2:1 decimation, cheap anti-alias)
  * S16 -> S8 : signed 8-bit = sample >> 8  (SCSP PCM8B interprets signed 8-bit)

Output cd/GHZSFX.PCM (big-endian, SH-2/SCSP native):
  magic 'P6SF' (4)  |  version u16 = 1  |  count u16  |  sampleRate u32 = 22050
  reserved u32 (0)
  then `count` entries, each 24 bytes:
     md5(name) digest         16 bytes   (GEN_HASH_MD5 of e.g. "Global/Ring.wav")
     byteOffsetInData         u32        (from the start of the DATA blob)
     sampleCount              u32        (S8 samples; SCSP LEA = count-1)
  then the DATA blob: each SFX's S8 samples, 2-byte aligned (SCSP SA byte addr).

The engine side (next unit) uploads DATA to sound RAM once at Green Hill StageLoad,
builds a hash->(sramOffset,count) table, makes LoadSfxToSlot resolve these names
(scope set, no F32 alloc), and a per-frame pump keys-on an SCSP voice (PCM8B=1,
OCT=-1) when a channel arms one of these soundIDs.
"""
from __future__ import annotations

import hashlib
import os
import struct
import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parent.parent
SOUNDFX = _ROOT / "extracted" / "Data" / "SoundFX"
OUT = _ROOT / "cd" / "GHZSFX.PCM"

BUDGET = 76 * 1024          # free sound-RAM window above the MenuBleep slot
TARGET_RATE = 22050

# PRIORITY-ordered Green Hill Act 1 gameplay SFX (most frequent / iconic first).
# Names are the exact GetSfx() strings the decomp objects use (hash key).
PRIORITY = [
    "Global/Ring.wav",       # ring collect -- THE signature sound
    "Global/Jump.wav",       # every jump
    "Global/Spring.wav",     # springs everywhere in GHZ
    "Global/Destroy.wav",    # badnik pop
    "Global/Hurt.wav",       # taking damage
    "Global/LoseRings.wav",  # ring scatter
    "Global/Land.wav",       # landing
    "Global/ScoreAdd.wav",   # score tally
    "Global/Roll.wav",       # spin/roll
    "Global/Release.wav",    # spindash release
    "Global/Grab.wav",       # grabbing/holding
    "Global/Skidding.wav",   # skid
    "Global/SignPost.wav",   # act-clear signpost
    "Global/Push.wav",       # pushing blocks
    "Global/Charge.wav",     # spindash charge
    "Global/DropDash.wav",   # drop dash
    "Global/Flying.wav",     # Tails flight
    "Global/Spike.wav",      # spikes
]


def read_wav_mono_s16(path: Path):
    d = path.read_bytes()
    assert d[:4] == b"RIFF" and d[8:12] == b"WAVE", f"not a WAV: {path}"
    fmt = d.find(b"fmt ")
    ch = struct.unpack_from("<H", d, fmt + 10)[0]
    rate = struct.unpack_from("<I", d, fmt + 12)[0]
    bits = struct.unpack_from("<H", d, fmt + 22)[0]
    di = d.find(b"data")
    size = struct.unpack_from("<I", d, di + 4)[0]
    body = d[di + 8: di + 8 + size]
    assert ch == 1 and bits == 16, f"{path}: expected mono/16-bit, got ch={ch} bits={bits}"
    samples = list(struct.unpack_from("<%dh" % (len(body) // 2), body, 0))
    return rate, samples


def to_s8_22050(samples, rate):
    # 44100 -> 22050: average adjacent pairs (2:1 decimate w/ mild anti-alias).
    if rate == 44100:
        out16 = [(samples[i] + samples[i + 1]) // 2 for i in range(0, len(samples) - 1, 2)]
    elif rate == 22050:
        out16 = samples
    else:
        # generic nearest-neighbour resample to 22050
        ratio = rate / TARGET_RATE
        n = int(len(samples) / ratio)
        out16 = [samples[int(i * ratio)] for i in range(n)]
    # S16 -> signed S8 (SCSP PCM8B). >>8 keeps the high byte = signed 8-bit.
    return bytes((s >> 8) & 0xFF for s in out16)


def main():
    if not SOUNDFX.exists():
        sys.stderr.write(f"build_sfx_pack: {SOUNDFX} not found (extract Data.rsdk first)\n")
        return 2
    entries = []          # (name, md5digest, s8bytes)
    total = 0
    skipped = []
    hdr_per_entry = 24
    for name in PRIORITY:
        p = SOUNDFX / name
        if not p.exists():
            skipped.append((name, "missing"))
            continue
        rate, s16 = read_wav_mono_s16(p)
        s8 = to_s8_22050(s16, rate)
        if len(s8) & 1:
            s8 += b"\x00"                       # 2-byte align for SCSP SA
        # greedy budget: header (fixed by final count) + all data must fit
        prospective = sum(len(e[2]) for e in entries) + len(s8)
        prospective_hdr = 16 + hdr_per_entry * (len(entries) + 1)
        if prospective + prospective_hdr > BUDGET:
            skipped.append((name, f"over budget ({len(s8)} B)"))
            continue
        md5 = hashlib.md5(name.encode()).digest()
        entries.append((name, md5, s8))
        total += len(s8)

    # assemble
    data = bytearray()
    offsets = []
    for _, _, s8 in entries:
        offsets.append(len(data))
        data += s8
    out = bytearray()
    out += b"P6SF"
    out += struct.pack(">HH", 1, len(entries))
    out += struct.pack(">I", TARGET_RATE)
    out += struct.pack(">I", 0)
    for (name, md5, s8), off in zip(entries, offsets):
        out += md5
        out += struct.pack(">II", off, len(s8))
    out += data

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(out)

    print("=" * 68)
    print("build_sfx_pack  (Green Hill gameplay SFX -> S8 @22050 SCSP pack)")
    print("=" * 68)
    print(f"  budget (free sound-RAM window) : {BUDGET} B ({BUDGET/1024:.0f} KB)")
    print(f"  packed SFX                     : {len(entries)}")
    for (name, _, s8), off in zip(entries, offsets):
        print(f"    {name:22s} {len(s8):6d} B  @+0x{off:05x}")
    if skipped:
        print("  skipped (budget/missing):")
        for name, why in skipped:
            print(f"    {name:22s} {why}")
    print(f"  total file size                : {len(out)} B ({len(out)/1024:.1f} KB)")
    print(f"  data blob                      : {total} B ({total/1024:.1f} KB)")
    print(f"  -> {OUT}")
    # 7 = the measured-feasible count for the 76 KB free sound-RAM window with the
    # two biggest core SFX (Roll/SignPost/Charge, all >30 KB S8) excluded. This is
    # the physical budget, not an arbitrary target -- see the header + the memory note.
    if len(entries) < 7:
        print("build_sfx_pack: WARNING only "
              f"{len(entries)} SFX fit -- expected >= 7 in the 76 KB window")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
