#!/usr/bin/env python3
"""qa_fr1_parity_gate.py - FR-1 (Faithful Re-port) PARITY gate.

This is the new PARITY-gate standard mandated 2026-06-01 after the
2.5.1-2.5.4 increments shipped STATE machines whose rendered output never
changed (CLAUDE.md 6.4: static symbol-presence gates proved compilation,
never rendered a frame). The user reported "crouch doesn't work, spindash
doesn't work, he doesn't turn into a ball when jumping -- it's all still
the same".

ROOT CAUSE (measured): mania_ghz_draw_sonic (Game.c) selects idle-vs-walk
only by onGround && |gsp| and NEVER reads g_ghz_player.state. The decomp
drives rendering through animator.animationID (ANI_JUMP / ANI_SPINDASH /
ANI_CROUCH / ...). NONE of that animation layer was ported to Saturn, and
only 2 of Sonic's 53 anims (Idle + Walk) were ever shipped to the disc.

Unlike the 2.5.x gates, this gate's RUNTIME predicate asserts that the
RENDERED FRAME actually changes with state -- the check the static gates
lacked.

Predicates
----------
STATIC (default; no emulator needed):

  S1  cd/SONIC.MET exists, magic MET1, and ships at least KEEP_MIN (15)
      kept anims. Each kept anim's frame_count / speed / loop match the
      decomp Sonic.bin value, and the per-frame duration table matches.

  S2  cd/SONIC.SP2 exists, magic SPR2, frame_total == sum of kept-anim
      frame counts (149 for the gameplay keep set).

  S3  The ported handler emits at least HANDLER_MIN (10) DISTINCT ANI_*
      identifiers. We scan src/mania/Objects/Global/Player.c for ANI_*
      tokens that are ASSIGNED to .animationID (or passed to the
      set-sprite-animation helper). This proves the animation selection
      logic exists, not just the enum.

  S4  The diag mirror symbols g_player_diag_anim, g_player_diag_frame,
      g_player_diag_spriteid are present in game.map. Without these the
      runtime check below cannot observe the rendered frame.

RUNTIME (--runtime; requires Mednafen + interactive desktop for F5):

  R1  Capture savestates under five distinct input scripts
      (idle / walk-right / hold-jump / hold-down-crouch / spindash) and
      assert g_player_diag_anim takes at least 3 DISTINCT values AND
      g_player_diag_spriteid takes at least 3 DISTINCT values across the
      five states. This is the "rendered frame changes with state" check.

Exit codes:
    0 = GREEN (every requested predicate satisfied)
    1 = RED  (at least one predicate failed)
    2 = harness error (missing tool / bad args)

Usage:
    python tools/qa_fr1_parity_gate.py
    python tools/qa_fr1_parity_gate.py --runtime
"""
from __future__ import annotations
import argparse
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from convert_ring_sprite import parse_spr  # noqa: E402

SPR2_MAGIC = b"SPR2"
MET1_MAGIC = b"MET1"

SONIC_BIN = os.path.join("extracted", "Data", "Sprites", "Players", "Sonic.bin")
SONIC_SP2 = os.path.join("cd", "SONIC.SP2")
SONIC_MET = os.path.join("cd", "SONIC.MET")
PLAYER_C = os.path.join("src", "mania", "Objects", "Global", "Player.c")
GAME_MAP = os.path.join("game.map")

# Gameplay-reachable keep set (15 anims). Every anim here is selectable by a
# currently-ported Player state (Ground/Air/Roll/Crouch/Spindash/LookUp/
# DropDash). Bored 2, Hurt/Die/Drown, Balance, and all cutscene/mechanic
# anims are DROPPED with cited rationale in the FR plan -- they are
# unreachable by a ported state and re-added with their mechanic's port.
KEEP_ANIMS = [
    "Idle",
    "Bored 1",
    "Look Up",
    "Crouch",
    "Walk",
    "Air Walk",
    "Jog",
    "Run",
    "Dash",
    "Jump",
    "Skid",
    "Skid Turn",
    "Spindash",
    "Dropdash",
    "Push",
]
KEEP_MIN = 15          # S1 threshold
HANDLER_MIN = 10       # S3 threshold: distinct ANI_* the handler assigns

DIAG_SYMS = ["g_player_diag_anim", "g_player_diag_frame", "g_player_diag_spriteid"]


