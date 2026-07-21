#!/usr/bin/env python3
"""qa_parity_oracle.py -- COMPREHENSIVE, decomp-derived whole-arc parity oracle.

BINDING PURPOSE (user, 2026-07-16): the oracle must NOT be scoped to the handful
of symptoms the user happened to list -- that list was EXAMPLES, not the spec.
The oracle derives "what MUST be true" from the DECOMP GROUND TRUTH for EVERY
object class and EVERY subsystem, then reports whatever diverges live -- so it
surfaces bugs neither the user nor the agent has named. NO pixels, NO visual
inspection ("you shouldn't need visual inspection to fix this with your memory
tools" -- correct). The agent owns bug discovery; the user must never enumerate.

Ground truth (all decomp-derived, not a symptom list):
  * docs/scene_objects.json  -> per-scene stage_config.objects (EVERY class the
    decomp registers for that stage) + sfx list. The manifest of what must exist.
  * the LIVE object-class REGISTRATION table (sceneInfo.classCount +
    stageObjectIDs @0x002FEF80 + objectClassList backing @0x060D8000, 72B/entry,
    name resolved by md5(objectName) -- the SAME idiom as _classreg_probe.py).
  * the LIVE entity pool (qa_trace generic walk) -> per-class instance counts,
    animID, visible, position, state.
  * per-class frame-count witnesses (p6_w_<class>_aniframes) -> valid animID range.
  * the all-ordinal edge-audit array p6_w_edge_hits[P6_EDGE_MAX] -> ANY ported
    object that forwarded to its own STUB (silently-broken port, every ordinal).
  * subsystem witnesses (str/sfx audio, tick/cont speed, pal_hash palette).

Generic detectors (each catches a WHOLE CLASS of bug across ALL objects, so an
unlisted bug is still caught):
  D1 REGISTER   a manifest class that is NOT in the live registration table
                (whole object class never registered -- ANY class).
  D2 CLASSREG   a registered class whose *staticVars is NULL / classID mismatches
                its slot (broken registration / link-time stub bind).
  D3 SPAWN      a registered gameplay class with ZERO live instances where the
                manifest expects placements (placed-but-not-spawned -- ANY class).
  D4 ANIM       any live entity whose animID is out of range for its class's
                animation set (wrong/garbage pose -- ANY entity, not just named).
  D5 VISIBLE    a gameplay scene with no visible player, or an entity with a
                sane position but never drawn (invisible-sprite class).
  D6 EDGE       any nonzero p6_w_edge_hits[ordinal] (broken-port class, ALL ports).
  D7 SPEED      game-speed d(tick)/d(wall) < 55Hz (logic-slow / #243 pacing).
  D8 AUDIO      BGM/SFX differential vs the decomp's Music track map + the live
                channel array: no music, WRONG music (Saturn boot chime not game
                BGM), requested-but-not-playing, dead SFX, garbage soundIDs.
  D9 PALETTE    the composited CRAM (SH-2-hashed into WRAM witnesses -> netmem sees
                the render backend): magenta/pink count (wrong-colour / CRAM-bank
                collision -- the pink-flash class, ANY bank) + blank/black CRAM.
  D10 TEMPORAL  entity-count collapse or a player frozen in place across the
                window (streaming/pool loss, stuck-state -- generic).
  D11 SANE      universal invariants (qa_invariants: sane pos, players, camera).
  D12 SPRITE    VDP1 drop count rising (MISSING SPRITES the engine wanted drawn)
                + slot-cache thrash (sprites FLICKER).
  D13 PERF      render fps << 60 in gameplay (unplayable framerate = parity miss).
  D14 LAYER     VDP2 BG plane unarmed (black sky, GHZ+GHZCutscene) or FG-High not
                composited (missing foreground tiles, GHZ).
  BLIND         a backend witness that read None for the WHOLE scene window -> the
                dimension it feeds is blind HERE; reported LOUD, never silently
                skipped (None != clean -- the anti-false-GREEN guard, FN-2).

COVERAGE (what a defect in each maps to; * = needs a render witness, now present):
  sprite present  D12*  sprite anim   D4    colour/pink  D9*   BGM/SFX      D8
  missing layer   D14*  registration  D1    spawn        D3    framerate    D13*
  game speed      D7    stuck/frozen  D10   sane pos     D11   broken port  D6
  STILL PARTIAL (honest): exact per-sprite SCREEN PLACEMENT (pixel x,y) and
  BGM<->animation SYNC need a render-differential vs a PC-reference frame trace
  (the ultimate catch-all) -- the witness tier above catches presence/colour/
  count/layer/fps, not sub-pixel position. Registration D1/D3 are gated on the
  live class-table read (skew under investigation).

Usage (boots nothing -- run tools/_gl_boot.ps1 first so RA is live on game.cue):
  pwsh tools/_gl_boot.ps1
  python tools/qa_parity_oracle.py [SECONDS] [PERIOD]
Exit 0 = no divergence seen, 1 = >=1 divergence (the work queue), 2 = the live
harness is unhealthy (loud self-verify -- garbage is never trusted).
"""
from __future__ import annotations

import glob
import hashlib
import importlib.util
import json
import os
import re
import sys
import time
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_ROOT = _HERE.parent


def _load(mod, fn):
    spec = importlib.util.spec_from_file_location(mod, _HERE / fn)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)  # type: ignore[union-attr]
    return m


qa_trace = _load("qa_trace", "qa_trace.py")
qa_invariants = _load("qa_invariants", "qa_invariants.py")
qa_netmem = _load("qa_netmem", "qa_netmem.py")

# positionals = args that are neither a --flag NOR the value that follows --baseline
# (that value is a path, not the SECS/PERIOD positionals -- else it was misparsed
# as PERIOD and crashed float()).
_argv = sys.argv[1:]
_skip = set()
for _i, _a in enumerate(_argv):
    if _a == "--baseline" and _i + 1 < len(_argv):
        _skip.add(_i + 1)
_POS = [a for _i, a in enumerate(_argv) if not a.startswith("--") and _i not in _skip]
SECS = float(_POS[0]) if len(_POS) > 0 else 180.0
PERIOD = float(_POS[1]) if len(_POS) > 1 else 1.0
MAP = (_ROOT / "game.map").read_text(errors="replace")

# --- live registration-table geometry (from _classreg_probe.py, decomp-cited) ---
OBJCLASS_BASE = 0x060D8000
OBJCLASS_SIZE = 72
STAGEIDS = 0x002FEF80
EDGE_MAX = 96             # p6_w_edge_hits[P6_EDGE_MAX]; P6_EDGE_MAX=96 (p6_closure_edge.c:44)
                          # -- reading past 96 over-reads adjacent memory (garbage ordinals).

