#!/usr/bin/env python3
# =============================================================================
# qa_title_load.py -- Task #271 FIX gate: the per-scene TILESET-DECODE load cost.
#
# MEASURED ROOT CAUSE (savestate-attributed, p6_w_p2seeks per-step VBL split):
#   The front-end's 7.9 s phase-2 load is dominated by S8 LoadSceneFolder = 4.65 s,
#   and the 4.4 s of that is the SH-2 LZW decode of the 262,144-px tileset GIF in
#   RSDK::LoadStageGIF. The LZW prefix/suffix/stack tables (~12 KB) thrash the SH-2's
#   4 KB cache -> ~450 cycles/px. UNIVERSAL: every scene's 16x16Tiles.gif is the same
#   16x16384 (1024 tiles padded), so every scene pays ~4.4 s regardless of tiles used.
#
# THE FIX (this gate verifies):
#   tools/build_predecoded_tilesets.py pre-decodes every GIF OFFLINE -> raw index
#   plane + palette in cd/<TAG>{TIL,PAL}.BIN. The Saturn arm in LoadStageGIF memcpy's
#   the raw plane straight into tilesetPixels (ms) and converts the palette, skipping
#   the LZW entirely. The pre-decode is byte-identical to ReadGifPictureData (gate W2,
#   qa_p6_gif.py).
#
# WITNESSES (savestate peek, mcs_extract):
#   _p6_w_gif_b0   -- repurposed: 1 == the pre-decode path served the tileset
#                     (0 == LZW-decode fallback). The binary "fix active" signal.
#   _p6_w_gif_hash -- repurposed: bytes loaded from <TAG>TIL.BIN (>0 sanity).
#   _p6_w_p2seeks[4] -- cumulative phase-2 VBL at marks [0]=S7-end (phase-2 start),
#                       [1]=after S8 LoadSceneFolder, [2]=after S9, [3]=after S10.
#                       S8 = p2seeks[1]-p2seeks[0] is the tileset step this fix cuts.
#
# RED baseline (decode path, MEASURED this session before the fix):
#   gif_b0 = 0;  S8 = ~279 vbl (4.65 s);  phase-2 total = ~472 vbl (7.87 s).
# GREEN target (pre-decode path):
#   gif_b0 = 1;  S8 <= S8_CEIL_VBL (90 vbl = 1.5 s; Title TIL=110 KB ~= 0.73 s read).
#
#   T1 pre-decode path served the tileset (gif_b0 == 1)            RED 0 -> GREEN 1
#   T2 S8 LoadSceneFolder <= S8_CEIL_VBL                           RED 279 -> GREEN ~48
#   T3 TIL bytes loaded (gif_hash > 0)                             sanity
#   T4 title actually ran (cont_frames > MIN_FRAMES)               not-too-early
#   T5 settled render NOT regressed (fps >= RENDER_FLOOR_FPS)      load fix != render
#
#   python tools/_portspike/qa_title_load.py [savestate.mcs] [game.map]
# =============================================================================
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location(
    "qa_p6_scene", os.path.join(HERE, "qa_p6_scene.py"))
_scene = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_scene)

MCS_DEFAULT = os.path.join(HERE, "_loadtime.mcs")

S8_CEIL_VBL = 90          # 1.5 s -- RED 279 (4.65 s) -> GREEN ~48 (0.8 s)
P2_CEIL_VBL = 360         # 6.0 s phase-2 total -- RED 472 (7.87 s) -> GREEN ~241 (4.0 s)
MIN_FRAMES = 50
RENDER_FLOOR_FPS = 30.0


def _pk(mod, sec, mp, perm, name):
    a = _scene.map_symbol(mp, name)
    return _scene.peek_u32(mod, sec, a, perm, signed=True) if a else None


def _pk_arr(mod, sec, mp, perm, name, n):
    base = _scene.map_symbol(mp, name)
    if base is None:
        return None
    return [_scene.peek_u32(mod, sec, base + 4 * i, perm, signed=True) for i in range(n)]


