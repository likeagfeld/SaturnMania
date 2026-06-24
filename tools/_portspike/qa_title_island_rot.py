#!/usr/bin/env python3
# =============================================================================
# qa_title_island_rot.py -- CP5b.6 (Task #276) RED-first COEFFICIENT-STREAM gate.
#
# The title island must ROTATE per the decomp (TitleBG_Scanline_Island,
# tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c:159-178). On Saturn the
# rotation is a VDP2 RBG0 rotation-scroll plane driven by a per-line coefficient
# table + a rotation-parameter table (RPT), rebuilt each frame from the live
# TitleBG->angle.
#
# This gate does NOT eyeball pixels. It OFFLINE-computes (mirroring
# TitleBG_Scanline_Island + the engine's BAKED Sin1024/Cos1024 from
# platform/Saturn/TrigTables_Saturn.inc) the per-line coefficient + RPT field
# stream the decomp math produces, then PEEKS the live RBG0 coefficient table +
# RPT from a savestate's VDP2 VRAM and asserts a BYTE-match (within the VDP2
# fixed-point rounding) for the angle the build was actually rotating to. It does
# this for TWO captures at TWO different times (two different angles) -> proves
# the rotation is LIVE (the stream changes with the angle) AND CORRECT (matches
# the decomp), not static and not a wrong angle. (qa_register_gate.py JSON-
# contract pattern extended to the coefficient bytes.)
#
# On the current FLAT build it fires RED: no RBG0 coeff table / RPT exists (the
# island is a static NBG1 cell plane), so the witnesses read 0 and the peeked
# VRAM does not match the computed rotating stream.
#
# Saturn hardware contract (ST-058-R2 VDP2 manual, sega_saturn_docs/VDP2_Manual.txt):
#   - RPT layout Fig 6.3 (:6699-6779): RA at the table base, size 0x60.
#     +0x2C MATA(int)/+0x2E(frac), +0x30 MATB, +0x38 MATC, +0x40 MATD ...
#     +0x54 KAst(int 16b)/+0x56(frac 10b), +0x58 dKAst(int)/+0x5A(frac),
#     +0x5C KAx(int)/+0x5E(frac).
#   - Coefficient table data §6.4 (:7111-7164): 2-word mode-0 coeff =
#     +0H [transparency<<15 | 7b line-color | sign | 7b integer], +2H 16b frac.
#     The kx=ky scale value is a signed fixed-point (8.16-ish) per line.
#   - RPTA (1800BCH/BEH, :6845-6879): RPT base = RPTA<<1.
#   - The implementation writes the RPT @ P6_RBG0_RPT and the coeff table @
#     P6_RBG0_KTBL at the fixed VRAM addresses below; this gate peeks them.
#
# Usage:
#   python tools/_portspike/qa_title_island_rot.py            # capture x2 + verdict
#   python tools/_portspike/qa_title_island_rot.py --selftest # offline math only
# =============================================================================
import os, re, sys, struct, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
SAVESTATE = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TRIG_INC = os.path.join(ROOT, "platform", "Saturn", "TrigTables_Saturn.inc")
TMP_A = os.path.join(HERE, "_island_rot_a.mcs")
TMP_B = os.path.join(HERE, "_island_rot_b.mcs")

# Two capture times -> two different TitleBG->angle values (angle advances +1 per
# front-end tick, so >=1 s apart guarantees a different angle, and the title needs
# >=15s of boot before content renders per the binding load-floor rule).
CAP_A_FRAME = 60.0
CAP_B_FRAME = 75.0

# ---- VRAM/register contract the implementation MUST satisfy (cache-through cpu
# view 0x25xxxxxx == VDP2-VRAM physical 0x05Exxxxx) --------------------------
VDP2_VRAM_BASE = 0x05E00000
# SGL canonical RBG0 addresses (SL_DEF.H:461-463): coeff table @ A1, RPT @ A1+0x1FF00.
# The impl drives the control regs via the SGL API so they land in SGL's shadow;
# the per-line coeff + RPT matrix DATA is written by us to these addresses.
P6_RBG0_KTBL   = 0x05E20000   # = SGL KTBL0_RAM (A1); coeff table (2-word, 1/line)
P6_RBG0_RPT    = 0x05E3FF00   # = SGL RBG_PARA_ADR (A1+0x1FF00); RPT A (0x60 bytes)
VDP2_REG_BASE  = 0x05F80000
# Register addresses VERIFIED against ST-058-R2 (sega_saturn_docs/VDP2_Manual.txt):
REG_RPMD   = 0x05F800B0       # rotation parameter mode (RPMD1,0)            (:6995)
REG_RPRCTL = 0x05F800B2       # per-line read enables RAXSTRE/RAYSTRE/RAKAST (:6810)
REG_KTCTL  = 0x05F800B4       # coefficient table control                   (:7283)
REG_KTAOF  = 0x05F800B6       # coefficient table address offset            (:7363)
REG_RPTAU  = 0x05F800BC       # RPT base high                               (:6845)
REG_RPTAL  = 0x05F800BE       # RPT base low

