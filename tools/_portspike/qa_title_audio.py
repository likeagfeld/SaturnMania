#!/usr/bin/env python3
# =============================================================================
# qa_title_audio.py -- split #2 (title BGM) ON-SPEAKER gate: measure whether the
# captured audio is NON-SILENT, and WHEN sound starts (the title BGM = CD-DA
# track 3 / TitleScreen, started by PlayStream once the title scene loads).
#
# The decomp TitleSetup plays Music_PlayTrack(TRACK_STAGE) -> TitleScreen.ogg;
# the Saturn HandleStreamLoad maps that to CUE audio track 3 -> jo_audio_play_
# cd_track(3) -> real Red-book CD-DA. So the recorded WAV should be SILENT during
# the boot/load, then carry music once the title is up.
#
# Reads a Mednafen -soundrecord WAV (standard 44.1 kHz/16-bit stereo PCM) and
# reports windowed RMS so we see silence->music transition, not just a global
# average (a global average is dominated by the long silent load).
#
#   T1 audio is NON-SILENT somewhere (peak window RMS >= RMS_FLOOR)  RED 0 -> GREEN
#   T2 the non-silent region is sustained (>= MIN_MUSIC_SEC of music, not a blip)
#
# Usage: python tools/_portspike/qa_title_audio.py [_arc_sound.wav]
# =============================================================================
import os
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))

RMS_FLOOR = 350.0      # 16-bit PCM; silence floor ~ <50, real CD-DA music ~ >1000
MIN_MUSIC_SEC = 3.0    # sustained music, not a 1-window click
WIN_SEC = 0.5


def main(argv):
    wav = argv[1] if len(argv) > 1 else os.path.join(ROOT, "_arc_sound.wav")
    if not os.path.isabs(wav):
        wav = os.path.join(ROOT, wav)
    print("=" * 70)
    print("TITLE AUDIO GATE (split #2) -- non-silent + sustained BGM")
    print("=" * 70)
    if not os.path.isfile(wav):
        print("RESULT: RED -- WAV missing (%s)" % wav); return 1

    # Mednafen's -soundrecord WAV is left UNTERMINATED when the process is killed:
    # the RIFF + data chunk size fields stay 0, so wave.open reads 0 frames (or
    # rejects it). The fmt chunk (ch/rate/bits) IS valid + the PCM body is all
    # there. Parse the header for ch/rate, then read raw int16 from the data offset
    # to end-of-file. Falls back to the wave module if the header IS terminated.
    nch, sw, fr = 2, 2, 48000
    pcm_off, pcm_len = 44, 0
    with open(wav, "rb") as f:
        hdr = f.read(4096)
        f.seek(0, 2); fsize = f.tell()
    if hdr[:4] != b"RIFF" or hdr[8:12] != b"WAVE":
        print("RESULT: RED -- not a RIFF/WAVE container"); return 1
    # walk chunks for 'fmt ' and 'data'
    import struct
    off = 12
    while off + 8 <= len(hdr):
        cid = hdr[off:off + 4]; csz = struct.unpack("<I", hdr[off + 4:off + 8])[0]
        if cid == b"fmt ":
            _fmt, nch, fr, _br, _ba, bits = struct.unpack("<HHIIHH", hdr[off + 8:off + 8 + 16])
            sw = bits // 8
        if cid == b"data":
            pcm_off = off + 8
            pcm_len = csz if csz > 0 else (fsize - pcm_off)  # 0 => unterminated -> to EOF
            break
        off += 8 + csz + (csz & 1)
    if pcm_len <= 0:
        pcm_len = fsize - pcm_off
    nfr = pcm_len // (sw * max(nch, 1))
    print("  format: %d ch, %d-bit, %d Hz, %d frames (%.1f s)  [data@%d len=%d, unterminated-OK]"
          % (nch, sw * 8, fr, nfr, nfr / float(fr) if fr else 0, pcm_off, pcm_len))
    if sw != 2 or nfr == 0:
        print("RESULT: RED -- unexpected sample width / empty (%d-bit, %d frames)" % (sw * 8, nfr)); return 1

    with open(wav, "rb") as f:
        f.seek(pcm_off); raw = f.read(nfr * sw * nch)
    a = np.frombuffer(raw, dtype=np.int16).astype(np.float32)
    if nch > 1:
        a = a.reshape(-1, nch).mean(axis=1)

    win = int(WIN_SEC * fr)
    if win <= 0:
        print("RESULT: RED -- bad frame rate"); return 1
    nwin = len(a) // win
    rms = np.array([float(np.sqrt(np.mean(a[i * win:(i + 1) * win] ** 2)))
                    for i in range(nwin)])
    music_win = rms >= RMS_FLOOR
    peak = float(rms.max()) if nwin else 0.0
    first = next((i for i, m in enumerate(music_win) if m), -1)
    music_sec = float(music_win.sum()) * WIN_SEC

    print("  windows: %d (%.1fs each)   peak RMS=%.0f   floor=%.0f" % (nwin, WIN_SEC, peak, RMS_FLOOR))
    print("  first non-silent window: %s   sustained music: %.1f s"
          % (("t=%.1fs" % (first * WIN_SEC)) if first >= 0 else "NONE", music_sec))
    # compact RMS sparkline so the silence->music transition is visible
    if nwin:
        marks = "".join("#" if m else "." for m in music_win)
        print("  RMS map (.=silent #=music): %s" % marks)
    print("-" * 70)
    t1 = peak >= RMS_FLOOR
    t2 = music_sec >= MIN_MUSIC_SEC
    for name, ok, det in [
        ("T1 audio NON-SILENT (peak RMS >= floor)", t1, "peak=%.0f" % peak),
        ("T2 sustained BGM (>= %.0fs of music)" % MIN_MUSIC_SEC, t2, "music=%.1fs" % music_sec),
    ]:
        print("  [%s] %s   (%s)" % ("GREEN" if ok else " RED ", name, det))
    print("-" * 70)
    if t1 and t2:
        print("RESULT: GREEN -- title audio present (peak RMS %.0f, %.1fs of music)." % (peak, music_sec)); return 0
    print("RESULT: RED -- title audio silent/blip (peak RMS %.0f, %.1fs music)." % (peak, music_sec)); return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
