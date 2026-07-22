#!/usr/bin/env python3
"""qa_sfx_audible.py -- RED/GREEN gate for gameplay-SFX AUDIBILITY (#325).

The dead-SFX defect has two independent root causes:
  1. LOAD  : F32-in-WRAM pool exhaustion (memory/dead-sfx-rootcause-f32-pool-
             exhaustion.md) -- fixed by the S8 SCSP-sound-RAM pack.
  2. TRIGGER: the KYONEX-isolation loop KEY-OFFED every concurrent voice
             (SCSP_Manual Fig 4.8; memory/sfx-kyonex-keyoff-truncation-fixed.md)
             -- every SFX key-on truncated the others to silence.

Neither is netmem- or savestate-observable (the symptom lives in the emulated
SCSP DAC output). This gate measures the ACTUAL output: it counts distinct SFX
onset events in a Mednafen `-soundrecord` WAV within a time window.

To isolate SFX from BGM cleanly, capture with the CD-DA tracks silenced (zero-
fill cd_audio/track0N.bin, restore after) so the ONLY audio in the WAV is the
SCSP direct-slot SFX -- then any onset in the Green Hill window (>200 s of the
full boot->GHZ chain) is a gameplay SFX reaching the DAC.

  capture (BGM silenced): tools/qa_sfx_capture.ps1 -Seconds 260 -Out w.wav
  gate:                    python tools/qa_sfx_audible.py w.wav --start 200 --min 3

Pre-KYONEX-fix this fires RED (GHZ SFX truncated to inaudible); post-fix GREEN.
"""
import argparse, sys, wave, array


def load_mono(path):
    w = wave.open(path, "rb")
    ch, sr, n = w.getnchannels(), w.getframerate(), w.getnframes()
    a = array.array('h'); a.frombytes(w.readframes(n)); w.close()
    if ch == 2:
        mono = [(a[i] + a[i + 1]) * 0.5 for i in range(0, len(a), 2)]
    else:
        mono = list(a)
    return mono, sr


def onsets(mono, sr, t0, t1, win_ms=50.0, thresh=0.02):
    """Count rising edges (silence -> >thresh RMS) = distinct SFX onsets."""
    win = max(1, int(sr * win_ms / 1000.0))
    lo = int(t0 * sr)
    hi = min(len(mono), int(t1 * sr)) if t1 else len(mono)
    ev = []
    prev = 0.0
    for k in range(lo, hi - win, win):
        seg = mono[k:k + win]
        r = (sum(x * x for x in seg) / win) ** 0.5 / 32768.0
        pk = max(abs(x) for x in seg) / 32768.0
        if r > thresh and prev <= thresh:
            ev.append((k / sr, pk))
        prev = r
    return ev


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("--start", type=float, default=200.0,
                    help="window start seconds (default 200 = Green Hill in the chain)")
    ap.add_argument("--end", type=float, default=0.0,
                    help="window end seconds (0 = end of file)")
    ap.add_argument("--min", type=int, default=3,
                    help="minimum SFX onsets required for GREEN")
    ap.add_argument("--thresh", type=float, default=0.02,
                    help="onset RMS threshold (fraction full-scale)")
    a = ap.parse_args()

    mono, sr = load_mono(a.wav)
    dur = len(mono) / sr
    ev = onsets(mono, sr, a.start, a.end or dur, thresh=a.thresh)
    print(f"{a.wav}: dur={dur:.1f}s  window={a.start:.0f}-{a.end or dur:.0f}s  "
          f"onsets={len(ev)} (need >={a.min})")
    for t, pk in ev:
        print(f"  t={t:8.2f}s  peak={pk*100:5.1f}%FS")
    if len(ev) >= a.min:
        print("GREEN: gameplay SFX are reaching the SCSP DAC output.")
        return 0
    print("RED: no/insufficient gameplay SFX in output window.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
