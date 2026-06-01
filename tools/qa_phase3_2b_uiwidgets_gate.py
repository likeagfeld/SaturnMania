#!/usr/bin/env python3
"""
qa_phase3_2b_uiwidgets_gate.py - Phase 3.2.b (Task #146) RED/GREEN gate.

Per `memory/qa-iterative-improvement.md` v3: the gate MUST fire RED on
the current build BEFORE the fix lands. Watching the gate RED -> GREEN
is the only evidence the fix is correct.

Phase 3.2.b decomp-port deliverables (per docs/menusetup_decomposition_plan.md
section "3.2.b — Foundation: UIWidgets + asset bootstrap"):

  - Port `tools/_decomp_raw/SonicMania_Objects_Menu_UIWidgets.{c,h}`
    (353 LOC body + 75 LOC header) to `src/mania/Objects/Menu/UIWidgets.{c,h}`.
  - Extend `src/rsdk/string.{c,h}` — full RSDK::String port per
    `tools/_decomp_raw/_RSDKv5_Text.{cpp,hpp}` (init / set / append /
    copy / get-cstring / compare). Phase 3.2.a stub already implements
    these; gate verifies they exist + are non-stub.
  - Extend `src/rsdk/drawing.{c,h}` with three primitives needed by
    UIBackground.DrawNormal + UIWidgets:
      rsdk_fill_screen  (decomp Drawing.cpp:586 FillScreen — Saturn:
                         slBack1ColSet + VDP1 polygon overlay)
      rsdk_draw_circle  (decomp Drawing.cpp:1314 DrawCircle — Saturn:
                         VDP1 polygon vertex approximation)
      rsdk_draw_circle_outline
                        (decomp Drawing.cpp:1647 DrawCircleOutline —
                         Saturn: VDP1 line commands)
  - Wire UIBackground.DrawNormal per decomp UIBackground.c:44-70 using
    the new primitives.

Predicates:

  P1: src/mania/Objects/Menu/UIWidgets.c + .h exist.
      Define UIWidgets_Create / UIWidgets_Update / UIWidgets_StageLoad /
      UIWidgets_ApplyLanguage / UIWidgets_DrawRectOutline_Black /
      UIWidgets_DrawParallelogram / UIWidgets_DrawUpDownArrows /
      UIWidgets_DrawLeftRightArrows.

  P2: src/rsdk/string.{c,h} declares + defines the full ManiaString
      API: rsdk_init_string / rsdk_set_string / rsdk_append_text /
      rsdk_append_string / rsdk_copy_string / rsdk_get_cstring /
      rsdk_compare_strings.

  P3: src/rsdk/drawing.{c,h} declares + defines rsdk_fill_screen,
      rsdk_draw_circle, rsdk_draw_circle_outline. The body must NOT be
      a Phase-A4 sentinel no-op (we check for non-trivial body via line
      count + that the function calls into jo_engine / SGL primitives
      like slBack1ColSet OR jo_set_screens_order OR slPutPolygon OR
      slLine OR a VDP1 command-table writer).

  P4: src/mania/Objects/Menu/UIBackground.c::UIBackground_DrawNormal
      no longer contains the Phase 3.2.a "FIXME Phase 3.2.c" stub
      marker AND calls the new rsdk_fill_screen + rsdk_draw_circle (or
      rsdk_draw_circle_outline) primitives at least once each.

Expected RED on pre-edit build:
  P1: FAIL — UIWidgets.c absent.
  P2: PASS — the Phase 3.2.a string API already implements these (this
       predicate verifies non-regression: the 3.2.a stub is the spec).
  P3: FAIL — rsdk_fill_screen body is a Phase-A4 sentinel no-op; the
       circle primitives do not exist.
  P4: FAIL — UIBackground_DrawNormal still has FIXME Phase 3.2.c stub.

Expected GREEN after Phase 3.2.b lands:
  P1..P4 all PASS.

Exit codes:
  0 = all green
  1 = any red

Usage:
    python tools/qa_phase3_2b_uiwidgets_gate.py [--verbose]
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


# --- P1: UIWidgets.{c,h} exist with the canonical entry-points ----------

REQUIRED_UIWIDGETS_SYMBOLS = [
    "UIWidgets_Create",
    "UIWidgets_Update",
    "UIWidgets_LateUpdate",
    "UIWidgets_StaticUpdate",
    "UIWidgets_Draw",
    "UIWidgets_StageLoad",
    "UIWidgets_ApplyLanguage",
    "UIWidgets_DrawRectOutline_Black",
    "UIWidgets_DrawParallelogram",
    "UIWidgets_DrawUpDownArrows",
    "UIWidgets_DrawLeftRightArrows",
]


def check_p1(verbose: bool) -> tuple[bool, str]:
    src_c = ROOT / "src" / "mania" / "Objects" / "Menu" / "UIWidgets.c"
    src_h = ROOT / "src" / "mania" / "Objects" / "Menu" / "UIWidgets.h"
    if not src_c.exists():
        return False, f"P1 RED: {src_c} does not exist"
    if not src_h.exists():
        return False, f"P1 RED: {src_h} does not exist"
    text = src_c.read_text(errors="replace")
    missing = [s for s in REQUIRED_UIWIDGETS_SYMBOLS
               if not re.search(rf"\b{re.escape(s)}\s*\(", text)]
    if missing:
        return False, f"P1 RED: UIWidgets.c missing definitions of {missing}"
    return True, (f"P1 GREEN: UIWidgets.{{c,h}} exist; all "
                  f"{len(REQUIRED_UIWIDGETS_SYMBOLS)} symbols defined")


# --- P2: ManiaString full API in src/rsdk/string.{c,h} ------------------

REQUIRED_STRING_SYMBOLS = [
    "rsdk_init_string",
    "rsdk_set_string",
    "rsdk_append_text",
    "rsdk_append_string",
    "rsdk_copy_string",
    "rsdk_get_cstring",
    "rsdk_compare_strings",
]


def check_p2(verbose: bool) -> tuple[bool, str]:
    src_c = ROOT / "src" / "rsdk" / "string.c"
    src_h = ROOT / "src" / "rsdk" / "string.h"
    if not src_c.exists() or not src_h.exists():
        return False, "P2 RED: src/rsdk/string.{c,h} missing"
    c_text = src_c.read_text(errors="replace")
    h_text = src_h.read_text(errors="replace")
    missing_def = [s for s in REQUIRED_STRING_SYMBOLS
                   if not re.search(rf"\b{re.escape(s)}\s*\(", c_text)]
    missing_decl = [s for s in REQUIRED_STRING_SYMBOLS
                    if not re.search(rf"\b{re.escape(s)}\b", h_text)]
    if missing_def or missing_decl:
        return False, (f"P2 RED: string API missing — defs:{missing_def} "
                       f"decls:{missing_decl}")
    return True, (f"P2 GREEN: all {len(REQUIRED_STRING_SYMBOLS)} "
                  "ManiaString API entries defined + declared")


# --- P3: drawing primitives exist with non-stub bodies ------------------

REQUIRED_DRAWING_SYMBOLS = [
    "rsdk_fill_screen",
    "rsdk_draw_circle",
    "rsdk_draw_circle_outline",
]


def _body_lines(text: str, fn_name: str) -> int:
    """Count lines in the function body. Returns 0 if not found."""
    m = re.search(rf"\b{re.escape(fn_name)}\s*\([^)]*\)\s*\{{",
                  text, re.DOTALL)
    if not m:
        return 0
    start = m.end() - 1  # at the opening brace
    depth = 0
    end = start
    for i in range(start, len(text)):
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                end = i
                break
    return text[start:end].count("\n")


def check_p3(verbose: bool) -> tuple[bool, str]:
    src_c = ROOT / "src" / "rsdk" / "drawing.c"
    src_h = ROOT / "src" / "rsdk" / "drawing.h"
    if not src_c.exists() or not src_h.exists():
        return False, "P3 RED: src/rsdk/drawing.{c,h} missing"
    c_text = src_c.read_text(errors="replace")
    h_text = src_h.read_text(errors="replace")
    missing_def = [s for s in REQUIRED_DRAWING_SYMBOLS
                   if not re.search(rf"\b{re.escape(s)}\s*\(", c_text)]
    missing_decl = [s for s in REQUIRED_DRAWING_SYMBOLS
                    if not re.search(rf"\b{re.escape(s)}\b", h_text)]
    if missing_def or missing_decl:
        return False, (f"P3 RED: drawing primitives missing — defs:"
                       f"{missing_def} decls:{missing_decl}")
    # Verify non-stub bodies: each function body must contain a call to a
    # Saturn graphics primitive (slBack1ColSet / slPutPolygon / slLine /
    # jo_set_screens_order / vdp1 command writer) AND must be > 4 lines.
    saturn_primitive_re = re.compile(
        r"\b(slBack1ColSet|slBack2ColSet|slPutPolygon|slLine|"
        r"jo_3d_draw_line|jo_3d_draw_quad|jo_clear_screen|"
        r"_jo_vdp1_set_command|jo_sprite_draw3D|slDispSprite|"
        r"vdp1_cmd_polygon|vdp1_cmd_line|vdp1_cmd_polyline|"
        r"vdp1_draw_polygon|vdp1_draw_line|jo_printf|"
        r"jo_background_3d_plane|slCurColor|slSetColRAM|"
        r"jo_vdp2_draw_bitmap_nbg1_line|jo_vdp2_clear_bitmap_nbg1|"
        r"jo_vdp2_draw_bitmap_nbg1_square|rsdk_fill_screen_solid)\b")
    weak: list[str] = []
    for fn in REQUIRED_DRAWING_SYMBOLS:
        body_re = re.search(
            rf"\b{re.escape(fn)}\s*\([^)]*\)\s*\{{(.*?)\n\}}",
            c_text, re.DOTALL)
        if not body_re:
            weak.append(f"{fn}:body-not-isolated")
            continue
        body = body_re.group(1)
        line_count = _body_lines(c_text, fn)
        has_primitive = bool(saturn_primitive_re.search(body))
        if line_count < 5 or not has_primitive:
            weak.append(f"{fn}:lines={line_count},has_primitive={has_primitive}")
    if weak:
        return False, (f"P3 RED: drawing primitives present but appear "
                       f"stubbed — {weak}")
    return True, (f"P3 GREEN: all {len(REQUIRED_DRAWING_SYMBOLS)} "
                  "drawing primitives have non-stub Saturn bodies")


# --- P4: UIBackground_DrawNormal fills in the stub ----------------------

def check_p4(verbose: bool) -> tuple[bool, str]:
    src_c = ROOT / "src" / "mania" / "Objects" / "Menu" / "UIBackground.c"
    if not src_c.exists():
        return False, "P4 RED: UIBackground.c missing"
    text = src_c.read_text(errors="replace")
    # Isolate UIBackground_DrawNormal body.
    m = re.search(r"\bUIBackground_DrawNormal\s*\([^)]*\)\s*\{",
                  text, re.DOTALL)
    if not m:
        return False, "P4 RED: UIBackground_DrawNormal not defined"
    start = m.end() - 1
    depth = 0
    end = start
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                end = i
                break
    body = text[start:end]
    # Must NOT contain Phase 3.2.a FIXME stub marker.
    if "FIXME Phase 3.2.c" in body or "FIXME Phase 3.2.b" in body:
        return False, "P4 RED: UIBackground_DrawNormal still has FIXME stub"
    # Must call the three new primitives.
    needs = ["rsdk_fill_screen", "rsdk_draw_circle"]
    missing = [n for n in needs if n not in body]
    if missing:
        return False, (f"P4 RED: UIBackground_DrawNormal missing calls to "
                       f"{missing}")
    return True, ("P4 GREEN: UIBackground_DrawNormal wires "
                  "rsdk_fill_screen + rsdk_draw_circle per decomp L44-70")


# --- driver -------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    print("Phase 3.2.b (Task #146) UIWidgets + ManiaString + drawing primitives gate")
    print("=" * 78)
    results = [
        check_p1(args.verbose),
        check_p2(args.verbose),
        check_p3(args.verbose),
        check_p4(args.verbose),
    ]
    for ok, msg in results:
        print(("  PASS  " if ok else "  FAIL  ") + msg)
    overall = all(ok for ok, _ in results)
    print("=" * 78)
    print(f"OVERALL: {'GREEN' if overall else 'RED'}")
    return 0 if overall else 1


if __name__ == "__main__":
    sys.exit(main())
