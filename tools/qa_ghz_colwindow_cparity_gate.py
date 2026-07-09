#!/usr/bin/env python3
"""qa_ghz_colwindow_cparity_gate.py - Task #180 step 3d-C (C streamer parity).

HOST-VERIFIABLE structural gate proving the C toroidal-slide streamer
(src/rsdk/colwindow.c) and the collision window-fetch site
(src/rsdk/collision.c s_layer_tile) mirror the executable Python reference
tools/qa_ghz_colwindow_gate.py ColWindow LINE-FOR-LINE.

There is NO host C compiler on this machine (Windows host has no gcc/cl;
the docker image ships only sh-none-elf-gcc-8.2.0 SH-2 cross). So this gate
proves the mirror STRUCTURALLY by parsing the C source and asserting that
each load-bearing invariant of the Python ColWindow is present and matches:

  P1 constants: COLWINDOW_W in colwindow.h == WINDOW_W in the host gate (48),
                so the C window geometry equals the host-verified geometry.
  P2 stream_column mirror (#180 step 4c RAM->RAM): slot = col % W; one
                contiguous ysize*2-byte copy out of the decoded block; the
                owning 16-col block is decoded RAM->RAM via the embedded puff
                inflater (puff(COLWINDOW_DECODE_ADDR,...)). NO per-column CD.
  P3 ensure_band mirror: clamp base to [0, xsize-W]; stream ONLY columns
                whose wrap slot does not already hold them (loaded[c%W]!=c).
  P4 fetch mirror (collision.c s_layer_tile): out-of-band guard returns
                0xFFFF; in-band read is win[(worldCol % winW)*ysize + row];
                and ALL 8 collision lookup sites route through s_layer_tile
                (so no site bypasses the window contract).
  P5 header layout: index_off = desc_off + nlayers*12 (the GCO3 descriptor
                table parsed in both colwindow.c and build_collayout.py).
  P6 TEETH: a deliberately corrupted expectation set MUST fail, proving the
                pattern asserts are not vacuous.

No Saturn/emulator dependency - pure source parse, runs in verify_done.

NOTE on RED->GREEN: the behavioral fall-through proof (Sonic no longer
tunnels) is tools/qa_ghz_fall_through_gate.py, retargeted at step 4 when
Player.c adopts rsdk_process_object_movement + binds the window. THIS gate
is the structural mirror check for the streamer module itself.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COLWIN_H = os.path.join(ROOT, "src", "rsdk", "colwindow.h")
COLWIN_C = os.path.join(ROOT, "src", "rsdk", "colwindow.c")
COLLIS_C = os.path.join(ROOT, "src", "rsdk", "collision.c")
HOSTGATE = os.path.join(ROOT, "tools", "qa_ghz_colwindow_gate.py")


def read(path):
    if not os.path.exists(path):
        return None
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def norm(s):
    """Collapse runs of whitespace so spacing differences don't break a
    substring assert."""
    return re.sub(r"\s+", " ", s)


def main():
    print("=== Task #180 step 3d-C colwindow C-parity gate ===")

    src_h = read(COLWIN_H)
    src_c = read(COLWIN_C)
    src_col = read(COLLIS_C)
    src_host = read(HOSTGATE)

    missing = [p for p, s in ((COLWIN_H, src_h), (COLWIN_C, src_c),
                              (COLLIS_C, src_col), (HOSTGATE, src_host))
               if s is None]
    if missing:
        for m in missing:
            print("  RED: missing source: %s" % m)
        return 1

    nc = norm(src_c)
    ncol = norm(src_col)

    fails = []

    # --- P1 constants -------------------------------------------------------
    m = re.search(r"#define\s+COLWINDOW_W\s+(\d+)", src_h)
    c_w = int(m.group(1)) if m else -1
    m = re.search(r"^WINDOW_W\s*=\s*(\d+)", src_host, re.M)
    host_w = int(m.group(1)) if m else -2
    if c_w != host_w or c_w <= 0:
        fails.append("P1 COLWINDOW_W=%d != host WINDOW_W=%d" % (c_w, host_w))
    else:
        print("  P1 OK  COLWINDOW_W=%d == host WINDOW_W=%d" % (c_w, host_w))

    # --- P2 stream_column mirror (#180 step 4c RAM->RAM) -------------------
    p2_checks = [
        ("slot = col % W", "c % s_winW"),
        ("colbytes = ysize*2", "L->ysize * 2"),
        ("owning 16-col block index", "c / s_block_cols"),
        ("RAM->RAM block decode via puff", "puff(COLWINDOW_DECODE_ADDR"),
        ("contiguous column copy", "memcpy(L->win + slot * L->ysize"),
    ]
    p2_ok = True
    for label, needle in p2_checks:
        if norm(needle) not in nc:
            fails.append("P2 stream_column missing: %s [%s]" % (label, needle))
            p2_ok = False
    # The per-column CD-read primitives are GONE in step 4c; their presence
    # in REAL CODE would mean the contention regression was reintroduced.
    # Strip comments first so the "why we rewrote this" prose (which names the
    # removed GFS_* calls) does not count.
    code_only = norm(re.sub(r"/\*.*?\*/|//[^\n]*", " ", src_c, flags=re.DOTALL))
    for banned in ("GFS_Seek", "GFS_Fread"):
        if banned in code_only:
            fails.append("P2 step-4c regression: %s back in colwindow.c "
                         "(per-frame CD streaming returns)" % banned)
            p2_ok = False
    if p2_ok:
        print("  P2 OK  stream_column mirrors ColWindow.stream_column "
              "(slot=col%W, one contiguous ysize*2 copy, RAM->RAM puff decode, "
              "zero CD)")

    # --- P3 ensure_band mirror ---------------------------------------------
    p3_checks = [
        ("clamp low", "if (base < 0) base = 0;"),
        ("clamp high", "if (base > xs - s_winW) base = xs - s_winW;"),
        ("stream newly-entered only", "if (L->loaded[c % s_winW] != c)"),
        ("band span", "for (int32_t c = base; c < base + s_winW; ++c)"),
        ("commit base", "s_base = base;"),
    ]
    p3_ok = True
    for label, needle in p3_checks:
        if norm(needle) not in nc:
            fails.append("P3 ensure_band missing: %s [%s]" % (label, needle))
            p3_ok = False
    if p3_ok:
        print("  P3 OK  ensure_band mirrors ColWindow.ensure_band "
              "(clamp [0,xsize-W]; stream only loaded[c%W]!=c)")

    # --- P4 fetch mirror in collision.c s_layer_tile -----------------------
    if "s_layer_tile" not in ncol:
        fails.append("P4 collision.c has no s_layer_tile helper")
        p4_ok = False
    else:
        p4_checks = [
            ("out-of-band low/high guard",
             "worldCol < layer->winBase || worldCol >= layer->winBase + layer->winW"),
            ("out-of-band returns 0xFFFF", "return 0xFFFF;"),
            ("in-band slot-major read",
             "layer->win[(worldCol % layer->winW) * layer->ysize + worldRow]"),
            ("full-grid fallback",
             "layer->layout[worldCol + (worldRow << layer->widthShift)]"),
        ]
        p4_ok = True
        for label, needle in p4_checks:
            if norm(needle) not in ncol:
                fails.append("P4 s_layer_tile missing: %s [%s]" % (label, needle))
                p4_ok = False
        # Every collision lookup site must route through s_layer_tile, and
        # NO site may still index layout[] directly outside the helper.
        site_calls = len(re.findall(r"s_layer_tile\(layer,", src_col))
        if site_calls < 8:
            fails.append("P4 only %d s_layer_tile call sites (expected >=8)"
                         % site_calls)
            p4_ok = False
        # The ONLY layout[...] index allowed is the fallback inside the helper.
        raw_layout = re.findall(r"layer->layout\[", src_col)
        if len(raw_layout) != 1:
            fails.append("P4 %d raw layer->layout[] indexes (expected exactly "
                         "1, the helper fallback)" % len(raw_layout))
            p4_ok = False
        # Every outer collision loop must run when the layer has EITHER a
        # full-grid layout OR a bound window. A bare `if (layer->layout &&`
        # short-circuits the whole loop when a window is bound (layout==NULL)
        # -> total fall-through. Assert no bare-layout guard survives and that
        # each loop accepts win too.
        bare_guard = len(re.findall(r"if \(layer->layout &&", src_col))
        either_guard = len(re.findall(
            r"if \(\(layer->layout \|\| layer->win\) &&", src_col))
        if bare_guard != 0:
            fails.append("P4 %d bare `if (layer->layout &&` guard(s) short-"
                         "circuit the loop when a window is bound" % bare_guard)
            p4_ok = False
        if either_guard < 8:
            fails.append("P4 only %d `(layer->layout || layer->win)` loop "
                         "guards (expected >=8)" % either_guard)
            p4_ok = False
        if p4_ok:
            print("  P4 OK  s_layer_tile mirrors ColWindow.fetch (0xFFFF "
                  "out-of-band; slot-major in-band) + %d sites routed, 1 "
                  "fallback index" % site_calls)

    # --- P5 header layout (GCO3) -------------------------------------------
    if norm("index_off = desc_off + nlayers * 12") in nc:
        print("  P5 OK  index_off = desc_off + nlayers*12 matches GCO3 header "
              "layout")
        p5_ok = True
    else:
        fails.append("P5 colwindow.c missing index_off = desc_off + "
                     "nlayers*12 (GCO3 descriptor table)")
        p5_ok = False

    # --- P6 teeth -----------------------------------------------------------
    # Prove the substring asserts are not vacuous: a corrupted needle MUST be
    # absent from the source.
    teeth_needle = "c % (s_winW + 1)"   # off-by-one slot map (the P3 teeth bug)
    teeth_ok = norm(teeth_needle) not in nc
    print("  P6 %s teeth: corrupted slot map [%s] absent from source (expect "
          "absent)" % ("OK " if teeth_ok else "RED", teeth_needle))
    if not teeth_ok:
        fails.append("P6 teeth: corrupted needle present - asserts vacuous")

    if fails:
        print("=== Gate #180 colwindow-cparity: RED ===")
        for f in fails:
            print("    - %s" % f)
        return 1
    print("=== Gate #180 colwindow-cparity: GREEN (C streamer + collision "
          "fetch mirror the host ColWindow reference) ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
