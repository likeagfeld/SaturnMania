#!/usr/bin/env python3
"""qa_phase1_31_deadlock_gate.py - Phase 1.31 Fix #2 gate.

Asserts that frames 75..90 of a 90-frame 3fps qa_diag capture are NOT
stuck (i.e. no consecutive identical-pixel run >= 4 frames). The
title-state machine's GHZ auto-advance was firing at +480 ticks past
TS_WAIT_FOR_ENTER, dropping into a broken GHZ chain that left the
screen as a stuck garbage NBG1 tile pattern (no diff between frames
87..90 in the baseline capture: delta=0.00).

RED-firing baseline (Phase 1.31 pre-fix, 2026-05-27):
  frame 87 t=28.38s  BIG_CHANGE  delta=103.73   (title->GHZ transition)
  frame 88 t=28.71s  delta=0.00   (STUCK)
  frame 89 t=29.04s  delta=0.00   (STUCK)
  frame 90 t=29.37s  delta=0.00   (STUCK)

GREEN target: keep the title alive through frame 90 (no auto-advance
into the broken GHZ chain).  Predicate: in the closing window
frames 75..90, no 4-frame stuck-run (delta<1.0 across 3 consecutive
inter-frame diffs).

Exit:
  0  GREEN (no stuck run)
  1  RED  (stuck run found)
"""
import sys
from pathlib import Path
try:
    from PIL import Image
    import numpy as np
except ImportError:
    sys.exit("requires Pillow + numpy")

ROOT = Path(__file__).resolve().parent.parent
START, END = 75, 90
STUCK_RUN_LEN = 4   # 4 consecutive identical frames = stuck
DELTA_THRESH = 1.0

ROI = (60, 60, 850, 690)

frames = []
for n in range(START, END + 1):
    p = ROOT / f"qa_diag_{n}.png"
    if not p.exists():
        sys.exit(f"missing {p}")
    img = Image.open(p).convert("RGB").crop(ROI)
    frames.append(np.array(img))

deltas = []
for i in range(1, len(frames)):
    d = float(np.abs(frames[i].astype(np.int32) - frames[i-1].astype(np.int32)).mean())
    deltas.append((START + i, d))

run = 0
worst_run = 0
worst_run_start = -1
cur_run_start = -1
for fno, d in deltas:
    if d < DELTA_THRESH:
        if run == 0:
            cur_run_start = fno - 1
        run += 1
        if run > worst_run:
            worst_run = run
            worst_run_start = cur_run_start
    else:
        run = 0

print("frames {}..{} inter-frame deltas:".format(START, END))
for fno, d in deltas:
    flag = "  STUCK" if d < DELTA_THRESH else ""
    print(f"  delta {fno-1:>2}->{fno:>2}: {d:>8.3f}{flag}")

print()
# A stuck-run of length N means N+1 identical frames.
if worst_run + 1 >= STUCK_RUN_LEN:
    print(f"RED: stuck run of {worst_run+1} identical frames starting at "
          f"frame {worst_run_start} (threshold >= {STUCK_RUN_LEN})")
    sys.exit(1)
print(f"GREEN: longest identical-frame run = {worst_run+1} (< {STUCK_RUN_LEN})")
sys.exit(0)
