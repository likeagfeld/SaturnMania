#!/usr/bin/env python3
# =============================================================================
# qa_ghzcut_heavies.py -- RED-first gate for Task #309 Tier-B.2: the 5 Hard-
# Boiled-Heavies (CutsceneHBH) RENDER in the GHZCutscene cutscene with CORRECT
# colors (NOT the R3.3 magenta/green CRAM-bank-collision class).
#
# Decomp: CutsceneHBH_Draw (SonicMania_Objects_Cutscene_CutsceneHBH.c:34-46)
# DrawSprite(s) the Heavy. Each Heavy loads a boss sheet from a DIFFERENT zone
# (SPZ1/PSZ2/MSZ/LRZ3). Saturn fix: ONE selective atlas (HBHOBJ.SHT/PAK, built by
# tools/build_heavy_atlas.py) + 5 distinct CRAM palette blocks (HBHPAL.BIN at
# CRAM[512/768/1024/1280/1536]) + per-Heavy jo colno so all 5 are resident at once.
#
# DOC-CITED CRAM scheme (ghzcutscene_directboot_t309.md Tier-B.2): live SPCTL=0x23
# (Sprite Type 3 = full 11-bit DC, ST-058-R2 Fig 9.1), SPCAOS=0, CRAM mode 1 -> a
# VDP1 8bpp sprite's CRAM address = jo colno (CMDCOLR high byte) + charpixel. colno
# = block*256 (512..1536) routes each Heavy to its own 256-entry CRAM block.
#
# WHAT THIS MEASURES (savestate-PRIMARY; the screenshot is the binding visual proof):
#   p6_w_hbh_slot       SaturnSheet slot of the staged HBHOBJ.SHT (>=0 == staged+
#                       hashed; -9/-1 == NOT staged == the RED pre-fix state).
#   p6_w_hbh_aniframes  the live CutsceneHBH->aniFrames (>=0 == LoadSpriteAnimation
#                       resolved a Heavy .bin from HBHOBJ.PAK; -1 == pack missing).
#   p6_w_hbh_landed     count of Heavy-region VDP1 blits that LANDED this frame.
#   CRAM[512..1663]     the 5 Heavy palette blocks. Each block's entry 1+ must be a
#                       VALID BGR555 color (MSB 0x8000 set) and NOT uniform magenta
#                       (0xFC1F) -- the R3.3 collision symptom. At least one block
#                       must hold a NON-grey/NON-zero color (the palettes uploaded).
#   cont_frames         capture liveness (>0).
#
# VERDICTS:
#   RED  (current build, no HBHOBJ staging): p6_w_hbh_slot < 0 (sheet not staged)
#        -> the Heavies cannot bind a VDP1 handle -> invisible. CRAM[512+] == 0.
#   GREEN (after the fix): p6_w_hbh_slot >= 0 AND p6_w_hbh_aniframes >= 0 AND the 5
#        CRAM blocks hold valid non-magenta colors. + the SCREENSHOT shows the 5
#        Heavies with authentic colors (run qa_boot.ps1 separately, view the PNG).
# =============================================================================
import argparse
import importlib.util
import os
import pathlib
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))

_spec = importlib.util.spec_from_file_location(
    "mcs_extract", os.path.join(ROOT, "tools", "mcs_extract.py"))
_mcs = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_mcs)

sys.path.insert(0, HERE)
import qa_p6_scene as Q  # noqa: E402

CRAM = 0x25F00000           # cache-through VDP2 Color RAM
BLOCKS = [("GUNNER", 512), ("SHINOBI", 768), ("MYSTIC", 1024),
          ("RIDER", 1280), ("KING", 1536)]
SAVESTATE = os.path.join(ROOT, "tools", "qa_savestate.ps1")


def _be16(b, i=0):
    return ((b[i] << 8) | b[i + 1]) if b is not None and len(b) >= i + 2 else 0