# scenes where the decomp starts a BGM track on entry (Music_PlayTrack)
AUDIO_EXPECT = {"Title", "Menu", "AIZ", "GHZCutscene", "GHZ"}
UI_SCENES = {"", "?", None, "Logos", "Title", "Menu"}
# authoritative CUE-track per scene (from AudioDevice::HandleStreamLoad's name->track
# map, p6_io_main.cpp: GreenHill1=2 TitleScreen=3 AngelIsland=4 RubyPresence=5
# HBHMischief=6 BossEggman1=7). A set = the tracks legitimately heard in that scene
# (AIZ's beats transition AngelIsland->RubyPresence->HBHMischief->Eggman1). None =
# don't assert an exact track (only require SOME track playing). This is the audio
# REFERENCE: str_track != this = wrong BGM (the "Saturn boot chime not music" class).
# Decomp-verified per-scene CD-DA track sets (from HandleStreamLoad's name->track
# map + AIZSetup.c's cited beat sequence). The AIZ intro and the GHZCutscene
# (Heavies dig-site) are ONE CONTINUOUS cutscene that steps through the intro
# tracks: AngelIsland(4) -> RubyPresence(5, AIZSetup.c:188) -> HBHMischief(6,
# :372/:724) -> BossEggman1(7, :568/:809). So GHZCutscene legitimately continues
# one of {4,5,6,7} (measured track 5 = RubyPresence is CORRECT, NOT wrong) -- the
# earlier {6,2} was an unverified guess that false-flagged it. GHZ = GreenHill1(2).
BGM_EXPECT = {"GHZ": {2}, "AIZ": {4, 5, 6, 7}, "GHZCutscene": {4, 5, 6, 7}}


def build_name_dict():
    """Every Mania object name from the decomp file set + the scene manifest ->
    md5(name) digest (both byte orders, per _classreg_probe wswap idiom). Built at
    runtime so it never drifts from the cached decomp / manifest."""
    names = set()
    for f in glob.glob(str(_ROOT / "tools/_decomp_raw/SonicMania_Objects_*")):
        m = re.match(r"SonicMania_Objects_[^_]+_(.+)\.(c|h)$", os.path.basename(f))
        if m:
            names.add(m.group(1))
    # COMPLETE object-name set (544 classes) from the whole-game census -- this is
    # what makes per-class REGISTER/SPAWN parity COMPREHENSIVE (the cached decomp
    # file set is only ~41 names -> the old dict resolved 1/60 GHZ classes -> D1/D3
    # were structurally blind. object_census.json['objects'] is every Mania class).
    try:
        _oc = json.loads((_ROOT / "docs/object_census.json").read_text())
        for o in (_oc.get("objects") or {}):
            names.add(o)
    except Exception:
        pass
    try:
        man = json.loads((_ROOT / "docs/scene_objects.json").read_text())
        for zd in man.values():
            for o in (zd.get("stage_config", {}).get("objects", []) or []):
                names.add(o)
    except Exception:
        man = {}
    h2n = {}
    for n in names:
        d = hashlib.md5(n.encode()).digest()
        h2n[d] = n
        h2n[b"".join(d[i:i + 4][::-1] for i in range(0, 16, 4))] = n
    return h2n, man


H2N, MANIFEST = build_name_dict()

# Optional persisted classID->name map (tools/qa_classid_map.json), generated by
# dumping the FULL live objectClassList (0x060D8000) once per build and resolving
# every well-formed hash entry. When present it widens REGISTER/SPAWN/ANIM naming
# beyond what the per-run hash pass resolves. Keys are classID strings.
# REGENERATE after every rebuild (classIDs are registration-order; they can shift
# when the registered object set changes) -- the map records the build's game.map
# _end as a fingerprint and is IGNORED with a NOTE if it mismatches (stale map
# must not silently mislabel classes).
CLASSID_MAP = {}
try:
    _cm = json.loads((_HERE / "qa_classid_map.json").read_text())
    _fp = _cm.get("_end_fingerprint")
    _cur = re.search(r"0x([0-9a-fA-F]{16})\s+_end\s*$", MAP, re.M)
    if _fp and _cur and int(_cur.group(1), 16) == _fp:
        CLASSID_MAP = {int(k): v for k, v in _cm.get("map", {}).items()}
    elif _cm:
        sys.stderr.write("qa_parity_oracle: qa_classid_map.json is for a DIFFERENT build "
                         "(fingerprint mismatch) -- ignored; regenerate via --dump-classmap\n")
except FileNotFoundError:
    pass
except Exception as e:  # noqa: BLE001
    sys.stderr.write(f"qa_parity_oracle: classid map unreadable ({e}) -- ignored\n")

# EVERY witness the oracle reads. Listed centrally so --selftest can prove they
# ALL resolve in the CURRENT game.map BEFORE a live run -- a renamed/dropped
# symbol would otherwise read None and make a detector silently skip (false
# GREEN = the exact lying-gate this project forbids). This is the anti-regression
# guard for the ORACLE itself.
CORE_SYMS = ["p6_w_edge_hits", "p6_w_tick_frames", "p6_w_cont_frames",
             "p6_w_str_state", "p6_w_str_track", "p6_w_sfx_inited", "p6_w_pal_hash",
             # audio-differential (live channels) + SFX liveness
             "p6_w_snd_plays",
             # Nyquist-proof gameplay-SFX/BGM activity witnesses (GAP-A1 fix): the
             # engine channels[] show real gameplay SFX/stream arming (verbatim decomp
             # logic runs) but nothing drains them to the SCSP -> dead SFX. Sampled at
             # 60Hz SH-2-side into monotonic accumulators so a 1Hz oracle read still
             # sees every arm via delta. sfx_armed rising + snd_plays flat = dead SFX.
             "p6_w_sfx_armed_frames", "p6_w_sfx_arm_events", "p6_w_sfx_last_id",
             "p6_w_stream_frames", "p6_w_sfx_keyons",
             # backend-parity render witnesses (SH-2-hashed CRAM -> netmem-visible).
             # These are what make PALETTE/pink-flash detection possible at all --
             # without them D9 is blind (the whole reason the old oracle missed the
             # user's "tons of pink flashes"). A build lacking them = loud selftest RED.
             # magenta_max/magenta_frames are the Nyquist-proof transient-flash pair
             # (GAP-5/FN-1: the 1Hz read misses a sub-second pink flash; the monotonic
             # per-frame accumulators catch it by delta). The witness now scans the
             # FULL 2048-entry CRAM (was 1024 -> blind to VDP1 sprite palette banks).
             "p6_w_cram_hash", "p6_w_cram_magenta", "p6_w_cram_nonzero",
             "p6_w_cram_magenta_max", "p6_w_cram_magenta_frames",
             # per-256-bank magenta breakdown -> static-baseline vs real-flash split
             "p6_w_cram_mag_bank",
             # sprite/draw pipeline (missing-sprite, flicker, layer coverage)
             "p6_w_vdp1_drops", "p6_w_vdp1_evicts", "p6_w_dl_cmds_max",
             "p6_w_b1_registered", "p6_w_fg_highfill"]
REG_SYMS = ["RSDK::sceneInfo", "RSDK::objectEntityList", "RSDK::currentSceneFolder",
            "RSDK::channels"]

