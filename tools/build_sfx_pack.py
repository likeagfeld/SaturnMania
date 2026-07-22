#!/usr/bin/env python3
"""build_sfx_pack.py -- offline Green Hill gameplay-SFX pack for the SCSP.

MEASURED CONTEXT (2026-07-20/21, memory/dead-sfx-rootcause-f32-pool-exhaustion):
gameplay SFX are silent because Mania decodes them to F32 in the ~32 KB Saturn
DATASET_SFX WRAM pool (pool exhausts after 1-2 -> only 3/256 load). The fix
(user-chosen "8-bit + downsample repack") holds the SFX as native SCSP samples in
sound RAM. sound RAM is 512 KB SGL-shared.

BUDGET (MEASURED 2026-07-21, GHZ + AIZ savestate SCSP dumps): the SGL sound driver
+ its scene PCM blocks occupy 0x00000-0x40000 (a solid 64 KB block at 0x30000-
0x40000 persists across scenes). The window 0x40000-0x6C000 = 176 KB reads ALL-ZERO
at GHZ gameplay AND at AIZ -> reclaimable. This is 2.3x the prior conservative
0x6D000 (76 KB) window. p6_sfx.c relocates P6_SFX_PCM_OFF to 0x40000 to match.

PER-ENTRY RATE (v3): 176 KB still can't hold the full iconic set at S8@44100->22050
(the 5 longest -- Roll/Charge/SignPost/LoseRings/Release -- total ~190 KB alone). So
the LONG sounds (22050 size over LONG_THRESHOLD) drop to 11025 Hz (SCSP OCT=-2), the
short frequent ones stay 22050 (OCT=-1). Each entry now carries its own OCT nibble;
the pump programs PITCH per voice. This fits the full GHZ Act1 SFX set (incl.
spindash Charge/Release) in 176 KB.

Conversion (all Green Hill SFX measured mono/44100/16-bit):
  * 44100->22050: average sample pairs (2:1 decimate)   -> OCT -1 (0x7800)
  * 44100->11025: average sample quads (4:1 decimate)   -> OCT -2 (0x7000)
  * S16 -> S8    : signed 8-bit = sample >> 8            (SCSP PCM8B=1)

Output cd/GHZSFX.PCM (big-endian, SH-2/SCSP native):
  magic 'P6SF' (4) | version u16 = 3 | count u16 | baseRate u32 (0, per-entry now)
  reserved u32 (0)
  then `count` entries, each 16 bytes:
     djb2(name)         u32   (of e.g. "Global/Ring.wav"; engine djb2 mirrors Audio.cpp:430)
     byteOffsetInData   u32   (from the start of the DATA blob)
     sampleCount        u32   (S8 samples; SCSP LEA = count-1)
     octNibble          u32   (low byte: 0xF=22050/OCT-1, 0xE=11025/OCT-2)
  then the DATA blob: each SFX's S8 samples, 2-byte aligned (SCSP SA byte addr).
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path


def djb2(s: str) -> int:
    """Mirror the engine-side djb2 (Audio.cpp:430): h=5381; h=((h<<5)+h)^byte."""
    h = 5381
    for b in s.encode():
        h = (((h << 5) + h) ^ b) & 0xFFFFFFFF
    return h

_ROOT = Path(__file__).resolve().parent.parent
SOUNDFX = _ROOT / "extracted" / "Data" / "SoundFX"
OUT = _ROOT / "cd" / "GHZSFX.PCM"

# MEASURED-free contiguous window 0x40000-0x7E000 = 248 KB (MenuBleep relocated to
# the top 0x7E000 in p6_snd.c; SGL uses nothing above 0x40000). Hold a margin.
BUDGET = 244 * 1024
# THREE-TIER rate by the sound's S8@22050 size, so EVERYTHING fits with no drops
# (user directive 2026-07-21 "compress until everything fits, no dropping assets").
# Short/frequent stay crisp @22050; medium -> 11025; the longest -> 5512.
TIER_22050 = 15 * 1024   # <= this @22050 -> keep 22050 (OCT -1)
TIER_11025 = 28 * 1024   # <= this @22050 -> 11025 (OCT -2); above -> 5512 (OCT -3)

OCT_22050 = 0xF   # SCSP OCT=-1 -> 22050
OCT_11025 = 0xE   # SCSP OCT=-2 -> 11025
OCT_5512  = 0xD   # SCSP OCT=-3 -> 5512

# DIAGNOSTIC (2026-07-22): the direct-slot SFX voice is MEASURED playing correctly
# (savestate: keyed, full envelope, DISDL=7, valid data) yet SILENT, while the ONLY
# ear-confirmed direct-slot audio (P6.6b, 175d278) used 16-bit (PCM8B=0). The 8-bit
# path was never ear-confirmed. So emit the most-frequent SFX as 16-bit (proven
# format) and the rest as 8-bit: if the 16-bit ones are audible @GHZ and the 8-bit
# aren't, PCM8B/8-bit is the bug. 16-bit doubles size, so only the frequent set.
FMT_S8  = 0
FMT_S16 = 1
# All 8-bit now (the real bug was byte-vs-word UPLOAD width, not sample format --
# SCSP_Manual SS-3.1 line 1229; fixed in p6_sfx.c). All-S8 fits all 30 SFX, no drops.
# (Per-entry fmt kept for future selective 16-bit quality if budget allows.)
S16_NAMES = set()

# Full Green Hill Act 1 gameplay SFX set (exact GetSfx() strings = hash keys).
# Everything GHZ objects trigger: player abilities, rings, monitors+shields,
# badnik pops, spring, spikes, signpost, tally. Priority-ordered (most iconic
# first) only affects packing order; ALL must fit (three-tier compression).
PRIORITY = [
    "Global/Ring.wav",       # ring collect -- THE signature sound
    "Global/Jump.wav",       # every jump
    "Global/Spring.wav",     # springs
    "Global/Destroy.wav",    # badnik/monitor pop
    "Global/Hurt.wav",       # taking damage
    "Global/LoseRings.wav",  # ring scatter
    "Global/Land.wav",       # landing
    "Global/ScoreAdd.wav",   # score tally tick
    "Global/Charge.wav",     # spindash charge
    "Global/Release.wav",    # spindash release
    "Global/Roll.wav",       # spin/roll
    "Global/DropDash.wav",   # drop dash
    "Global/Skidding.wav",   # skid
    "Global/SignPost.wav",   # act-clear signpost
    "Global/Grab.wav",       # grabbing/holding
    "Global/Push.wav",       # pushing blocks
    "Global/Flying.wav",     # Tails flight
    "Global/SpikesMove.wav", # moving spikes
    "Global/BlueShield.wav", # blue shield monitor
    "Global/BubbleShield.wav",
    "Global/FireShield.wav",
    "Global/LightningShield.wav",
    "Global/InstaShield.wav",
    "Global/Slide.wav",      # slide
    "Global/Tired.wav",      # Tails flight tired
    "Global/OuttaHere.wav",  # get-out-of-here
    "Global/ScoreTotal.wav", # tally total
    "Stage/Explosion.wav",   # badnik explosion
    "Stage/Bumper.wav",      # bumper/spikes bounce
    "Stage/Fireball.wav",    # BuzzBomber projectile
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


def _to_22050_s16(samples, rate):
    if rate == 44100:
        return [(samples[i] + samples[i + 1]) // 2 for i in range(0, len(samples) - 1, 2)]
    if rate == 22050:
        return samples
    ratio = rate / 22050.0
    n = int(len(samples) / ratio)
    return [samples[int(i * ratio)] for i in range(n)]


def _decimate(s16, factor):
    if factor <= 1:
        return s16
    return [sum(s16[i:i + factor]) // factor
            for i in range(0, len(s16) - factor + 1, factor)]


def _trim_silence(s16, thresh=64):
    """Drop leading/trailing near-silence (|sample| < thresh) to save bytes."""
    lo, hi = 0, len(s16)
    while lo < hi and abs(s16[lo]) < thresh:
        lo += 1
    while hi > lo and abs(s16[hi - 1]) < thresh:
        hi -= 1
    return s16[lo:hi] if hi > lo else s16


def _s16_to_s8(s16):
    b = bytes((s >> 8) & 0xFF for s in s16)   # signed 8-bit high byte (SCSP PCM8B)
    if len(b) & 1:
        b += b"\x00"                           # 2-byte align for SCSP SA
    return b


def _s16_to_be_bytes(s16):
    return b"".join(struct.pack(">h", max(-32768, min(32767, s))) for s in s16)


def encode(samples, rate, want_s16):
    """Return (databytes, octNibble, fmt, sampleCount).
    want_s16: emit 16-bit @22050 (proven-audible format, PCM8B=0) -- diagnostic.
    else three-tier 8-bit by 22050 size (22050/11025/5512), silence-trimmed."""
    base = _trim_silence(_to_22050_s16(samples, rate))   # 22050-domain samples
    if want_s16:
        return _s16_to_be_bytes(base), OCT_22050, FMT_S16, len(base)
    size22 = len(base)                                    # S8 bytes @22050 == sample count
    if size22 <= TIER_22050:
        b = _s16_to_s8(base);              return b, OCT_22050, FMT_S8, len(b)
    if size22 <= TIER_11025:
        b = _s16_to_s8(_decimate(base, 2)); return b, OCT_11025, FMT_S8, len(b)
    b = _s16_to_s8(_decimate(base, 4));     return b, OCT_5512, FMT_S8, len(b)


def main():
    if not SOUNDFX.exists():
        sys.stderr.write(f"build_sfx_pack: {SOUNDFX} not found (extract Data.rsdk first)\n")
        return 2
    entries = []          # (name, djb2key, databytes, oct, fmt, sampleCount)
    skipped = []
    hdr_per_entry = 16
    for name in PRIORITY:
        p = SOUNDFX / name
        if not p.exists():
            skipped.append((name, "missing"))
            continue
        rate, s16 = read_wav_mono_s16(p)
        data_b, octv, fmt, scount = encode(s16, rate, name in S16_NAMES)
        prospective = sum(len(e[2]) for e in entries) + len(data_b)
        prospective_hdr = 16 + hdr_per_entry * (len(entries) + 1)
        if prospective + prospective_hdr > BUDGET:
            skipped.append((name, f"over budget ({len(data_b)} B)"))
            continue
        entries.append((name, djb2(name), data_b, octv, fmt, scount))

    data = bytearray()
    offsets = []
    for e in entries:
        offsets.append(len(data))
        data += e[2]
    out = bytearray()
    out += b"P6SF"
    out += struct.pack(">HH", 4, len(entries))   # v4: per-entry fmt (bit8 of flags)
    out += struct.pack(">I", 0)
    out += struct.pack(">I", 0)
    for (name, key, data_b, octv, fmt, scount), off in zip(entries, offsets):
        # flags: oct nibble in low byte, fmt in bit 8. sampleCount drives LEA.
        out += struct.pack(">IIII", key, off, scount, octv | (fmt << 8))
    out += data

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(out)

    print("=" * 70)
    print("build_sfx_pack v4 (Green Hill SFX -> mixed 8/16-bit SCSP pack, 248KB window)")
    print("=" * 70)
    print(f"  budget window : {BUDGET} B ({BUDGET/1024:.0f} KB)")
    print(f"  packed SFX    : {len(entries)}")
    ratemap = {OCT_22050: "22050", OCT_11025: "11025", OCT_5512: " 5512"}
    for (name, _, data_b, octv, fmt, scount), off in zip(entries, offsets):
        depth = "S16" if fmt == FMT_S16 else " S8"
        print(f"    {name:24s} {len(data_b):6d} B  @+0x{off:05x}  {ratemap[octv]}Hz {depth}")
    if skipped:
        print("  skipped (budget/missing):")
        for name, why in skipped:
            print(f"    {name:22s} {why}")
    total = len(data)
    print(f"  total file size                      : {len(out)} B ({len(out)/1024:.1f} KB)")
    print(f"  data blob                            : {total} B ({total/1024:.1f} KB)")
    print(f"  -> {OUT}")
    # 12 = the practical floor for the 176 KB window incl. spindash Charge/Release.
    if len(entries) < 12:
        print(f"build_sfx_pack: WARNING only {len(entries)} SFX fit -- expected >= 12")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
