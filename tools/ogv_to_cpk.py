#!/usr/bin/env python3
"""
ogv_to_cpk.py - Convert any video (OGV/MP4/etc.) to Saturn Cinepak FILM (.CPK).

Saturn's official MovieToSaturn_J encoder is not in the documentation we have.
This tool replicates its role using ffmpeg's libavcodec Cinepak encoder for
the video portion and writing the FILM container header + sample table by
hand per the CpkHeader / CpkFilmSample struct layout in SGL_CPK.H:

  Offset  Size  Field
   0      4     "FILM"          (BE bytes)
   4      4     size_header     (offset to first sample = header + STAB bytes)
   8      4     version         (typically 0x00010000)
  12      4     reserved        (0)
  16      4     "FDSC"
  20      4     size_fdsc       (0x0000001C)
  24      4     c_type          ("cvid" big-endian)
  28      4     height          (px)
  32      4     width           (px)
  36      1     color           (15 or 24)
  37      1     sound_channel   (0 = no audio for this build)
  38      1     sound_smpling_bit
  39      1     sound_compress  (0=PCM, 1=ADPCM)
  40      4     sound_smpling_rate
  44      4     ckey
  48      4     "STAB"
  52      4     size_stab       ((sample_total + 1) * 16)
  56      4     time_scale_film (600 = standard 1/600 sec)
  60      4     sample_total
  64+    16     per sample: { offset, size, time, duration }   (all BE u32)

Each sample is a single Cinepak video frame chunk in raw form (no AVI
wrapper). ffmpeg's Cinepak encoder writes one chunk per frame; we strip
the AVI container and concatenate the chunks into media-data following
the header.

Audio is currently SKIPPED -- the build's CD-DA track 03 (title music)
covers audio while the video plays silently. Adding ADPCM audio is a
follow-up; see SGL_CPK.H sound_compress=1 path.

Usage:
    python tools/ogv_to_cpk.py extracted/Data/Video/Mania.ogv \\
        --out cd/INTRO.CPK [--width 256] [--height 176] [--fps 15]

Requires: ffmpeg on PATH.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile


# ---- AVI parser (extracts Cinepak frames from ffmpeg's output) -----------

def _read_chunk(data, p):
    """Read a 4cc + size + payload from AVI bytes. Returns (4cc, payload, next_p)."""
    if p + 8 > len(data):
        return None
    fourcc = data[p:p + 4]
    size = struct.unpack_from("<I", data, p + 4)[0]
    body = data[p + 8:p + 8 + size]
    next_p = p + 8 + size + (size & 1)  # word-aligned
    return fourcc, body, next_p


def _extract_video_frames_from_avi(avi_bytes):
    """Walk the AVI structure looking for the 'movi' LIST and each '00dc'
    (uncompressed video) or stream-0 chunk inside it. Return a list of
    raw Cinepak frame chunks.

    AVI structure:
        RIFF + size + "AVI "
            LIST + size + "hdrl"   (headers - we skip)
            LIST + size + "movi"   (media data - frames live here)
                each frame: NN dc + size + chunk  (NN = stream index)
            ...
    """
    if avi_bytes[:4] != b"RIFF" or avi_bytes[8:12] != b"AVI ":
        sys.exit("not a RIFF/AVI file (ffmpeg output unexpected)")

    p = 12
    frames = []
    while p < len(avi_bytes) - 8:
        r = _read_chunk(avi_bytes, p)
        if r is None: break
        fourcc, body, next_p = r
        if fourcc == b"LIST" and body[:4] == b"movi":
            # walk inside movi
            q = 4  # skip the "movi" 4cc marker inside body
            while q < len(body) - 8:
                rr = _read_chunk(body, q)
                if rr is None: break
                f4, fb, qq = rr
                # Video frames: stream-N "dc" tag (e.g. "00dc"). Stream index
                # is in the first two ASCII digits. We assume single-stream
                # video, so just match the "dc" suffix.
                if len(f4) == 4 and f4[2:4] == b"dc" and fb:
                    frames.append(fb)
                q = qq
        p = next_p
    return frames


# ---- FILM container writer ----------------------------------------------

CPK_MAGIC_FILM = b"FILM"
CPK_MAGIC_FDSC = b"FDSC"
CPK_MAGIC_STAB = b"STAB"
CPK_MAGIC_CVID = b"cvid"

TIME_SCALE = 600   # Sega standard


def _build_cpk(frames, width, height, fps, color_bits=15):
    """Assemble a complete .CPK byte stream from the video frames."""
    sample_total = len(frames)
    size_stab = (sample_total + 1) * 16
    size_header = 16 + 4 + 4 + 0x1C + 8 + size_stab  # FILM(16) + FDSC + body + STAB header + table
    # Recompute carefully:
    #   FILM header: 16 bytes
    #   FDSC marker + size: 8 bytes
    #   FDSC body: 0x1C = 28 bytes
    #   STAB marker + size: 8 bytes
    #   STAB body: 4 (time_scale) + 4 (sample_total) + sample_total * 16
    fdsc_body_size = 0x1C
    stab_body_size = 8 + sample_total * 16
    size_header = 16 + 8 + fdsc_body_size + 8 + stab_body_size

    out = bytearray()

    # FILM header (16 bytes)
    out += CPK_MAGIC_FILM
    out += struct.pack(">I", size_header)
    out += struct.pack(">I", 0x00010000)  # version 1.0
    out += struct.pack(">I", 0)            # reserved

    # FDSC chunk
    out += CPK_MAGIC_FDSC
    out += struct.pack(">I", fdsc_body_size)
    out += CPK_MAGIC_CVID                  # c_type
    out += struct.pack(">I", height)
    out += struct.pack(">I", width)
    out += struct.pack(">B", color_bits)
    out += struct.pack(">B", 0)            # sound_channel = 0 (no audio)
    out += struct.pack(">B", 0)            # sound_smpling_bit
    out += struct.pack(">B", 0)            # sound_compress
    out += struct.pack(">I", 0)            # sound_smpling_rate
    out += struct.pack(">I", 0)            # ckey

    # STAB chunk
    out += CPK_MAGIC_STAB
    out += struct.pack(">I", stab_body_size)
    out += struct.pack(">I", TIME_SCALE)
    out += struct.pack(">I", sample_total)

    # Sample table
    frame_duration = TIME_SCALE // fps
    # Each sample's offset is from media start (= size_header). We compute
    # offsets cumulatively across frame sizes.
    media_offset = 0
    sample_table_pos = len(out)
    # First emit placeholder entries to reserve the table bytes; we'll
    # patch the offsets after computing.
    for _ in range(sample_total):
        out += struct.pack(">IIII", 0, 0, 0, 0)

    # Append frame payloads + record their offsets
    sample_records = []
    for i, frame_data in enumerate(frames):
        offset = media_offset
        size = len(frame_data)
        time = i * frame_duration
        duration = frame_duration
        sample_records.append((offset, size, time, duration))
        out += frame_data
        media_offset += size

    # Patch the sample table
    for i, (offset, size, time, duration) in enumerate(sample_records):
        struct.pack_into(">IIII", out, sample_table_pos + i * 16,
                         offset, size, time, duration)

    return bytes(out)


# ---- Driver -------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", help="source video (OGV/MP4/etc.)")
    ap.add_argument("--out", required=True, help="output .CPK")
    ap.add_argument("--width", type=int, default=256,
                    help="output video width in px (must be multiple of 4, "
                         "default 256). Saturn Cinepak is bandwidth-limited.")
    ap.add_argument("--height", type=int, default=176,
                    help="output video height in px (must be multiple of 4, "
                         "default 176)")
    ap.add_argument("--fps", type=int, default=15,
                    help="output frame rate (default 15). Lower = smaller file.")
    ap.add_argument("--max-duration", type=float, default=None,
                    help="optionally cap encoded length in seconds")
    args = ap.parse_args()

    if args.width % 4 or args.height % 4:
        sys.exit("width/height must be multiples of 4 (Cinepak constraint)")

    # Stage 1: ffmpeg encode -> intermediate AVI with Cinepak codec.
    with tempfile.NamedTemporaryFile(suffix=".avi", delete=False) as tmp:
        avi_path = tmp.name
    try:
        # Filter chain on the OUTPUT side so the rate-decimation actually
        # decimates (an input-side `-r` would just stretch timestamps).
        # `pad` letterboxes wide sources (Mania.ogv is 1024x512 = 2:1) into
        # our chosen output aspect with black bars.
        vf = (f"fps={args.fps},"
              f"scale={args.width}:{args.height}:force_original_aspect_ratio=decrease,"
              f"pad={args.width}:{args.height}:(ow-iw)/2:(oh-ih)/2:color=black")
        cmd = [
            "ffmpeg", "-y",
            "-i", args.input,
            "-an",                                # no audio in the CPK
            "-c:v", "cinepak",
            "-vf", vf,
            "-f", "avi",
        ]
        if args.max_duration is not None:
            cmd += ["-t", str(args.max_duration)]
        cmd.append(avi_path)

        print("ffmpeg:", " ".join(cmd))
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            sys.stderr.write(r.stderr[-2000:])
            sys.exit(f"ffmpeg failed: rc={r.returncode}")

        # Stage 2: parse out the Cinepak frame chunks.
        with open(avi_path, "rb") as f:
            avi_bytes = f.read()
        frames = _extract_video_frames_from_avi(avi_bytes)
        if not frames:
            sys.exit("no video frames found in ffmpeg AVI output")
        print(f"extracted {len(frames)} Cinepak frame chunks "
              f"(total {sum(len(f) for f in frames):,} bytes)")

    finally:
        try: os.unlink(avi_path)
        except OSError: pass

    # Stage 3: write the FILM container.
    cpk_bytes = _build_cpk(frames, args.width, args.height, args.fps)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)) or ".",
                exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(cpk_bytes)
    secs = len(frames) / args.fps
    print(f"wrote {args.out}  ({len(cpk_bytes):,} bytes, "
          f"{args.width}x{args.height} @ {args.fps}fps, "
          f"~{secs:.1f}s, video-only)")


if __name__ == "__main__":
    main()
