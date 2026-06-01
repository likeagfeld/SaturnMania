#!/usr/bin/env python3
"""qa_title3d_gate.py - Gate V1.29c: assert cd/TITLE3D.DAT + cd/TITLE3D.PAL
exist with the expected shape AND that the palette contains the sky-blue
+ green-foliage hue signature characteristic of the Mania title island
(per the decomp's Title TileLayer 3 composition).  Also asserts src/main.c
references TITLE3D.DAT/PAL (not the legacy TITLE.DAT/PAL).

Per CLAUDE.md §4.7 and memory/qa-iterative-improvement.md: the gate must
fire RED on the current (pre-repoint) build (TITLE3D.* missing OR main.c
still references TITLE.DAT) and GREEN after the asset + main.c repoint
both land.  Decomp-canonical asset source per CLAUDE.md §1.

Exit 0 = GREEN.  Exit 1 = RED.
"""
import colorsys, struct, sys, re
from pathlib import Path

ROOT     = Path(__file__).resolve().parent.parent
OUT_W    = 256
OUT_H    = 256
DAT_PATH = ROOT / "cd/TITLE3D.DAT"
PAL_PATH = ROOT / "cd/TITLE3D.PAL"
MAIN_C   = ROOT / "src/main.c"


def check_exists_sized(path, expected_size, label):
    if not path.exists():
        print(f"FAIL: {label} missing: {path}")
        return False
    actual = path.stat().st_size
    if actual != expected_size:
        print(f"FAIL: {label} size {actual} != expected {expected_size}: {path}")
        return False
    print(f"OK: {label} present, {actual} B")
    return True


def palette_hue_histogram(pal_bytes):
    """Decode 256 RGB555 BE entries, return (sky_blue_count, green_count,
    nonzero_count).  Sky-blue: HSV hue 180..240 deg, saturation >= 0.20.
    Green-foliage: HSV hue 80..160 deg, saturation >= 0.20."""
    sky = 0
    grn = 0
    nz  = 0
    for i in range(256):
        word = struct.unpack(">H", pal_bytes[i*2:i*2+2])[0]
        if word == 0: continue
        nz += 1
        r5 = (word >> 10) & 0x1F
        g5 = (word >>  5) & 0x1F
        b5 = (word      ) & 0x1F
        r = (r5 << 3 | r5 >> 2) / 255.0
        g = (g5 << 3 | g5 >> 2) / 255.0
        b = (b5 << 3 | b5 >> 2) / 255.0
        h, s, v = colorsys.rgb_to_hsv(r, g, b)
        hue_deg = h * 360.0
        if s >= 0.20:
            if 180.0 <= hue_deg <= 240.0: sky += 1
            if  80.0 <= hue_deg <= 160.0: grn += 1
    return sky, grn, nz


def main():
    failures = []
    # 1) asset shape
    if not check_exists_sized(DAT_PATH, OUT_W * OUT_H, "TITLE3D.DAT"):
        failures.append("DAT shape")
    if not check_exists_sized(PAL_PATH, 512, "TITLE3D.PAL"):
        failures.append("PAL shape")

    # 2) palette signature (only if PAL is readable)
    if PAL_PATH.exists():
        pal = PAL_PATH.read_bytes()
        sky, grn, nz = palette_hue_histogram(pal)
        print(f"palette: {nz}/256 non-zero, "
              f"sky-blue hues={sky}, green-foliage hues={grn}")
        # Thresholds: Mania title island palette typically has 20+ sky-blue
        # entries (sky + sea + cloud shadows) and 15+ green entries
        # (palm trees + grass).  Threshold loose enough to tolerate
        # median-cut quantisation variance.
        if sky < 10:
            print(f"FAIL: sky-blue hue count {sky} < 10 "
                  f"(palette doesn't look like a Mania island)")
            failures.append("sky hue")
        else:
            print(f"OK: sky-blue hue count {sky} >= 10")
        if grn < 8:
            print(f"FAIL: green-foliage hue count {grn} < 8")
            failures.append("green hue")
        else:
            print(f"OK: green-foliage hue count {grn} >= 8")

    # 3) main.c references the new asset names
    if MAIN_C.exists():
        src = MAIN_C.read_text(encoding="utf-8")
        # Find the setup_title_bg loads.  The legacy strings are
        # "TITLE.PAL" / "TITLE.DAT"; we want the new names referenced.
        has_new_dat = '"TITLE3D.DAT"' in src
        has_new_pal = '"TITLE3D.PAL"' in src
        # We also want the LEGACY names NOT loaded (the file may still
        # reference TITLE.DAT in comments — be tolerant).  Specifically
        # look for jo_fs_read_file("TITLE.DAT" ...) or "TITLE.PAL"
        # function-call patterns.
        legacy_dat_load = re.search(r'jo_fs_read_file\s*\(\s*"TITLE\.DAT"', src)
        legacy_pal_load = re.search(r'jo_fs_read_file\s*\(\s*"TITLE\.PAL"', src)
        if not has_new_dat:
            print(f'FAIL: src/main.c does not reference "TITLE3D.DAT"')
            failures.append("main.c DAT name")
        else:
            print(f'OK: src/main.c references "TITLE3D.DAT"')
        if not has_new_pal:
            print(f'FAIL: src/main.c does not reference "TITLE3D.PAL"')
            failures.append("main.c PAL name")
        else:
            print(f'OK: src/main.c references "TITLE3D.PAL"')
        if legacy_dat_load:
            print(f'FAIL: src/main.c still has jo_fs_read_file("TITLE.DAT") '
                  f'call at byte offset {legacy_dat_load.start()}')
            failures.append("legacy DAT load")
        else:
            print(f'OK: no legacy jo_fs_read_file("TITLE.DAT") call')
        if legacy_pal_load:
            print(f'FAIL: src/main.c still has jo_fs_read_file("TITLE.PAL") '
                  f'call at byte offset {legacy_pal_load.start()}')
            failures.append("legacy PAL load")
        else:
            print(f'OK: no legacy jo_fs_read_file("TITLE.PAL") call')

    if failures:
        print(f"\nGate V1.29c: RED ({len(failures)} failure(s): "
              f"{', '.join(failures)})")
        return 1
    print(f"\nGate V1.29c: GREEN")
    return 0


if __name__ == "__main__":
    sys.exit(main())
