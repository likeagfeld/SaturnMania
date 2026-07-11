#!/usr/bin/env python3
# _gen_autorun_table.py -- generate the P6_GHZ_AUTORUN scripted-input hazard
# table (tools/_portspike/_p6/p6_autorun_table.h) FROM THE SCENE MANIFEST
# (extracted/Data/Stages/GHZ/Scene1.bin via tools/_ghz1_obstacle_map.py).
# Data-driven per the signpost-campaign mandate: jump waypoints come from the
# authored obstacle coordinates, not trial-and-error. Diagnostic flavor only
# (the header is included solely under -DP6_GHZ_AUTORUN).
#
# Runtime contract (InputDevice_Saturn.cpp autorun block):
#   hold A while player.x in [h.x - LEAD, h.x - GAP] and (h.y - player.y) in
#   [-JUMP_DY_UP, JUMP_DY_DOWN] -- one press edge per window, sustained hold
#   for full jump height. Overrides are raw (x0,x1,y0,y1,buttons) boxes for
#   live-iteration fixes (buttons: bit10=A jump, bit15=SUPPRESS-RIGHT,
#   bit13=DOWN, bit12=UP -- matching P6_PAD_* in InputDevice_Saturn.cpp).
import json, subprocess, sys
from pathlib import Path

HAZ = {"Motobug", "Crabmeat", "Batbrain", "Newtron", "Spikes", "SpikeLog"}

rows = json.loads(subprocess.check_output(
    [sys.executable, "tools/_ghz1_obstacle_map.py", "--json"]).decode())

pts = [(r["x"], r["y"]) for r in rows if r["cls"] in HAZ]
# Chopper (run-1 iteration, 2026-07-10): choppers sit in the water below each
# bridge (authored y = bridge y + 216: (1184,1120) vs bridge (1184,904);
# (1180,1916) vs bridge (1184,1736)) and leap up THROUGH the planks to deck
# height. The runtime dy filter compares against the player's running height,
# so register the hazard at the LEAP APEX (authored y - 216 = the deck) --
# the player then jumps just before the chopper column while crossing.
pts += [(r["x"], r["y"] - 216) for r in rows if r["cls"] == "Chopper"]

# ---- terrain STEP waypoints (run-2 iteration, 2026-07-10) ------------------
# Measured stall: x2166 y876 Player_State_Ground anim17 (push) against the
# authored 32px step at x2180 (floor profile 896 -> 864 -> 832 at 2244;
# tools/_floor_profile.py from TileConfig.bin+Scene1.bin). Classic step
# physics stop a runner at any vertical step > the 16px step-up limit, so a
# traversal bot must jump. Generate a jump waypoint for EVERY upward step
# >= 24px found in the collision floor profile (all surfaces per column, both
# FG layers, plane A) -- fully data-driven, no hand placement.
import importlib.util
_fp_spec = importlib.util.spec_from_file_location("fp", "tools/_floor_profile.py")
fp = importlib.util.module_from_spec(_fp_spec)
import sys as _sys
_argv = _sys.argv; _sys.argv = ["fp"]  # suppress its __main__ CLI
_fp_spec.loader.exec_module(fp)
_sys.argv = _argv


def surfaces(wx, ytop=600, ybot=2100):
    """All floor surface ys in the column (top edge of each solid run)."""
    out = []
    for layer in ("FG Low", "FG High"):
        y = ytop
        while y < ybot:
            s = fp.col_floor_y(layer, wx, y, ybot, 0)
            if s is None:
                break
            out.append(s)
            # skip through the solid run: advance until a non-solid gap
            y = s + 24
    return sorted(set(out))

steps = []
prev = None
for wx in range(96, 15900, 8):
    cur = surfaces(wx)
    if prev is not None:
        for s1 in cur:
            # a surface at wx that is 24..120px HIGHER than a surface at wx-8
            # with no equal-height continuation = an upward step edge
            if any(24 <= (s0 - s1) <= 120 for s0 in prev) and not any(abs(s0 - s1) <= 8 for s0 in prev):
                steps.append((wx, s1 + 8))  # y just below the new deck edge
    prev = cur
