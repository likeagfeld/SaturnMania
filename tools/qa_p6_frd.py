#!/usr/bin/env python3
# =============================================================================
# qa_p6_frd.py -- P6_FRAMEDIR stage-1 gate (feature checklist
# docs/feature_checklists/sprite_frame_directory.md sec 6/7).
#
# Three layers, all must pass:
#
#   F1 OFFLINE STRUCTURAL REPLAY: for every cd/*.FRD blob, replay N probe
#      rects through a byte-exact Python mirror of the SH-2 consumer
#      (SaturnFrameDir_Lookup binary search, SaturnFrameDir.cpp:145-185:
#      key order (sy,sx,w,h), 16 B entries, big-endian) and verify the
#      pattern bytes round-trip against the source GIF crop (the same
#      contract the converter self-tests assert, re-proven through the
#      CONSUMER's search path). Also re-verify the manifest counts.
#
#   F2 LIVE WITNESS CONTRACT (game.map symbols + RetroArch UDP reader):
#        p6_w_frd_active  == --expect          (post-seam staged slots)
#        p6_w_frd_lookups  > 0                 (the hook actually serves)
#        p6_w_frd_misses  == 0                 (every rect pre-cut)
#        p6_w_frd_hash[i] / p6_w_frd_bytes[i] for i < active each match
#        exactly one offline blob's djb2/size   (staged cart copy intact)
#      A nonzero miss decodes the p6_w_frd_missrect/misswh/missslot ring.
#
#   RED conditions (any): the p6_w_frd_* symbols are ABSENT from game.map
#   (pre-FRD build -- this is the RED the gate must fire before the
#   wiring lands), active==0, lookups==0, misses>0, hash mismatch.
#
# Usage:
#   python tools/qa_p6_frd.py --offline-only          # F1 only (no live)
#   python tools/qa_p6_frd.py --expect 9              # F1 + F2 (live read)
#   python tools/qa_p6_frd.py --map game.map --expect 9
# =============================================================================
import argparse
import importlib.util
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
CD = os.path.join(ROOT, "cd")
MANIFEST = os.path.join(HERE, "frame_dir_manifest.json")
SPRITES = os.path.join(ROOT, "extracted", "Data", "Sprites")

PROBES_PER_SHEET = 8


def djb2(b):
    h = 5381
    for c in b:
        h = ((h << 5) + h) ^ c
        h &= 0xFFFFFFFF
    return h


def lookup_mirror(blob, frames, sx, sy, w, h):
    """Byte-exact mirror of SaturnFrameDir_Lookup (SaturnFrameDir.cpp)."""
    lo, hi = 0, frames - 1
    while lo <= hi:
        mid = (lo + hi) >> 1
        e = 12 + mid * 16
        esx, esy, ew, eh = struct.unpack(">HHHH", blob[e:e + 8])
        if esy != sy:
            c = esy - sy
        elif esx != sx:
            c = esx - sx
        elif ew != w:
            c = ew - w
        else:
            c = eh - h
        if c == 0:
            off, mode, pw8, lutIx = struct.unpack(">IBBH", blob[e + 8:e + 16])
            return {"off": off, "mode": mode, "pw": pw8 * 8, "lutIdx": lutIx}
        if c < 0:
            lo = mid + 1
        else:
            hi = mid - 1
    return None


