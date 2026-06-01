#!/usr/bin/env python3
"""qa_phase2_4i_asset_authenticity_gate.py - Phase 2.4i Task #154 RED gate.

Per `memory/decomp-assets-only-no-synthesis.md` (BINDING): every shipped
pixel/sample/byte MUST trace from extracted/Data/... (the Data.rsdk
extraction). Synthesizing or hand-crafting assets is forbidden. This gate
proves -- by CONTENT PROVENANCE, not filename -- that the two classes of
fabricated assets the user flagged are gone and replaced by authentic ones:

  Violation A -- HUD digits/glyphs were FABRICATED by tools/make_digit_font.py
                 (hand-drawn 8x8 glyphs -> cd/DIGITS.SPR). Replaced by the
                 authentic cd/HUD.SP2 + cd/HUD.MET atlas built from
                 extracted/Data/Sprites/Global/HUD.bin via build_entity_atlas.

  Violation B -- SFX PCMs were SYNTHESIZED by tools/make_audio.py (sum-of-sines).
                 Replaced by ffmpeg re-encodes of the authentic
                 extracted/Data/SoundFX/Global/*.wav via convert_audio.py.

  Violation C -- dead fabricated object sprites (cd/{SPRING,MONITOR,SIGNPOST}.SPR
                 from tools/make_object_sprites.py). Deleted (live code uses .SP2).

Predicates:

  P1  No synthesis scripts exist (make_audio.py, make_digit_font.py,
      make_object_sprites.py). Their continued presence is itself a
      provenance hazard regardless of filename.

  P2  No fabricated output files exist (cd/DIGITS.SPR, cd/STAGEBGM.PCM,
      cd/SPRING.SPR, cd/MONITOR.SPR, cd/SIGNPOST.SPR).

  P3  cd/HUD.SP2 + cd/HUD.MET exist, are valid SPR2/MET1, and CONTENT
      MATCHES a fresh build from extracted/Data/Sprites/Global/HUD.bin
      (byte-identical). Proves the HUD pixels came from the decomp atlas,
      not from a hand-drawn font.

  P4  Each LIVE SFX PCM (the 5 loaded by Entities.c entities_load_sfx:
      RING/JUMP/BREAK/STOMP/BOUNCE) CONTENT MATCHES a fresh convert_audio.py
      re-encode of its mapped extracted WAV (byte-identical). Proves the
      samples came from the decomp WAV, not a sine synthesizer. (HURT/LOSE
      are re-encoded too but are not live-loaded in the current build, so
      they are verified opportunistically if present.)

Usage:
    python tools/qa_phase2_4i_asset_authenticity_gate.py

Exit codes:
    0 = GREEN (every predicate satisfied)
    1 = RED  (at least one predicate failed)
"""
from __future__ import annotations
import os, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)

SPR2_MAGIC = b"SPR2"
MET1_MAGIC = b"MET1"

# --- Provenance maps -------------------------------------------------------

SYNTH_SCRIPTS = [
    "tools/make_audio.py",
    "tools/make_digit_font.py",
    "tools/make_object_sprites.py",
    "tools/make_badnik_sprite.py",
]

FABRICATED_OUTPUTS = [
    "cd/DIGITS.SPR",     # hand-drawn glyph font (Violation A)
    "cd/STAGEBGM.PCM",   # synthesized BGM, dead (Violation B)
    "cd/SPRING.SPR",     # dead fake object sprite (Violation C)
    "cd/MONITOR.SPR",    # dead fake object sprite (Violation C)
    "cd/SIGNPOST.SPR",   # dead fake object sprite (Violation C)
    "cd/BADNIK.SPR",     # procedural placeholder critter, dead (Violation D)
]

# HUD atlas provenance: which decomp bin + which anims to drop + frame caps.
# Mirrors the build_entity_atlas.py MANIFEST "HUD" entry exactly so the
# gate re-derives the SAME authentic atlas and byte-compares.
HUD_BIN   = "extracted/Data/Sprites/Global/HUD.bin"
HUD_DROP  = ["Player Name", "Got Through", "Act", "Game Over",
             "Time Over", "Competition", "Hyper Numbers"]
