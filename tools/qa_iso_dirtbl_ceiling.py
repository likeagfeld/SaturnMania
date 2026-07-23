#!/usr/bin/env python
# qa_iso_dirtbl_ceiling.py -- RED-first gate for the GFS directory-table overflow
# bug class (task: badnik/HUD/Water sheets fail to bind after the 4bpp pink fix).
#
# MECHANISM (measured, GFS.C/GFS_DIR.C source-cited):
#   The shipping/chain build boots via jo HAL (p6_jo_boot.c -> jo_core_init ->
#   jo_fs_init), so the ACTIVE GFS directory table is jo-engine's __jo_fs_dirtbl,
#   sized JO_FS_MAX_FILES (conf.h). p6_gfs.c's P6_GFS_MAX_DIR (22) is DEAD in this
#   build -- p6_gfs_init() is never called.
#   gfdr_setupDirNameTbl (GFS_DIR.C:438-470) reads AT MOST NDIR=JO_FS_MAX_FILES
#   root directory records in on-disc (alphabetical) order. Any file at ISO root
#   position >= JO_FS_MAX_FILES gets NO dirtbl slot -> GFS_NameToId (GFS.C:340,
#   GFS_DIR.C:244-257) returns -1 -> rsdk_storage_load_to_lwram / Saturn_fOpen
#   fail -> the sheet never binds (sheetID 0xFF -> shared-slot corruption).
#
# GATE: assert (root entries incl. '.' + '..') <= JO_FS_MAX_FILES. Fires RED when
# the disc has more root entries than the dirtbl can hold.
#
# Usage: python tools/qa_iso_dirtbl_ceiling.py [game.iso] [--max N]
#   --max overrides JO_FS_MAX_FILES autodetection from jo-engine/jo_engine/jo/conf.h
import struct, sys, re, os

def parse_iso_root(iso):
    f = open(iso, "rb")
    f.seek(0)
    head = f.read(16)
    if head[0:12] == b"\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\x00":
        SECT, DOFF = 2352, 16
    else:
        SECT, DOFF = 2048, 0
    def lsn(n):
        f.seek(n * SECT + DOFF); return f.read(2048)
    pvd = lsn(16)
    assert pvd[1:6] == b"CD001", pvd[1:6]
    rr = pvd[156:156+34]
    lba = struct.unpack("<I", rr[2:6])[0]
    length = struct.unpack("<I", rr[10:14])[0]
    data = b"".join(lsn(lba + i) for i in range((length + 2047) // 2048))
    names, pos = [], 0
    while pos < length:
        rl = data[pos]
        if rl == 0:
            nxt = ((pos // 2048) + 1) * 2048
            if nxt >= length: break
            pos = nxt; continue
        fl = data[pos + 32]
        names.append(data[pos + 33:pos + 33 + fl].decode("latin1"))
        pos += rl
    return names

def detect_max():
    conf = os.path.join(os.path.dirname(__file__), "..", "jo-engine",
                        "jo_engine", "jo", "conf.h")
    try:
        txt = open(conf, "r", errors="ignore").read()
        m = re.search(r"#\s*define\s+JO_FS_MAX_FILES\s*\(?\s*(\d+)", txt)
        if m: return int(m.group(1))
    except OSError:
        pass
    return None

def main():
    iso = "game.iso"
    override = None
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] == "--max":
            override = int(args[i + 1]); i += 2
        else:
            iso = args[i]; i += 1
    names = parse_iso_root(iso)
    nroot = len(names)
    maxf = override if override is not None else detect_max()
    print("ISO:", iso)
    print("root entries (incl. '.' and '..'):", nroot)
    print("JO_FS_MAX_FILES (active dirtbl NDIR):", maxf)
    if maxf is None:
        print("RESULT: SKIP (could not detect JO_FS_MAX_FILES)")
        return 0
    if nroot > maxf:
        dropped = names[maxf:]
        print("DROPPED (no dirtbl slot -> GFS_NameToId returns -1):")
        for d in dropped:
            print("   ", d)
        print(f"RESULT: RED -- {nroot} root entries > {maxf} dirtbl slots "
              f"({nroot - maxf} file(s) unresolvable)")
        return 1
    print(f"RESULT: GREEN -- {nroot} <= {maxf} (margin {maxf - nroot})")
    return 0

if __name__ == "__main__":
    sys.exit(main())
