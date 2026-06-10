#!/usr/bin/env python3
"""Probe 3 raster-budget gate (engine true-port feasibility spike, Task #194).

Re-derives the architecturally-decisive number from the REAL SH-2 assembly
the toolchain emits for RSDK's verbatim software-raster inner loop
(probe3_raster.s, function _blit_ink_none, the FLIP_NONE/INK_NONE path of
Drawing.cpp:2924-2938).

It counts the per-pixel instruction body the compiler actually generated, then
compares one full-screen software blit against the single-SH-2 frame budget.

The verdict does NOT depend on exact branch/memory-wait timing: it uses the
ABSOLUTE FLOOR of 1 cycle per emitted instruction with ZERO memory wait states
and ZERO branch penalty -- a bound no real SH-2 can beat. If even that floor
exceeds the frame budget, software rendering is infeasible and the Saturn
device backend must render via VDP1/VDP2 hardware (as the hand-port already
does).

Exit 0  = measurement parsed; prints the budget table + verdict.
Exit 2  = could not parse the assembly (toolchain/codegen changed) -- investigate.
"""
import re
import sys
import pathlib

HERE = pathlib.Path(__file__).resolve().parent
ASM = HERE / "probe3_raster.s"

# --- Saturn frame budget (CLAUDE.md hardware table; 320 mode NTSC) ----------
SH2_HZ_320 = 26_800_000          # one SH-2, 320 mode
FPS = 60.0
BUDGET_1SH2 = SH2_HZ_320 / FPS   # cycles/frame, single SH-2
SCREEN_W, SCREEN_H = 320, 224
FULLSCREEN_PX = SCREEN_W * SCREEN_H   # 71680


def parse_inner_loop():
    """Count the instructions in _blit_ink_none's inner per-pixel loop (.L5).

    Returns (body_instr_count, opaque_path_count, transparent_path_count).
    """
    if not ASM.exists():
        print(f"FAIL: {ASM} not found -- compile probe3_raster.c first", file=sys.stderr)
        sys.exit(2)
    lines = ASM.read_text().splitlines()

    # Locate the _blit_ink_none function, then its inner loop label .L5 and the
    # delayed conditional branch (bf.s/bt.s) that targets .L5 (loop-back).
    try:
        fn = next(i for i, l in enumerate(lines) if l.strip() == "_blit_ink_none:")
    except StopIteration:
        print("FAIL: _blit_ink_none not found in assembly", file=sys.stderr)
        sys.exit(2)

    # Find the *innermost* loop: the conditional delayed branch whose target is
    # the nearest preceding local label. In this codegen that is `.L5`.
    loopback = None
    for i in range(fn, len(lines)):
        m = re.match(r"\s*(bf\.s|bt\.s|bf|bt)\s+(\.L\d+)", lines[i])
        if m:
            tgt = m.group(2)
            # is the target a label that appears BEFORE this branch (a back-edge)?
            tgt_idx = next((j for j in range(fn, i) if lines[j].strip() == tgt + ":"), None)
            if tgt_idx is not None:
                # innermost = smallest span; the per-pixel loop is the tightest.
                span = i - tgt_idx
                if loopback is None or span < loopback[2]:
                    loopback = (tgt_idx, i, span, tgt)
    if loopback is None:
        print("FAIL: no back-edge branch found (codegen changed)", file=sys.stderr)
        sys.exit(2)

    start_idx, branch_idx, _, label = loopback

    # Instruction lines between the loop label (exclusive) and the loop-back
    # branch's delay slot (inclusive). A delayed branch executes the following
    # instruction, so the body spans start+1 .. branch_idx+1.
    body = []
    for j in range(start_idx + 1, branch_idx + 2):
        s = lines[j].strip()
        if not s or s.endswith(":") or s.startswith("."):
            continue  # skip labels and assembler directives
        body.append(s)

    # Opaque path executes every body instruction (the `if (*pixels>0)` test
    # falls through). Transparent path takes the inner bt.s skip, executing
    # only: the byte load, the test, the skip-branch + its delay slot, and the
    # loop tail (dt, bf.s, delay slot).
    body_n = len(body)
    # transparent path = body minus the conditionally-skipped store block.
    # Identify the skip branch (bt.s) inside the body and its target label.
    trans_skipped = 0
    for k, ins in enumerate(body):
        m = re.match(r"(bt\.s|bf\.s)\s+(\.L\d+)", ins)
        if m:
            skip_tgt = m.group(2)
            # find that label position within the surrounding function
            lbl_pos = next((j for j in range(branch_idx, start_idx, -1)
                            if lines[j].strip() == skip_tgt + ":"), None)
            if lbl_pos is not None:
                # count body instructions between this branch's delay slot and the label
                # (those are the ones skipped when the branch is taken)
                # delay slot = k+1 is executed; skipped = from k+2 up to the label
                # approximate by counting body entries that map to lines > branch line and < lbl
                pass
    # The skipped store block in this codegen is the 4 instrs: add r0,r0 / add r8,r0
    # / mov.w @(r0,r10),r2 / mov.w r2,@r3 -- detectable as the 16-bit indexed load+store.
    has_idx_load = any(re.match(r"mov\.w\s+@\(r\d+,r\d+\),", b) for b in body)
    has_store = any(re.match(r"mov\.w\s+r\d+,@r\d+", b) for b in body)
    if has_idx_load and has_store:
        trans_skipped = 4  # add,add,mov.w(load),mov.w(store)
    opaque_n = body_n
    transparent_n = body_n - trans_skipped
    return body_n, opaque_n, transparent_n, label, body


