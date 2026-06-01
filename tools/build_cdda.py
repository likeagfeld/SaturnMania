#!/usr/bin/env python3
"""build_cdda.py - turn a 44.1 kHz / 16-bit stereo WAV at cd_audio/track02.wav
(or whatever the user supplies) into a Red-book-compatible CD audio track and
rewrite game.cue into a multi-track cuesheet (data track 1 + audio track 2).

Saturn's CD block plays raw Red-book audio tracks transparently via SCSP --
no main-RAM usage at all, which is why CDDA is the standard path for game
BGM on Saturn / PSX / Sega CD. jo_audio_play_cd_track(2, 2, true) is the
runtime call that starts it.

Raw CD audio is signed-16-bit little-endian stereo at 44.1 kHz, packed
2352 bytes per sector (588 samples * 4 bytes). We strip the WAV header
from the input (standard 44-byte RIFF/WAVE preamble for PCM, or scan
for the 'data' chunk) and write the PCM body as track02.bin.

This tool is content-agnostic: feed it whatever WAV you have. To produce a
suitable WAV from an arbitrary source file, run:
    python tools/convert_audio.py <your_input> --out cd_audio/track02.wav
    (or use ffmpeg directly: ffmpeg -i <input> -ac 2 -ar 44100 -c:a pcm_s16le
     cd_audio/track02.wav)

Usage:
    python tools/build_cdda.py cd_audio/track02.wav
        --bin-out cd_audio/track02.bin --cue-out game.cue --iso game.iso
"""
import argparse, os, struct, sys


def strip_wav_header(wav_bytes):
    """Return the raw PCM payload from a RIFF/WAVE file. Scans for the
    'data' chunk rather than assuming the canonical 44-byte preamble, so
    files with extra metadata chunks (LIST/INFO/cue/etc.) still work.
    Validates the format chunk is PCM s16 stereo @ 44100 Hz (Red-book)."""
    if wav_bytes[:4] != b"RIFF" or wav_bytes[8:12] != b"WAVE":
        sys.exit("input is not a RIFF/WAVE file")
    p = 12
    fmt_seen = False
    while p < len(wav_bytes) - 8:
        cid  = wav_bytes[p:p+4]
        sz   = struct.unpack_from("<I", wav_bytes, p+4)[0]
        body = wav_bytes[p+8:p+8+sz]
        if cid == b"fmt ":
            wfmt, ch, sr, _br, _ba, bps = struct.unpack_from("<HHIIHH", body)
            if wfmt != 1: sys.exit(f"fmt is not PCM (wFormatTag={wfmt})")
            if ch != 2:   sys.exit(f"need stereo, got {ch} channels")
            if sr != 44100: sys.exit(f"need 44100 Hz, got {sr} Hz")
            if bps != 16: sys.exit(f"need 16-bit, got {bps}-bit")
            fmt_seen = True
        elif cid == b"data":
            if not fmt_seen:
                sys.exit("'data' chunk appeared before 'fmt '")
            return body[:sz]
        p += 8 + sz + (sz & 1)                  # pad to even byte
    sys.exit("no 'data' chunk found in WAV")


def pad_to_sector(pcm):
    """Pad the PCM to a 2352-byte sector boundary; CD audio is read in
    whole sectors. The padding is silence (zero samples)."""
    SEC = 2352
    rem = len(pcm) % SEC
    if rem:
        pcm += b"\x00" * (SEC - rem)
    return pcm


def write_full_multitrack_cue(cue_path, iso_path, audio_bins):
    """Emit a CUE with track 01 = data ISO + tracks 02..N = each audio bin.
    audio_bins is a list of (bin_path, audio_secs) for tracks 02 onwards."""
    cue_dir = os.path.dirname(os.path.abspath(cue_path)) or "."
    iso_rel = os.path.relpath(os.path.abspath(iso_path), cue_dir).replace("\\", "/")
    with open(cue_path, "w") as f:
        f.write(f'FILE "{iso_rel}" BINARY\n')
        f.write( '  TRACK 01 MODE1/2048\n')
        f.write( '    INDEX 01 00:00:00\n')
        for i, (bin_path, _secs) in enumerate(audio_bins):
            track_num = i + 2
            bin_rel = os.path.relpath(os.path.abspath(bin_path), cue_dir).replace("\\", "/")
            f.write(f'FILE "{bin_rel}" BINARY\n')
            f.write(f'  TRACK {track_num:02d} AUDIO\n')
            f.write( '    PREGAP 00:02:00\n')
            f.write( '    INDEX 01 00:00:00\n')
    total_secs = sum(s for _, s in audio_bins)
    print(f"{cue_path}: {1+len(audio_bins)}-track CUE (data + "
          f"{len(audio_bins)} audio = {total_secs:.1f}s total)")


