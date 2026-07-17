#!/usr/bin/env python3
"""Gates for the 2026-07-17 batch regressions (user report on d1ea60d+a8fa854).

R2 "spawn next to the save post" (suspect StarPost 8d98790):
  G1/G2 -- the api->starpost_slot per-frame rewire leaves the PACK `StarPost`
  global DANGLING across a scene load: ClearStageObjects NULLs *staticVars
  (rsdkv5 Object.cpp:1979-1987), DefragmentAndGarbageCollectStorage moves the
  DATASET_STG pool, AllocateStorage lays the new scene's blocks -- all INSIDE
  one frame, while the pack copy still holds last frame's address. Pack writers
  that run DURING the load then stomp re-allocated pool memory: SaveGame is
  GameConfig global #2 and StarPost #18 (parse_gameconfig, MEASURED), so
  SaveGame_StageLoad -> SaveGame_LoadSaveData's fresh-act StarPost reset
  (decomp SaveGame.c:151-163) fires through the dangling pointer at EVERY
  folder-change load (Menu seam included). The fix contract:
    G1: p6_scene_load_and_arm (p6_io_main.cpp) detaches the pack pointer
        (p6_starpost_detach) BEFORE any load step.
    G2: p6_starpost_detach (p6_closure_edge.c) re-aims the pack pointer at the
        safe zeroed instance AND re-zeroes it -- the decomp fresh-act reset
        semantics (SaveGame.c:153-162: postIDs/playerPositions/playerDirections/
        stored* -> 0) aimed at the instance the pack writes can safely hit.
  G4 (don't-regress): the ONLY stage-load spawn reposition stays the verbatim
  decomp guard `if (StarPost->postIDs[p])` (StarPost.c:92) -- first-entry
  postIDs are zero (fresh statics are zero-allocated, Storage.cpp:350-351, and
  G2 keeps the safe instance zero) -> spawn source = Scene1.bin Player
  placement (GHZ1 slot 887 x=108, MEASURED).

R1 "black menu" (suspect FXFade d1ea60d):
  G3a -- OFFLINE SIMULATION of the decomp FXFade state machine (FXFade.c:70-115)
  with the MEASURED Menu/Scene1.bin placement (slot 8: timer=512 speedIn=0
  wait=0 speedOut=12 color=0 oneWay=0 eventOnly=0) plus the MenuSetup_InitAPI
  hold (MenuSetup.c:414-415 writes timer=512 while !initializedAPI; on Saturn
  p6_menu_apic_init makes InitAPI return true on the FIRST StaticUpdate --
  p6_menu_closure.c:130-138). RED if the wash does NOT converge to 0 within 64
  ticks -- i.e. the gate FIRES if hypothesis R1-a (non-decaying fade) is TRUE
  in the decomp semantics the port compiles. (Result: it converges -- R1-a/b
  disproved at the state-machine level; the gate stays as a don't-regress on
  the ported semantics.)
  G3b -- the Saturn latch lifecycle (p6_ovl_ghz.c): the fade_fn wrapper must
  CONSUME (zero) both wash latches every call, and the Draw shim must latch
  only while timer>0. A stale latch would hold the VDP2 color offset black.

Run: python tools/qa_starpost_fxfade_gates.py   (exit 0 = all GREEN)
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
edge    = read(os.path.join(P6, "p6_closure_edge.c"))
ovl     = read(os.path.join(P6, "p6_ovl_ghz.c"))

# ---- G1: load-time detach call inside p6_scene_load_and_arm ----------------
m = re.search(r"static void p6_scene_load_and_arm\(void\)\s*\{", io_main)
if not m:
    check("G1.detach-call", False, "p6_scene_load_and_arm not found")
else:
    # take the function body up to the next file-scope function definition
    body = io_main[m.end():]
    nxt = re.search(r"\n\}\n(?:static |extern |__attribute__|/\*|//|#if)", body)
    span = body[: nxt.start() + 2] if nxt else body[:40000]
    called = "p6_starpost_detach()" in span
    # the call must precede the first load step (LoadSceneFolder / layout mount)
    if called:
        first_load = min([i for i in (span.find("LoadSceneFolder"),
                                      span.find("p6_layout_mount_for_scene")) if i >= 0]
                         or [len(span)])
        ok = span.find("p6_starpost_detach()") < first_load
        check("G1.detach-call", ok,
              "p6_starpost_detach() present and precedes the load steps" if ok
              else "p6_starpost_detach() called AFTER the load began")
    else:
        check("G1.detach-call", False,
              "p6_scene_load_and_arm has NO p6_starpost_detach() -- the pack "
              "StarPost pointer dangles across ClearStageObjects/defrag/realloc "
              "while SaveGame_StageLoad writes through it (SaveGame.c:151-163)")

# ---- G2: the detach helper re-aims + re-zeroes the safe instance ------------
dm = re.search(r"void p6_starpost_detach\(void\)\s*\{(.*?)\n\}", edge, re.S)
if not dm:
    check("G2.detach-helper", False,
          "p6_closure_edge.c does not define p6_starpost_detach")
else:
    b = dm.group(1)
    reaim = "StarPost = &p6_aiz_starpost_instance" in b and "StarPost = NULL" in b
    zeroed = ("p6_aiz_starpost_instance" in b
              and re.search(r"(memset|=\s*0;)", b) is not None
              and ("sizeof(p6_aiz_starpost_instance)" in b))
    check("G2.detach-helper", reaim and zeroed,
          "re-aims pack StarPost at the zeroed safe instance (P6_AIZ_TEST) / "
          "NULL, and re-zeroes the whole instance (SaveGame.c:153-162 fresh-act "
          "semantics)" if (reaim and zeroed) else
          f"helper incomplete (reaim={reaim} zeroed={zeroed})")

# ---- G3a: FXFade menu lifecycle simulation (decomp FXFade.c + MenuSetup.c) --
def simulate_menu_fade():
    # Placed vars, MEASURED from Menu/Scene1.bin slot 8 (this file's parser run
    # 2026-07-17): timer=512 speedIn=0 wait=0 speedOut=12 color=0 oneWay=0.
    timer, speedIn, speedOut, oneWay = 512, 0, 12, 0
    # FXFade_Create (FXFade.c:45-58): zero speeds default to 32; state by timer.
    if not speedIn:  speedIn = 32
    if not speedOut: speedOut = 32 if speedOut == 0 and False else speedOut or 32
    speedOut = 12  # placed nonzero -> kept (FXFade.c:48-49 guard is `if (!speedOut)`)
    state = "FadeIn" if timer > 0 else "FadeOut"
    initializedAPI = False
    washes = []
    alive = True
    for tick in range(64):
        # MenuSetup_StaticUpdate (non-Plus, MenuSetup.c:89-115): InitAPI writes
        # timer=512 while !initializedAPI; p6_menu_apic_init makes it return
        # true immediately (p6_menu_closure.c re-trace) -> one-frame hold.
        if not initializedAPI:
            timer = 512
            initializedAPI = True
        if alive:
            if state == "FadeIn":                      # FXFade.c:102-115
                if timer <= 0:
                    if oneWay: state = None
                    else: alive = False                # destroyEntity
                else:
                    timer -= speedOut
        wash = max(0, min(255, timer)) if alive and timer > 0 else 0
        washes.append(wash)
    return washes

w = simulate_menu_fade()
conv = next((i for i, v in enumerate(w) if v == 0), None)
check("G3a.menu-fade-converges", conv is not None and all(v == 0 for v in w[conv:]),
      f"decomp state machine clears the wash at tick {conv} "
      f"(512 hold 1 frame, then -12/frame; R1-a 'never ramps out' DISPROVED)"
      if conv is not None else "wash NEVER clears -- R1-a confirmed")

# ---- G3b: Saturn latch lifecycle source contract ----------------------------
fm = re.search(r"static void p6_ghzcut_fade_fn\(int \*outWhite, int \*outBlack\)\s*\{(.*?)\n\}", ovl, re.S)
if not fm:
    check("G3b.latch-consume", False, "p6_ghzcut_fade_fn not found")
else:
    b = fm.group(1)
    ok = ("s_fxfade_wash_w = 0" in b) and ("s_fxfade_wash_b = 0" in b)
    check("G3b.latch-consume", ok,
          "fade_fn zeroes both FXFade latches each call (stale-latch black "
          "impossible; R1-b disproved)" if ok else
          "fade_fn does NOT zero the latches -- a one-frame latch sticks (R1-b)")
sm = re.search(r"static void p6_fxfade_draw\(void\)\s*\{(.*?)\n\}", ovl, re.S)
if not sm:
    check("G3b.shim-latch-guard", False, "p6_fxfade_draw shim not found")
else:
    ok = re.search(r"if\s*\(v\s*>\s*0\)", sm.group(1)) is not None
    check("G3b.shim-latch-guard", ok,
          "shim latches only while timer>0 (settled oneWay fades latch nothing)"
          if ok else "shim latches without a timer>0 guard")

# ---- G4: don't-regress -- the verbatim postIDs spawn guard ------------------
sp = read(os.path.join(ROOT, "tools", "_decomp_raw",
                       "SonicMania_Objects_Global_StarPost.c"))
ok = "if (StarPost->postIDs[p])" in sp and "player->position.x = StarPost->playerPositions[p].x" in sp
check("G4.spawn-guard-verbatim", ok,
      "StarPost_StageLoad reposition stays behind the decomp postIDs guard "
      "(StarPost.c:92/119) -- zero postIDs at first entry == Scene.bin spawn"
      if ok else "the StageLoad reposition guard was altered")

print()
if fails:
    print("RED gates:", ", ".join(fails))
    sys.exit(1)
print("ALL GREEN")
sys.exit(0)
