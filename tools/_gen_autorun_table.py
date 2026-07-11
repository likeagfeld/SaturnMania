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
    # signpost r4 C8b: the x14516 SPRING-LAUNCH cliff. The path to the signpost
    # runs on the UPPER deck (floor y288, x14566..15792) 656px ABOVE the lower
    # approach (floor y944). A single jump cannot clear 656px -- the authored
    # Spring (14516,1070) must launch Sonic up-and-over onto the deck. MEASURED:
    # spurious terrain-step waypoints (14336,984)/(14512,984)/(14512,1080)/
    # (14672,1080) made him JUMP at the cliff instead of running into the spring
    # at speed -> he loses horizontal momentum, bounces vertically in place, and
    # wedges airborne gvel=0 at x14502 (state=Player_State_Ground+0xD0 animID=10
    # onG=0, permanent). REFINED (r4 cycle 2 measure): the lower platform is FG-
    # High y1088 up to x14514, where it STEPS UP 48px to y1040 -- the Spring
    # (14516,1070) sits ON the y1040 ledge. A 48px step > the 16px step-up limit
    # blocks a running Sonic at x14499 (MEASURED oscillation x14355<->14499,
    # never reaching the spring). He must: (a) keep speed on the y1088 approach
    # (suppress the too-early LEAD=96 jumps that fire at x14416), THEN (b) jump
    # EXACTLY at the x14514 step to mount the y1040 ledge and strike the spring.
    (14300, 14505, 1000, 1200, 0x0001),          # (a) preserve approach momentum
    (14505, 14520, 1030, 1100, (1 << 10)),       # (b) jump the 48px step -> spring
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