# per-class aniframes witness -> class name (drives the animID-range check)
ANIFRAMES = {
    "Ring": "p6_w_ring_aniframes", "Spring": "p6_w_spring_aniframes",
    "SpikeLog": "p6_w_spikelog_aniframes", "Spikes": "p6_w_spikes_aniframes",
    "ItemBox": "p6_w_itembox_aniframes", "Platform": "p6_w_platform_aniframes",
    "Shield": "p6_w_shield_aniframes", "Explosion": "p6_w_explosion_aniframes",
    "Animals": "p6_w_animals_aniframes", "Dust": "p6_w_dust_aniframes",
    "ScoreBonus": "p6_w_scorebonus_aniframes", "Motobug": "p6_w_batbrain_aniframes",
    "Batbrain": "p6_w_batbrain_aniframes", "Newtron": "p6_w_newtron_aniframes",
    "Bridge": "p6_w_brg_aniframes",
}


def s32(v):
    return v - 0x100000000 if v is not None and v >= 0x80000000 else v


def selftest():
    """OFFLINE guard (no emulator): prove the oracle's whole ground-truth surface
    is intact so it can NEVER rot into a false-GREEN. Fails LOUD (exit 2) if any
    witness symbol, ground-truth file, or the name dictionary is missing/degraded.
    Run this in verify_done.ps1 on EVERY build so a symbol rename / manifest drop
    is caught the moment it lands, not when a live sweep silently under-reports."""
    problems = []
    syms = list(CORE_SYMS) + list(REG_SYMS) + list(ANIFRAMES.values())
    for n in syms:
        if not re.search(r"0x[0-9a-fA-F]{16}\s+" + re.escape(n) + r"\s*$", MAP, re.M):
            problems.append(f"symbol MISSING from game.map: {n}")
    if not (_ROOT / "docs/scene_objects.json").exists():
        problems.append("ground-truth MISSING: docs/scene_objects.json")
    if len(H2N) < 200:
        problems.append(f"name dictionary degraded ({len(H2N)} entries; expect >=200 "
                        f"decomp+manifest object names) -- REGISTER/SPAWN/ANIM would be blind")
    if not glob.glob(str(_ROOT / "tools/_decomp_raw/SonicMania_Objects_*")):
        problems.append("decomp object cache MISSING: tools/_decomp_raw/SonicMania_Objects_*")
    print("=" * 72)
    print("qa_parity_oracle --selftest  (anti-lying-gate: prove the approach can't false-GREEN)")
    print("=" * 72)
    print(f"  witness symbols checked : {len(syms)}")
    print(f"  name-dict entries       : {len(H2N)}")
    print(f"  manifest scenes         : {len(MANIFEST)}")
    if problems:
        for p in problems:
            print(f"  FAIL {p}")
        print(f"qa_parity_oracle: SELFTEST RED -- {len(problems)} broken dependency(ies); "
              f"the oracle would UNDER-REPORT. Fix before trusting any GREEN.")
        return 2
    print("qa_parity_oracle: SELFTEST GREEN -- every ground-truth dependency resolves; "
          "no detector is blind.")
    return 0


def selftest_live():
    """LIVE guard (RA must be up): the offline selftest only proves the SYMBOLS
    exist in game.map -- it can't prove the netmem READ PATH delivers them. FN-2
    (adversarial-QA 2026-07-20): a witness that resolves in the map but reads None
    live still makes a detector silently skip. This connects and reads EVERY core
    witness once; any that read None are announced LOUD. Exit 2 if the harness is
    unhealthy or any core witness is dead live."""
    print("=" * 72)
    print("qa_parity_oracle --selftest-live  (prove the LIVE read path delivers every witness)")
    print("=" * 72)
    try:
        o = Oracle()
    except Exception as e:
        sys.stderr.write(f"selftest-live: LIVE HARNESS UNHEALTHY -- {e}\n")
        return 2
    dead = []
    for n in CORE_SYMS:
        try:
            v = o.w(n)
        except Exception as e:  # noqa: BLE001
            dead.append(f"{n} (read raised {e})")
            continue
        if v is None:
            dead.append(f"{n} (read None -- detector BLIND)")
    ch = o.channels()
    if not ch:
        dead.append("RSDK::channels (empty -- D8 audio channel array blind)")
    print(f"  core witnesses probed live : {len(CORE_SYMS)}")
    print(f"  channel array entries       : {len(ch)}")
    if dead:
        for d in dead:
            print(f"  DEAD {d}")
        print(f"qa_parity_oracle: SELFTEST-LIVE RED -- {len(dead)} witness(es) dead on the live "
              f"read path; those detectors would false-GREEN. Fix before trusting a run.")
        return 2
    print("qa_parity_oracle: SELFTEST-LIVE GREEN -- every core witness delivers a live value.")
    return 0


