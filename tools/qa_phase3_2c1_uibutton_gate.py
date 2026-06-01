#!/usr/bin/env python3
"""
qa_phase3_2c1_uibutton_gate.py - Phase 3.2.c.1 (Task #148) RED/GREEN gate.

Per `memory/qa-iterative-improvement.md` v3: the gate MUST fire RED on
the current build BEFORE the fix lands. Watching the gate RED -> GREEN
is the only evidence the fix is correct.

Phase 3.2.c.1 decomp-port deliverables (per docs/menusetup_decomposition_plan.md
sub-task 3.2.c decomposed into .1 — UIButton + UIButtonPrompt +
UISubHeading + VDP1 polygon emitter):

  - Port `tools/_decomp_raw/SonicMania_Objects_Menu_UIButton.{c,h}`
    (968 LOC body + 85 LOC header) -> src/mania/Objects/Menu/UIButton.{c,h}
  - Port `SonicMania_Objects_Menu_UIButtonPrompt.{c,h}` (567 + 101 LOC)
    -> src/mania/Objects/Menu/UIButtonPrompt.{c,h}
  - Port `SonicMania_Objects_Menu_UISubHeading.{c,h}` (451 + 65 LOC)
    -> src/mania/Objects/Menu/UISubHeading.{c,h}
  - Extend src/rsdk/drawing.{c,h} with the three VDP1 polygon emitter
    primitives: rsdk_draw_rect / rsdk_draw_line / rsdk_draw_face.
    Saturn impl uses SGL slPutPolygon (PDATA) per ST-238-R1 / SL_DEF.H.
  - Wire Phase 3.2.a UIControl stubs (SetupButtons / MenuChangeButtonInit
    / ProcessButtonInput) with the now-ported UIButton dispatch.
  - Fill Phase 3.2.b UIWidgets DrawRectOutline_* / DrawRightTriangle /
    DrawEquilateralTriangle / DrawParallelogram stubs with their
    canonical decomp bodies that call the new VDP1 primitives.

Predicates:

  P1: src/mania/Objects/Menu/UIButton.{c,h} exist + define the
      canonical entry-points (Update / LateUpdate / StaticUpdate /
      Draw / Create / StageLoad / ManageChoices / GetChoicePtr /
      ProcessButtonCB / ButtonEnterCB / ButtonLeaveCB / SelectedCB /
      State_HandleButtonEnter / State_HandleButtonLeave / State_Selected).

  P2: src/rsdk/drawing.{c,h} declares + defines the three VDP1
      polygon primitives rsdk_draw_rect / rsdk_draw_line /
      rsdk_draw_face with non-stub bodies that call slPutPolygon.

  P3: src/mania/Objects/Menu/UIButtonPrompt.{c,h} + UISubHeading.{c,h}
      exist + define their canonical entry-points (Update / Draw /
      Create / StageLoad).

  P4: src/mania/Objects/Menu/UIControl.c::UIControl_SetupButtons /
      UIControl_MenuChangeButtonInit / UIControl_ProcessButtonInput
      bodies no longer contain the Phase 3.2.a/.b FIXME stub markers
      AND now reference the UIButton class.

  P5: src/mania/Objects/Menu/UIWidgets.c bodies for
      DrawRectOutline_Black / DrawRectOutline_Blended /
      DrawRectOutline_Flash / DrawRightTriangle / DrawEquilateralTriangle /
      DrawParallelogram are no longer (void)stub bodies AND call into
      the new rsdk_draw_rect / rsdk_draw_line / rsdk_draw_face
      primitives.

Expected RED on pre-edit build (before Phase 3.2.c.1 work):
  P1: FAIL — UIButton.c absent.
  P2: FAIL — rsdk_draw_rect / rsdk_draw_line / rsdk_draw_face absent.
  P3: FAIL — UIButtonPrompt.c + UISubHeading.c absent.
  P4: FAIL — UIControl stubs still contain "FIXME Phase 3.2.b" markers.
  P5: FAIL — UIWidgets Draw* helpers are still (void)stub bodies.

Expected GREEN after Phase 3.2.c.1 lands:
  P1..P5 all PASS.

Exit codes:
  0 = all green
  1 = any red

Usage:
    python tools/qa_phase3_2c1_uibutton_gate.py [--verbose]
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


# --- P1: UIButton.{c,h} exist with canonical entry-points ---------------

REQUIRED_UIBUTTON_SYMBOLS = [
    "UIButton_Update",
    "UIButton_LateUpdate",
    "UIButton_StaticUpdate",
    "UIButton_Draw",
    "UIButton_Create",
    "UIButton_StageLoad",
    "UIButton_ManageChoices",
    "UIButton_GetChoicePtr",
    "UIButton_ProcessButtonCB",
    "UIButton_ButtonEnterCB",
    "UIButton_ButtonLeaveCB",
    "UIButton_SelectedCB",
    "UIButton_State_HandleButtonEnter",
    "UIButton_State_HandleButtonLeave",
    "UIButton_State_Selected",
]


def check_p1(verbose: bool) -> tuple[bool, str]:
    src_c = ROOT / "src" / "mania" / "Objects" / "Menu" / "UIButton.c"
    src_h = ROOT / "src" / "mania" / "Objects" / "Menu" / "UIButton.h"
    if not src_c.exists():
        return False, f"P1 RED: {src_c} does not exist"
    if not src_h.exists():
        return False, f"P1 RED: {src_h} does not exist"
    text = src_c.read_text(errors="replace")
    missing = [s for s in REQUIRED_UIBUTTON_SYMBOLS
               if not re.search(rf"\b{re.escape(s)}\s*\(", text)]
    if missing:
        return False, f"P1 RED: UIButton.c missing definitions of {missing}"
    return True, (f"P1 GREEN: UIButton.{{c,h}} exist; all "
                  f"{len(REQUIRED_UIBUTTON_SYMBOLS)} symbols defined")


# --- P2: VDP1 polygon emitter primitives in src/rsdk/drawing.{c,h} -----

REQUIRED_DRAW_PRIMS = [
    "rsdk_draw_rect",
    "rsdk_draw_line",
    "rsdk_draw_face",
]


def _body_text(text: str, fn_name: str) -> str | None:
    m = re.search(rf"\b{re.escape(fn_name)}\s*\([^)]*\)\s*\{{",
                  text, re.DOTALL)
    if not m:
        return None
    start = m.end() - 1
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start:i]
    return None


def check_p2(verbose: bool) -> tuple[bool, str]:
    src_c = ROOT / "src" / "rsdk" / "drawing.c"
    src_h = ROOT / "src" / "rsdk" / "drawing.h"
    if not src_c.exists() or not src_h.exists():
        return False, "P2 RED: src/rsdk/drawing.{c,h} missing"
    c_text = src_c.read_text(errors="replace")
    h_text = src_h.read_text(errors="replace")
    missing_def = [s for s in REQUIRED_DRAW_PRIMS
                   if not re.search(rf"\b{re.escape(s)}\s*\(", c_text)]
    missing_decl = [s for s in REQUIRED_DRAW_PRIMS
                    if not re.search(rf"\b{re.escape(s)}\b", h_text)]
    if missing_def or missing_decl:
        return False, (f"P2 RED: VDP1 polygon primitives missing — defs:"
                       f"{missing_def} decls:{missing_decl}")
    # Verify non-stub bodies route through SGL slPutPolygon.
    weak: list[str] = []
    for fn in REQUIRED_DRAW_PRIMS:
        body = _body_text(c_text, fn)
        if body is None:
            weak.append(f"{fn}:body-not-isolated")
            continue
        line_count = body.count("\n")
        # Direct call OR via _emit_polygon4 helper.
        has_primitive = ("slPutPolygon" in body
                         or "_emit_polygon4" in body)
        if line_count < 4 or not has_primitive:
            weak.append(f"{fn}:lines={line_count},"
                        f"has_primitive={has_primitive}")
    if weak:
        return False, (f"P2 RED: VDP1 primitives present but stubbed — "
                       f"{weak}")
    return True, (f"P2 GREEN: all {len(REQUIRED_DRAW_PRIMS)} VDP1 "
                  "polygon primitives wired via SGL slPutPolygon")


# --- P3: UIButtonPrompt + UISubHeading ---------------------------------

REQUIRED_BUTTONPROMPT_SYMBOLS = [
    "UIButtonPrompt_Update",
    "UIButtonPrompt_Draw",
    "UIButtonPrompt_Create",
    "UIButtonPrompt_StageLoad",
    "UIButtonPrompt_SetButtonSprites",
    "UIButtonPrompt_GetGamepadType",
    "UIButtonPrompt_State_CheckIfSelected",
    "UIButtonPrompt_State_Selected",
]
REQUIRED_SUBHEADING_SYMBOLS = [
    "UISubHeading_Update",
    "UISubHeading_Draw",
    "UISubHeading_Create",
    "UISubHeading_StageLoad",
]


def check_p3(verbose: bool) -> tuple[bool, str]:
    bp_c = ROOT / "src" / "mania" / "Objects" / "Menu" / "UIButtonPrompt.c"
    bp_h = ROOT / "src" / "mania" / "Objects" / "Menu" / "UIButtonPrompt.h"
    sh_c = ROOT / "src" / "mania" / "Objects" / "Menu" / "UISubHeading.c"
    sh_h = ROOT / "src" / "mania" / "Objects" / "Menu" / "UISubHeading.h"
    missing_files = [p for p in (bp_c, bp_h, sh_c, sh_h) if not p.exists()]
    if missing_files:
        return False, (f"P3 RED: missing files {[str(p) for p in missing_files]}")
    bp_text = bp_c.read_text(errors="replace")
    sh_text = sh_c.read_text(errors="replace")
    missing_bp = [s for s in REQUIRED_BUTTONPROMPT_SYMBOLS
                  if not re.search(rf"\b{re.escape(s)}\s*\(", bp_text)]
    missing_sh = [s for s in REQUIRED_SUBHEADING_SYMBOLS
                  if not re.search(rf"\b{re.escape(s)}\s*\(", sh_text)]
    if missing_bp or missing_sh:
        return False, (f"P3 RED: UIButtonPrompt missing:{missing_bp} "
                       f"UISubHeading missing:{missing_sh}")
    return True, ("P3 GREEN: UIButtonPrompt.{c,h} + UISubHeading.{c,h} "
                  "exist; all canonical entry-points defined")


# --- P4: UIControl stubs wired -----------------------------------------

def check_p4(verbose: bool) -> tuple[bool, str]:
    src_c = ROOT / "src" / "mania" / "Objects" / "Menu" / "UIControl.c"
    if not src_c.exists():
        return False, "P4 RED: UIControl.c missing"
    text = src_c.read_text(errors="replace")
    # Required: the FIXME Phase 3.2.b stub markers must be gone from the
    # three target function bodies AND each function must reference
    # UIButton class (string "UIButton" or a UIButton_ symbol call).
    targets = [
        "UIControl_SetupButtons",
        "UIControl_MenuChangeButtonInit",
        "UIControl_ProcessButtonInput",
    ]
    bad = []
    for fn in targets:
        body = _body_text(text, fn)
        if body is None:
            bad.append(f"{fn}:not-isolated")
            continue
        if "FIXME Phase 3.2.b" in body:
            bad.append(f"{fn}:has-fixme-stub")
            continue
        # Must reference UIButton or UIButton_ method.
        if ("UIButton" not in body
                and "buttons[" not in body
                and "buttonCount" not in body):
            bad.append(f"{fn}:no-uibutton-ref")
    if bad:
        return False, f"P4 RED: UIControl stubs not wired — {bad}"
    return True, ("P4 GREEN: UIControl SetupButtons + "
                  "MenuChangeButtonInit + ProcessButtonInput now wire "
                  "UIButton dispatch (FIXME markers cleared)")


# --- P5: UIWidgets Draw* helpers filled in -----------------------------

def check_p5(verbose: bool) -> tuple[bool, str]:
    src_c = ROOT / "src" / "mania" / "Objects" / "Menu" / "UIWidgets.c"
    if not src_c.exists():
        return False, "P5 RED: UIWidgets.c missing"
    text = src_c.read_text(errors="replace")
    # The decomp-faithful Phase 3.2.c.1 fill turns each helper into a
    # body that calls rsdk_draw_rect or rsdk_draw_face. We check for
    # the presence of any of these calls inside each function body.
    targets = [
        "UIWidgets_DrawRectOutline_Black",
        "UIWidgets_DrawRectOutline_Blended",
        "UIWidgets_DrawRectOutline_Flash",
        "UIWidgets_DrawRightTriangle",
        "UIWidgets_DrawEquilateralTriangle",
        "UIWidgets_DrawParallelogram",
    ]
    bad = []
    for fn in targets:
        body = _body_text(text, fn)
        if body is None:
            bad.append(f"{fn}:not-isolated")
            continue
        # The Phase 3.2.b stub form is `(void)x; (void)y; ...` — verify
        # the body no longer matches that pattern AND that it calls a
        # new VDP1 primitive.
        # Stub detection: every line is a (void)cast.
        non_void_lines = [
            ln for ln in body.splitlines()
            if ln.strip() and not re.match(r"\s*\(void\)", ln.strip())
            and not ln.strip().startswith("/*")
            and not ln.strip().startswith("*")
            and not ln.strip().startswith("//")
            and ln.strip() not in ("{", "}")
        ]
        if not non_void_lines:
            bad.append(f"{fn}:still-stub")
            continue
        has_prim = ("rsdk_draw_rect" in body or
                    "rsdk_draw_face" in body or
                    "rsdk_draw_line" in body)
        if not has_prim:
            bad.append(f"{fn}:no-vdp1-primitive-call")
    if bad:
        return False, f"P5 RED: UIWidgets Draw* stubs not filled — {bad}"
    return True, ("P5 GREEN: UIWidgets DrawRectOutline_* / DrawRightTriangle / "
                  "DrawEquilateralTriangle / DrawParallelogram now route "
                  "through VDP1 polygon emitter")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    print("Phase 3.2.c.1 (Task #148) UIButton + UIButtonPrompt + UISubHeading + "
          "VDP1 polygon emitter gate")
    print("=" * 78)
    results = [
        check_p1(args.verbose),
        check_p2(args.verbose),
        check_p3(args.verbose),
        check_p4(args.verbose),
        check_p5(args.verbose),
    ]
    for ok, msg in results:
        print(("  PASS  " if ok else "  FAIL  ") + msg)
    overall = all(ok for ok, _ in results)
    print("=" * 78)
    print(f"OVERALL: {'GREEN' if overall else 'RED'}")
    return 0 if overall else 1


if __name__ == "__main__":
    sys.exit(main())