def f1_offline(man):
    ok = True
    gif_cache = {}
    try:
        from PIL import Image
    except ImportError:
        Image = None
    for m in man["sheets"]:
        path = os.path.join(CD, m["file"])
        if not os.path.exists(path):
            print("F1 RED: missing blob %s (run build_frame_dir.py --all8)" % path)
            ok = False
            continue
        blob = open(path, "rb").read()
        if len(blob) != m["bytes"]:
            print("F1 RED: %s size %d != manifest %d" % (m["file"], len(blob), m["bytes"]))
            ok = False
            continue
        magic, frames, luts, W, H = struct.unpack(">4sHHHH", blob[:12])
        if magic != b"FRD1" or frames != m["frames"] or luts != m["luts"]:
            print("F1 RED: %s header (%r,%d,%d) != manifest (%d,%d)"
                  % (m["file"], magic, frames, luts, m["frames"], m["luts"]))
            ok = False
            continue
        # probe rects: spread across the sorted directory
        step = max(1, frames // PROBES_PER_SHEET)
        px = Wg = None
        if Image is not None:
            gif = os.path.join(SPRITES, m["sheet"].replace("/", os.sep))
            if os.path.exists(gif):
                im = gif_cache.get(gif)
                if im is None:
                    im = Image.open(gif)
                    gif_cache[gif] = im
                px = im.tobytes()
                Wg = im.size[0]
        for fi in range(0, frames, step):
            e = 12 + fi * 16
            sx, sy, w, h = struct.unpack(">HHHH", blob[e:e + 8])
            r = lookup_mirror(blob, frames, sx, sy, w, h)
            if r is None:
                print("F1 RED: %s search MISSED its own entry %d rect (%d,%d,%d,%d)"
                      % (m["file"], fi, sx, sy, w, h))
                ok = False
                continue
            if r["mode"] != 0:
                print("F1 RED: %s rect (%d,%d,%d,%d) mode %d != 0 (stage-1 is --all8)"
                      % (m["file"], sx, sy, w, h, r["mode"]))
                ok = False
            if r["pw"] < w or r["pw"] % 8:
                print("F1 RED: %s pw %d invalid for w %d" % (m["file"], r["pw"], w))
                ok = False
            pat = blob[r["off"]:r["off"] + r["pw"] * h]
            if px is not None:
                want = b"".join(px[(sy + rr) * Wg + sx:(sy + rr) * Wg + sx + w]
                                + b"\0" * (r["pw"] - w) for rr in range(h))
                if pat != want:
                    print("F1 RED: %s rect (%d,%d,%d,%d) pattern != GIF crop"
                          % (m["file"], sx, sy, w, h))
                    ok = False
    if ok:
        print("F1 GREEN: offline structural replay OK (%d sheets, %d probes/sheet)"
              % (len(man["sheets"]), PROBES_PER_SHEET))
    return ok


def f2_live(man, map_path, expect, host, port):
    spec = importlib.util.spec_from_file_location("qa_trace", os.path.join(HERE, "qa_trace.py"))
    qt = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(qt)
    if not os.path.exists(map_path):
        print("F2 RED: no %s" % map_path)
        return False
    mt = open(map_path, errors="replace").read()
    rd = qt.Reader(True, None, host, port)

    def sym(n):
        return rd.sym(mt, n)

    need = ["p6_w_frd_staged", "p6_w_frd_active", "p6_w_frd_lookups",
            "p6_w_frd_misses", "p6_w_frd_hash", "p6_w_frd_bytes",
            "p6_w_frd_frames", "p6_w_frd_missrect", "p6_w_frd_misswh",
            "p6_w_frd_missslot"]
    addr = {n: sym(n) for n in need}
    missing = [n for n, a in addr.items() if a is None]
    if missing:
        print("F2 RED: symbols ABSENT from %s: %s" % (map_path, ", ".join(missing)))
        print("        (pre-P6_FRAMEDIR build -- the wiring has not landed)")
        return False

    def r32(a, off=0):
        return rd.r32(a + off)

    staged = r32(addr["p6_w_frd_staged"])
    active = r32(addr["p6_w_frd_active"])
    lookups = r32(addr["p6_w_frd_lookups"])
    misses = r32(addr["p6_w_frd_misses"])
    if None in (staged, active, lookups, misses):
        print("F2 RED: live read returned None (RA dead? boot the ISO via qa_live.ps1)")
        return False
    print("F2 live: staged=%d active=%d lookups=%d misses=%d" %
          (staged, active, lookups, misses))
    ok = True
    if active != expect:
        print("F2 RED: active %d != expected %d" % (active, expect))
        ok = False
    if lookups <= 0:
        print("F2 RED: lookups == 0 (hook never served a miss)")
        ok = False
    if misses != 0:
        print("F2 RED: misses %d != 0 -- miss ring:" % misses)
        for i in range(min(misses, 4)):
            slot = r32(addr["p6_w_frd_missslot"], 4 * i)
            rect = r32(addr["p6_w_frd_missrect"], 4 * i) or 0
            wh = r32(addr["p6_w_frd_misswh"], 4 * i) or 0
            print("        slot=%s sx=%d sy=%d w=%d h=%d"
                  % (slot, (rect >> 16) & 0xFFFF, rect & 0xFFFF,
                     (wh >> 16) & 0xFFFF, wh & 0xFFFF))
        ok = False
    # djb2 identity: each active slot must match exactly one offline blob
    by_size = {}
    for m in man["sheets"]:
        blob = open(os.path.join(CD, m["file"]), "rb").read()
        by_size.setdefault(len(blob), []).append((m["file"], djb2(blob)))
    for i in range(max(active or 0, 0)):
        hb = (r32(addr["p6_w_frd_hash"], 4 * i) or 0) & 0xFFFFFFFF
        nb = r32(addr["p6_w_frd_bytes"], 4 * i)
        cand = by_size.get(nb, [])
        hit = [f for f, hh in cand if hh == hb]
        if not hit:
            print("F2 RED: slot %d (bytes=%s hash=0x%08x) matches NO offline blob"
                  % (i, nb, hb))
            ok = False
        else:
            print("F2 slot %d = %s (djb2 0x%08x, %d B) MATCH" % (i, hit[0], hb, nb))
    if ok:
        print("F2 GREEN: live witness contract OK")
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--offline-only", action="store_true")
    ap.add_argument("--map", default=os.path.join(ROOT, "game.map"))
    ap.add_argument("--expect", type=int, default=9,
                    help="expected p6_w_frd_active at the sampled leg (chain GHZ landing = 9)")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=55355)
    a = ap.parse_args()
    man = json.load(open(MANIFEST))
    ok = f1_offline(man)
    if not a.offline_only:
        ok = f2_live(man, a.map, a.expect, a.host, a.port) and ok
    print("qa_p6_frd: %s" % ("GREEN" if ok else "RED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
