#!/usr/bin/env python3
"""convert_audio.py - generic ffmpeg wrapper that emits the raw signed-8-bit
mono PCM format jo_audio_load_pcm wants (JoSoundMono8Bit), for the Saturn
SCSP playback path.

This tool is content-agnostic: point it at any audio file you have on your
machine -- your own recordings, royalty-free clips, your own conversions of
media you own -- and it spits out a Saturn-loadable PCM at your chosen sample
rate. What you put in is your decision.

Requires ffmpeg on PATH (https://ffmpeg.org). Per jo's own docs (audio.h):
    ffmpeg -i input -f s8 -ac 1 -ar <rate> output.PCM

For BGM, sound RAM is tight on Saturn: ~512 KB of SCSP memory shared with
SFX, so prefer 11025 Hz / 8000 Hz mono for long loops. For SFX a higher
rate is fine -- they're tiny.

Usage:
    python tools/convert_audio.py <input> --out cd/STAGEBGM.PCM --rate 11025
    python tools/convert_audio.py <input> --out cd/RINGSFX.PCM  --rate 22050
"""
import argparse, os, shutil, subprocess, sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input", help="any audio file ffmpeg can read")
    ap.add_argument("--out",  required=True,
                    help="output path (recommended: cd/SOMETHING.PCM)")
    ap.add_argument("--rate", type=int, default=11025,
                    help="sample rate (8000-44100; 11025 is a good BGM default)")
    ap.add_argument("--ffmpeg", default="ffmpeg",
                    help="ffmpeg binary path or name on PATH")
    args = ap.parse_args()

    if shutil.which(args.ffmpeg) is None:
        sys.exit(f"ffmpeg not found on PATH (looked for {args.ffmpeg!r}). "
                 "Install ffmpeg -- https://ffmpeg.org -- and re-run.")
    if not os.path.exists(args.input):
        sys.exit(f"input not found: {args.input}")
    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)

    cmd = [args.ffmpeg, "-y", "-i", args.input,
           "-f", "s8", "-ac", "1", "-ar", str(args.rate), args.out]
    print("running:", " ".join(cmd))
    rc = subprocess.call(cmd)
    if rc != 0:
        sys.exit(f"ffmpeg failed with exit {rc}")

    size = os.path.getsize(args.out)
    secs = size / args.rate
    print(f"{args.out}: {size} bytes ({secs:.2f}s of s8 mono @ {args.rate} Hz)")


if __name__ == "__main__":
    main()