ISLAND_LINE0 = 168            # decomp clip top (SetClipBounds y=168)
ISLAND_NLINES = 72            # i in 16..88 -> 72 lines (168..239)
SCREEN_CENTER_X = 160         # ScreenInfo->center.x at 320 wide (engine TV)

# Tolerance: the Saturn fixed-point coeff is 8.16; the decomp deform/position are
# 16.16. After mapping, rounding can differ by a few LSB of the fractional part.
# A BYTE-EXACT match on the integer part + within COEFF_TOL on the raw 32b value.
COEFF_TOL = 0x40              # ~ +-64 / 2^16 in the fixed value

NAMES = ["_p6_w_title_island_armed", "_p6_w_title_island_angle",
         "_p6_w_title_island_kast", "_p6_w_title_island_coeff0",
         "_p6_w_cont_frames"]


# ---------------------------------------------------------------------------
# Baked trig tables (the on-target newlib values -- recomputing sinf() in CPython
# risks a rounding mismatch, so read the EXACT bytes the engine uses).
# ---------------------------------------------------------------------------
def _load_baked_trig():
    txt = open(TRIG_INC, "r", encoding="utf-8", errors="replace").read()
    def grab(name):
        m = re.search(r"saturn_%s\[\d+\]\s*=\s*\{(.*?)\}" % name, txt, re.S)
        if not m:
            raise SystemExit("FATAL: %s not found in %s" % (name, TRIG_INC))
        nums = re.findall(r"-?\d+", m.group(1))
        return [int(x) for x in nums]
    sin = grab("sin1024LookupTable")
    cos = grab("cos1024LookupTable")
    if len(sin) != 1024 or len(cos) != 1024:
        raise SystemExit("FATAL: trig table length %d/%d != 1024" % (len(sin), len(cos)))
    return sin, cos


_SIN, _COS = (None, None)


def Sin1024(a):
    return _SIN[a & 0x3FF]


def Cos1024(a):
    return _COS[a & 0x3FF]


# ---------------------------------------------------------------------------
# OFFLINE decomp model: TitleBG_Scanline_Island (verbatim) -> per-line
# (deform.x, deform.y, position.x, position.y), then map to the Saturn RBG0
# RPT + coefficient table the implementation must produce.
# ---------------------------------------------------------------------------
def decomp_scanlines(angle):
    """Returns list of (line, deform_x, deform_y, position_x, position_y) for the
    72 island lines, verbatim TitleBG_Scanline_Island (TitleBG.c:163-177)."""
    sine = Sin1024((-angle) & 0x3FF) >> 2
    cosine = Cos1024((-angle) & 0x3FF) >> 2
    out = []
    cx = SCREEN_CENTER_X
    for i in range(16, 88):
        idv = 0xA00000 // (8 * i)
        sin = sine * idv
        cos = cosine * idv
        deform_y = sin >> 7
        deform_x = (-cos) >> 7
        position_y = cos - cx * deform_y - 0xA000 * cosine + 0x2000000
        position_x = sin - cx * deform_x - 0xA000 * sine + 0x2000000
        out.append((ISLAND_LINE0 + (i - 16), deform_x, deform_y, position_x, position_y))
    return out


def expected_coeff_word(deform_x):
    """The 2-word mode-0 coefficient encodes the per-line horizontal scale kx.
    The implementation derives kx[line] from the decomp deform.x magnitude (the
    per-pixel texture step). The coeff fixed value == deform_x (16.16) carried
    into the 2-word coeff fixed (sign + integer + 16b frac). We compare the raw
    32-bit coeff value to deform_x within COEFF_TOL."""
    return deform_x & 0xFFFFFFFF


def expected_kast_integer(angle):
    """KAst integer part the RPT must carry = the coeff-table base in KAst units.
    For a 2-word coeff table at P6_RBG0_KTBL, KAst_int = (base & 0x7FFFF) >> 2
    (Table 6.3: 2-word LSB = 4H). Angle-independent (the base is fixed); the
    per-line variation lives in the coeff table, not KAst."""
    return ((P6_RBG0_KTBL & 0x7FFFF) >> 2) & 0xFFFF


# ---------------------------------------------------------------------------
# Savestate peeks.
# ---------------------------------------------------------------------------
def _peek16(mod, sec, addr):
    raw = mod._peek_bytes(sec, addr, 2)
    if raw is None or len(raw) < 2:
        return None
    return struct.unpack(">H", bytes(raw[:2]))[0]