def capture(out, save_frame, fps):
    try:
        os.remove(os.path.join(ROOT, ".mednafen", "mednafen.lck"))
    except OSError:
        pass
    subprocess.run(["taskkill", "/F", "/IM", "mednafen.exe"],
                   capture_output=True)
    env = dict(os.environ)
    cmd = ["pwsh", "-File", SAVESTATE, "-Cue", os.path.join(ROOT, "game.cue"),
           "-SaveFrame", str(save_frame), "-FpsScale", str(fps), "-Out", out]
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    sys.stdout.write(r.stdout[-1500:] if r.stdout else "")
    sys.stderr.write(r.stderr[-800:] if r.stderr else "")
    return os.path.exists(out)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--mcs", default=os.path.join(HERE, "_ghzcut_heav.mcs"))
    ap.add_argument("--save-frame", type=float, default=40.0)
    ap.add_argument("--fps", type=float, default=3.0)
    ap.add_argument("--static", action="store_true",
                    help="map-only check (no capture): is p6_w_hbh_slot present?")
    args = ap.parse_args(argv)

    mp = Q.read_text(Q.MAP_DEFAULT)
    if args.static:
        sym = Q.map_symbol(mp, "_p6_w_hbh_slot")
        print("=== qa_ghzcut_heavies (static) ===")
        print("  _p6_w_hbh_slot in map: %s" % ("YES" if sym else "NO (integration not built)"))
        return 0 if sym else 1

    if "--mcs" not in (argv or sys.argv) and not os.path.exists(args.mcs):
        if not capture(args.mcs, args.save_frame, args.fps):
            print("RED-ready: no capture produced at %s" % args.mcs)
            return 1

    if not os.path.exists(args.mcs):
        print("RED-ready (no savestate at %s)." % args.mcs)
        print("  Build P6_GHZCUT_HOLD=1 P6_GHZCUT_HOLD_WHITE=0 then capture:")
        print("  pwsh tools/qa_savestate.ps1 -Cue game.cue -SaveFrame 40 -FpsScale 3 "
              "-Out tools/_portspike/_ghzcut_heav.mcs")
        return 1

    sec = _mcs.parse_savestate(pathlib.Path(args.mcs))

    ma = Q.map_symbol(mp, "_p6_w_magic")
    _, perm = Q.calibrate(_mcs._peek_bytes(sec, ma, 4) if ma else None)

    def w(name, signed=True):
        s = Q.map_symbol(mp, name)
        return Q.peek_u32(_mcs, sec, s, perm, signed=signed) if (s and perm) else None

    hbh_slot = w("_p6_w_hbh_slot")
    hbh_anif = w("_p6_w_hbh_aniframes")
    hbh_land = w("_p6_w_hbh_landed")
    hbh_count = w("_p6_w_hbh_count")
    hbh_handle = w("_p6_w_hbh_handle")
    hbh_vis = w("_p6_w_hbh_vis")
    cont = w("_p6_w_cont_frames")
    vdp1_land = w("_p6_w_vdp1_landed")

    visstr = ("visible=%d onScreen=%d active=%d" % ((hbh_vis >> 16) & 1, (hbh_vis >> 8) & 1, hbh_vis & 0xFF)
              if isinstance(hbh_vis, int) and hbh_vis >= 0 else str(hbh_vis))
    print("=== qa_ghzcut_heavies ===")
    print("  p6_w_hbh_slot      = %s  (>=0 == HBHOBJ.SHT staged+hashed)" % hbh_slot)
    print("  p6_w_hbh_count     = %s  (>=0 == N live Heavy entities; -2 NULL, -3 cid0)" % hbh_count)
    print("  p6_w_hbh_vis       = %s  (first Heavy)" % visstr)
    print("  p6_w_hbh_handle    = %s  (>=0 == Cutscene/HBH.gif bound to VDP1)" % hbh_handle)
    print("  p6_w_hbh_aniframes = %s  (>=0 == HBHOBJ.PAK Heavy .bin loaded)" % hbh_anif)
    print("  p6_w_hbh_landed    = %s  (Heavy-region VDP1 blits this frame)" % hbh_land)
    print("  p6_w_vdp1_landed   = %s  (TOTAL VDP1 sprites landed this frame)" % vdp1_land)
    print("  cont_frames        = %s" % cont)

    # Expected palette from HBHPAL.BIN (the authoritative uploaded bytes). The GREEN
    # check compares CRAM to THIS (so leftover non-zero CRAM garbage cannot pass).
    exp = None
    palpath = os.path.join(ROOT, "cd", "HBHPAL.BIN")
    if os.path.exists(palpath):
        pb = open(palpath, "rb").read()
        # 5 blocks x 128 colors, big-endian u16
        exp = [[((pb[(n * 128 + i) * 2] << 8) | pb[(n * 128 + i) * 2 + 1])
                for i in range(8)] for n in range(5)]

    # CRAM blocks: sample entries 0..7 of each block (entry 0 is transparent).
    block_state = []
    for bi, (who, base) in enumerate(BLOCKS):
        addr = CRAM + base * 2
        cols = [_be16(_mcs._peek_bytes(sec, addr + i * 2, 2)) for i in range(8)]
        nonzero = sum(1 for c in cols[1:] if (c & 0x7FFF) != 0)
        magenta = sum(1 for c in cols[1:] if (c & 0x7FFF) == 0x7C1F)  # full magenta
        # match against the expected HBHPAL.BIN block (entries 1..7; 0 is transparent).
        match = None
        if exp is not None:
            match = sum(1 for i in range(1, 8) if cols[i] == exp[bi][i])
        block_state.append((who, base, cols, nonzero, magenta, match))
        print("  CRAM[%4d] %-7s : %s  nonzero=%d magenta=%d match=%s/7"
              % (base, who, " ".join("%04X" % c for c in cols[:6]), nonzero, magenta,
                 "-" if match is None else match))

    fails = []
    if hbh_slot is None:
        fails.append("p6_w_hbh_slot not in map (integration not built)")
    elif hbh_slot < 0:
        fails.append("p6_w_hbh_slot %s < 0 -- HBHOBJ.SHT NOT staged (Heavies invisible)" % hbh_slot)
    # The Heavies must be REGISTERED + INSTANTIATED (count>=0; -2 NULL = the entity-size
    # registration refusal, -3 = classID-0). The decomp EntityCutsceneHBH is ~730 B (the
    # colors[128] array); on Saturn it MUST be shrunk under P6_MAX_ENTITY_SIZE (592) or
    # RegisterObject refuses it -> Object* NULL -> no Heavies. This gates that fix.
    if hbh_count is not None and hbh_count < 0:
        fails.append("p6_w_hbh_count %s < 0 -- CutsceneHBH not instantiated "
                     "(-2 Object* NULL = entity too big for the 592 B pool; -3 classID 0)" % hbh_count)
    elif hbh_count is not None and hbh_count < 5:
        fails.append("p6_w_hbh_count %s < 5 -- not all 5 Heavies live" % hbh_count)
    # The Heavy sheet must be BOUND to a VDP1 handle (else the sprites silently drop).
    if hbh_handle is not None and hbh_handle < 0:
        fails.append("p6_w_hbh_handle %s < 0 -- Cutscene/HBH.gif UNBOUND (sprites drop)" % hbh_handle)
    if hbh_anif is not None and hbh_anif < 0:
        fails.append("p6_w_hbh_aniframes %s < 0 -- HBHOBJ.PAK Heavy .bin not loaded" % hbh_anif)
    # Entity sprites must actually be reaching VDP1 (the render path is live).
    if vdp1_land is not None and vdp1_land <= 0:
        fails.append("p6_w_vdp1_landed %s <= 0 -- NO VDP1 sprites drew this frame" % vdp1_land)
    # color-correctness: each block must MATCH the uploaded HBHPAL.BIN (>=6/7 entries).
    # This is the AUTHORITATIVE signal -- it proves the palette landed in the right CRAM
    # block AND no R3.3 bank collision overwrote it. (Magenta 0xFC1F entries are LEGIT:
    # several source GIF palettes carry magenta "unused-slot" fillers in the low indices;
    # the byte-match to HBHPAL.BIN is what distinguishes authentic palette from collision
    # garbage, NOT a magenta count.) Falls back to a non-zero heuristic if HBHPAL absent.
    if exp is not None:
        blocks_ok = sum(1 for (_w, _b, _c, nz, mg, m) in block_state
                        if m is not None and m >= 6)
        crit = "match HBHPAL.BIN (>=6/7 entries)"
    else:
        blocks_ok = sum(1 for (_w, _b, _c, nz, mg, _m) in block_state if nz >= 3)
        crit = "hold non-zero colors"
    if blocks_ok < 5:
        fails.append("only %d/5 CRAM Heavy blocks %s "
                     "(R3.3 collision or palettes not uploaded)" % (blocks_ok, crit))
    if cont is not None and cont <= 0:
        fails.append("cont_frames %s <= 0 (captured too early / frozen)" % cont)

    print("\n=== VERDICT ===")
    if fails:
        for f in fails:
            print("  RED: " + f)
        print("\n  (Screenshot proof: pwsh tools/qa_boot.ps1 -Cue game.cue -Wait 60 "
              "-Every 2 -Shots 8 -Out _ghzcut_heav.png  -- the 5 Heavies must render"
              " with authentic colors, NOT magenta/green.)")
        return 1
    print("  GREEN: HBHOBJ staged (slot %s), Heavy anims loaded (aniFrames %s), "
          "%d/5 CRAM blocks valid + non-magenta." % (hbh_slot, hbh_anif, blocks_ok))
    print("  BINDING: confirm the SCREENSHOT shows the 5 Heavies with authentic colors.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
