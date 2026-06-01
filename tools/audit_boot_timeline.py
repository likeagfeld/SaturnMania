#!/usr/bin/env python3
"""audit_boot_timeline.py - Phase 1.29 STEP 1: quantify the cold-boot asset
load path so the boot-delay reduction work has measurable before/after data.

Per CLAUDE.md §4.5.1 Audit 4 (boot-delay budget):
    expected_load_time_sec = file_size_KB / 150 + gfs_seek_count * 0.1
    (Mednafen emulated 1x CD = ~150 KB/s, GFS_Seek ~100 ms per seek)

This script does NOT execute the Saturn - it statically enumerates the
synchronous load list from src/main.c::jo_main + src/mania/Game.c::
mania_engine_init + src/mania/Objects/Title/TitleAssets.c::title_assets_load
+ src/mania/Objects/Common/Entities.c::entities_load_assets and pairs each
filename with its on-disk size + a conservative seek-count estimate (each
GFS_Open + each GFS_Seek is one seek; multi-sector reads in the TSONIC /
ELECTRA loaders contribute one seek per loop iteration).

Output: tools/audit_boot_timeline_report.json + printable table.
"""
import json
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CD_DIR = ROOT / "cd"
REPORT_PATH = Path(__file__).resolve().parent / "audit_boot_timeline_report.json"

# Mednafen Saturn emulated CD bandwidth (1x ~150 KB/s per ST-136-R2 §3.2)
# and per-seek penalty (GFS_Seek issues an SH-1 seek; emulated time ~100 ms).
CD_BANDWIDTH_KBPS = 150.0
SEEK_TIME_SEC = 0.1


# Synchronous-load enumeration. Each entry corresponds to a real loader call
# in the cold-boot path. Order matches the order they fire from jo_main:
#   1. setup_title_bg()                   -> TITLE.PAL, TITLE.DAT
#   2. mania_engine_init()
#      2a. setup_title_assets() ->
#          title_assets_load() ->
#             load_spr_atlas(MWINGS.SPR)
#             load_spr_atlas(MRIBSIDE.SPR)
#             load_spr_atlas(MLOGO.SPR)
#             load_spr_atlas(MRIBBON.SPR)
#             load_spr_atlas(MRING.SPR)
#             load_spr_atlas(MPRESS.SPR)
#             load_tsonic_atlas()         -> TSONIC.ATL (8 keyframes + 12 finger)
#             load_electricity_atlas()    -> ELECTRA.ATL (8 keyframes)
#      2b. entities_load_assets() ->
#             DIGITS.SPR, RING.SPR, SPRING.SPR, MONITOR.SPR, SIGNPOST.SPR,
#             BADNIK.SPR, RINGSFX/JUMPSFX/BREAKSFX/STOMPSFX/BOUNCESFX
# Each .SPR jo_sprite_add path = 1 open + 1 read of whole file (no seeks).
# Each .ATL path = 1 open + 1 header read + N per-keyframe (1 seek + 1 read)
# pairs. TSONIC loads 8 body keyframes + 12 finger frames = 20 seek+read
# cycles plus header read. ELECTRA loads 8 keyframes = 8 seek+read cycles.

# Format: (label, filename, seek_count_for_loader, mandatory_for_first_paint,
#         deferable_to_async)
COLD_BOOT_ASSETS = [
    # Step 1: backdrop ---------------------------------------------------
    ("setup_title_bg/TITLE.PAL",      "TITLE.PAL",   1,  True,  False),
    ("setup_title_bg/TITLE.DAT",      "TITLE.DAT",   1,  True,  False),
    # Step 2a: title sprite atlases (.SPR) - synchronous in
    # setup_title_assets, BEFORE the title scene paints its first frame.
    ("title_assets/MWINGS.SPR",       "MWINGS.SPR",  1,  False, True),
    ("title_assets/MRIBSIDE.SPR",     "MRIBSIDE.SPR",1,  False, True),
    ("title_assets/MLOGO.SPR",        "MLOGO.SPR",   1,  False, True),
    ("title_assets/MRIBBON.SPR",      "MRIBBON.SPR", 1,  False, True),
    ("title_assets/MRING.SPR",        "MRING.SPR",   1,  False, True),
    ("title_assets/MPRESS.SPR",       "MPRESS.SPR",  1,  False, True),
    # Step 2a TSONIC.ATL: header + (8 body keyframes + 12 finger) seek+read pairs.
    ("title_assets/TSONIC.ATL",       "TSONIC.ATL", 21,  True,  False),
    # Step 2a ELECTRA.ATL: header + 8 keyframes seek+read pairs.
    ("title_assets/ELECTRA.ATL",      "ELECTRA.ATL", 9,  False, True),
    # Step 2b: gameplay entity assets - currently loaded at engine init
    # even though none of them draw until the GHZ scene.
    ("entities/DIGITS.SPR",           "DIGITS.SPR",  1,  False, True),
    ("entities/RING.SPR",             "RING.SPR",    1,  False, True),
    ("entities/SPRING.SPR",           "SPRING.SPR",  1,  False, True),
    ("entities/MONITOR.SPR",          "MONITOR.SPR", 1,  False, True),
    ("entities/SIGNPOST.SPR",         "SIGNPOST.SPR",1,  False, True),
    ("entities/BADNIK.SPR",           "BADNIK.SPR",  1,  False, True),
    ("entities/RINGSFX.PCM",          "RINGSFX.PCM", 1,  False, True),
    ("entities/JUMPSFX.PCM",          "JUMPSFX.PCM", 1,  False, True),
    ("entities/BREAKSFX.PCM",         "BREAKSFX.PCM",1,  False, True),
    ("entities/STOMPSFX.PCM",         "STOMPSFX.PCM",1,  False, True),
    ("entities/BOUNCESFX.PCM",        "BOUNCESFX.PCM",1, False, True),
]


