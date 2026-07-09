#!/usr/bin/env python3
"""qa_chain_titlecard.py -- GL1 (frontend_full_chain_parity, scene-6 row): the
"GREEN HILL ZONE" act card MUST slide in on the chain GHZCutscene->GHZ arrival.

RED baseline (2026-07-06, _shots/game-260706-203843.png): the chain reaches the
playable Green Hill Zone (Sonic on the FG, waterfall/grass/bridge) with NO title
card -- p6_w_tc_state never leaves -1 (card never spawned), the direct list emits
zero TitleCard polys.

The card is TRANSIENT (slides in over ~80 frames then away), so a single
savestate snapshot can miss it. This gate has two modes:

  --live  : poll the RUNNING RetroArch (UDP 55355). Waits until the chain reaches
            currentSceneFolder=="GHZ", then samples p6_w_tc_state (must advance
            past SETUPBG=0, i.e. the slide ran) and p6_w_tc_draw_faces (must be
            > 0 on at least one sample = the card emitted colored polys into the
            direct list). This is the STRUCTURAL witness gate.

  --shot PNG [PNG ...] : the AUTHORITATIVE visual check. Counts pixels matching
            the four TitleCard strip colors (blue/red/orange/green, decomp
            TitleCard.c L678-682) across the given landing-window screenshots.
            GREEN when the combined strip-color mass exceeds --min-strip-px on at
            least one frame (the card's diagonals are on screen).

Witness symbols (p6_wave1_reg.c, WRAM-H .bss; addr from game.map):
  p6_w_tc_state       int32  TC_STATE_* (-1 never spawned; >=1 = slid past setup)
  p6_w_tc_draw_faces  int32  direct-list poly cmds emitted by the card last draw

Usage:
  python tools/qa_chain_titlecard.py --live [--map game.map] [--secs 240]
  python tools/qa_chain_titlecard.py --shot _shots/foo.png [more.png ...]

Exit 0 = GREEN, 1 = RED, 2 = harness/parse error.
"""
from __future__ import annotations

import argparse
import importlib.util
import re
import sys
import time
from pathlib import Path

_HERE = Path(__file__).resolve().parent


def _load(mod, fn):
    spec = importlib.util.spec_from_file_location(mod, _HERE / fn)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)  # type: ignore[union-attr]
    return m


def map_symbol(map_text: str, name: str) -> int:
    m = re.search(r"0x([0-9a-fA-F]{16})\s+" + re.escape(name) + r"\s*$", map_text, re.M)
    if not m:
        raise KeyError(name)
    return int(m.group(1), 16)


# TitleCard strip colors. Two reference sets per strip:
#  - SOURCE: the decomp 8-bit RGB (TitleCard.c L678-682, non-Plus).
#  - RENDER: the ACTUAL on-chip colour MEASURED from the live chain build
#    (2026-07-06, _shots/game-260706-214050.png). The GHZ landing composites the
#    VDP1 card through an active VDP2 COLOUR OFFSET (the arrival FXRuby/fade
#    path, p6_io_main.cpp:7869 s_ovl.fade_fn -> p6_vdp2_fade_apply), which adds a
#    white bias -> the card renders pastel/washed vs the source RGB. That colour
#    wash is a separate, pre-existing VDP2 fade interaction; the STRIPS are
#    clearly present + hue-distinct (measured: strip mass 28378 on the card
#    frame vs 0 on the no-card landing). The gate matches EITHER reference so it
#    is robust to the wash yet stays RED on the no-card GHZ scenery.
STRIP_REFS = {
    "blue":   [(0x40, 0x60, 0xB0), (151, 168, 212)],
    "red":    [(0xF0, 0x50, 0x30), (247, 160, 142)],
    "orange": [(0xF0, 0x8C, 0x18), (247, 190, 129)],
    "green":  [(0x60, 0xC0, 0xA0), (168, 221, 203)],
    "yellow": [(0xF0, 0xC8, 0x00), (247, 225, 116)],  # card BG (also identifies the card)
}
# SOURCE-only refs (decomp RGB, RGB555-quantized). Used by --require-source-sat:
# after the saturation fix (clear the VDP2 colour offset while the card is up) the
# strips must match these, NOT the washed render points above. The washed render
# is a DISTINCT, well-separated colour (a white-biased pastel), so a strip mass
# measured against SOURCE-only refs is ~0 on the washed frame (RED) and large on
# the fixed frame (GREEN).
STRIP_REFS_SOURCE = {
    "blue":   [(0x40, 0x60, 0xB0)],
    "red":    [(0xF0, 0x50, 0x30)],
    "orange": [(0xF0, 0x8C, 0x18)],
    "green":  [(0x60, 0xC0, 0xA0)],
    "yellow": [(0xF0, 0xC8, 0x00)],
}
STRIP_COLORS = STRIP_REFS  # (kept name for the counts dict below)
# Tolerance covers RGB555 quantization (+-7/ch) + a little antialias slack;
# both reference points are far from the GHZ scenery (deep-navy sky, saturated
# foliage greens, brown ground), verified RED on the no-card frames.
COLOR_TOL = 16