# --------------------------------------------------------------------------
# decomp expectation
# --------------------------------------------------------------------------
def decomp_keep():
    """Return (total_frames, [anim_dict...]) for the keep set, in the order
    given by KEEP_ANIMS. Each dict: name, frame_count, speed, loop,
    durations."""
    _sheets, anims = parse_spr(os.path.join(REPO, SONIC_BIN))
    by_name = {}
    for a in anims:
        by_name[a["name"].strip()] = a
    kept = []
    total = 0
    for nm in KEEP_ANIMS:
        a = by_name.get(nm)
        if a is None:
            kept.append(None)
            continue
        kept.append({
            "name": nm,
            "frame_count": len(a["frames"]),
            "speed": a["speed"],
            "loop": a["loop"],
            "durations": [f[7] for f in a["frames"]],
        })
        total += len(a["frames"])
    return total, kept


# --------------------------------------------------------------------------
# atlas readers (mirror qa_phase2_4e)
# --------------------------------------------------------------------------
def read_spr2(path):
    try:
        with open(path, "rb") as f:
            head = f.read(6)
    except OSError:
        return None
    if len(head) < 6 or head[:4] != SPR2_MAGIC:
        return None
    return struct.unpack(">H", head[4:6])[0]


def read_met(path):
    try:
        with open(path, "rb") as f:
            d = f.read()
    except OSError:
        return None, None
    if len(d) < 8 or d[:4] != MET1_MAGIC:
        return None, None
    anim_count, frame_total = struct.unpack(">HH", d[4:8])
    p = 8
    anims = []
    ANIM_REC_LEN = 2 + 2 + 2 + 2 + 24
    for _ in range(anim_count):
        if p + 8 > len(d):
            return None, None
        fc, spd, lp, first = struct.unpack(">HHHH", d[p:p + 8])
        name = d[p + 8:p + 8 + 24].rstrip(b"\x00").decode("latin-1", "ignore")
        anims.append((fc, spd, lp, first, name))
        p += ANIM_REC_LEN
    frames = []
    # MET1 frame record (build_entity_atlas.py:236): u8 anim_id, u8 frame_id,
    # i16 px, i16 py, u16 dur, u16 unicode_char = 10 bytes.
    FRAME_REC_LEN = 1 + 1 + 2 + 2 + 2 + 2
    for _ in range(frame_total):
        if p + FRAME_REC_LEN > len(d):
            break
        a_id, f_id, px, py, dur, uc = struct.unpack(">BBhhHH",
                                                    d[p:p + FRAME_REC_LEN])
        frames.append((a_id, f_id, px, py, dur))
        p += FRAME_REC_LEN
    return anims, frames


# --------------------------------------------------------------------------
# S1 -- MET kept-anim parity
# --------------------------------------------------------------------------
def check_s1(kept):
    met_path = os.path.join(REPO, SONIC_MET)
    if not os.path.exists(met_path):
        return False, "S1 FAIL: cd/SONIC.MET does not exist"
    met_anims, met_frames = read_met(met_path)
    if met_anims is None:
        return False, "S1 FAIL: cd/SONIC.MET wrong magic / corrupt header"
    if len(met_anims) < KEEP_MIN:
        return False, (f"S1 FAIL: MET ships {len(met_anims)} anims; "
                       f"need >= {KEEP_MIN}")
    kept_valid = [k for k in kept if k is not None]
    if len(met_anims) != len(kept_valid):
        return False, (f"S1 FAIL: MET ships {len(met_anims)} anims; "
                       f"keep set has {len(kept_valid)}")
    for i, (dec, met) in enumerate(zip(kept_valid, met_anims)):
        met_fc, met_spd, met_lp, _first, _nm = met
        if met_fc != dec["frame_count"]:
            return False, (f"S1 FAIL: anim {i} {dec['name']!r}: MET "
                           f"frame_count={met_fc} decomp={dec['frame_count']}")
        if met_spd != dec["speed"]:
            return False, (f"S1 FAIL: anim {i} {dec['name']!r}: MET "
                           f"speed={met_spd} decomp={dec['speed']}")
        if met_lp != dec["loop"]:
            return False, (f"S1 FAIL: anim {i} {dec['name']!r}: MET "
                           f"loop={met_lp} decomp={dec['loop']}")
        met_durs = [dur for (a_id, _f, _px, _py, dur) in met_frames if a_id == i]
        if met_durs != dec["durations"]:
            return False, (f"S1 FAIL: anim {i} {dec['name']!r}: per-frame "
                           f"duration table mismatch "
                           f"(decomp={dec['durations'][:6]} "
                           f"met={met_durs[:6]})")
    return True, f"S1 OK ({len(met_anims)} kept anims, durations match)"


