#!/usr/bin/env python3
"""R1 "black menu" (chain) -- structural gate for the apic_init timing fix.

MEASURED ROOT (live qa_netmem over headless RA, Menu window of the CHAIN build):
  p6_w_fxfade_timer = 500 STUCK (never decrements past the first FadeIn tick),
  p6_w_fxfade_draws climbs (the shim runs every frame), p6_w_ghzcut_fade = 500
  -> p6_vdp2_fade_apply washes the whole screen BLACK. The FXFade placed in
  Menu/Scene1.bin (slot 8: timer=512 speedIn=0 wait=0 speedOut=12) runs exactly
  ONE FadeIn tick (512-12=500) then FREEZES, because MenuSetup->initializedAPI
  never latches -> MenuSetup_InitAPI (MenuSetup.c:414-415) keeps FORCING
  fxFade->timer=512 (and the entity's own FadeIn never gets to drive it down).

WHY initializedAPI never latches in the CHAIN (the source defect):
  The AUTH-GATE FLIP p6_menu_apic_init (p6_menu_closure.c:130) installs the
  offline APICallback (authStatus/storageStatus/saveStatus=STATUS_OK) +
  globals->noSave=true that InitAPI (MenuSetup.c:467) reads to return true.
  It is triggered at the TOP of p6_frontend_frame (p6_io_main.cpp:7820) behind
  a function-local `static s_menu_apic_done` one-shot, gated ONLY on
  s_ovl.menu_apic_init_fn != NULL. In the PLAIN-MENU boot p6_menu_reload runs
  FIRST (p6_io_main.cpp:9850) so the first p6_frontend_frame ticks with
  currentSceneFolder=="Menu" -> apic_init runs at the right moment. In the
  CHAIN, boot loads LOGOS first; s_ovl.menu_apic_init_fn is already non-NULL
  (set at overlay entry), so apic_init fires during LOGOS frame 1 and latches
  s_menu_apic_done=1. By the time the chain reaches the Menu (Logos->Title->
  Menu), the one-shot is spent and the Logos-time APICallback/noSave writes do
  not survive the intervening TitleSetup + MenuSetup StageLoad scene inits ->
  InitAPI reads authStatus!=STATUS_OK / noSave!=true -> returns false forever ->
  initializedAPI never latches -> the 512-force holds -> the fade never rolls out
  -> BLACK.

FIX CONTRACT (parity-correct: make apic_init run while the Menu is the loaded
  scene, exactly like the plain-menu timing):
  the apic_init one-shot MUST be gated on currentSceneFolder=="Menu" so it
  cannot fire during Logos/Title and DOES fire on the first Menu frame (the
  same moment plain-menu runs it). This gate asserts that source contract.

RED on the current build (the one-shot is folder-agnostic -> fires during
Logos). GREEN after the fold-gate lands.

Run: python tools/qa_menu_fade.py   (exit 0 = GREEN)
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
P6   = os.path.join(ROOT, "tools", "_portspike", "_p6")

def read(p):
    with open(p, "r", encoding="utf-8", errors="replace") as f:
        return f.read()

fails = []
def check(name, ok, detail):
    print(f"[{'GREEN' if ok else 'RED  '}] {name}: {detail}")
    if not ok:
        fails.append(name)

io_main = read(os.path.join(P6, "p6_io_main.cpp"))

# ---- G1: the apic_init one-shot must be folder-gated to "Menu" --------------
# Find the apic_init trigger block (the s_ovl.menu_apic_init_fn call site).
m = re.search(
    r"s_menu_apic_done\s*&&\s*s_ovl\.menu_apic_init_fn.*?menu_apic_init_fn\(\);",
    io_main, re.S)
if not m:
    check("G1.apic-init-folder-gate", False,
          "apic_init trigger block (s_menu_apic_done / menu_apic_init_fn) not found")
else:
    # The guard that STARTS the apic block: from the surrounding if(...) down to
    # the call, there must be a currentSceneFolder=="Menu" test so the one-shot
    # cannot latch during Logos/Title.
    # Look at a window from ~200 chars before the call to the call itself.
    start = max(0, m.start() - 260)
    window = io_main[start:m.end()]
    folder_gated = re.search(
        r'strcmp\s*\(\s*currentSceneFolder\s*,\s*"Menu"\s*\)', window) is not None
    check("G1.apic-init-folder-gate", folder_gated,
          'apic_init one-shot is gated on currentSceneFolder=="Menu" -- it '
          "cannot fire during Logos/Title, and runs on the first Menu frame "
          "(the plain-menu timing) so MenuSetup_InitAPI reads the installed "
          "APICallback/noSave and initializedAPI latches -> the 512-force stops "
          "-> the FXFade rolls out"
          if folder_gated else
          "apic_init one-shot is NOT folder-gated -- in the CHAIN it fires "
          "during LOGOS frame 1 (menu_apic_init_fn already non-NULL) and the "
          "spent one-shot never re-runs for the Menu -> initializedAPI never "
          "latches -> fade stuck at 500 -> BLACK MENU")

# ---- G2: don't-regress -- the plain-menu boot still runs reload before tick -
# p6_menu_reload() must still precede the first p6_frontend_frame() in the
# P6_FRONTEND_MENU boot branch (so the folder is "Menu" on the first tick).
mb = re.search(r"p6_menu_reload\(\);\s*\n\s*if\s*\(p6_ghz_continuous_armed\)\s*\n\s*p6_frontend_frame\(\);",
               io_main)
check("G2.plain-menu-boot-order", mb is not None,
      "plain-menu boot still runs p6_menu_reload() then p6_frontend_frame() "
      "(folder=='Menu' on the first tick -> apic_init fires there too)"
      if mb is not None else
      "plain-menu boot order changed -- reload must precede the first tick")

print()
if fails:
    print("RED gates:", ", ".join(fails))
    sys.exit(1)
print("ALL GREEN")
sys.exit(0)
