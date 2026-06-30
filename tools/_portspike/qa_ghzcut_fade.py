#!/usr/bin/env python3
# =============================================================================
# qa_ghzcut_fade.py -- RED-first gate for Task #309 Tier-B.1: the FXRuby fade
# (GHZCutscene arrival cutscene) renders as a Saturn VDP2 Color Offset hardware
# effect, NOT the invisible RSDKv5 software-framebuffer FillScreen.
#
# Decomp: FXRuby_Draw (SonicMania_Objects_Cutscene_FXRuby.c:32-58) FillScreen ->
# Drawing.cpp:586 writes the SW framebuffer the Saturn true-port never presents.
# Fix: drive the full-screen white/black wash via the VDP2 Color Offset Function
# (ST-058-R2 Chapter 13, p249-254 -- "Can also be used for fade-in and fade-out").
#
# WHAT THIS MEASURES (mirrors tools/_portspike/qa_p6_aiz_bg.py's VDP2-reg peek):
#   CLOFEN 0x25F80110  color-offset ENABLE  (N1COEN=bit1 NBG1, SPCOEN=bit6 SPR)
#   CLOFSL 0x25F80112  color-offset SELECT  (0 = use offset A)
#   COAR   0x25F80114  offset A RED   (9-bit signed; +255 white, -255 black)
#   COAG   0x25F80116  offset A GREEN
#   COAB   0x25F80118  offset A BLUE
#   p6_w_ghzcut_fade   engine witness = (fadeWhite<<16)|(fadeBlack&0xFFFF) the
#                      overlay read off the live FXRuby entity this frame.
#
# Verdicts:
#   RED  (current build, no fade code): CLOFEN==0 AND COAR==0 -> no offset ->
#        the FG/sprites render at full brightness, no wash.
#   GREEN (after the fix, with P6_GHZCUT_HOLD_WHITE=256 -> a full WHITE wash):
#        CLOFEN bit1 & bit6 set, COAR==COAG==COAB==+255 (0x00FF, positive 9-bit),
#        and p6_w_ghzcut_fade white==256 black==0.
#   --expect black : assert the BLACK wash instead (COA*==-255, witness black>0).
#   --expect any   : assert ANY non-zero offset is live (CLOFEN!=0 or COA*!=0).
#
# BINDING (skill gotcha #4): a VISUAL effect's gate must also confirm the on-
# screen symptom -- capture _ghzcut_fade_*.png alongside and verify the wash by
# eye / a luma measure. This gate is the quantitative register half.
# =============================================================================
import argparse
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))

_spec = importlib.util.spec_from_file_location(
    "mcs_extract", os.path.join(ROOT, "tools", "mcs_extract.py"))
_mcs = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_mcs)

sys.path.insert(0, HERE)
import qa_p6_scene as Q  # noqa: E402  (map_symbol/calibrate/peek_u32 helpers)

# VDP2 Color Offset registers (ST-058-R2 Chapter 13). Cache-through 0x25F8xxxx.
VDP2_CLOFEN = 0x25F80110
VDP2_CLOFSL = 0x25F80112
VDP2_COAR = 0x25F80114
VDP2_COAG = 0x25F80116
VDP2_COAB = 0x25F80118

# SGL screen-enable bits (SL_DEF.H:543,548): NBG1ON=1<<1, SPRON=1<<6.
N1COEN = 1 << 1
SPCOEN = 1 << 6


def _be16(b, i):
    return ((b[i] << 8) | b[i + 1]) if b is not None and len(b) >= i + 2 else 0


