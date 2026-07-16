#!/usr/bin/env python3
# qa_parity_arc.py -- WHOLE-ARC visual+perf parity sweep (binding-whole-arc-parity-process).
# Boots nothing itself: run tools/_gl_boot.ps1 first, then this. It walks the live chain
# boot->Logos->Title->Menu->AIZ->GHZCutscene->GHZ, and every PERIOD seconds captures a
# RENDERED screenshot (UDP SCREENSHOT -> _shots/), tagging it (idx, folder, cont, fps) and
# computing pixel-stats (mean luma + pink/black auto-flag) so the agent can VIEW every frame
# and judge parity vs the ORIGINAL (decomp + refs). This is the tool that stops witness-green
# QA: it produces the frame set the agent MUST view + a manifest of fps/flags per scene.
#
# Usage:  pwsh tools/_gl_boot.ps1   (then)   python tools/qa_parity_arc.py [SECONDS] [PERIOD]
# Output: _shots/parity_<idx>_<folder>_c<cont>_<fps>fps<flag>.png  +  _shots/parity_manifest.txt
import importlib.util, socket, time, glob, os, sys
from pathlib import Path
H = Path("tools")
def load(m, f):
    s = importlib.util.spec_from_file_location(m, H / f); x = importlib.util.module_from_spec(s); s.loader.exec_module(x); return x
qt = load("qa_trace", "qa_trace.py")
mt = Path("game.map").read_text(errors="replace")
rd = qt.Reader(True, None, "127.0.0.1", 55355)
SHOTS = "/d/sonicmaniasaturn/_shots"
SECS   = float(sys.argv[1]) if len(sys.argv) > 1 else 150.0
PERIOD = float(sys.argv[2]) if len(sys.argv) > 2 else 2.0
csf = rd.sym(mt, "RSDK::currentSceneFolder")
def fol():
    try: return rd.mem.read_saturn(csf, 16).split(b"\0")[0].decode(errors="replace") or "?"
    except Exception: return "?"
def u(n):
    try: return rd.r32(rd.sym(mt, n))
    except Exception: return None

# optional pixel-stats (PIL). If absent, flags stay "-".
try:
    from PIL import Image
    HAVE_PIL = True
except Exception:
    HAVE_PIL = False
def pix_flag(png):
    if not HAVE_PIL: return "-", -1
    try:
        im = Image.open(png).convert("RGB"); im = im.resize((80, 50))
        px = list(im.getdata()); n = len(px)
        r = sum(p[0] for p in px)/n; g = sum(p[1] for p in px)/n; b = sum(p[2] for p in px)/n
        luma = 0.299*r + 0.587*g + 0.114*b
        flag = ""
        if luma < 12: flag += "BLACK"                    # all-black frame (missing render)
        if r > 90 and b > 90 and g < r*0.6 and g < b*0.6: flag += "PINK"  # magenta/pink sky/palette bug
        if luma > 235: flag += "WHITE"                   # blown-out (flash/fade stuck)
        return (flag or "ok"), round(luma, 1)
    except Exception:
        return "err", -1

sk = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); sk.settimeout(1.0)
manifest = []; idx = 0; t0 = time.time()
prev_cont = None; prev_wall = None
print("idx   t   folder        cont     fps   luma  flag   file")
while time.time() - t0 < SECS:
    f = fol(); c = u("p6_w_cont_frames"); w = time.time()
    fps = (c - prev_cont) / (w - prev_wall) if (prev_cont is not None and c is not None and w > (prev_wall or w)) else 0.0
    prev_cont, prev_wall = c, w
    before = set(glob.glob(SHOTS + "/*.png"))
    sk.sendto(b"SCREENSHOT\n", ("127.0.0.1", 55355)); time.sleep(0.5)
    new = sorted(set(glob.glob(SHOTS + "/*.png")) - before)
    if new:
        flag, luma = pix_flag(new[-1])
        safef = (f or "none").replace("/", "_")
        dst = "%s/parity_%02d_%s_c%s_%dfps_%s.png" % (SHOTS, idx, safef, c, round(fps), flag)
        try: os.replace(new[-1], dst); fn = os.path.basename(dst)
        except Exception as e: fn = "RENAME_ERR:%s" % e
        manifest.append((idx, round(w-t0,1), f, c, round(fps,1), luma, flag, fn))
        print("%3d %4.0f %-12s %-7s %5.1f %5s %-6s %s" % (idx, w-t0, f, c, fps, luma, flag, fn))
    idx += 1; time.sleep(PERIOD)
open(SHOTS + "/parity_manifest.txt", "w").write(
    "idx t folder cont fps luma flag file\n" +
    "\n".join("%d %.1f %s %s %.1f %s %s %s" % m for m in manifest))
# scene summary
print("\n=== per-scene summary (fps range + flags) ===")
scenes = {}
for m in manifest:
    scenes.setdefault(m[2], []).append(m)
for sc, ms in scenes.items():
    fpss = [x[4] for x in ms]; flags = sorted(set(x[6] for x in ms if x[6] not in ("ok","-")))
    print("  %-12s frames=%d fps=%.1f-%.1f  flags=%s  first=%s" % (
        sc, len(ms), min(fpss), max(fpss), ",".join(flags) or "none", ms[0][7]))
print("\nVIEW every parity_*.png and judge vs the ORIGINAL (decomp + refs). BLACK/PINK/WHITE = auto-suspect.")