def size_kb(path: Path) -> float:
    try:
        return path.stat().st_size / 1024.0
    except FileNotFoundError:
        return 0.0


def expected_sec(bytes_kb: float, seeks: int) -> float:
    return bytes_kb / CD_BANDWIDTH_KBPS + seeks * SEEK_TIME_SEC


def main():
    rows = []
    cum = 0.0
    cum_first_paint = 0.0
    cum_deferable = 0.0
    missing = []

    for label, fname, seeks, mandatory, deferable in COLD_BOOT_ASSETS:
        p = CD_DIR / fname
        kb = size_kb(p)
        if kb == 0.0:
            missing.append(fname)
        sec = expected_sec(kb, seeks)
        cum += sec
        if mandatory:
            cum_first_paint += sec
        if deferable:
            cum_deferable += sec
        rows.append({
            "label": label,
            "filename": fname,
            "size_kb": round(kb, 2),
            "seeks": seeks,
            "expected_load_sec": round(sec, 3),
            "mandatory_for_first_paint": mandatory,
            "deferable": deferable,
        })

    # Print table -----------------------------------------------------------
    print("=" * 90)
    print("Phase 1.29 STEP 1 - Cold-boot asset load audit (statically derived)")
    print("=" * 90)
    print(f"{'Label':<32} {'Size KB':>9} {'Seeks':>6} {'Sec':>7} {'1st':>4} {'Defr':>5}")
    print("-" * 90)
    for r in rows:
        print(f"{r['label']:<32} {r['size_kb']:>9.2f} {r['seeks']:>6d} "
              f"{r['expected_load_sec']:>7.3f} "
              f"{'Y' if r['mandatory_for_first_paint'] else '-':>4} "
              f"{'Y' if r['deferable'] else '-':>5}")
    print("-" * 90)
    print(f"{'TOTAL synchronous cold-boot':<32} {'':<9} {'':<6} {cum:>7.3f} s")
    print(f"{'  mandatory for first paint':<32} {'':<9} {'':<6} "
          f"{cum_first_paint:>7.3f} s")
    print(f"{'  deferable to async':<32} {'':<9} {'':<6} "
          f"{cum_deferable:>7.3f} s")
    if missing:
        print(f"WARN: {len(missing)} asset(s) missing from cd/: {missing}")
    print("=" * 90)
    print(f"Budget threshold (per CLAUDE.md §4.5.1 Audit 4): 5.000 s")
    if cum > 5.0:
        print(f"AUDIT: BUDGET EXCEEDED ({cum:.3f} > 5.000 s)")
        verdict = "BUDGET_EXCEEDED"
    else:
        print(f"AUDIT: within budget")
        verdict = "within_budget"

    out = {
        "schema_version": 1,
        "cd_bandwidth_kbps": CD_BANDWIDTH_KBPS,
        "seek_time_sec": SEEK_TIME_SEC,
        "rows": rows,
        "cumulative_total_sec": round(cum, 3),
        "cumulative_first_paint_sec": round(cum_first_paint, 3),
        "cumulative_deferable_sec": round(cum_deferable, 3),
        "missing_assets": missing,
        "budget_threshold_sec": 5.0,
        "verdict": verdict,
    }
    REPORT_PATH.write_text(json.dumps(out, indent=2))
    print(f"Report written: {REPORT_PATH}")
    # Non-zero exit if over budget, so this tool can also act as a gate.
    sys.exit(0 if cum <= 5.0 else 2)


if __name__ == "__main__":
    main()