def _s9(v):
    """Interpret a VDP2 color-offset register's low 9 bits as signed."""
    v &= 0x1FF
    return v - 0x200 if (v & 0x100) else v


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("state")
    ap.add_argument("--expect", choices=["white", "black", "any"], default="white")
    args = ap.parse_args(argv)

    if not os.path.exists(args.state):
        print("RED-ready (no savestate at %s)." % args.state)
        print("  Gate in place; capture a HELD GHZCutscene state after the build:")
        print("  pwsh -File tools/qa_savestate.ps1 -Cue game.cue -SaveFrame 60 "
              "-Out tools/_portspike/_ghzcut_fade_hold.mcs")
        return 1

    import pathlib
    sec = _mcs.parse_savestate(pathlib.Path(args.state))

    clofen = _be16(_mcs._peek_bytes(sec, VDP2_CLOFEN, 2), 0)
    clofsl = _be16(_mcs._peek_bytes(sec, VDP2_CLOFSL, 2), 0)
    coar = _be16(_mcs._peek_bytes(sec, VDP2_COAR, 2), 0)
    coag = _be16(_mcs._peek_bytes(sec, VDP2_COAG, 2), 0)
    coab = _be16(_mcs._peek_bytes(sec, VDP2_COAB, 2), 0)
    coar_s, coag_s, coab_s = _s9(coar), _s9(coag), _s9(coab)

    # Engine witness (overlay-read FXRuby fade fields). Map symbol + calibrated perm.
    mp = Q.read_text(Q.MAP_DEFAULT)
    ma = Q.map_symbol(mp, "_p6_w_magic")
    _, perm = Q.calibrate(_mcs._peek_bytes(sec, ma, 4) if ma else None)
    fsym = Q.map_symbol(mp, "_p6_w_ghzcut_fade")
    packed = Q.peek_u32(_mcs, sec, fsym, perm) if (fsym and perm) else None
    fade_w = (packed >> 16) & 0xFFFF if packed is not None else None
    fade_b = (packed & 0xFFFF) if packed is not None else None
    cf_sym = Q.map_symbol(mp, "_p6_w_cont_frames")
    cont = Q.peek_u32(_mcs, sec, cf_sym, perm, signed=True) if (cf_sym and perm) else None

    en_nbg1 = bool(clofen & N1COEN)
    en_spr = bool(clofen & SPCOEN)

    print("=== qa_ghzcut_fade (expect=%s) ===" % args.expect)
    print("  CLOFEN @0x%08X = 0x%04X  (NBG1 bit1=%s, SPR bit6=%s)"
          % (VDP2_CLOFEN, clofen, "on" if en_nbg1 else "OFF",
             "on" if en_spr else "OFF"))
    print("  CLOFSL @0x%08X = 0x%04X  (offset %s)"
          % (VDP2_CLOFSL, clofsl, "B" if (clofsl & (N1COEN | SPCOEN)) else "A"))
    print("  COAR/COAG/COAB = 0x%04X/0x%04X/0x%04X  = signed %d/%d/%d"
          % (coar, coag, coab, coar_s, coag_s, coab_s))
    print("  p6_w_ghzcut_fade = %s  (fadeWhite=%s, fadeBlack=%s)"
          % ("None" if packed is None else "0x%08X" % (packed & 0xFFFFFFFF),
             fade_w, fade_b))
    print("  cont_frames      = %s" % cont)

    fails = []
    # Offset must be ENABLED on the two visible GHZCutscene layers (NBG1 + SPR).
    if not (en_nbg1 and en_spr):
        fails.append("CLOFEN 0x%04X missing NBG1(bit1) and/or SPR(bit6) enable"
                     % clofen)
    # Offset A selected for those screens (CLOFSL bit clear == use A).
    if clofsl & (N1COEN | SPCOEN):
        fails.append("CLOFSL 0x%04X selects offset B for NBG1/SPR (want A)" % clofsl)

    if args.expect == "white":
        # +255 (full white). Accept any strongly-positive equal RGB offset.
        if not (coar_s >= 200 and coag_s >= 200 and coab_s >= 200):
            fails.append("offset A RGB %d/%d/%d not a strong POSITIVE (white) wash"
                         % (coar_s, coag_s, coab_s))
        if fade_w is not None and fade_w <= 0:
            fails.append("witness fadeWhite %s <= 0 (no white wash live)" % fade_w)
    elif args.expect == "black":
        if not (coar_s <= -200 and coag_s <= -200 and coab_s <= -200):
            fails.append("offset A RGB %d/%d/%d not a strong NEGATIVE (black) wash"
                         % (coar_s, coag_s, coab_s))
        if fade_b is not None and fade_b <= 0:
            fails.append("witness fadeBlack %s <= 0 (no black wash live)" % fade_b)
    else:  # any
        if clofen == 0 and coar_s == 0 and coag_s == 0 and coab_s == 0:
            fails.append("no color-offset live at all (CLOFEN==0, COA*==0)")

    if cont is not None and cont <= 0:
        fails.append("cont_frames %s <= 0 (captured too early / frozen)" % cont)

    print("\n=== VERDICT ===")
    if fails:
        for f in fails:
            print("  RED: " + f)
        return 1
    print("  GREEN: FXRuby fade live via VDP2 color offset "
          "(CLOFEN=0x%04X, A-RGB=%d/%d/%d)" % (clofen, coar_s, coag_s, coab_s))
    return 0


if __name__ == "__main__":
    sys.exit(main())