class Oracle:
    def __init__(self):
        self.rd = qa_trace.Reader(True, None, "127.0.0.1", 55355)  # loud self-verify
        self.mem = self.rd.mem

    def sym(self, n):
        return self.rd.sym(MAP, n)

    def w(self, n):
        a = self.sym(n)
        return self.rd.r32(a) if a is not None else None

    def rb(self, a, n):
        out = bytearray()
        left, cur = n, a
        while left > 0:
            take = min(2000, left)
            out += self.mem.read_saturn(cur, take)
            cur += take
            left -= take
        return bytes(out)

    def edge_hits(self):
        """Return {ordinal: count} for every nonzero p6_w_edge_hits slot."""
        a = self.sym("p6_w_edge_hits")
        if a is None:
            return {}
        raw = self.rb(a, 4 * EDGE_MAX)
        out = {}
        for i in range(EDGE_MAX):
            v = int.from_bytes(raw[4 * i:4 * i + 4], "big")
            # WRAM-H pair-swap already undone by read_saturn; value is direct
            if v:
                out[i] = v
        return out

    def registration(self):
        """Walk the LIVE object-class registration table -> list of
        {listIdx, classID, name, sv_ok}. Empty if classCount implausible."""
        si_sym = self.sym("RSDK::sceneInfo")
        if si_sym is None:
            return []
        si = self.rb(si_sym, 40)
        class_count = (si[30] << 8) | si[31]
        if class_count == 0 or class_count > 0x100:
            return []
        ids_raw = self.rb(STAGEIDS, 4 * class_count)
        ids = [int.from_bytes(ids_raw[4 * i:4 * i + 4], "big") for i in range(class_count)]
        maxid = max(ids) if ids else 0
        cls_raw = self.rb(OBJCLASS_BASE, OBJCLASS_SIZE * (maxid + 1))
        rows = []
        for listIdx, cid_idx in enumerate(ids):
            off = cid_idx * OBJCLASS_SIZE
            nm = H2N.get(cls_raw[off:off + 16]) or CLASSID_MAP.get(cid_idx, "?")
            sv_ptr = int.from_bytes(cls_raw[off + 56:off + 60], "big")  # staticVars @ +16+40
            # valid staticVars homes: WRAM-L, WRAM-H, AND the 4MB cart (overlay-resident
            # classes -- Options/MenuSetup/GHZCutsceneST/Batbrain -- keep statics on cart,
            # 0x02xxxxxx / cache-through 0x22xxxxxx). WRAM-only was a false-CLASSREG flood.
            sv_ok = (0x00200000 <= sv_ptr < 0x00300000 or 0x06000000 <= sv_ptr < 0x06100000
                     or 0x02000000 <= sv_ptr < 0x03000000 or 0x22000000 <= sv_ptr < 0x23000000)
            rows.append({"listIdx": listIdx, "classID": cid_idx, "name": nm, "sv_ok": sv_ok})
        return rows

    def measure_speed_light(self, dt=3.0):
        """Game-speed via a DEDICATED minimal-read burst (only tick/cont), so the
        heavy structural sample's UDP read-load can't stall the emulator during
        the measurement (observer effect). Matches qa_chain_speed.py's method --
        which reads 60.3 tick/s at GHZ where the coupled heavy-sample read only
        23 (a pure artifact). Returns (tick_per_s, render_per_s)."""
        t0 = self.w("p6_w_tick_frames"); c0 = self.w("p6_w_cont_frames"); w0 = time.time()
        time.sleep(dt)
        t1 = self.w("p6_w_tick_frames"); c1 = self.w("p6_w_cont_frames"); dw = time.time() - w0
        if None in (t0, t1, c0, c1) or dw <= 0:
            return None, None
        return (t1 - t0) / dw, (c1 - c0) / dw

    def channels(self):
        """Read the live engine SFX/stream channel array (RSDK::channels[16],
        ChannelInfo 36 B: soundID int16 @+32, state uint8 @+35). States: 0 IDLE,
        1 SFX, 2 STREAM, 3 LOADING_STREAM, 0x40 PAUSED. This is the AUTHORITATIVE
        audio state -- what the engine is actually playing right now."""
        a = self.sym("RSDK::channels")
        if a is None:
            return []
        raw = self.rb(a, 36 * 16)
        out = []
        for c in range(16):
            b = c * 36
            sid = int.from_bytes(raw[b + 32:b + 34], "big")
            if sid >= 0x8000:
                sid -= 0x10000
            out.append({"ch": c, "soundID": sid, "state": raw[b + 35]})
        return out

    def sample(self):
        s = qa_trace.sample(self.rd, MAP, 700)   # raises loud if unhealthy
        s["reg"] = self.registration()
        s["edge"] = self.edge_hits()
        s["tick"] = self.w("p6_w_tick_frames")
        s["cont"] = self.w("p6_w_cont_frames")
        s["str_state"] = self.w("p6_w_str_state")
        s["str_track"] = self.w("p6_w_str_track")
        s["sfx_inited"] = self.w("p6_w_sfx_inited")
        s["snd_plays"] = self.w("p6_w_snd_plays")
        # Nyquist-proof gameplay-SFX/BGM activity (60Hz SH-2 accumulators):
        s["sfx_armed_frames"] = self.w("p6_w_sfx_armed_frames")
        s["sfx_arm_events"] = self.w("p6_w_sfx_arm_events")
        s["sfx_last_id"] = s32(self.w("p6_w_sfx_last_id"))
        s["stream_frames"] = self.w("p6_w_stream_frames")
        s["sfx_keyons"] = self.w("p6_w_sfx_keyons")   # SCSP-S16 pump key-ons (audible path)
        s["channels"] = self.channels()
        s["pal_hash"] = self.w("p6_w_pal_hash")
        # backend-parity render witnesses (SH-2-hashed CRAM -> netmem-visible):
        # the port's render bugs live in CRAM/VDP, invisible to WRAM otherwise.
        s["cram_hash"] = self.w("p6_w_cram_hash")
        s["cram_magenta"] = self.w("p6_w_cram_magenta")
        s["cram_nonzero"] = self.w("p6_w_cram_nonzero")
        s["cram_magenta_max"] = self.w("p6_w_cram_magenta_max")
        s["cram_magenta_frames"] = self.w("p6_w_cram_magenta_frames")
        # per-256-entry-bank magenta breakdown -> distinguishes STATIC unused-bank
        # over-count (constant across scenes) from real scene-varying displayed pink.
        _mb = self.sym("p6_w_cram_mag_bank")
        if _mb is not None:
            _raw = self.rb(_mb, 32)
            s["cram_mag_bank"] = [int.from_bytes(_raw[i * 4:i * 4 + 4], "big") for i in range(8)]
        else:
            s["cram_mag_bank"] = None
        # sprite/draw pipeline (missing-sprite + flicker + layer coverage):
        s["vdp1_drops"] = self.w("p6_w_vdp1_drops")    # sprites the emitter DROPPED
        s["vdp1_evicts"] = self.w("p6_w_vdp1_evicts")  # slot-cache thrash (flicker)
        s["dl_cmds_max"] = self.w("p6_w_dl_cmds_max")  # peak VDP1 cmds/frame
        s["b1_reg"] = self.w("p6_w_b1_registered")     # VDP2 BG plane armed (sky)
        s["fg_highfill"] = self.w("p6_w_fg_highfill")  # FG-High tiles composited
        s["aniframes"] = {c: self.w(sym) for c, sym in ANIFRAMES.items()}
        return s


def dump_classmap():
    """Dump the FULL live objectClassList (all 128 slots, not just the current
    scene's stage list), resolve every well-formed hash entry via the decomp name
    dictionary, and persist tools/qa_classid_map.json fingerprinted to THIS
    build's _end. Run once per build (RA must be live). Widens the oracle's
    REGISTER/SPAWN/ANIM naming coverage across scene changes."""
    try:
        o = Oracle()
    except Exception as e:
        sys.stderr.write(f"dump-classmap: LIVE HARNESS UNHEALTHY -- {e}\n")
        return 2
    raw = o.rb(OBJCLASS_BASE, OBJCLASS_SIZE * 128)
    out = {}
    for cid in range(128):
        nm = H2N.get(raw[cid * OBJCLASS_SIZE:cid * OBJCLASS_SIZE + 16])
        if nm:
            out[cid] = nm
    fp = re.search(r"0x([0-9a-fA-F]{16})\s+_end\s*$", MAP, re.M)
    doc = {"_end_fingerprint": int(fp.group(1), 16) if fp else None,
           "map": {str(k): v for k, v in sorted(out.items())}}
    (_HERE / "qa_classid_map.json").write_text(json.dumps(doc, indent=1))
    print(f"dump-classmap: resolved {len(out)}/128 classIDs -> tools/qa_classid_map.json "
          f"(fingerprint _end=0x{doc['_end_fingerprint']:X})" if fp else
          f"dump-classmap: resolved {len(out)}/128 (NO _end in map -- fingerprint None)")
    for k, v in sorted(out.items()):
        print(f"  {k:3d} {v}")
    return 0