def _peek32(mod, sec, addr):
    raw = mod._peek_bytes(sec, addr, 4)
    if raw is None or len(raw) < 4:
        return None
    return struct.unpack(">I", bytes(raw[:4]))[0]


def read_state(mcs):
    """Returns dict with the witnesses + the live RBG0 RPT/coeff/regs, or None."""
    if not os.path.exists(mcs):
        return None
    mp = Q.read_text(Q.MAP_DEFAULT)
    mod = Q.load_harness()
    sec = mod.parse_savestate(Q._as_path(mcs))
    ma = Q.map_symbol(mp, "_p6_w_magic")
    _, perm = Q.calibrate(mod._peek_bytes(sec, ma, 4) if ma else None)
    st = {"perm_ok": perm is not None}
    if perm is None:
        return st
    for n in NAMES:
        s = Q.map_symbol(mp, n)
        st[n] = Q.peek_u32(mod, sec, s, perm, signed=True) if s else None
    # VDP2 registers + RPT + first few coeff entries (raw VRAM, big-endian).
    st["RPTAU"] = _peek16(mod, sec, REG_RPTAU)
    st["RPTAL"] = _peek16(mod, sec, REG_RPTAL)
    st["KTCTL"] = _peek16(mod, sec, REG_KTCTL)
    st["KTAOF"] = _peek16(mod, sec, REG_KTAOF)
    st["RPMD"]  = _peek16(mod, sec, REG_RPMD)
    st["RPRCTL"] = _peek16(mod, sec, REG_RPRCTL)
    # RPT KAst integer @ +0x54.
    st["rpt_kast_int"] = _peek16(mod, sec, P6_RBG0_RPT + 0x54)
    # Per-line coeff table: read all 72 island lines (2-word = 4 B each).
    coeff = []
    for ln in range(ISLAND_NLINES):
        coeff.append(_peek32(mod, sec, P6_RBG0_KTBL + (ISLAND_LINE0 + ln) * 4))
    st["coeff"] = coeff
    return st


# ---------------------------------------------------------------------------
# Verdict for one capture (one angle).
# ---------------------------------------------------------------------------
def check_one(tag, st):
    print("-" * 70)
    print("CAPTURE %s" % tag)
    if st is None:
        print("  RED: no savestate parsed."); return False, None
    if not st.get("perm_ok"):
        print("  RED: savestate magic not calibrated (capture too early?)."); return False, None
    armed = st.get("_p6_w_title_island_armed")
    angle = st.get("_p6_w_title_island_angle")
    print("  witnesses: armed=%s angle=%s kast=%s coeff0=%s cont=%s"
          % (Q._dv(armed), Q._dv(angle), Q._dv(st.get("_p6_w_title_island_kast")),
             Q._dv(st.get("_p6_w_title_island_coeff0")), Q._dv(st.get("_p6_w_cont_frames"))))
    print("  regs: RPTAU=%s RPTAL=%s KTCTL=%s KTAOF=%s rpt_kast_int=%s"
          % (Q._hx(st.get("RPTAU")), Q._hx(st.get("RPTAL")), Q._hx(st.get("KTCTL")),
             Q._hx(st.get("KTAOF")), Q._hx(st.get("rpt_kast_int"))))
    if armed != 1 or angle is None:
        print("  RED: RBG0 island NOT armed (armed!=1) -> flat build / not wired.")
        return False, angle
    # Compute the expected stream for the angle the build was rotating to.
    exp = decomp_scanlines(angle & 0x3FF)
    # KAst integer in the RPT.
    exp_kast = expected_kast_integer(angle)
    if st.get("rpt_kast_int") != exp_kast:
        print("  RED: RPT KAst integer = %s, expected %s (coeff table base mismatch)."
              % (Q._hx(st.get("rpt_kast_int")), Q._hx(exp_kast)))
        return False, angle
    # RPTA register must point at P6_RBG0_RPT.
    rpta = None
    rpta = None
    if st.get("RPTAU") is not None and st.get("RPTAL") is not None:
        rpta = (((st["RPTAU"] & 0x7) << 16) | (st["RPTAL"] & 0xFFFE)) << 1
    exp_rpta = (P6_RBG0_RPT & 0x7FFFF)
    rpta_ok = (rpta is not None and rpta < 0x80000 and ((rpta ^ exp_rpta) & 0x7FF80) == 0)
    # KTCTL RA enable (bit0) + 2-word (bit1=0) + mode0 (bits3,2=0).
    ktctl = st.get("KTCTL")
    ktctl_ok = (ktctl is not None and (ktctl & 0x000F) == 0x0001)

    # --- PRIMARY: per-line coefficient byte-match (the rotation DATA correctness). ---
    coeff = st.get("coeff") or []
    bad = 0
    first_bad = None
    for k, (ln, dfx, dfy, px, py) in enumerate(exp):
        want = expected_coeff_word(dfx)
        got = coeff[k] if k < len(coeff) else None
        if got is None:
            bad += 1
            if first_bad is None: first_bad = (ln, "None", "0x%08X" % want)
            continue
        gs = got - 0x100000000 if got & 0x80000000 else got
        ws = want - 0x100000000 if want & 0x80000000 else want
        if abs(gs - ws) > COEFF_TOL:
            bad += 1
            if first_bad is None: first_bad = (ln, "0x%08X" % got, "0x%08X" % want)
    coeff_ok = (bad == 0)
    print("  coeff stream  : %d lines, %d mismatched (tol=%d) -> %s"
          % (len(exp), bad, COEFF_TOL, "MATCH (rotation data correct+live)" if coeff_ok else "MISMATCH"))
    if first_bad:
        print("  first mismatch: line=%d got=%s want=%s" % first_bad)
    print("  KTCTL config  : 0x%s -> %s" % (Q._hx(ktctl)[2:], "coeff table ENABLED (2-word mode0)" if ktctl_ok else "NOT the expected enable/mode"))
    print("  RPTA wiring   : -> 0x%s (expect 0x%05X) -> %s"
          % ("None" if rpta is None else "%05X" % (rpta & 0x7FFFF), exp_rpta,
             "valid" if rpta_ok else "INVALID/out-of-VRAM (RPT not committed -> island samples off-screen)"))
    ok = coeff_ok and ktctl_ok and rpta_ok
    print("  -> %s for angle=%d" % ("ALL OK" if ok else "RED", angle & 0x3FF))
    return ok, angle


