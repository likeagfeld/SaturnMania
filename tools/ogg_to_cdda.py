#!/usr/bin/env python3
"""ogg_to_cdda.py - Transcode a Mania OGG -> CD-DA-ready WAV honouring the
hand-curated loop point in tools/loops.json.

Mania's OGG files have no LOOPSTART/LOOPLENGTH Vorbis tags; the per-stage
RSDK Setup objects pass loop sample positions to PlayStream() directly.
Since we don't run those C++ scripts on Saturn, we maintain the loop
positions ourselves in `tools/loops.json` (see
memory/bgm-loops-hand-curated.md for the binding rule).

This tool:
  1. Reads `tools/loops.json` for the chosen track's entry.
  2. ffmpeg-decodes the OGG to 44.1 kHz 16-bit stereo PCM (CD-DA format).
  3. If the entry has an explicit `loop_start_samples` / `loop_end_samples`,
     trims the WAV to [0..loop_end] so CD-DA looping at end-of-file lands
     on `loop_start` — i.e. the WAV file contains a single "intro + one
     loop body", and the CD block loops from end back to beginning, but
     the user-perceived loop is loop_start->loop_end. (Mania-style.)
     If `loop_end_samples` is null, end-of-file is used.
     If `loop_start_samples` is 0 and `loop_end_samples` is null, no trim.
  4. If the entry has no measured loop (loop_start is null), the tool
     refuses unless `--allow-unmeasured` is passed, AND prints a loud
     warning so a shipped build cannot accidentally include an unmeasured
     loop.
  5. Writes the final WAV to the chosen output (typically cd_audio/track02.wav).

The existing tools/build_cdda.py then converts the WAV to raw CD audio
and rewrites game.cue with the AUDIO track entry; that pipeline already
exists in build.bat. This tool only does the OGG->WAV step.

Usage:
  python tools/ogg_to_cdda.py \\
      --in extracted/Data/Music/GreenHill1.ogg \\
      --out cd_audio/track02.wav
"""
import argparse
import json
import os
import subprocess
import sys
import wave


def _load_loops(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def _ffmpeg_decode(ogg_path, wav_path):
    """Decode the OGG to 44.1 kHz / 16-bit / stereo WAV via ffmpeg."""
    cmd = ["ffmpeg", "-y", "-i", ogg_path,
           "-ac", "2", "-ar", "44100",
           "-sample_fmt", "s16",
           "-acodec", "pcm_s16le",
           "-loglevel", "error",
           wav_path]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"ffmpeg failed:\n{r.stderr}")


def _trim_wav(wav_path, end_samples):
    """In-place truncate the WAV at `end_samples`. Bytes-per-sample-frame
    = channels * sample_width = 2 * 2 = 4 for CD-DA."""
    with wave.open(wav_path, "rb") as r:
        params = r.getparams()
        nframes = r.getnframes()
        if end_samples >= nframes:
            return  # nothing to trim
        frames = r.readframes(end_samples)
    with wave.open(wav_path, "wb") as w:
        w.setparams(params._replace(nframes=end_samples))
        w.writeframes(frames)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--in", dest="src", required=True,
                    help="source OGG, e.g. extracted/Data/Music/GreenHill1.ogg")
    ap.add_argument("--out", required=True,
                    help="destination WAV, e.g. cd_audio/track02.wav")
    ap.add_argument("--loops-json", default="tools/loops.json")
    ap.add_argument("--allow-unmeasured", action="store_true",
                    help="permit shipping a track whose loop is null (DO NOT "
                         "set this casually; loops.json must be populated)")
    args = ap.parse_args()

    src_name = os.path.basename(args.src)
    loops = _load_loops(args.loops_json)
    entry = loops.get(src_name)
    if entry is None:
        sys.exit(f"\nFATAL: '{src_name}' is not listed in {args.loops_json}.\n"
                 f"Add an entry per the schema in memory/bgm-loops-hand-curated.md\n"
                 f"before transcoding this track.\n")

    loop_start = entry.get("loop_start_samples")
    loop_end   = entry.get("loop_end_samples")
    note       = entry.get("note", "")
    todo       = entry.get("todo", "")

    if loop_start is None:
        msg = (f"\n*** UNMEASURED LOOP for {src_name} ***\n"
               f"loops.json marks this track as TODO: {todo!r}\n")
        if not args.allow_unmeasured:
            sys.exit(msg + "\nRefusing to transcode. Either populate the loop "
                     "entry in tools/loops.json, or pass --allow-unmeasured "
                     "(NOT recommended for shipped builds).\n")
        sys.stderr.write(msg + "Continuing anyway because --allow-unmeasured "
                         "was passed. Track will use full-file CD-DA loop.\n")

    if note:
        sys.stderr.write(f"[ogg_to_cdda] {src_name}: {note}\n")

    os.makedirs(os.path.dirname(os.path.abspath(args.out)) or ".",
                exist_ok=True)
    _ffmpeg_decode(args.src, args.out)

    # Apply loop_end trim if requested. loop_end_samples=null => no trim
    # (file is left at full length; the CD block loops from EOF to start).
    if loop_end is not None:
        before = os.path.getsize(args.out)
        _trim_wav(args.out, int(loop_end))
        after = os.path.getsize(args.out)
        sys.stderr.write(
            f"[ogg_to_cdda] {src_name}: trimmed to {loop_end} samples "
            f"({before} -> {after} B)\n")

    if loop_start is not None and loop_start != 0:
        # The Saturn CD block loops EOF -> position 0 (or start_offset
        # passed to jo_audio_play_cd_track). For a proper Mania-style loop
        # where the loop body starts mid-file, we'd need to engineer the
        # WAV so that EOF -> 0 lands on loop_start. That's only possible
        # if we cut the intro into a separate track, OR if loop_start == 0.
        # For now: warn that mid-file loop_start isn't honoured by simple
        # CD-DA, and link the upgrade path.
        sys.stderr.write(
            f"[ogg_to_cdda] WARNING: {src_name} has loop_start_samples="
            f"{loop_start} (mid-file). The current CD-DA single-track "
            f"pipeline can only loop EOF->0; the intro will replay on each "
            f"loop. Upgrade path: split the OGG into intro+body and write "
            f"the body to its own CD-DA track that auto-plays after intro.\n")

    print(f"[ogg_to_cdda] OK  {args.src} -> {args.out}  "
          f"({os.path.getsize(args.out):,} B)")


if __name__ == "__main__":
    main()