def _close(px, refs, tol=COLOR_TOL):
    for ref in refs:
        if abs(px[0] - ref[0]) <= tol and abs(px[1] - ref[1]) <= tol and abs(px[2] - ref[2]) <= tol:
            return True
    return False


# GL1 GLYPH text-presence: the zone-name + ZONE letters render from the Display
# GCT. Their letter BODY has a DARK blue-gray gradient (MEASURED Display.gif idx
# 32..35 = (40,32,40),(56,48,64),(72,72,104),(88,112,144)); every letter uses
# these darker body slots (Name idx {33,35}, Zone idx {32,33,34,35}). Those DARK
# blue-grays appear NOWHERE ELSE on the card AND -- critically -- the arrival
# WHITE wash pushes colours toward WHITE, so it cannot manufacture them (MEASURED:
# the washed no-text frame _shots/214050 has 0 px matching these at tol<=14, vs
# 5489 false hits if the LIGHTER glyph slots 37..41 were used -- those collide
# with the pastel wash). So the dark subset is a collision-free, wash-robust
# "letters are drawn" signal that is a meta-QA-valid RED on the no-text frame.
GLYPH_BODY_REFS = [
    (40, 32, 40), (56, 48, 64), (72, 72, 104), (88, 112, 144),
]
GLYPH_TOL = 10


def _scan(px, w, h, refs_map, glyph_refs):
    counts = {k: 0 for k in refs_map}
    glyph_px = 0
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            c = px[x, y]
            for name, ref in refs_map.items():
                if _close(c, ref):
                    counts[name] += 1
                    break
            if glyph_refs is not None and _close(c, glyph_refs, GLYPH_TOL):
                glyph_px += 1
    return counts, glyph_px


def check_shots(paths, min_strip_px: int, require_text: bool, require_source_sat: bool,
                min_text_px: int) -> int:
    try:
        from PIL import Image
    except Exception:
        sys.stderr.write("qa_chain_titlecard: Pillow (PIL) required for --shot mode\n")
        return 2
    refs_map = STRIP_REFS_SOURCE if require_source_sat else STRIP_COLORS
    best = None
    best_counts = None
    for p in paths:
        pth = Path(p)
        if not pth.exists():
            sys.stderr.write(f"qa_chain_titlecard: missing shot {p}\n")
            continue
        im = Image.open(pth).convert("RGB")
        w, h = im.size
        px = im.load()
        counts, glyph_px = _scan(px, w, h, refs_map, GLYPH_BODY_REFS)
        strip_mass = counts["blue"] + counts["red"] + counts["orange"] + counts["green"]
        # rank by strip_mass + glyph_px so the best frame is the one that best
        # satisfies BOTH clauses (a card frame with letters up).
        score = strip_mass + (glyph_px if require_text else 0)
        if best is None or score > best:
            best = score
            best_counts = (pth.name, counts, strip_mass, glyph_px)
    if best_counts is None:
        sys.stderr.write("qa_chain_titlecard: no readable shots\n")
        return 2
    name, counts, strip_mass, glyph_px = best_counts
    print(f"  best frame: {name}")
    print(f"  strip refs: {'SOURCE-only (saturated)' if require_source_sat else 'source|washed'}")
    for k in ("blue", "red", "orange", "green", "yellow"):
        print(f"    {k:7s} px(strided) = {counts[k]}")
    print(f"  strip mass (b+r+o+g)   = {strip_mass}  (need > {min_strip_px})")
    ok = strip_mass > min_strip_px
    if require_text:
        print(f"  glyph-body px(strided) = {glyph_px}  (need > {min_text_px}) "
              f"[zone-name/ZONE letters present]")
        ok = ok and glyph_px > min_text_px
    print(f"qa_chain_titlecard[shot]: -> {'GREEN' if ok else 'RED'}")
    return 0 if ok else 1