HUD_CAPS  = {"Numbers": 10}

# SFX provenance: cd PCM -> extracted WAV. Each entry cites the decomp
# RSDK.GetSfx / RSDK.PlaySfx site the mapping derives from.
# (live = loaded by src/mania/Objects/Common/Entities.c entities_load_sfx)
SFX_RATE = 22050
SFX_MAP = [
    # (cd_pcm, extracted_wav, live, decomp_cite)
    ("cd/RINGSFX.PCM",   "extracted/Data/SoundFX/Global/Ring.wav",     True,
     "Ring.c:109 Ring->sfxRing = GetSfx(Global/Ring.wav)"),
    ("cd/JUMPSFX.PCM",   "extracted/Data/SoundFX/Global/Jump.wav",     True,
     "Player.c:3327 PlaySfx(Player->sfxJump)"),
    ("cd/BREAKSFX.PCM",  "extracted/Data/SoundFX/Global/Destroy.wav",  True,
     "ItemBox.c:203/845 + Player.c:2509 PlaySfx(Destroy)"),
    ("cd/STOMPSFX.PCM",  "extracted/Data/SoundFX/Global/Destroy.wav",  True,
     "Player.c:2509 badnik-stomp PlaySfx(Destroy)"),
    ("cd/BOUNCESFX.PCM", "extracted/Data/SoundFX/Global/Spring.wav",   True,
     "Spring.c:131 Spring->sfxSpring = GetSfx(Global/Spring.wav)"),
    ("cd/HURTSFX.PCM",   "extracted/Data/SoundFX/Global/Hurt.wav",     False,
     "Player.c:3598 PlaySfx(Player->sfxHurt) (not live-loaded in current build)"),
    ("cd/LOSESFX.PCM",   "extracted/Data/SoundFX/Global/LoseRings.wav", False,
     "Player.c:3621 PlaySfx(Player->sfxLoseRings) (not live-loaded)"),
]


def p(s):
    print(s)


def rel(path):
    return os.path.join(REPO, path)


# --- P1 --------------------------------------------------------------------

def check_p1():
    p("P1: no synthesis scripts present")
    bad = [s for s in SYNTH_SCRIPTS if os.path.exists(rel(s))]
    for s in bad:
        p(f"    RED: synthesis script still present: {s}")
    if not bad:
        p("    GREEN: all 3 synthesis scripts removed")
    return not bad


# --- P2 --------------------------------------------------------------------

def check_p2():
    p("P2: no fabricated output files present")
    bad = [f for f in FABRICATED_OUTPUTS if os.path.exists(rel(f))]
    for f in bad:
        p(f"    RED: fabricated asset still present: {f}")
    if not bad:
        p("    GREEN: all 5 fabricated output files removed")
    return not bad


# --- P3 --------------------------------------------------------------------