def main():
    body_n, opaque_n, transparent_n, label, body = parse_inner_loop()

    # FLOOR cycle model: 1 cycle / emitted instruction, zero memory wait, zero
    # branch penalty. No real SH-2 beats this. (Realistic is strictly worse:
    # the loop-back bf.s taken costs +1, and every WRAM access adds wait states.)
    floor_opaque = opaque_n
    floor_transparent = transparent_n
    real_opaque = opaque_n + 1   # taken delayed bf.s loop-back = 2 not 1

    fs_floor = FULLSCREEN_PX * floor_opaque       # cycles for 1 opaque full-screen, FLOOR
    fs_real = FULLSCREEN_PX * real_opaque

    print("=" * 70)
    print("Probe 3 -- SH-2 software-raster budget (Task #194 true-port spike)")
    print("=" * 70)
    print(f"Source loop : _blit_ink_none .{label}  (verbatim RSDK Drawing.cpp:2924-2938)")
    print(f"Emitted body: {body_n} instructions/iteration")
    print("  " + "\n  ".join(body))
    print("-" * 70)
    print(f"Per opaque pixel : {opaque_n} instr  -> FLOOR {floor_opaque} cyc "
          f"(realistic ~{real_opaque}+ with branch+WRAM waits)")
    print(f"Per transparent  : {transparent_n} instr -> FLOOR {floor_transparent} cyc")
    print("-" * 70)
    print(f"Full screen      : {SCREEN_W}x{SCREEN_H} = {FULLSCREEN_PX} px")
    print(f"1 full-screen blit FLOOR    : {fs_floor:,} cyc")
    print(f"1 full-screen blit realistic: {fs_real:,} cyc")
    print(f"Single-SH-2 budget @60fps   : {BUDGET_1SH2:,.0f} cyc/frame")
    print(f"Dual-SH-2 budget   @60fps   : {2*BUDGET_1SH2:,.0f} cyc/frame")
    print("-" * 70)
    ratio_floor = fs_floor / BUDGET_1SH2
    ratio_dual = fs_floor / (2 * BUDGET_1SH2)
    print(f"1 fullscreen / 1-SH-2 budget (FLOOR): {ratio_floor:.2f}x")
    print(f"1 fullscreen / 2-SH-2 budget (FLOOR): {ratio_dual:.2f}x")
    print("Mania GHZ composites ~3-4 fullscreen-equiv layers + sprite overdraw.")
    print("=" * 70)

    if fs_floor > BUDGET_1SH2:
        print("VERDICT: SOFTWARE RENDER INFEASIBLE on SH-2.")
        print("  Even the unreachable floor (1 cyc/instr, 0 wait, 0 branch")
        print("  penalty) for ONE opaque full-screen exceeds the single-SH-2")
        print("  60fps frame budget. RSDK's software rasterizer (Drawing.cpp")
        print("  pixel loops) MUST NOT be compiled into the Saturn core; the")
        print("  device backend must render via VDP1 sprites + VDP2 scroll")
        print("  planes -- exactly the translation the hand-port src/rsdk/")
        print("  drawing layer already implements.")
        print("RESULT: HARDWARE-RENDER-REQUIRED (expected, reproducible).")
        return 0
    else:
        print("VERDICT: software render fits the floor -- re-examine assumptions.")
        return 0


if __name__ == "__main__":
    sys.exit(main())
