#!/usr/bin/env python3
# =============================================================================
# qa_p6_pool_compact.py -- I3b 2b COMPACTION runtime gate (RED-first).
#
# The DE-RISK milestone for the camera-local pool shrink: p6_pool_compact relocates
# EVERY populated scene entity into a dense physical pool [RESERVE, RESERVE+n) via the
# non-identity remap (byte-plan proven offline by qa_p6_pool_compact_model), reserves
# physical R+n as a classID=0 dummy, shifts temp 1:1 down, and flips p6_pool_scene_phys
# to n+1. This exercises the FULL streaming machinery (non-identity remap + relocation +
# inverse + loop1-physical) with ALL entities present, BEFORE the streaming shrink to
# 640+dormant. Correctness is proven by the bijection self-check (C3) AND the whole-level
# feature gate qa_p6_ghz_regression (R0-R16, run separately -- an entity corrupted by the
# relocation breaks its feature).
#
#   C1 boot healthy        cont_frames > 0
#   C2 compaction ran      0 < compact_n == pool_npop      (relocated the measured set)
#   C3 bijection ok        compact_bij_ok == 1             (remap/inv round-trip, all
#                          populated resolve live, dummy inert)  <-- the core proof
#   C4 pool shrunk         compact_sphys == compact_n+1 < SCENEENTITY_COUNT
#   C5 relocation moved    compact_lastP == RESERVE+compact_n-1  (the highest-LOGICAL
#                          populated entity landed at the last DENSE physical slot ->
#                          a high logical slot really was remapped to a low physical one)
#
# RED on HEAD (the compact_* witnesses are absent until 2b lands). GREEN after the build,
# with R0-R16 (qa_p6_ghz_regression) confirming no entity was corrupted by the relocation.
#
#   python tools/_portspike/qa_p6_pool_compact.py            # boot+capture
#   python tools/_portspike/qa_p6_pool_compact.py --mcs X.mcs
# =============================================================================
import os, re, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

GATE_FRAME = 130.0
ROOT      = os.path.normpath(os.path.join(HERE, "..", ".."))
SAVESTATE = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS   = os.path.join(HERE, "_p6", "_p6_compact.mcs")
OBJHPP    = os.path.join(ROOT, "rsdkv5-src", "RSDKv5", "RSDK", "Scene", "Object.hpp")

NAMES = ["_p6_w_cont_frames", "_p6_w_pool_npop", "_p6_w_compact_n", "_p6_w_compact_sphys",
         "_p6_w_compact_dummy", "_p6_w_compact_bij_ok", "_p6_w_compact_lastL", "_p6_w_compact_lastP"]


def live_define(name):
    txt = open(OBJHPP, "r", encoding="utf-8", errors="replace").read()
    m = re.search(r"^#define\s+" + re.escape(name) + r"\s+\(?\s*([0-9xXa-fA-F]+)", txt, re.M)
    return int(m.group(1), 0) if m else None


def capture(out):
    try:
        os.remove(os.path.join(ROOT, ".mednafen", "mednafen.lck"))
    except OSError:
        pass
    r = subprocess.run(["pwsh", SAVESTATE, "-Cue", "game.cue", "-SaveFrame", str(GATE_FRAME), "-Out", out],
                       capture_output=True, text=True)
    sys.stdout.write(r.stdout[-300:] if r.stdout else "")
    return os.path.exists(out)


def main(argv):
    mcs = argv[argv.index("--mcs") + 1] if "--mcs" in argv else None
    if mcs is None:
        mcs = TMP_MCS
        if not capture(mcs):
            print("FAIL: capture produced no savestate"); return 1

    R    = live_define("RESERVE_ENTITY_COUNT")
    SCN  = live_define("SCENEENTITY_COUNT")
    mod = Q.load_harness()
    map_text = Q.read_text(Q.MAP_DEFAULT)
    sections = mod.parse_savestate(Q._as_path(mcs))
    magic = Q.map_symbol(map_text, "_p6_w_magic")
    raw = mod._peek_bytes(sections, magic, 4) if magic else None
    _, perm = Q.calibrate(raw)
    if perm is None:
        print("RED: magic uncalibrated (image not loaded / captured too early)"); return 1

    v = {}
    for n in NAMES:
        a = Q.map_symbol(map_text, n)
        v[n] = Q.peek_u32(mod, sections, a, perm, signed=True) if a else None

    cont   = v["_p6_w_cont_frames"]
    npop   = v["_p6_w_pool_npop"]
    cn     = v["_p6_w_compact_n"]
    sphys  = v["_p6_w_compact_sphys"]
    dummy  = v["_p6_w_compact_dummy"]
    bij    = v["_p6_w_compact_bij_ok"]
    lastL  = v["_p6_w_compact_lastL"]
    lastP  = v["_p6_w_compact_lastP"]

    print("=== qa_p6_pool_compact (I3b 2b -- dense relocation of all populated scene entities) ===")
    print("  mcs           = %s" % os.path.basename(mcs))
    print("  cont_frames   = %s" % Q._dv(cont))
    print("  pool_npop     = %s   compact_n = %s   (relocated populated scene slots)" % (Q._dv(npop), Q._dv(cn)))
    print("  compact_sphys = %s   (NEW p6_pool_scene_phys; SCENEENTITY_COUNT=%s)" % (Q._dv(sphys), SCN))
    print("  compact_dummy = %s   bij_ok = %s" % (Q._dv(dummy), Q._dv(bij)))
    print("  lastL = %s -> lastP = %s   (expect lastP == RESERVE+compact_n-1 = %s)"
          % (Q._dv(lastL), Q._dv(lastP), (R + cn - 1) if (cn is not None and cn > 0) else "?"))
    print("-" * 64)

    if cn is None or bij is None:
        print("RED: compact_* witnesses absent -- 2b compaction not in this build (expected RED on HEAD).")
        return 1

    c1 = cont is not None and cont > 0
    c2 = cn > 0 and npop is not None and cn == npop
    c3 = bij == 1
    c4 = sphys is not None and sphys == cn + 1 and sphys < SCN
    c5 = lastP is not None and lastP == R + cn - 1
    for tag, ok, detail in [
        ("C1 boot healthy (cont>0)", c1, Q._dv(cont)),
        ("C2 compaction ran (0<compact_n==pool_npop)", c2, "%s==%s" % (Q._dv(cn), Q._dv(npop))),
        ("C3 bijection ok (bij_ok==1)", c3, Q._dv(bij)),
        ("C4 pool shrunk (sphys==n+1<%d)" % SCN, c4, Q._dv(sphys)),
        ("C5 relocation moved (lastP==R+n-1)", c5, "%s==%s" % (Q._dv(lastP), (R + cn - 1) if cn else "?")),
    ]:
        print("  [%s] %s = %s" % ("GREEN" if ok else " RED ", tag, detail))

    if c1 and c2 and c3 and c4 and c5:
        print("RESULT: GREEN -- all populated scene entities relocated into a dense physical pool via the")
        print("        non-identity remap; bijection holds; pool shrunk to n+1. Run qa_p6_ghz_regression")
        print("        (R0-R16) to confirm no entity was corrupted by the byte-copy.")
        return 0
    print("RESULT: RED -- compaction incomplete/incorrect (see the failing assertion).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