# merge steps within 48px (double-steps -> one longer hold window)
merged = []
for x, y in steps:
    if merged and x - merged[-1][0] < 48 and abs(y - merged[-1][1]) < 96:
        continue
    merged.append((x, y))
print("terrain steps found: %d (merged %d)" % (len(steps), len(merged)))
pts += merged
pts.sort()

# ---- live-iteration overrides (x0, x1, y0, y1, buttons) --------------------
# buttons bit meanings mirror P6_PAD_*: A=1<<10 (jump), RIGHT-SUPPRESS uses
# 1<<15 (consumed by the autorun block as "do not force RIGHT here"),
# bit0 (0x0001) = SUPPRESS-JUMP (no scripted jump fires in the box).
# Empty until live iteration finds a spot the generic hazard-jump rule misses;
# each addition must cite the measured stall/death (x, y, cause) from the live
# position trace.
OVERRIDES = [
    # signpost r4 C8: GHZ1 rolling hill x5376-5636 (floor profile: FG-Low
    # y704 valley -> y512 crest -> y704, FG-High ramp y693->528 at x5508-5555).
    # The climbable slope carries a running Sonic up on momentum (MEASURED: the
    # runner reaches x5562 y612 unaided). But spurious hazard waypoints 136-320px
    # BELOW the deck (Spikes 5368,y820; terrain FG-High/water steps 5520,y1000+)
    # matched the +160 dy-DOWN filter and made him JUMP on the crest -> airborne
    # launch off the x5628 down-edge -> permanent x~5572 oscillation
    # (state=Player_State_Ground+0xD0, animID=10 air, onG=0). Suppress ALL
    # scripted jumps across the hill so he runs it through.
    (5340, 5640, 480, 760, 0x0001),
    # signpost r6 C8b/C9: the x14516 PLANE-SWITCH + spring section. ROUND-5
    # DEFINITIVE MECHANISM + r6 LIVE TRACE (tools/_gaptrace.py):
    #  - The Spring(14516,1070) type=2 flipFlag=1 is HORIZONTAL, velocity.x =
    #    -0xA0000 = LAUNCHES LEFT (a BARRIER, not a launcher). MEASURED live:
    #    stall gvel = -655360 = -0xA0000 exactly.
    #  - The r4 x14484 jump-box (1<<10) arced the runner INTO that leftward
    #    spring at x14437 -> apex y991 x14502 -> spring knocks him back to
    #    x14218 -> PERMANENT oscillation x14218<->14502 (MEASURED r6 trace,
    #    max_x pinned 14502). REMOVED.
    #  - The forward route is a PLANE-SWITCH section: PlaneSwitch objects at
    #    x14540/14672/14768/14800/14812/14820/14824/14840 flip the collision
    #    plane; the y848-896 deck (x14500-15092, continuous to SignPost y896 @
    #    x15092+) is on the plane the switch selects. The runner must arrive
    #    GROUNDED AT SPEED so the x14540 PlaneSwitch flips him onto that deck --
    #    NOT airborne from a jump into the spring.
    #  - r6 BREAKTHROUGH (live _floor_profile + _gaptrace): the low deck is NOT
    #    a dead-end. It is FG-High.A/B y1088 up to x14512, where it STEPS UP 96px
    #    to a y992 ledge (FG-High x14514-14572), which continues as FG-Low.A/B
    #    y992->y1040 from x14578 UNBROKEN to the SignPost (which itself sits on
    #    FG-Low.A/B y1088 at x15700-15844). The Spring(14516,1070) guards the
    #    y992 step with a leftward launch (y-hitbox [1054,1086]); a GROUNDED
    #    runner is bounced left at x14499 every pass (MEASURED r6 cycle-2:
    #    permanent oscillation x14246<->14499, gvel flips to -0xA0000 at x14494).
    #    The 96px step (y1088->y992) also exceeds the 16px step-up limit, so he
    #    must JUMP it regardless. His jump apex from y1088 is ~y992 = EXACTLY the
    #    ledge height, and rising above the spring y-hitbox (y<=1054) before he
    #    reaches x14516 avoids the leftward launch.
    #  - FIX (r6): (a) suppress ALL scripted jumps across the low-path approach
    #    x13300-14484 (the terrain steps at x13350/x14016/x14048/x14080/x14176
    #    that launched him airborne) so he runs grounded at full gvel; (b) a
    #    TIGHT jump box at x14486-14505 (just before the spring left edge x14499)
    #    so he hops the 96px step, clears the spring hitbox airborne, and lands
    #    on the y992 ledge -> continues on the y992/y1040 deck to the SignPost.
    #    The unstick fallback still fires inside the suppress box for small
    #    terrain-step wedges (r6 InputDevice fix). Diagnostic flavor only.
    #  - r6 cycle-4/5 MEASURED (high-rate _gaptrace): the spring y-hitbox reaches
    #    much HIGHER than r5's [1054,1086] -- it catches the player even airborne
    #    at body-center y1008 (bottom ~y1028). To clear it his body BOTTOM must be
    #    above ~y1011 (center ~y991) BY the time he reaches the spring x-left-edge
    #    x14499. Cycle-5 jumped at x14450 (~49px lead) -> only reached y1015 at
    #    x14499 -> body bottom y1035 still overlapped -> spring killed gvel at
    #    x14502 and launched him left (max_x pinned 14502). FIX (cycle-6): jump at
    #    x14400 (~99px lead) so ~22 frames of rise put his body bottom above y1011
    #    by x14499 -> clears the spring -> arc lands on the y992 ledge/y1040 deck
    #    (x14514-14572+). Suppress ends x14398 so it does not cancel the jump.
    (13300, 14398, 900, 1300, 0x0001),          # suppress-jump: run grounded to the pre-spring jump point
    (14400, 14442, 1000, 1110, (1 << 10)),      # jump ~99px before the spring -> clear it airborne -> y992 ledge
    # signpost r4 C8c: the x9718 recurrent DEATH (~1 of every 2 lives) traces to
    # the (8160,1272) terrain-step jump arcing the runner airborne over the y1216
    # valley into the y2048 pit at x9718. A valley-wide suppress box (8050-9300)
    # was TRIED (cycle 6) but WEDGED him against the BreakableWall (8400/8432,1184)
    # -- he needs running SPEED to break it, and suppress also disabled the
    # unstick. Reverted: the x9718 death stays STOCHASTIC (survivable -- he clears
    # it ~1 of 2 lives via respawn) rather than a hard stall. Left for a future
    # narrower fix (a jump at x9560-9640 keyed to the ACTUAL fall-onset y~1360,
    # not y1080-1300 which the cycle-5 attempt missed).
]