def main(argv):
    import pathlib
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp_path = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("PRE-DECODED TILESET LOAD GATE  (Task #271 fix)")
    print("=" * 72)
    if not os.path.isfile(mp_path):
        print("RESULT: RED -- link map missing (%s)" % mp_path); return 1
    if not os.path.isfile(mcs):
        print("RESULT: RED -- savestate missing (%s)" % mcs); return 1

    mp = _scene.read_text(mp_path)
    mod = _scene.load_harness()
    sec = mod.parse_savestate(pathlib.Path(mcs))
    ma = _scene.map_symbol(mp, _scene.SYM_MAGIC)
    if ma is None:
        print("RESULT: RED -- magic symbol absent from map"); return 1
    _, perm = _scene.calibrate(mod._peek_bytes(sec, ma, 4))
    if perm is None:
        print("RESULT: RED -- magic mis-decode"); return 1

    cf = _pk(mod, sec, mp, perm, "_p6_w_cont_frames")
    gif_b0 = _pk(mod, sec, mp, perm, "_p6_w_gif_b0")
    gif_hash = _pk(mod, sec, mp, perm, "_p6_w_gif_hash")
    p2 = _pk_arr(mod, sec, mp, perm, "_p6_w_p2seeks", 4)
    spike = _pk(mod, sec, mp, perm, "_p6_w_perf_vbl_jo_max") or 0
    fr = _pk(mod, sec, mp, perm, "_p6_w_perf_frames")
    vb = _pk(mod, sec, mp, perm, "_p6_w_perf_vblanks")

    # per-step phase-2 breakdown
    s8 = s9 = s10 = p2tot = None
    if p2 and all(v is not None for v in p2):
        s8 = p2[1] - p2[0]
        s9 = p2[2] - p2[1]
        s10 = p2[3] - p2[2]
        p2tot = p2[3] - p2[0]

    render_fps = (60.0 * (fr - 1) / (vb - spike)) if (fr and vb and (vb - spike) > 0) else 0.0

    print("  cont_frames        = %s" % cf)
    print("  gif_b0 (predecode) = %s   (1 == pre-decode served the tileset)" % gif_b0)
    print("  gif_hash (TIL B)   = %s" % gif_hash)
    if p2tot is not None:
        print("  phase-2 marks vbl  = %s" % p2)
        print("  S8 LoadSceneFolder = %4d vbl = %5.2f s   <- tileset step (RED ~279)" % (s8, s8 / 60.0))
        print("  S9 LoadSceneAssets = %4d vbl = %5.2f s" % (s9, s9 / 60.0))
        print("  S10 InitObjects    = %4d vbl = %5.2f s" % (s10, s10 / 60.0))
        print("  phase-2 TOTAL      = %4d vbl = %5.2f s   (RED ~472 / 7.87 s)" % (p2tot, p2tot / 60.0))
    else:
        print("  phase-2 marks vbl  = %s  (p6_w_p2seeks absent -- T2 cannot evaluate)" % p2)
    print("  settled render fps = %.2f" % render_fps)
    print("-" * 72)

    t1 = (gif_b0 == 1)
    t2 = (s8 is not None and s8 <= S8_CEIL_VBL)
    t3 = (gif_hash is not None and gif_hash > 0)
    t4 = (cf is not None and cf > MIN_FRAMES)
    t5 = (render_fps >= RENDER_FLOOR_FPS)
    checks = [
        ("T1 pre-decode path served the tileset (gif_b0 == 1)", t1,
         "gif_b0=%s" % gif_b0),
        ("T2 S8 LoadSceneFolder <= %d vbl (%.1f s)" % (S8_CEIL_VBL, S8_CEIL_VBL / 60.0), t2,
         ("S8=%d vbl (%.2f s)" % (s8, s8 / 60.0)) if s8 is not None else "S8 unavailable"),
        ("T3 TIL bytes loaded (gif_hash > 0)", t3, "gif_hash=%s" % gif_hash),
        ("T4 title actually ran (cont_frames > %d)" % MIN_FRAMES, t4, "cont_frames=%s" % cf),
        ("T5 settled render NOT regressed (fps >= %.0f)" % RENDER_FLOOR_FPS, t5,
         "render_fps=%.2f" % render_fps),
    ]
    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- tileset served from pre-decode; S8 %d->%d vbl; render %.1f fps."
              % (279, s8, render_fps))
        return 0
    print("RESULT: RED -- tileset NOT served from the pre-decode (decode path / asset missing).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
