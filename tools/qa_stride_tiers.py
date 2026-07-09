#!/usr/bin/env python3
# =============================================================================
# qa_stride_tiers.py -- RED-first gate for S1 (entity-stride TIER model).
#
# Plan: WHOLE_GAME_MASSPORT_PLAN.md S1 + s7.2. The Saturn entity pool today
# (Object.hpp:56-67) is a 2-tier dual-stride:
#     reserve [0,0x40)      WIDE   = ENTITY_WIDE_SIZE (556)
#     scene   [0x40,0x480)  NARROW = sizeof(EntityBase) (344)
#     temp    [0x480,0x4C0) WIDE   = 556
# RegisterObject REFUSES any class whose EntityXxx struct > ENTITY_WIDE_SIZE
# (Object.hpp:61-63). So every registerable class must fit 556 today -- and the
# census shows 23 classes exceed 556 (max UICreditsText 1056). Those classes
# cannot be registered until the wide tier rises to an x-wide tier (>=1056).
#
# This gate (offline, no build): unions docs/dropin_census.json struct_over_344
# across every scene -> the distinct over-344 classes and their MAX measured
# struct size, parses ENTITY_WIDE_SIZE + the NARROW width from Object.hpp, and
# asserts every over-344 class fits the CURRENT refusal threshold
# (ENTITY_WIDE_SIZE). It MUST be RED on the current tree (classes > 556 exist)
# and turns GREEN when S1 raises the x-wide tier to cover the measured max.
#
# It also prints the tier histogram (344<x<=556 "wide", 556<x<=1088 "x-wide",
# x>1088 "over") so S1 can size the x-wide tier from data, not a guess.
# =============================================================================
import json
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DROPIN = os.path.join(ROOT, "docs", "dropin_census.json")
OBJECT_HPP = os.path.join(ROOT, "rsdkv5-src", "RSDKv5", "RSDK", "Scene", "Object.hpp")

# Proposed S1 tier widths (the x-wide tier must cover the measured max; this is
# the design the GREEN side asserts toward). NARROW is read from the header.
PROPOSED_WIDE = 576       # covers Player 556 / GameOver 452 / ImageTrail 440
PROPOSED_XWIDE = 1088     # 0x440-aligned; covers UICreditsText 1056


def parse_int(tok):
    tok = tok.strip()
    return int(tok, 16) if tok.lower().startswith("0x") else int(tok)


def hpp_constants():
    txt = open(OBJECT_HPP, "r", encoding="utf-8", errors="replace").read()
    m = re.search(r"#define\s+ENTITY_WIDE_SIZE\s+\(?\s*(\d+)", txt)
    if not m:
        raise RuntimeError("could not parse ENTITY_WIDE_SIZE from Object.hpp")
    return {"ENTITY_WIDE_SIZE": int(m.group(1)), "NARROW": 344}  # sizeof(EntityBase)


def over344_union():
    """Union every scene's struct_over_344 map -> {class: max size seen}."""
    d = json.load(open(DROPIN, "r", encoding="utf-8"))
    out = {}

    def walk(o):
        if isinstance(o, dict):
            if "struct_over_344" in o and isinstance(o["struct_over_344"], dict):
                for k, v in o["struct_over_344"].items():
                    try:
                        sz = int(v)
                    except (TypeError, ValueError):
                        continue
                    out[k] = max(out.get(k, 0), sz)
            for v in o.values():
                walk(v)
        elif isinstance(o, list):
            for v in o:
                walk(v)

    walk(d)
    return out


def main():
    c = hpp_constants()
    WIDE = c["ENTITY_WIDE_SIZE"]
    NARROW = c["NARROW"]

    classes = over344_union()
    if not classes:
        print("RED: no struct_over_344 data found in %s" % DROPIN)
        return 1

    biggest = max(classes.values())
    over_wide = {k: v for k, v in classes.items() if v > WIDE}
    in_wide = {k: v for k, v in classes.items() if NARROW < v <= WIDE}
    over_xwide = {k: v for k, v in classes.items() if v > PROPOSED_XWIDE}

    print("=== qa_stride_tiers :: S1 entity-stride tier model ===")
    print("Object.hpp: NARROW(sizeof EntityBase)=%d  ENTITY_WIDE_SIZE(refusal)=%d"
          % (NARROW, WIDE))
    print("proposed S1 tiers: NARROW=%d WIDE=%d X-WIDE=%d" % (NARROW, PROPOSED_WIDE, PROPOSED_XWIDE))
    print("over-344 distinct classes=%d  biggest=%d (%s)"
          % (len(classes), biggest, max(classes, key=classes.get)))
    print("histogram: 344<x<=%d (fit WIDE)=%d ; %d<x<=%d (need X-WIDE)=%d ; x>%d (over)=%d"
          % (WIDE, len(in_wide), WIDE, PROPOSED_XWIDE, len(over_wide), PROPOSED_XWIDE, len(over_xwide)))

    print("\n-- classes that EXCEED the current refusal threshold %d (un-registerable today) --" % WIDE)
    for k, v in sorted(over_wide.items(), key=lambda kv: -kv[1]):
        print("  %-26s %5d" % (k, v))

    if over_xwide:
        print("\n!! classes EXCEEDING the proposed X-WIDE tier %d (tier must grow) !!" % PROPOSED_XWIDE)
        for k, v in sorted(over_xwide.items(), key=lambda kv: -kv[1]):
            print("  %-26s %5d" % (k, v))

    # ---- RED-first contract ----
    # Today every registerable class must fit ENTITY_WIDE_SIZE; classes > WIDE
    # are refused -> RED. S1 turns this GREEN by making the x-wide tier cover
    # `biggest` (and updating RegisterObject's refusal threshold to it).
    if over_wide:
        print("\nRED: %d class(es) exceed the current wide tier %d -> refused by RegisterObject"
              " (Object.hpp:61-63). Biggest=%d. S1 must raise the x-wide tier to >= %d."
              % (len(over_wide), WIDE, biggest, biggest))
        return 1
    if biggest > WIDE:
        print("\nRED: biggest over-344 class %d > wide tier %d." % (biggest, WIDE))
        return 1
    print("\nGREEN: every over-344 class (max %d) fits the wide tier %d." % (biggest, WIDE))
    return 0


if __name__ == "__main__":
    sys.exit(main())