def main():
    if "--selftest-live" in sys.argv:
        return selftest_live()
    if "--selftest" in sys.argv:
        return selftest()
    if "--dump-classmap" in sys.argv:
        return dump_classmap()
    baseline_path = None
    for i, a in enumerate(sys.argv):
        if a == "--baseline" and i + 1 < len(sys.argv):
            baseline_path = Path(sys.argv[i + 1])
    # A live run MUST NOT proceed on a rotted approach -- run the offline selftest
    # first; if the ground-truth surface is broken, a live GREEN would be a lie.
    if selftest() != 0:
        sys.stderr.write("qa_parity_oracle: refusing live run -- selftest RED (see above)\n")
        return 2
    try:
        o = Oracle()
    except Exception as e:
        sys.stderr.write(f"qa_parity_oracle: LIVE HARNESS UNHEALTHY -- {e}\n")
        return 2

    samples = []
    t0 = time.time()
    prev = None
    scene_speed = {}   # folder -> (tick_per_s, render_per_s) via a LIGHT burst (no observer effect)
    stable_seen = {}   # folder -> consecutive settled samples (n_entities>3) seen so far
    print("t     folder        n   reg  tick     cont  edge palhash")
    consec_fail = 0
    while time.time() - t0 < SECS:
        try:
            s = o.sample()
            consec_fail = 0
        except Exception as e:
            # TRANSIENT-TOLERANT (2026-07-20): a single UDP hiccup (WinError 10054
            # connection-reset / timeout while RA is inside a heavy scene load)
            # previously ABORTED the whole arc run after one sample. Retry with
            # backoff; only give up after 10 consecutive failures (~50 s dead air
            # = the core is genuinely gone, not busy).
            consec_fail += 1
            sys.stderr.write(f"qa_parity_oracle: sample failed ({consec_fail}/10) -- {e}\n")
            if consec_fail >= 10:
                sys.stderr.write("qa_parity_oracle: 10 consecutive failures -- core gone; aborting.\n")
                return 2
            time.sleep(5.0)
            continue
        s["wall"] = time.time() - t0
        # game-SPEED must be measured with MINIMAL reads (the heavy structural
        # sample's UDP load stalls the emulator -> a false low reading). Take one
        # dedicated light burst per distinct scene the first time it's seen.
        fol = s["folder"]
        # FP-2 fix (adversarial-QA 2026-07-20): measure game-speed only once the scene
        # is SETTLED, never on first sighting. The first n>3 sample often still lands
        # in the scene-LOAD window where tick is stalled -> a false "0.0 tick/s" /
        # slow-motion RED (the memory-documented artifact). Require TWO consecutive
        # settled samples (scene has been live >=1 period) before the light burst.
        if fol not in UI_SCENES and s["n_entities"] and s["n_entities"] > 3:
            stable_seen[fol] = stable_seen.get(fol, 0) + 1
            if fol not in scene_speed and stable_seen[fol] >= 2:
                scene_speed[fol] = o.measure_speed_light(3.0)
        else:
            stable_seen[fol] = 0
        print("%4.0f  %-12s %3s  %3d  %7s %7s  %3d %8s" % (
            s["wall"], (s["folder"] or "?")[:12], s["n_entities"], len(s["reg"]),
            s["tick"], s["cont"], len(s["edge"]),
            s["pal_hash"] if s["pal_hash"] is not None else "?"))
        samples.append(s)
        time.sleep(PERIOD)

    # ---------- per-scene DECOMP-vs-LIVE divergence analysis ----------
    print("\n" + "=" * 80)
    print("PARITY DIVERGENCE LIST  (decomp ground-truth vs live -- comprehensive, per class)")
    print("=" * 80)
    scenes = {}
    for s in samples:
        scenes.setdefault(s["folder"], []).append(s)

    div = []

    def D(scene, code, msg):
        div.append((scene, code, msg))
        print(f"  [{(scene or '?'):12s}] {code:8s} {msg}")

    for folder, ss in scenes.items():
        last = ss[-1]
        man = None
        # manifest lookup: scene_objects.json is keyed by zone folder (GHZ, AIZ, ...)
        for k, zd in MANIFEST.items():
            if k == folder or zd.get("folder") == folder:
                man = zd
                break
        expected = set((man or {}).get("stage_config", {}).get("objects", []) or [])

        # FN-2 fix (adversarial-QA 2026-07-20): a witness that reads None every
        # sample makes its detector SILENTLY SKIP -> false GREEN, the exact lying
        # gate this project forbids. None is NOT clean: if a backend witness is dead
        # across the WHOLE scene window, the corresponding dimension is BLIND here,
        # so say so LOUD as a divergence (a blind detector is a defect to fix, not a
        # pass). Only flag witnesses that SHOULD be live in this scene (the CRAM/audio
        # witnesses are front-end-chain-gated; in a plain-GHZ build they are absent by
        # design and selftest already caught that offline).
        BLIND_WITNESSES = {
            "cram_magenta": "D9 PALETTE (pink/colour)",
            "cram_nonzero": "D9 PALETTE (blank screen)",
            "cram_magenta_frames": "D9 PALETTE (transient pink flash, Nyquist)",
            "vdp1_drops": "D12 SPRITE (missing sprites)",
            "vdp1_evicts": "D12 SPRITE (flicker)",
            "sfx_armed_frames": "D8 AUDIO (gameplay SFX liveness)",
            "stream_frames": "D8 AUDIO (BGM stream liveness)",
            "cont": "D7/D13 SPEED/PERF (frame clock)",
        }
        for key, dim in BLIND_WITNESSES.items():
            if all(s.get(key) is None for s in ss):
                D(folder, "BLIND", f"witness '{key}' read None for ALL {len(ss)} samples -> "
                                   f"{dim} is BLIND in this scene (None != clean; fix the witness "
                                   f"or the read path before trusting any GREEN here)")

        # registration set seen across the window (union -- a class may register late)
        reg_rows = {}
        for s in ss:
            for r in s["reg"]:
                reg_rows[r["name"]] = r
        reg_names = set(n for n in reg_rows if n != "?")

        # D1 REGISTER: a decomp class not registered live. HONEST-ONLY: asserting a
        # class is ABSENT requires resolving the WHOLE registered set by name. The
        # objectClassList name-hash (md5, halfword-swapped) resolves cleanly for
        # well-formed entries but many stage indices are empty/pointer slots, so
        # resolution is partial. If we did NOT resolve the full set, we CANNOT
        # conclude absence (that produced a false-positive flood in run 1) -> emit a
        # coverage NOTE, not a divergence. Per-class absence is asserted ONLY when
        # every registered class resolved.
        classcount = len(last["reg"])
        resolved_count = len(reg_names)
        if expected:
            if classcount > 0 and resolved_count >= classcount:
                for nm in sorted(expected - reg_names):
                    D(folder, "REGISTER", f"decomp class '{nm}' NOT registered "
                                          f"({resolved_count}/{classcount} classes resolved) -- absent")
            else:
                print(f"  [{folder or '?':12s}] NOTE     REGISTER coverage-limited: "
                      f"{resolved_count}/{classcount} class names resolved, manifest expects "
                      f"{len(expected)} stage classes -- per-class absence NOT asserted "
                      f"(name-resolution calibration TODO; not a false GREEN, a KNOWN blind spot)")

        # D2 CLASSREG: a RESOLVED class whose *staticVars is NULL. Restricted to
        # resolved names -- an unresolved ('?') entry is often an empty/unused class
        # slot whose all-zero staticVars is expected, not a broken port.
        for nm, r in reg_rows.items():
            if nm == "?":
                continue
            if not r["sv_ok"]:
                D(folder, "CLASSREG", f"class '{nm}' (id {r['classID']}) *staticVars unreadable/NULL "
                                      f"-- broken registration / stub bind")

        # per-class live instance counts (by classID -> name via registration)
        id2name = {r["classID"]: r["name"] for r in last["reg"]}
        live_counts = {}
        for e in last["entities"]:
            nm = id2name.get(e["classID"], f"cid{e['classID']}")
            live_counts[nm] = live_counts.get(nm, 0) + 1

        # D3 SPAWN: a registered gameplay class with 0 live instances (in a gameplay
        # scene). Reported for the classes the manifest lists as stage content --
        # excludes pure setup/API/global-manager classes that never instantiate.
        if folder not in UI_SCENES and expected:
            NONSPAWN = {"GHZSetup", "APICallback", "BadnikHelpers", "Music", "Zone",
                        "COverlay", "AIZSetup", "CPZSetup", "CutsceneSeq"}
            # only judge a SETTLED scene -- a mid-load sample (n_entities ~0) has
            # legitimately not spawned anything yet (GHZCutscene at t=189 false-flagged).
            if (last["n_entities"] or 0) >= 4:
                for nm in sorted(expected & reg_names):
                    if nm in NONSPAWN:
                        continue
                    if live_counts.get(nm, 0) == 0:
                        D(folder, "SPAWN", f"registered class '{nm}' has 0 live instances "
                                           f"(placed-but-not-spawned? verify vs Scene{folder} placements)")

        # D4 ANIM: any live entity with an out-of-range animID for its class
        for e in last["entities"]:
            nm = id2name.get(e["classID"], None)
            aid = e["animID"]
            maxf = last["aniframes"].get(nm) if nm else None
            if aid is None:
                continue
            if aid >= 0xFF00 or aid > 200:
                D(folder, "ANIM", f"entity '{nm or ('cid'+str(e['classID']))}' slot{e['slot']} "
                                  f"animID={aid} is garbage (out of any valid range)")
            elif maxf and maxf > 0 and aid >= maxf:
                D(folder, "ANIM", f"entity '{nm}' slot{e['slot']} animID={aid} >= class frame "
                                  f"count {maxf} -- wrong/undefined pose")

        # D5 VISIBLE: gameplay scene, no visible player
        if folder not in UI_SCENES:
            for s in ss:
                players = [e for e in s["entities"] if e["classID"] == 8]
                if players and not any((p.get("visible") or 0) for p in players):
                    D(folder, "VISIBLE", f"no VISIBLE player at t={s['wall']:.0f}s "
                                         f"(characters missing / not drawn)")
                    break
            # FN-4 fix (adversarial-QA 2026-07-20): the old check ONLY looked at the
            # player (classID 8). A total sprite blackout (badniks/rings/bridges all
            # invisible while the world is populated) passed. Flag a SETTLED gameplay
            # scene where MANY entities are live but NONE are drawn -- the whole
            # visible-sprite class, not just the player.
            for s in ss:
                if (s["n_entities"] or 0) >= 8:
                    vis = sum(1 for e in s["entities"] if (e.get("visible") or 0))
                    if vis == 0:
                        D(folder, "VISIBLE", f"ZERO of {s['n_entities']} live entities are visible at "
                                             f"t={s['wall']:.0f}s -- total sprite blackout (nothing drawn)")
                        break

        # D6 EDGE: any edge-audit ordinal that fired DURING THIS SCENE's window.
        # p6_w_edge_hits[] is CUMULATIVE SINCE BOOT (p6_closure_edge.c) -- attributing
        # the running total to whichever scene was being sampled mis-blamed AIZ for
        # hits accumulated in Menu (calibration finding, 2026-07-16). Use the
        # first-vs-last delta within the scene window; a count that grew here fired here.
        first_e, last_e = ss[0]["edge"], ss[-1]["edge"]
        edge_delta = {}
        for k, v in last_e.items():
            d = v - first_e.get(k, 0)
            if d > 0:
                edge_delta[k] = d
        if edge_delta:
            D(folder, "EDGE", f"edge-audit ordinals fired IN this scene {dict(sorted(edge_delta.items()))} "
                              f"(delta within window; map ordinal->fn via p6_closure_edge.c) "
                              f"-- a boundary stub was crossed (classify dead-cosmetic vs broken-gameplay)")

        # D7 SPEED: game-speed vs 60Hz, from the DEDICATED light burst (not the
        # heavy-sample cadence, which is observer-contaminated). game-time < wall =
        # slow-motion; game-time == 60 but low render = choppy (render-bound, a
        # DIFFERENT problem -- reported as info, not a speed divergence).
        if folder in scene_speed and scene_speed[folder][0] is not None:
            tps, rps = scene_speed[folder]
            if tps < 55.0:
                D(folder, "SPEED", f"game-speed {tps:.1f} tick/s vs 60 = {tps/60*100:.0f}% "
                                   f"realtime (SLOW-MOTION / logic pacing #243)")
            elif rps < 20.0:
                print(f"  [{folder or '?':12s}] NOTE     game-speed OK ({tps:.0f} tick/s = "
                      f"{tps/60*100:.0f}% realtime) but render {rps:.0f} fps -- CHOPPY not slow "
                      f"(render-bound, #243 catch-up; separate from parity)")

        # D8 AUDIO (differential vs the decomp's BGM reference + the live channel
        # array). Catches the WHOLE audio-defect class: no music, wrong music (the
        # "Saturn boot chime instead of game music"), BGM requested-but-not-playing,
        # dead SFX, garbage channels -- all from the AUTHORITATIVE engine audio state.
        if folder in AUDIO_EXPECT:
            tracks = set(s["str_track"] for s in ss if s["str_track"] is not None)
            stream_on = any(any(ch["state"] == 2 for ch in (s.get("channels") or []))
                            for s in ss)
            if tracks and max(tracks) <= 0:
                D(folder, "AUDIO", f"NO CD-DA BGM mapped (p6_w_str_track={sorted(tracks)}) -- the "
                                   f"engine requested a stream with no CUE-track mapping -> SILENCE "
                                   f"or the default/boot chime instead of game music")
            elif not stream_on:
                D(folder, "AUDIO", f"BGM track {sorted(t for t in tracks if t > 0)} requested but NO "
                                   f"channel in CHANNEL_STREAM state -- CD-DA not actually playing")
            exp = BGM_EXPECT.get(folder)
            if exp is not None and tracks:
                wrong = sorted(t for t in tracks if t > 0 and t not in exp)
                if wrong:
                    D(folder, "AUDIO", f"WRONG BGM track {wrong} (decomp expects {sorted(exp)} for "
                                       f"{folder}) -- wrong music playing")
            # SFX liveness (GAP-A1 upgrade): the DEAD-SFX class the old check missed.
            # p6_w_snd_plays only counts the boot proof + the fixed MenuBleep re-trigger
            # -- it is BLIND to gameplay SFX. The new 60Hz-sampled channel witnesses see
            # the verbatim decomp PlaySfx arming channels[]: sfx_arm_events counts every
            # new-soundID arm. The DIVERGENCE is gameplay REQUESTING SFX (arm_events
            # grew) while the SCSP-audible path did NOT play them (snd_plays flat) ==
            # the sound is armed in the engine but never reaches the speaker = dead SFX.
            if folder not in UI_SCENES:
                armev = [s.get("sfx_arm_events") for s in ss if s.get("sfx_arm_events") is not None]
                plays = [s["snd_plays"] for s in ss if s["snd_plays"] is not None]
                # keyons = the SCSP-S16 gameplay-SFX pump (the fix that made SFX audible,
                # e124f99). snd_plays only tracks the boot proof + MenuBleep, so a
                # dead-SFX verdict MUST also require keyons flat -- else D8 false-flags
                # the very build that fixed it.
                keyons = [s.get("sfx_keyons") for s in ss if s.get("sfx_keyons") is not None]
                arm_grew = len(armev) >= 2 and (max(armev) - min(armev)) > 0
                plays_flat = len(plays) >= 2 and (max(plays) - min(plays)) == 0
                keyons_flat = not keyons or (max(keyons) - min(keyons)) == 0
                if arm_grew and plays_flat and keyons_flat:
                    D(folder, "AUDIO", f"gameplay armed {max(armev) - min(armev)} SFX events "
                                       f"(engine channels[]) but BOTH the SCSP pump (keyons) and the "
                                       f"proof count are FLAT -- SFX REQUESTED but NOT PLAYED (dead SFX)")
                elif arm_grew and not keyons_flat:
                    print(f"  [{folder or '?':12s}] NOTE     gameplay SFX AUDIBLE -- armed "
                          f"{max(armev)-min(armev)} + SCSP pump keyed {max(keyons)-min(keyons)} voices "
                          f"(the e124f99 SFX fix is live; snd_plays is a stale counter, ignored)")
                elif len(armev) >= 3 and (max(armev) - min(armev)) == 0 and (last["n_entities"] or 0) >= 6:
                    D(folder, "AUDIO", f"NO SFX arming at all across the window (sfx_arm_events flat "
                                       f"at {armev[0]}) with {last['n_entities']} live entities -- "
                                       f"gameplay produced zero SFX (silent world)")
            # BGM stream liveness cross-check (Nyquist): a scene that should have BGM
            # but never held a STREAM channel for any frame in the window.
            strf = [s.get("stream_frames") for s in ss if s.get("stream_frames") is not None]
            if strf and (max(strf) - min(strf)) == 0 and folder in BGM_EXPECT:
                D(folder, "AUDIO", f"no STREAM channel active for ANY frame in the window "
                                   f"(p6_w_stream_frames flat at {strf[0]}) -- BGM not audibly "
                                   f"playing (expected track {sorted(BGM_EXPECT[folder])})")
            # garbage channel: an active SFX channel whose soundID is out of range.
            for ch in (last.get("channels") or []):
                if ch["state"] == 1 and not (0 <= ch["soundID"] < 0x100):
                    D(folder, "AUDIO", f"channel {ch['ch']} plays garbage soundID={ch['soundID']} "
                                       f"(outside sfxList[0..255]) -- wrong/corrupt SFX")

        # D9 PALETTE (differential vs the composited CRAM, hashed SH-2-side into WRAM
        # witnesses so netmem sees the render backend). CRAM is where colour bugs
        # live -- the engine palette can be correct while composition is wrong. This
        # catches the pink-flash class GENERALLY (magenta in ANY bank, ANY scene) +
        # blank/black screens, no keyword enumeration.
        # D9 RECALIBRATED (2026-07-20, per-bank measurement): the raw magenta count
        # is ~80% STATIC UNUSED-BANK over-count -- banks 3-7 (533 entries) read
        # byte-IDENTICAL across Title/Menu/AIZ/GHZCutscene/GHZ (proven via
        # p6_w_cram_mag_bank; displayed palettes MUST vary between scenes, so a
        # cross-scene constant == unused CRAM, not visible pink). Flagging the raw
        # total false-positived every scene. A real pink FLASH is a CHANGE, so flag
        # on WITHIN-SCENE magenta VARIANCE (max-min across this scene's window),
        # NOT static presence. See memory/pink-cram-mostly-detector-overcount.
        mags = [s["cram_magenta"] for s in ss if s.get("cram_magenta") is not None]
        nzs = [s["cram_nonzero"] for s in ss if s.get("cram_nonzero") is not None]
        FLASH_SWING = 24    # entries; a real in-scene composition flash, not noise
        if len(mags) >= 2 and (max(mags) - min(mags)) >= FLASH_SWING:
            banks = ss[-1].get("cram_mag_bank")
            D(folder, "PALETTE", f"pink FLASH -- CRAM magenta swung {min(mags)}->{max(mags)} "
                                 f"WITHIN this scene (a real composition change, not the static "
                                 f"unused-bank baseline){f'; per-bank {banks}' if banks else ''}")
        elif mags and max(mags) > 4:
            banks = ss[-1].get("cram_mag_bank")
            # constant magenta = the static baseline. Report the SMALL scene-composed
            # component (banks 0-2, the primary NBG/composed palette region) if it is
            # itself large; banks 3-7 are the proven static over-count -> info only.
            comp = sum(banks[:3]) if banks else None
            if comp is not None and comp > 24:
                D(folder, "PALETTE", f"magenta in the COMPOSED palette region "
                                     f"(banks0-2={banks[:3]}, sum {comp}) -- possible wrong colour in a "
                                     f"displayed NBG/palette (banks3-7 {banks[3:]} are the static "
                                     f"unused-bank baseline, ignored)")
            else:
                print(f"  [{folder or '?':12s}] NOTE     CRAM magenta {max(mags)} is STATIC "
                      f"(constant across the window; per-bank {banks}) -- the unused-bank baseline, "
                      f"NOT a visible flash (was a D9 false-positive pre-recalibration)")
        if nzs and folder not in ("", "?", None, "Logos") and max(nzs) < 8:
            D(folder, "PALETTE", f"CRAM nearly empty (max {max(nzs)} nonzero of 2048) -- "
                                 f"blank/black screen (palette not composed)")

        # D12 SPRITE (missing sprites): the engine's draw list requests N sprites;
        # the Saturn VDP1 emitter DROPS a sprite it can't seat (slot-pool overflow,
        # unbound sheet, oversize). p6_w_vdp1_drops counts EVERY dropped blit -> a
        # rising drop count = sprites the game wanted on screen that AREN'T. General
        # missing-sprite detector across ALL objects. evicts = slot-cache thrash =
        # sprites flickering in/out (the visible-but-blinking class).
        # D12 RECALIBRATED (2026-07-20, per-sample measurement): p6_w_vdp1_drops is
        # a monotonic counter. Measured at settled GHZ it JUMPS ~285 once at the
        # scene-load/handoff and then stays CONSTANT -> the drops are a one-time load
        # burst (sheets not yet resident during the handoff), NOT ongoing gameplay
        # loss. The old max-min-across-the-whole-window caught that LOAD burst and
        # mis-reported "missing sprites" during play. Measure the delta over the
        # SETTLED tail (skip the first sample, which straddles the load) so only
        # ONGOING drops flag; report a large constant total as a load-time NOTE.
        drp = [s["vdp1_drops"] for s in ss if s.get("vdp1_drops") is not None]
        settled_drp = drp[1:] if len(drp) >= 3 else drp
        if len(settled_drp) >= 2 and (max(settled_drp) - min(settled_drp)) > 8:
            D(folder, "SPRITE", f"VDP1 dropped {max(settled_drp) - min(settled_drp)} sprite blits "
                                f"DURING SETTLED play -- ongoing MISSING SPRITES (slot-pool overflow / "
                                f"unbound sheet / oversize)")
        elif drp and max(drp) > 8 and len(settled_drp) >= 2 and (max(settled_drp) - min(settled_drp)) == 0:
            print(f"  [{folder or '?':12s}] NOTE     VDP1 drops constant at {max(drp)} across settled play "
                  f"-- a ONE-TIME load/handoff burst (sheets not yet resident at the seam), NOT an "
                  f"ongoing gameplay miss (was a D12 false-positive pre-recalibration)")
        ev = [s["vdp1_evicts"] for s in ss if s.get("vdp1_evicts") is not None]
        conts = [s["cont"] for s in ss if s.get("cont") is not None]
        if len(ev) >= 2 and len(conts) >= 2 and (conts[-1] - conts[0]) > 0:
            evr = (max(ev) - min(ev)) / max(1, (conts[-1] - conts[0]))
            if evr > 2.0:
                D(folder, "SPRITE", f"VDP1 slot-cache thrash {evr:.1f} evicts/frame -- sprites "
                                    f"FLICKER (re-staged mid-frame; the title-vdp1-slot-thrash class)")

        # D13 PERF (performance parity): the reference runs at 60 fps. A gameplay
        # scene rendering far below that is a real parity failure (the user plays a
        # slideshow), reported as a DIVERGENCE now (was a soft NOTE). tick<55 is
        # slow-MOTION (already D7); this is render-fps (choppy) as a first-class miss.
        if folder in scene_speed and scene_speed[folder][1] is not None:
            _tps, rps = scene_speed[folder]
            if folder not in UI_SCENES and rps < 20.0:
                D(folder, "PERF", f"render {rps:.0f} fps (reference 60) -- unplayable framerate "
                                  f"(#243 chain draw wall; a parity failure, not just 'choppy')")

        # D14 LAYER (missing background / foreground): a gameplay scene must arm its
        # VDP2 BG plane (sky/parallax) and composite FG tiles. b1_registered==0 = no
        # BG plane (black sky class); fg_highfill==0 = no FG-High tiles (missing
        # foreground / grass). Both are the "half the screen is missing" class.
        # FN-6 fix (adversarial-QA 2026-07-20): b1_registered (the VDP2 BG-plane
        # armed witness) is meaningful for EVERY scene that renders a NBG BG behind a
        # transparent FG -- GHZ AND GHZCutscene use the same NBG-BG path (#310), so a
        # black sky there was previously undetectable. fg_highfill stays GHZ-specific
        # (it counts GHZ FG-High tiles). Title/Menu/AIZ black-screen is covered by the
        # D9 cram_nonzero blank-CRAM detector (scene-agnostic), so no false blind.
        if folder in ("GHZ", "GHZCutscene"):
            b1 = [s["b1_reg"] for s in ss if s.get("b1_reg") is not None]
            if b1 and max(b1) == 0:
                D(folder, "LAYER", "VDP2 BG plane never armed (p6_w_b1_registered=0) -- "
                                   "black sky / missing background")
        if folder == "GHZ":  # fg_highfill is GHZ-FG specific
            fgh = [s["fg_highfill"] for s in ss if s.get("fg_highfill") is not None]
            if fgh and max(fgh) == 0:
                D(folder, "LAYER", "FG-High layer never composited (p6_w_fg_highfill=0) -- "
                                   "missing foreground tiles (grass/palms)")

        # D10 TEMPORAL: entity-count collapse or a frozen player across the window
        ns = [s["n_entities"] for s in ss]
        if len(ns) >= 3 and max(ns) >= 6 and min(ns) <= 2:
            D(folder, "TEMPORAL", f"entity count collapsed {max(ns)}->{min(ns)} across the window "
                                  f"-- streaming/pool loss")
        if folder not in UI_SCENES and len(ss) >= 4:
            pxs = []
            for s in ss:
                pl = [e for e in s["entities"] if e["classID"] == 8]
                pxs.append(pl[0]["x"] if pl and pl[0]["x"] is not None else None)
            good = [x for x in pxs if x is not None]
            if len(good) >= 4 and len(set(good)) == 1:
                D(folder, "TEMPORAL", f"player X frozen at {good[0]} across {len(good)} samples "
                                      f"-- stuck-state (death/no-respawn/no-input class)")

        # D11 SANE: universal invariants
        cutscene = folder in ("AIZ", "GHZCutscene")
        for name, ok, detail, advisory in qa_invariants.invariants(last, cutscene=cutscene):
            if not ok and not advisory:
                D(folder, "SANE", f"{name}: {detail}")

    print("-" * 80)
    from collections import Counter
    byscene = {}
    for scene, code, _ in div:
        byscene.setdefault(scene, []).append(code)
    visited = sorted(scenes.keys(), key=lambda x: (x is None, x))
    if div:
        print("summary (this IS the work queue -- decomp-derived, not a symptom list):")
        for scene, codes in byscene.items():
            print(f"   [{scene or '?':12s}] {dict(Counter(codes))}")
    else:
        print("qa_parity_oracle: NO divergence from decomp ground-truth across the arc")

    # ---- REGRESSION GATE: compare to a recorded baseline so a fix in one scene
    # can't silently reintroduce/add a divergence in another (whole-arc, per the
    # binding rule). Keyset = {(scene, code)} for scenes visited THIS run. ----
    cur = sorted(set((sc, cd) for sc, cd, _ in div))
    rc = 1 if div else 0
    if baseline_path is not None:
        if not baseline_path.exists():
            baseline_path.write_text(json.dumps(
                {"visited": visited, "divergences": [list(k) for k in cur]}, indent=1))
            print(f"\n[baseline] wrote initial baseline -> {baseline_path} "
                  f"({len(cur)} divergence keys, {len(visited)} scenes). "
                  f"Future runs FAIL on any NEW key in a visited scene.")
        else:
            base = json.loads(baseline_path.read_text())
            base_keys = set(tuple(k) for k in base.get("divergences", []))
            # only judge scenes covered by BOTH runs (don't false-regress an
            # unreached scene). NEW key in a shared scene = REGRESSION.
            shared = set(visited) & set(base.get("visited", []))
            new = sorted(k for k in cur if k not in base_keys and k[0] in shared)
            cleared = sorted(k for k in base_keys if k not in set(cur) and k[0] in shared)
            print("\n[regression gate vs baseline]")
            if cleared:
                print(f"  CLEARED ({len(cleared)}): " +
                      ", ".join(f"{s}:{c}" for s, c in cleared) + "  (improvement)")
            if new:
                print(f"  NEW REGRESSIONS ({len(new)}): " +
                      ", ".join(f"{s}:{c}" for s, c in new))
                print("  -> RED: a change reintroduced/added a divergence in a covered scene.")
                rc = 1
            else:
                print("  no new divergence in any covered scene -> approach did NOT regress.")
                # a clean run that only cleared items should still exit 0
                if not div:
                    rc = 0
    if div:
        print(f"qa_parity_oracle: {len(div)} divergence(s) present.")
    return rc


if __name__ == "__main__":
    sys.exit(main())
