#!/usr/bin/env python3
# qa_objprof_watch.py -- #325 front-end ProcessObjects profiler over the live chain.
#
# Samples the shipping witnesses each period and emits JSONL with the split the
# lever decision needs (data-driven, per the ProcessObjects-dominates finding):
#   * cyc_obj           : FRT ticks of the WHOLE catch-up group (N ticks/frame)
#   * tick_last (N)     : logic ticks in that group (P6_TICK_CATCHUP, cap 4)
#   * objsec_static     : the class StaticUpdate loop (LAST tick, FRT)  [#325 new]
#   * objsec_loop1/2/3  : inRange-scan+Update / typeGroup / lateUpdate (LAST tick)
#   * stat_max/_cls/_n  : worst single staticUpdate + its stage class index
#   * scan_pop/maxslot/bounds, scancull_near/n, fe_ghz_cull : scan shape + cull state
#   * objupd_us/n[64]   : per-classID Update FRT accumulation (loop1 internals)
# fps = 60 * d(cont_frames)/d(vbl_count) between samples (headless-speed invariant).
#
# --ab SECS : once the GHZ landing is live, after SECS of cull-ON sampling poke
#             g_p6_fe_cull_override=0 (cull OFF = the 186898e RED behavior), sample
#             SECS, then poke 1 back. The JSONL rows carry cull state -> the gate
#             computes RED vs GREEN from ONE session.
#
# Host-side only -- zero shipping-frame cost. Run against qa_live.ps1 -NoMonitor.
import importlib.util, json, sys, time
from pathlib import Path

H = Path(__file__).resolve().parent
def load(m, f):
    s = importlib.util.spec_from_file_location(m, H / f)
    x = importlib.util.module_from_spec(s); s.loader.exec_module(x); return x
qt = load("qa_trace", "qa_trace.py")
mt = Path("game.map").read_text(errors="replace")
rd = qt.Reader(True, None, "127.0.0.1", 55355)
csf = rd.sym(mt, "RSDK::currentSceneFolder")
SYMS = {}
def A(nm):
    a = SYMS.get(nm)
    if a is None:
        a = rd.sym(mt, nm) or rd.sym(mt, "RSDK::" + nm) or 0
        SYMS[nm] = a
    return a
def U(nm):
    a = A(nm)
    return rd.r32(a) if a else None
def folder():
    try: return rd.mem.read_saturn(csf, 16).split(b"\0")[0].decode(errors="replace")
    except Exception: return None
def s32(v): return v - 0x100000000 if v is not None and v >= 0x80000000 else v

def write_saturn32(addr, val):
    """WRITE_CORE_RAM with the Beetle 16-bit pair-swap applied (inverse of
    read_saturn). WRAM-H phys -> SYSTEM_RAM offset 0x100000+(a-0x06000000)."""
    off = None
    for lo, hi, base in rd.mem._REGIONS:
        if lo <= addr < hi:
            off = base + (addr - lo); break
    if off is None:
        raise ValueError("addr not SYSTEM_RAM")
    raw = bytearray(int(val & 0xFFFFFFFF).to_bytes(4, "big"))
    for i in range(0, 4, 2):
        raw[i], raw[i + 1] = raw[i + 1], raw[i]
    payload = " ".join("%02x" % b for b in raw)
    # WRITE_CORE_RAM sends NO reply in this RetroArch build (MEASURED: recvfrom
    # timed out and killed the first A/B run) -- fire-and-forget, then verify by
    # reading the value back (READ replies normally).
    rd.mem.sock.sendto(("WRITE_CORE_RAM %x %s\n" % (off, payload)).encode("ascii"), rd.mem.addr)
    return None