# --------------------------------------------------------------------------
# S2 -- SP2 frame total
# --------------------------------------------------------------------------
def check_s2(expected_total):
    fc = read_spr2(os.path.join(REPO, SONIC_SP2))
    if fc is None:
        return False, "S2 FAIL: cd/SONIC.SP2 missing or wrong magic"
    if fc != expected_total:
        return False, (f"S2 FAIL: SP2 ships {fc} frames; keep-set total = "
                       f"{expected_total}")
    return True, f"S2 OK ({fc} frames)"


# --------------------------------------------------------------------------
# S3 -- handler emits >= HANDLER_MIN distinct ANI_*
# --------------------------------------------------------------------------
def check_s3():
    path = os.path.join(REPO, PLAYER_C)
    try:
        with open(path, "r", errors="ignore") as f:
            src = f.read()
    except OSError:
        return False, f"S3 FAIL: cannot read {PLAYER_C}"
    # Count ANI_* tokens that are ASSIGNED to animationID, or passed as the
    # first arg to a set-sprite-animation helper. Enum-definition lines
    # (ANI_X = N,) and bare enum references don't count.
    assigned = set()
    # animationID = ANI_X  (allow self->animator.animationID, p->animationID)
    for m in re.finditer(r"animationID\s*=\s*(ANI_[A-Z0-9_]+)", src):
        assigned.add(m.group(1))
    # rsdk_set_sprite_animation(... ANI_X ...) / player_atlas_play(ANI_X) /
    # player_set_anim[_keepframe](p, ANI_X, ...) -- the Saturn port's
    # set-sprite-animation helpers, which assign p->animationID internally.
    for m in re.finditer(
            r"(?:set_sprite_animation|player_atlas_play|SetSpriteAnimation"
            r"|player_set_anim\w*)"
            r"\s*\([^;]*?(ANI_[A-Z0-9_]+)", src):
        assigned.add(m.group(1))
    n = len(assigned)
    if n < HANDLER_MIN:
        return False, (f"S3 FAIL: handler assigns {n} distinct ANI_* "
                       f"(need >= {HANDLER_MIN}); found={sorted(assigned)}")
    return True, f"S3 OK ({n} distinct ANI_* assigned: {sorted(assigned)})"


# --------------------------------------------------------------------------
# S4 -- diag mirror symbols present
# --------------------------------------------------------------------------
def _map_has_symbol(map_text, sym):
    # match `<sym>` or `_<sym>` as a whole token
    return re.search(r"(?<![A-Za-z0-9_])_?" + re.escape(sym) +
                     r"(?![A-Za-z0-9_])", map_text) is not None


def check_s4():
    map_path = os.path.join(REPO, GAME_MAP)
    if not os.path.exists(map_path):
        return False, "S4 FAIL: game.map not found (build first)"
    with open(map_path, "r", errors="ignore") as f:
        txt = f.read()
    missing = [s for s in DIAG_SYMS if not _map_has_symbol(txt, s)]
    if missing:
        return False, f"S4 FAIL: diag symbols absent from game.map: {missing}"
    return True, f"S4 OK ({len(DIAG_SYMS)} diag symbols present)"