def check_live(map_path: str, secs: float, host: str, port: int) -> int:
    qa_netmem = _load("qa_netmem", "qa_netmem.py")
    mcs = _load("mcs_extract", "mcs_extract.py")  # noqa: F841 (kept for symmetry)
    try:
        map_text = Path(map_path).read_text(errors="replace")
        sym_state = map_symbol(map_text, "p6_w_tc_state")
        sym_faces = map_symbol(map_text, "p6_w_tc_draw_faces")
        sym_folder = map_symbol(map_text, "RSDK::currentSceneFolder")
    except KeyError as e:
        sys.stderr.write(f"qa_chain_titlecard: symbol missing from map: {e}\n")
        return 2
    except FileNotFoundError:
        sys.stderr.write(f"qa_chain_titlecard: map not found: {map_path}\n")
        return 2

    mem = qa_netmem.RetroMem(host, port, timeout=2.0)

    def folder() -> str:
        try:
            raw = mem.read_saturn(sym_folder, 16)
        except Exception:
            return ""
        return raw.split(b"\0", 1)[0].decode("latin1", "replace")

    reached_ghz = False
    max_state = -1
    max_faces = 0
    t0 = time.time()
    last_print = 0.0
    while time.time() - t0 < secs:
        f = folder()
        if f == "GHZ":
            reached_ghz = True
            try:
                st = mem.read32_saturn(sym_state)
                fc = mem.read32_saturn(sym_faces)
                # int32 sign
                if st >= 0x80000000:
                    st -= 0x100000000
                if st > max_state:
                    max_state = st
                if 0 <= fc < 1000 and fc > max_faces:
                    max_faces = fc
            except Exception:
                pass
        now = time.time()
        if now - last_print > 3.0:
            print(f"  [{now - t0:5.1f}s] folder={f!r} tc_state(max)={max_state} tc_faces(max)={max_faces}")
            last_print = now
        # card is transient; sample fast
        time.sleep(0.05)

    if not reached_ghz:
        sys.stderr.write("qa_chain_titlecard: chain never reached currentSceneFolder=='GHZ' "
                         f"within {secs:.0f}s (is the live boot running + advancing?)\n")
        return 2
    # GREEN: the card spawned (state left -1) AND at least one frame emitted
    # colored polys (faces>0). A slid card advances state to >=1 (OPENINGBG+).
    spawned = max_state >= 0
    slid = max_state >= 1
    drew = max_faces > 0
    ok = spawned and slid and drew
    print(f"  reached GHZ    = {reached_ghz}")
    print(f"  tc_state (max) = {max_state}  ({'spawned' if spawned else 'NEVER SPAWNED'}; "
          f"{'slid' if slid else 'stuck at setup'})")
    print(f"  tc_faces (max) = {max_faces}  ({'drew polys' if drew else 'NO POLYS EMITTED'})")
    print(f"qa_chain_titlecard[live]: -> {'GREEN' if ok else 'RED'}")
    return 0 if ok else 1


def main(argv=None) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--map", default=str(_HERE.parent / "game.map"))
    p.add_argument("--live", action="store_true", help="poll running RetroArch witnesses")
    p.add_argument("--secs", type=float, default=240.0, help="live poll budget (chain reaches GHZ in ~2 min)")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=55355)
    p.add_argument("--shot", nargs="+", help="screenshot PNG(s) of the landing window")
    p.add_argument("--min-strip-px", type=int, default=200,
                   help="min combined strip-color pixels (strided) to pass --shot")
    p.add_argument("--require-text", action="store_true",
                   help="GL1 glyphs: also require zone-name/ZONE letter pixels present")
    p.add_argument("--min-text-px", type=int, default=60,
                   help="min glyph-body pixels (strided) to pass --require-text")
    p.add_argument("--require-source-sat", action="store_true",
                   help="GL1 saturation: strips must match SOURCE refs (RED on the washed render)")
    a = p.parse_args(argv)

    if a.shot:
        return check_shots(a.shot, a.min_strip_px, a.require_text, a.require_source_sat, a.min_text_px)
    if a.live:
        return check_live(a.map, a.secs, a.host, a.port)
    p.error("pass --live or --shot")
    return 2


if __name__ == "__main__":
    sys.exit(main())
