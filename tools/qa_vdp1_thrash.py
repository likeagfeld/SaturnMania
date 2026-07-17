#!/usr/bin/env python3
"""qa_vdp1_thrash.py -- RED/GREEN gate for the GHZ VDP1 slot-pool capacity
(Fix 2, user-symptom-map-v2-2026-07-17 R3: digits/ball/ring flashing).

ROOT (MEASURED live, settled GHZ, 2026-07-17): FRD layer CLEAN (239 lookups,
0 misses) -- the flashing is VDP1 SLOT-CACHE LRU THRASH: p6_w_vdp1_evicts =
6.10/frame with dl_cmds_max = 43 sprites/frame. ~6 textures re-stage every
frame, so a jid slot's pixel content alternates digit/ball/ring frame-to-frame
(title-vdp1-slot-thrash class). The alternating rects (HUD digits 9x14, ring
12x16, StarPost ball 16x16) ALL route to the GHZCUT TINY 16x20 bucket, which
had only P6_BK0 = 8 slots.

OFFLINE CAPACITY MODEL (this gate; the primary verifies live post-build):
  T1  TINY demand: enumerated from the ACTUAL anim bins (parse_spr on
      Global/{HUD,Ring,StarPost,Dust,ScoreBonus}.bin) -- worst concurrent
      distinct <=16x20 rects at settled GHZ play:
        10 digit glyphs (0-9 across score+time+rings+lives)
      +  1 HUD small element ('x' 9x13)
      +  1 placed-ring frame (global animator: all rings share 1 rect/frame)
      +  3 ring-collect sparkles (concurrent sparkles at different ages)
      +  3 StarPost (bulb + 2 star-spin rects)
      +  1 dust puff (16x20)
      +  2 ScoreBonus popups
      = 21 -> capacity must be >= ceil(21 * 1.2) = 26 (20% headroom).
  T2  64x80 bucket: measured chain worst fmax = 17 (#324 re-carve, GHZ
      landing) -> capacity >= 18 (no-regress).
  T3  VDP1 user-area budget: bucket VRAM sum + reserve <= 466,232 B
      (JO_VDP1_USER_AREA_SIZE). Reserve = TitleCard glyph cache worst
      (computed from Global/TitleCard.bin: 7 name letters + 4 zone letters +
      1 act number + 2 decorations, mult-8 padded) + 3 lazy fill sprites
      (16x16x2B = 512 B each) + 2 KB safety.

Exit 0 = GREEN, 1 = RED.
Live criteria for the primary (NOT checked here): d(p6_w_vdp1_evicts)/frame
<= 0.5 at settled GHZ (qa_live/RA harness); no digit-alternation on the
lamppost ball; p6_w_buck0_fmax <= P6_BK0. Post-build: qa_p6_mapoverlap
(_end < 0x060C8000; the +476 B slot .bss is front-end-flavor only).
"""
import math
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from convert_ring_sprite import parse_spr  # noqa: E402

V1 = (ROOT / "tools/_portspike/_p6/p6_vdp1.c").read_text(errors="replace")
SPRITES = ROOT / "extracted/Data/Sprites"
VDP1_USER_AREA = 466232  # JO_VDP1_USER_AREA_SIZE (0x71D38)

fails = []


def check(name, ok, detail=""):
    print(f"  [{'GREEN' if ok else 'RED  '}] {name}" + (f" -- {detail}" if detail else ""))
    if not ok:
        fails.append(name)


def first_define(name):
    m = re.search(rf"#define\s+{name}\s+(\d+)", V1)
    return int(m.group(1)) if m else None