hdr = Path("tools/_portspike/_p6/p6_autorun_table.h")
lines = []
lines.append("/* AUTO-GENERATED by tools/_gen_autorun_table.py -- DO NOT HAND-EDIT.")
lines.append(" * GHZ1 hazard waypoints from extracted/Data/Stages/GHZ/Scene1.bin")
lines.append(" * (Scene.cpp:558-780 walk). Diagnostic P6_GHZ_AUTORUN flavor only. */")
lines.append("#ifndef P6_AUTORUN_TABLE_H")
lines.append("#define P6_AUTORUN_TABLE_H")
lines.append("typedef struct { unsigned short x, y; } P6HazardPt;")
lines.append("static const P6HazardPt p6_autorun_hazards[] = {")
for x, y in pts:
    lines.append("    { %5d, %5d }," % (x, y))
lines.append("};")
lines.append("enum { P6_AUTORUN_NHAZ = sizeof(p6_autorun_hazards) / sizeof(p6_autorun_hazards[0]) };")
lines.append("typedef struct { unsigned short x0, x1, y0, y1, buttons; } P6AutorunOverride;")
lines.append("static const P6AutorunOverride p6_autorun_overrides[] = {")
for x0, x1, y0, y1, b in OVERRIDES:
    lines.append("    { %5d, %5d, %5d, %5d, 0x%04x }," % (x0, x1, y0, y1, b))
lines.append("    { 0, 0, 0, 0, 0 }, /* terminator (keeps the array non-empty) */")
lines.append("};")
lines.append("enum { P6_AUTORUN_NOVR = sizeof(p6_autorun_overrides) / sizeof(p6_autorun_overrides[0]) - 1 };")
lines.append("#endif")
hdr.write_text("\n".join(lines) + "\n")
print("wrote %s: %d hazards, %d overrides" % (hdr, len(pts), len(OVERRIDES)))