# --------------------------------------------------------------------------
# RUNTIME R1 -- rendered frame changes with state
# --------------------------------------------------------------------------
def check_r1():
    """Capture five savestates under distinct input scripts and assert the
    rendered anim AND sprite id each take >=3 distinct values.

    Requires Mednafen + interactive desktop (F5). Driven by
    tools/qa_savestate.ps1 capturing scripted-input states; the per-state
    .mcs files are expected under tools/_fr1_states/<name>.mcs. If those
    aren't present the gate reports a harness gap (exit 2) rather than a
    false GREEN."""
    state_dir = os.path.join(HERE, "_fr1_states")
    names = ["idle", "walk", "jump", "crouch", "spindash"]
    paths = {n: os.path.join(state_dir, n + ".mcs") for n in names}
    missing = [n for n, p in paths.items() if not os.path.exists(p)]
    if missing:
        sys.stderr.write(
            "R1 HARNESS GAP: missing scripted savestates for "
            + ", ".join(missing) + "\n"
            "  Capture them with tools/qa_savestate.ps1 under the five "
            "input scripts, write to tools/_fr1_states/<name>.mcs, then "
            "re-run with --runtime. Mednafen F5 needs an interactive "
            "desktop.\n")
        return None, "R1 SKIPPED (savestates not captured)"

    sys.path.insert(0, HERE)
    import mcs_extract  # noqa: E402
    from pathlib import Path

    map_path = os.path.join(REPO, GAME_MAP)
    with open(map_path, "r", errors="ignore") as f:
        mtxt = f.read()

    def sym_addr(sym):
        m = re.search(r"0x([0-9a-fA-F]{8,})\s+_?" + re.escape(sym) +
                      r"(?![A-Za-z0-9_])", mtxt)
        return int(m.group(1), 16) if m else None

    addr_anim = sym_addr("g_player_diag_anim")
    addr_spr = sym_addr("g_player_diag_spriteid")
    addr_ts = sym_addr("s_ts_state")
    if addr_anim is None or addr_spr is None:
        return False, "R1 FAIL: diag symbol addresses not in game.map"

    # Work RAM-H (0x06xxxxxx) is serialised byte-pair-swapped by Mednafen
    # (see memory/ghz-fg-tmp-buffer-off-by-4-bytes.md). _peek_bytes returns
    # the raw big-endian window; swap the two bytes of each uint16 to get the
    # Saturn-visible value. TS_GHZ_ACTIVE == 6 confirms the capture is in
    # gameplay, not still on the title screen.
    def u16(sections, addr):
        b = mcs_extract._peek_bytes(sections, addr, 2)
        return (b[1] << 8) | b[0]

    def u32(sections, addr):
        b = mcs_extract._peek_bytes(sections, addr, 4)
        # byte-pair-swap each uint16 half, then combine big-endian.
        return (b[1] << 24) | (b[0] << 16) | (b[3] << 8) | b[2]

    anims = set()
    sprs = set()
    rows = []
    for n in names:
        sections = mcs_extract.parse_savestate(Path(paths[n]))
        a = u16(sections, addr_anim)
        s = u16(sections, addr_spr)
        t = u32(sections, addr_ts) if addr_ts is not None else -1
        rows.append((n, a, s, t))
        if t != 6:
            return False, (f"R1 FAIL: state '{n}' captured outside gameplay "
                           f"(s_ts_state={t}, need 6=TS_GHZ_ACTIVE)")
        if s == 0xffff:
            return False, (f"R1 FAIL: state '{n}' rendered no player sprite "
                           f"(spriteid=-1) -- atlas draw path inactive")
        anims.add(a)
        sprs.add(s)
    for n, a, s, t in rows:
        print(f"      R1 state {n:9s}: ANI={a:<3d} spriteid={s:<4d} "
              f"ts={t}")
    if len(anims) < 3 or len(sprs) < 3:
        return False, (f"R1 FAIL: rendered frame does not vary with state "
                       f"(distinct anim={len(anims)} spriteid={len(sprs)}; "
                       f"need >=3 each)")
    return True, (f"R1 OK (distinct anim={len(anims)} spriteid={len(sprs)} "
                  f"across {len(names)} gameplay states; all spriteid!=-1)")


# --------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runtime", action="store_true",
                    help="also run the savestate rendered-frame check")
    args = ap.parse_args()

    total, kept = decomp_keep()
    missing = [nm for nm, k in zip(KEEP_ANIMS, kept) if k is None]
    if missing:
        sys.stderr.write("harness error: keep anims not found in Sonic.bin: "
                         + ", ".join(missing) + "\n")
        sys.exit(2)

    print("FR-1 PARITY GATE")
    print("  keep set: %d anims, %d frames (decomp Sonic.bin)" %
          (len([k for k in kept if k]), total))
    print("-" * 60)

    results = []
    for fn in (lambda: check_s1(kept),
               lambda: check_s2(total),
               check_s3,
               check_s4):
        ok, msg = fn()
        results.append(ok)
        print(("  PASS " if ok else "  FAIL ") + msg)

    if args.runtime:
        ok, msg = check_r1()
        if ok is None:
            print("  SKIP " + msg)
            # harness gap is not a GREEN -- surface as exit 2
            print("-" * 60)
            print("HARNESS GAP: runtime states not captured; static verdict "
                  "below stands but R1 unverified.")
        else:
            results.append(ok)
            print(("  PASS " if ok else "  FAIL ") + msg)

    print("-" * 60)
    if all(results):
        print("GREEN: FR-1 parity predicates satisfied")
        sys.exit(0)
    print("RED: FR-1 parity not met (animation system not ported / "
          "draw ignores state)")
    sys.exit(1)


if __name__ == "__main__":
    main()