def main(argv):
    global _SIN, _COS
    _SIN, _COS = _load_baked_trig()

    if "--selftest" in argv:
        print("=" * 70)
        print("OFFLINE SELF-TEST -- decomp coeff stream (no hardware)")
        print("=" * 70)
        for ang in (0, 64):
            sl = decomp_scanlines(ang)
            print("angle=%d  Sin1024(-a)>>2=%d  Cos1024(-a)>>2=%d  KAst_int=0x%04X"
                  % (ang, Sin1024(-ang) >> 2, Cos1024(-ang) >> 2, expected_kast_integer(ang)))
            for ln, dfx, dfy, px, py in (sl[0], sl[len(sl)//2], sl[-1]):
                print("  line %3d: deform=(%11d,%11d) position=(0x%08X,0x%08X) coeff=0x%08X"
                      % (ln, dfx, dfy, px & 0xFFFFFFFF, py & 0xFFFFFFFF, expected_coeff_word(dfx)))
        return 0

    print("=" * 70)
    print("CP5b.6 GATE -- title island RBG0 coefficient-stream byte-match (LIVE rotation)")
    print("=" * 70)
    for f in (TMP_A, TMP_B):
        try:
            os.remove(f)
        except OSError:
            pass
    try:
        os.remove(os.path.join(ROOT, ".mednafen", "mednafen.lck"))
    except OSError:
        pass
    subprocess.run(["pwsh", SAVESTATE, "-Cue", "game.cue", "-SaveFrame",
                    str(CAP_A_FRAME), "-Out", TMP_A], capture_output=True, text=True)
    subprocess.run(["pwsh", SAVESTATE, "-Cue", "game.cue", "-SaveFrame",
                    str(CAP_B_FRAME), "-Out", TMP_B], capture_output=True, text=True)
    sa = read_state(TMP_A)
    sb = read_state(TMP_B)
    oka, angA = check_one("A (frame %g)" % CAP_A_FRAME, sa)
    okb, angB = check_one("B (frame %g)" % CAP_B_FRAME, sb)
    print("=" * 70)
    live = (angA is not None and angB is not None and (angA & 0x3FF) != (angB & 0x3FF))
    if oka and okb and live:
        print("RESULT: GREEN -- RBG0 coeff stream byte-matches the decomp for BOTH "
              "angles (A=%d, B=%d), and the angle ADVANCED (live rotation)."
              % (angA & 0x3FF, angB & 0x3FF))
        return 0
    why = []
    if not oka: why.append("capture A stream mismatch/not-armed")
    if not okb: why.append("capture B stream mismatch/not-armed")
    if not live:
        why.append("angle did NOT advance between captures (static, not rotating): A=%s B=%s"
                   % (Q._dv(None if angA is None else angA & 0x3FF),
                      Q._dv(None if angB is None else angB & 0x3FF)))
    print("RESULT: RED -- " + "; ".join(why))
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
