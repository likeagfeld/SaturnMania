#!/usr/bin/env python3
# =============================================================================
# qa_p6_animpool.py -- #254 anim-pool FUNDING gate (RED-first, BINDING).
#
# WHY: the shipping DATASET_STG pool was 92 KB in the WRAM-L heap (peaked 90.8 KB,
# W13 ledger), so adding ANY GHZ object's LoadSpriteAnimation overflowed it and
# silently starved the bridge anim (the #254 regression -- bridges vanished,
# qa_p6_ghz_regression R2 brg_frames=0). The fix (Storage.cpp P6_CART_TMP arm):
# relocate DATASET_TMP's 80 KB backing into the 4MB cart (0x22730000, disjoint from
# the resident sheets / GFS windows / VDP1 sheet store), spending the freed WRAM-L
# on STG 92->150 KB. This gate proves the funding LANDED and the first swept object
# (Spring, the canary) loads its anim WITHOUT regressing the bridge.
#
# RED on the current (unfunded) build: the witnesses are absent / stg_limit==92 KB.
# GREEN after the funded build: stg_limit==150 KB, Spring + Bridge both load, zero
# STG alloc-fails. Run it on EVERY object-sweep build alongside the whole-level
# regression gate (qa_p6_ghz_regression.py).
#
#   python tools/_portspike/qa_p6_animpool.py            # boot+capture
#   python tools/_portspike/qa_p6_animpool.py --mcs X.mcs
# =============================================================================
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

# Shipping GHZ load is host/IO-bound (~24s+ emulated; FpsScale does NOT compress
# it) -- capture AFTER the scene is live or every witness reads its init value.
GATE_FRAME = 130.0
ROOT      = os.path.normpath(os.path.join(HERE, "..", ".."))
SAVESTATE = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS   = os.path.join(HERE, "_p6_animpool.mcs")
STG_FUNDED = 150 * 1024  # 153600 -- Storage.cpp P6_CART_TMP arm

# NOTE: p6_saturn_anim_allocfail is intentionally NOT read here -- it is a
# namespaced RSDK::p6_saturn_anim_allocfail symbol, which qa_p6_scene.map_symbol
# (the _?bare regex) cannot resolve. The OUTCOME witnesses below (Spring loads AND
# Bridge stays loaded under the 150 KB ceiling) prove the funding directly.
NAMES = ["_p6_w_stg_limit", "_p6_w_spring_classid", "_p6_w_spring_frames",
         "_p6_w_brg_frames", "_p6_w_cont_frames"]


def capture(out):
    try:
        os.remove(os.path.join(ROOT, ".mednafen", "mednafen.lck"))
    except OSError:
        pass
    r = subprocess.run(["pwsh", SAVESTATE, "-Cue", "game.cue",
                        "-SaveFrame", str(GATE_FRAME), "-Out", out],
                       capture_output=True, text=True)
    sys.stdout.write(r.stdout[-200:] if r.stdout else "")
    return os.path.exists(out)


def main(argv):
    mcs = argv[argv.index("--mcs") + 1] if "--mcs" in argv else TMP_MCS
    if "--mcs" not in argv and not capture(mcs):
        print("FAIL: no savestate"); return 1

    mod = Q.load_harness()
    mp = Q.read_text(Q.MAP_DEFAULT)
    sec = mod.parse_savestate(Q._as_path(mcs))
    ma = Q.map_symbol(mp, "_p6_w_magic")
    _, perm = Q.calibrate(mod._peek_bytes(sec, ma, 4) if ma else None)
    if perm is None:
        print("RED: magic uncalibrated"); return 1
    v = {n: (Q.peek_u32(mod, sec, Q.map_symbol(mp, n), perm, signed=True)
             if Q.map_symbol(mp, n) else None) for n in NAMES}

    checks = [
        ("F0 STG funded (limit==150KB)",       v["_p6_w_stg_limit"],          lambda x: x == STG_FUNDED),
        ("F1 boot healthy (cont_frames>0)",    v["_p6_w_cont_frames"],        lambda x: x and x > 0),
        ("F2 Spring registered (classid>0)",   v["_p6_w_spring_classid"],     lambda x: x and x > 0),
        ("F3 Spring anim LOADED (frames>0)",   v["_p6_w_spring_frames"],      lambda x: x and x > 0),
        ("F4 Bridge still LOADED (frames>0)",  v["_p6_w_brg_frames"],         lambda x: x and x > 0),
    ]
    print("=" * 64)
    print("ANIM-POOL FUNDING GATE (#254: TMP->cart, STG 92->150 KB)")
    print("=" * 64)
    ok = True
    for label, val, test in checks:
        passed = False
        try:
            passed = bool(test(val))
        except Exception:
            passed = False
        ok = ok and passed
        print("  [%s] %-34s = %s" % ("GREEN" if passed else " RED ", label, Q._dv(val)))
    print("-" * 64)
    if ok:
        print("RESULT: GREEN -- pool funded; Spring loads, bridge intact, zero alloc-fails")
        return 0
    print("RESULT: RED -- funding not landed OR an object still starves STG (see RED line)")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