def check_p3():
    p("P3: cd/HUD.SP2 + cd/HUD.MET authentic (byte-match fresh build from HUD.bin)")
    spr = rel("cd/HUD.SP2")
    met = rel("cd/HUD.MET")
    binp = rel(HUD_BIN)
    if not os.path.exists(spr) or not os.path.exists(met):
        p("    RED: cd/HUD.SP2 or cd/HUD.MET missing")
        return False
    if not os.path.exists(binp):
        p(f"    RED: source {HUD_BIN} missing -- cannot verify provenance")
        return False
    shipped_spr = open(spr, "rb").read()
    shipped_met = open(met, "rb").read()
    if shipped_spr[:4] != SPR2_MAGIC:
        p("    RED: cd/HUD.SP2 bad magic (not SPR2)")
        return False
    if shipped_met[:4] != MET1_MAGIC:
        p("    RED: cd/HUD.MET bad magic (not MET1)")
        return False
    # Re-derive the authentic atlas from the decomp bin into a temp dir,
    # then byte-compare. Any divergence => the shipped HUD is NOT a faithful
    # extraction of HUD.bin (i.e. synthesized / stale / wrong drop list).
    import build_entity_atlas as bea
    with tempfile.TemporaryDirectory() as td:
        ref_spr = os.path.join(td, "HUD.SP2")
        ref_met = os.path.join(td, "HUD.MET")
        bea.build_atlas(binp, ref_spr, ref_met,
                        drop_anims=HUD_DROP, frame_caps=HUD_CAPS)
        ref_spr_bytes = open(ref_spr, "rb").read()
        ref_met_bytes = open(ref_met, "rb").read()
    ok = True
    if shipped_spr != ref_spr_bytes:
        p(f"    RED: cd/HUD.SP2 ({len(shipped_spr)} B) != fresh build "
          f"({len(ref_spr_bytes)} B) -- pixels not from {HUD_BIN}")
        ok = False
    if shipped_met != ref_met_bytes:
        p(f"    RED: cd/HUD.MET ({len(shipped_met)} B) != fresh build "
          f"({len(ref_met_bytes)} B)")
        ok = False
    if ok:
        n = struct.unpack(">H", shipped_spr[4:6])[0]
        ac = struct.unpack(">H", shipped_met[4:6])[0]
        p(f"    GREEN: HUD.SP2/MET byte-identical to fresh build from "
          f"{HUD_BIN} ({n} frames, {ac} anims)")
    return ok


# --- P4 --------------------------------------------------------------------

def reencode_wav(wav, out_path, rate):
    """Run the project's authentic converter; returns the encoded bytes."""
    conv = rel("tools/convert_audio.py")
    rc = subprocess.call([sys.executable, conv, rel(wav),
                          "--out", out_path, "--rate", str(rate)],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if rc != 0 or not os.path.exists(out_path):
        return None
    return open(out_path, "rb").read()


def check_p4():
    p("P4: each SFX PCM authentic (byte-match convert_audio.py re-encode of decomp WAV)")
    # ffmpeg presence is a hard prerequisite for the re-encode comparison.
    import shutil
    if shutil.which("ffmpeg") is None:
        p("    RED: ffmpeg not on PATH -- cannot verify SFX provenance")
        return False
    ok = True
    with tempfile.TemporaryDirectory() as td:
        for cd_pcm, wav, live, cite in SFX_MAP:
            pcm_path = rel(cd_pcm)
            tag = "LIVE" if live else "aux "
            if not os.path.exists(pcm_path):
                if live:
                    p(f"    RED [{tag}] {cd_pcm} missing (cite: {cite})")
                    ok = False
                else:
                    p(f"    skip [{tag}] {cd_pcm} absent (not live; OK)")
                continue
            if not os.path.exists(rel(wav)):
                p(f"    RED [{tag}] source {wav} missing")
                ok = False
                continue
            ref = reencode_wav(wav, os.path.join(td, os.path.basename(cd_pcm)),
                               SFX_RATE)
            if ref is None:
                p(f"    RED [{tag}] convert_audio.py failed for {wav}")
                ok = False
                continue
            shipped = open(pcm_path, "rb").read()
            if shipped == ref:
                p(f"    GREEN [{tag}] {cd_pcm} == reencode({os.path.basename(wav)}) "
                  f"({len(shipped)} B) | {cite}")
            else:
                p(f"    RED [{tag}] {cd_pcm} ({len(shipped)} B) != reencode("
                  f"{os.path.basename(wav)}) ({len(ref)} B) -- not from decomp WAV")
                ok = False
    return ok


def main():
    p("=" * 68)
    p("Phase 2.4i asset-authenticity gate (provenance, not filename)")
    p("=" * 68)
    results = {
        "P1": check_p1(),
        "P2": check_p2(),
        "P3": check_p3(),
        "P4": check_p4(),
    }
    p("-" * 68)
    for k, v in results.items():
        p(f"  {k}: {'GREEN' if v else 'RED'}")
    allgreen = all(results.values())
    p("=" * 68)
    p(f"VERDICT: {'GREEN' if allgreen else 'RED'}")
    return 0 if allgreen else 1


if __name__ == "__main__":
    sys.exit(main())