def write_multitrack_cue(cue_path, iso_path, bin_path, audio_secs):
    """A two-track CUE: track 1 = the data ISO (MODE2/2352), track 2 = the
    audio raw (AUDIO). Both files reference each other in CD time
    coordinates (mm:ss:ff at 75 frames/sec); track 2 starts at 00:00:00 in
    its own file. The data track's pre-gap counts as part of itself.

    File paths in a CUE are resolved by the CD-image consumer (Mednafen,
    YabaSanshiro, real-hw burner) **relative to the CUE file's directory**,
    not the current working dir. So we compute relpaths from the CUE's
    parent — that way an ISO in the project root and a bin in cd_audio/
    both resolve correctly regardless of where the user invokes the
    consumer from."""
    cue_dir = os.path.dirname(os.path.abspath(cue_path)) or "."
    iso_rel = os.path.relpath(os.path.abspath(iso_path), cue_dir).replace("\\", "/")
    bin_rel = os.path.relpath(os.path.abspath(bin_path), cue_dir).replace("\\", "/")
    with open(cue_path, "w") as f:
        f.write(f'FILE "{iso_rel}" BINARY\n')
        # MODE1/2048: 2048-byte user-data sectors with no sync+header
        # preamble. This matches what mkisofs writes when invoked with
        # `-sectype data` (build.bat uses `-sectype 2352` but the actual
        # bytes mkisofs emits begin directly with the Saturn IP header
        # "SEGA SEGASATURN" -- verified via xxd of game.iso at offset 0
        # -- which is MODE1/2048 territory). Saying MODE2/2352 in the
        # CUE causes Mednafen 1.32+ to try to read sync bytes that
        # aren't there and reject the disc with "Could not find a
        # system that supports this CD."
        f.write( '  TRACK 01 MODE1/2048\n')
        f.write( '    INDEX 01 00:00:00\n')
        f.write(f'FILE "{bin_rel}" BINARY\n')
        f.write( '  TRACK 02 AUDIO\n')
        # Standard 2-second (150 sectors @ 75 fps) pregap between data
        # and the first audio track.
        f.write( '    PREGAP 00:02:00\n')
        f.write( '    INDEX 01 00:00:00\n')
    print(f"{cue_path}: 2-track CUE (data + {audio_secs:.1f}s audio)")


def _wav_to_bin(wav_path, bin_path):
    """Strip WAV header, sector-align, write raw CD-DA bin. Returns seconds."""
    with open(wav_path, "rb") as f:
        pcm = strip_wav_header(f.read())
    pcm = pad_to_sector(pcm)
    os.makedirs(os.path.dirname(bin_path) or ".", exist_ok=True)
    with open(bin_path, "wb") as f:
        f.write(pcm)
    secs = len(pcm) / (44100 * 4)
    print(f"{bin_path}: {len(pcm)} bytes ({secs:.2f}s, "
          f"{len(pcm)//2352} sectors)")
    return secs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav", nargs="*",
                    help="One or more 44.1 kHz / 16-bit stereo WAV inputs "
                         "(in track order: track 2, track 3, ...). If a "
                         "single WAV is supplied with --bin-out, the legacy "
                         "single-track behaviour applies.")
    ap.add_argument("--bin-out",
                    help="(single-track mode) destination .bin for the audio track")
    ap.add_argument("--cue-out", required=True)
    ap.add_argument("--iso",     required=True)
    ap.add_argument("--bin-dir", default="cd_audio",
                    help="(multi-track mode) directory to write track0N.bin into")
    args = ap.parse_args()

    if len(args.wav) == 1 and args.bin_out:
        # Legacy single-track mode (compatible with the existing build.bat
        # invocation): emit one .bin at the requested path + 2-track CUE.
        secs = _wav_to_bin(args.wav[0], args.bin_out)
        write_multitrack_cue(args.cue_out, args.iso, args.bin_out, secs)
        return

    if not args.wav:
        ap.error("provide at least one WAV (or --bin-out for single-track mode)")

    # Multi-track mode: each WAV becomes its own track. cd_audio/track02.bin,
    # cd_audio/track03.bin, ...  CUE references all of them.
    audio_bins = []
    for i, wav in enumerate(args.wav):
        bin_path = os.path.join(args.bin_dir, f"track{i+2:02d}.bin")
        secs = _wav_to_bin(wav, bin_path)
        audio_bins.append((bin_path, secs))
    write_full_multitrack_cue(args.cue_out, args.iso, audio_bins)


if __name__ == "__main__":
    main()
