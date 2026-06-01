#!/usr/bin/env python3
"""qa_sfx_probe_analyze.py -- measure whether PCM SFX is AUDIBLE.

User request 2026-05-29: "i havent confirmed sfx... need a test". The existing
Phase 2.4a gate (qa_phase2_4a_audio_gate.py) only checks the SFX *wiring* is
present (P1-P4 static), and its runtime P5 is a SKIP -- it never measured that
the PCM actually produces sound. This tool closes that gap.

Input: a WAV captured from a QA_SFX_PROBE build (BGM suppressed, jump SFX fired
on a fixed cadence in the GHZ-active tick). Because BGM is suppressed, ANY
non-trivial audio in the WAV must be the PCM SFX.

Method:
  - Parse the RIFF/WAVE header (fmt + data chunks).
  - Slide a window (default 50 ms) across the samples; compute per-window RMS as
    a fraction of full-scale.
  - A "burst" = a window whose RMS exceeds --burst-thresh (default 1% FS).
  - Report: overall peak, overall RMS, number of distinct bursts (runs of
    above-threshold windows), and the median spacing between burst onsets.

Verdict:
  GREEN (SFX audible) if >= --min-bursts distinct bursts are found.
  RED   (SFX silent)  otherwise.

The QA_SFX_PROBE build fires the jump SFX every QA_SFX_PROBE_PERIOD ticks
(default 30 = 0.5 s). Once GHZ-active is reached, a ~10 s window yields ~20
bursts, so --min-bursts 3 is a conservative pass bar that still REDs a fully
silent capture.

Run:
    python tools/qa_sfx_probe_analyze.py qa_sfx_probe.wav
    python tools/qa_sfx_probe_analyze.py qa_sfx_probe.wav --burst-thresh 0.01 --min-bursts 3
"""

import argparse
import struct
import sys


def read_wav(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[0:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("not a RIFF/WAVE file")
    pos = 12
    fmt = None
    pcm = None
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        (csz,) = struct.unpack_from("<I", data, pos + 4)
        # Streaming WAV writers (Mednafen -soundrecord) leave the data-chunk
        # size as 0 because the final length is unknown at header-write time.
        # Treat a zero/overflowing size on the data chunk as "rest of file".
        avail = len(data) - (pos + 8)
        if cid == b"data" and (csz == 0 or csz > avail):
            csz = avail
        body = data[pos + 8: pos + 8 + csz]
        if cid == b"fmt ":
            (afmt, ch, rate, _byterate, _align, bits) = struct.unpack_from(
                "<HHIIHH", body, 0)
            fmt = dict(afmt=afmt, ch=ch, rate=rate, bits=bits)
        elif cid == b"data":
            pcm = body
        pos += 8 + csz + (csz & 1)  # chunks are word-aligned
    if fmt is None or pcm is None:
        raise ValueError("missing fmt or data chunk")
    return fmt, pcm


def to_mono_floats(fmt, pcm):
    """Return a list of mono samples in [-1, 1]. Averages channels."""
    ch = fmt["ch"]
    bits = fmt["bits"]
    if bits == 16:
        n = len(pcm) // 2
        ints = struct.unpack_from("<%dh" % n, pcm, 0)
        scale = 32768.0
    elif bits == 8:
        n = len(pcm)
        ints = [b - 128 for b in pcm[:n]]
        scale = 128.0
    else:
        raise ValueError("unsupported bits-per-sample: %d" % bits)
    frames = len(ints) // ch
    mono = [0.0] * frames
    for i in range(frames):
        acc = 0
        base = i * ch
        for c in range(ch):
            acc += ints[base + c]
        mono[i] = (acc / ch) / scale
    return mono


def analyze(mono, rate, win_ms, burst_thresh):
    win = max(1, int(rate * win_ms / 1000.0))
    peak = 0.0
    sumsq_all = 0.0
    win_rms = []
    for start in range(0, len(mono) - win + 1, win):
        s = 0.0
        for j in range(start, start + win):
            v = mono[j]
            s += v * v
            a = v if v >= 0 else -v
            if a > peak:
                peak = a
        rms = (s / win) ** 0.5
        win_rms.append(rms)
        sumsq_all += s
    overall_rms = (sumsq_all / max(1, len(mono))) ** 0.5
    # Distinct bursts = runs of consecutive above-threshold windows.
    bursts = []  # (onset_window_index)
    in_burst = False
    for i, r in enumerate(win_rms):
        if r >= burst_thresh:
            if not in_burst:
                bursts.append(i)
                in_burst = True
        else:
            in_burst = False
    onsets_sec = [i * win / rate for i in bursts]
    spacings = [onsets_sec[i + 1] - onsets_sec[i]
                for i in range(len(onsets_sec) - 1)]
    spacings.sort()
    median_spacing = spacings[len(spacings) // 2] if spacings else None
    return dict(peak=peak, overall_rms=overall_rms, n_windows=len(win_rms),
                n_bursts=len(bursts), onsets_sec=onsets_sec,
                median_spacing=median_spacing, win=win)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("--win-ms", type=float, default=50.0)
    ap.add_argument("--burst-thresh", type=float, default=0.01,
                    help="per-window RMS (fraction of full-scale) to count as a burst")
    ap.add_argument("--min-bursts", type=int, default=3,
                    help="distinct bursts required for a GREEN verdict")
    args = ap.parse_args()

    fmt, pcm = read_wav(args.wav)
    print("=== SFX-probe WAV analysis ===")
    print("  file: %s" % args.wav)
    print("  fmt: %d ch, %d-bit, %d Hz, %d data bytes"
          % (fmt["ch"], fmt["bits"], fmt["rate"], len(pcm)))
    mono = to_mono_floats(fmt, pcm)
    dur = len(mono) / fmt["rate"] if fmt["rate"] else 0.0
    print("  duration: %.2f s (%d mono frames)" % (dur, len(mono)))
    r = analyze(mono, fmt["rate"], args.win_ms, args.burst_thresh)
    print("")
    print("  peak amplitude : %.4f  (%.2f%% full-scale)" % (r["peak"], r["peak"] * 100))
    print("  overall RMS    : %.5f  (%.3f%% full-scale)" % (r["overall_rms"], r["overall_rms"] * 100))
    print("  windows        : %d  (%.0f ms each)" % (r["n_windows"], args.win_ms))
    print("  burst thresh   : %.3f%% FS" % (args.burst_thresh * 100))
    print("  distinct bursts: %d" % r["n_bursts"])
    if r["median_spacing"] is not None:
        print("  median spacing : %.3f s between burst onsets" % r["median_spacing"])
    if r["onsets_sec"]:
        shown = ", ".join("%.2f" % t for t in r["onsets_sec"][:12])
        print("  onset times (s): %s%s" % (shown, " ..." if len(r["onsets_sec"]) > 12 else ""))
    print("")
    green = r["n_bursts"] >= args.min_bursts
    if green:
        print("VERDICT: GREEN -- SFX is AUDIBLE (%d bursts >= %d required)"
              % (r["n_bursts"], args.min_bursts))
    else:
        print("VERDICT: RED -- SFX appears SILENT (%d bursts < %d required)"
              % (r["n_bursts"], args.min_bursts))
    return 0 if green else 1


if __name__ == "__main__":
    sys.exit(main())