NAMES = ["p6_w_cont_frames", "p6_perf_vbl_count", "p6_w_perf_cks", "p6_w_tick_last",
         "p6_w_perf_cyc_input", "p6_w_perf_cyc_obj", "p6_w_perf_cyc_draw",
         "p6_w_perf_cyc_present", "p6_w_perf_cyc_fgbg",
         "p6_w_objsec_static", "p6_w_objsec_pre", "p6_w_objsec_loop1",
         "p6_w_objsec_loop2",
         "p6_w_objsec_loop3", "p6_w_stat_max", "p6_w_stat_max_cls", "p6_w_stat_n",
         "p6_w_scan_pop", "p6_w_scan_maxslot", "p6_w_scan_bounds",
         "p6_w_scancull_near", "p6_w_scancull_n", "g_p6_fe_ghz_cull",
         "g_p6_fe_cull_override", "p6_w_draw_tail"]

OUT = Path(sys.argv[1] if len(sys.argv) > 1 else "_objprof.jsonl")
DUR = float(sys.argv[2]) if len(sys.argv) > 2 else 480.0
AB = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0  # seconds per A/B phase at landing

def upd_arrays():
    """Read the per-classID Update AND StaticUpdate FRT accumulators (256 B each)."""
    out = {}
    for nm in ("p6_w_objupd_us", "p6_w_objupd_n", "p6_w_statupd_us", "p6_w_statupd_n"):
        a = A(nm)
        if not a:
            out[nm] = None; continue
        try:
            b = rd.mem.read_saturn(a, 256)
            out[nm] = [s32(int.from_bytes(b[i:i+4], "big")) for i in range(0, 256, 4)]
        except Exception:
            out[nm] = None
    return out

fout = OUT.open("w"); ts = time.time(); lastf = None
prev = {}
ghz_t0 = None; ab_state = "on"; ab_flip_t = None
while time.time() - ts < DUR:
    t = time.time() - ts
    r = {"t": round(t, 2), "folder": folder()}
    for nm in NAMES:
        r[nm.replace("p6_w_", "").replace("p6_perf_", "")] = s32(U(nm))
    # fps from deltas
    c, v = r.get("cont_frames"), r.get("vbl_count")
    if c is not None and v is not None and prev.get("c") is not None and v != prev["v"]:
        r["fps"] = round(60.0 * (c - prev["c"]) / (v - prev["v"]), 2)
    prev = {"c": c, "v": v}
    fout.write(json.dumps(r) + "\n"); fout.flush()
    if r["folder"] != lastf:
        print("[%6.1f] folder=%r cont=%s fps=%s" % (t, r["folder"], c, r.get("fps")), flush=True)
        lastf = r["folder"]
        # dump the per-class Update arrays at each leg transition (accumulators)
        ua = upd_arrays(); ua["t"] = r["t"]; ua["folder"] = r["folder"]; ua["kind"] = "objupd"
        fout.write(json.dumps(ua) + "\n"); fout.flush()
    if r["folder"] == "GHZ" and (c or 0) > 0:
        if ghz_t0 is None:
            ghz_t0 = t
            print("[%6.1f] GHZ landing live (cull=%s)" % (t, r.get("g_p6_fe_ghz_cull")), flush=True)
        if AB > 0:
            if ab_state == "on" and t - ghz_t0 > AB:
                write_saturn32(A("g_p6_fe_cull_override"), 0)
                print("[%6.1f] A/B: poked cull override=0 (RED behavior); readback=%s"
                      % (t, s32(U("g_p6_fe_cull_override"))), flush=True)
                ab_state = "off"; ab_flip_t = t
            elif ab_state == "off" and t - ab_flip_t > AB:
                write_saturn32(A("g_p6_fe_cull_override"), 1)
                print("[%6.1f] A/B: poked cull override=1 (GREEN back); readback=%s"
                      % (t, s32(U("g_p6_fe_cull_override"))), flush=True)
                ab_state = "done"; ab_flip_t = t
            elif ab_state == "done" and t - ab_flip_t > AB:
                break
        elif t - ghz_t0 > 45:
            break
    time.sleep(0.55)
# final per-class dump
ua = upd_arrays(); ua["t"] = round(time.time() - ts, 2); ua["folder"] = folder(); ua["kind"] = "objupd"
fout.write(json.dumps(ua) + "\n")
fout.close()
print("done ->", OUT)