# ---- parse the GHZCUT (chain) carve: FIRST #define occurrence of each -------
# (the GHZCUT branch precedes the MENU/#else branches in the file; the tiny-
# bucket flavor marker P6_GHZCUT_TINY_B1 must exist for the model to apply).
assert "P6_GHZCUT_TINY_B1" in V1, "GHZCUT tiny-bucket flavor marker missing"
BK0 = first_define("P6_BK0")
BK1 = first_define("P6_BK1")
BKW = first_define("P6_BKW")
BK2 = first_define("P6_BK2")
NSLOTS_CHAIN = 1  # GHZCUT implies MENU -> catch-all P6_VDP1_NSLOTS = 1 (p6_vdp1.c:89)
BOXES = [(16, 20, BK0), (64, 80, BK1), (176, 56, BKW), (160, 160, BK2),
         (248, 160, NSLOTS_CHAIN)]  # P6_BUCK GHZCUT table order (p6_vdp1.c)
print(f"carve: tiny={BK0} 64x80={BK1} wide={BKW} 160sq={BK2} catchall={NSLOTS_CHAIN}")


# ---- T1: tiny demand from the real bins --------------------------------------
def tiny_frames(binname, bw=16, bh=20):
    sheets, anims = parse_spr(str(SPRITES / binname))
    out = {}
    for a in anims:
        n = 0
        for f in a["frames"]:
            w, h = f[3], f[4]
            if 0 < w <= bw and 0 < h <= bh:
                n += 1
        if n:
            out[a["name"]] = n
    return out

hud = tiny_frames("Global/HUD.bin")
ring = tiny_frames("Global/Ring.bin")
star = tiny_frames("Global/StarPost.bin")
dust = tiny_frames("Global/Dust.bin")
score = tiny_frames("Global/ScoreBonus.bin")
# worst concurrent distinct tiny rects (see docstring derivation):
demand = (min(10, hud.get("Numbers", 0))            # distinct digit glyphs
          + (1 if hud.get("HUD Elements") else 0)   # lives 'x'
          + (1 if ring.get("Normal Ring") else 0)   # global ring frame
          + min(3, sum(1 for k in ring if k.startswith("Sparkle")))
          + (1 if star.get("Bulb Used") else 0)
          + min(2, sum(1 for k in star if k.startswith("Stars")))
          + (1 if dust.get("Dust Puff") else 0)
          + min(2, score.get("Scores", 0)))
need = math.ceil(demand * 1.2)
check(f"T1 tiny bucket capacity >= {need} (demand {demand} + 20%)",
      BK0 is not None and BK0 >= need, f"P6_BK0={BK0}")

# ---- T2: 64x80 bucket no-regress vs measured chain fmax ----------------------
check("T2 64x80 bucket >= 18 (measured chain fmax 17, #324)",
      BK1 is not None and BK1 >= 18, f"P6_BK1={BK1}")

# ---- T3: VDP1 user-area budget with data-driven reserve ----------------------
def pad8(w):
    return (w + 7) & ~7

sheets, tc = parse_spr(str(SPRITES / "Global/TitleCard.bin"))
by = {a["name"]: [(f[3], f[4]) for f in a["frames"]] for a in tc}
gmax = lambda n: max((pad8(w) * h for w, h in by.get(n, [(0, 0)])), default=0)
glyph_reserve = (7 * gmax("Name Letters")      # G,R,E,N,H,I,L distinct
                 + 4 * gmax("Zone Letters")
                 + 1 * gmax("Act Numbers")
                 + sum(pad8(w) * h for w, h in by.get("Decorations", [])))
reserve = glyph_reserve + 3 * 512 + 2048       # + lazy fills + safety
vram = sum(w * h * n for (w, h, n) in BOXES)
check(f"T3 VRAM {vram} + reserve {reserve} <= {VDP1_USER_AREA}",
      vram + reserve <= VDP1_USER_AREA,
      f"margin {VDP1_USER_AREA - vram} (glyphs {glyph_reserve})")

print()
if fails:
    print(f"RED: {len(fails)} failing: {', '.join(fails)}")
    sys.exit(1)
print("GREEN: capacity model holds. Primary verifies live: "
      "d(p6_w_vdp1_evicts)/frame <= 0.5 at settled GHZ; "
      "no digit-alternation on the lamppost ball; qa_p6_mapoverlap post-build.")
sys.exit(0)
