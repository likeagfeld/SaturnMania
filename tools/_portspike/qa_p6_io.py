#!/usr/bin/env python3
# P6.2 gate (Task #206) -- prove the real engine RSDK::LoadFile (Core/Reader.cpp)
# can OPEN and READ a real file off the Saturn CD on SH-2, through a Saturn-native
# FileIO backend (Reader.hpp FileIO/fOpen/fRead/fSeek/fTell/fClose override ->
# p6_gfs.c -> SGL GFS over the CD block), NOT through newlib stdio (which has no
# disc backend on this target -- p6_syscalls.c:_open returns -1).
#
# WHY THIS IS THE P6.2 MILESTONE (engine file I/O over CD/GFS)
#   P6.1 (#205) proved the engine's OWN dispatch table runs on SH-2. The engine's
#   real boot then needs to READ the disc: LoadSceneFolder/LoadSceneAssets all go
#   through RSDK::LoadFile -> fOpen/fRead. On PC that is fopen/fread; on Saturn
#   there is no stdio disc backend, so LoadFile must reach the CD block. P6.2 gives
#   Reader.hpp a Saturn FileIO that funnels fOpen/fRead/fSeek/fTell/fClose into SGL
#   GFS (GFS_NameToId/GFS_Open/GFS_GetFileSize/GFS_Fread) over the real CD. GREEN
#   proves the UNMODIFIED LoadFile body (Reader.cpp:250-339) opens a real on-disc
#   file, learns its true size (fSeek END + fTell), and reads its bytes -- on SH-2.
#
# THE RUNTIME WITNESSES (C-linkage globals in p6_ring.o(.bss), WRAM-H pinned bank,
# read from a Mednafen savestate; same byte order as _p6_w_magic)
#   REQUIRED (the gate hard-fails on any):
#     p6_w_magic        const 0x12345678 -- WRAM byte-order self-calibration.
#     p6_w_io_loaded    1  <- RSDK::LoadFile(&info,"P6IO.BIN",FMODE_RB) returned
#                       true (info.file non-NULL). In the RED build stdio fOpen
#                       hits the _open=-1 stub -> LoadFile false -> stays 0.
#     p6_w_io_filesize  256 <- info.fileSize. LoadFile (Reader.cpp:330-334) does
#                       fSeek(file,0,SEEK_END); fileSize=fTell(file); fSeek SET.
#                       So Saturn_fTell after SEEK_END must report the EXACT byte
#                       size GFS_GetFileSize yields (sctsz*(nsct-1)+lstsz).
#     p6_w_io_firstbytes 0xDEADBEEF <- ReadBytes(&info,hdr,4) then big-endian
#                       pack hdr[0..3]. P6IO.BIN's first four bytes are DE AD BE EF,
#                       so this proves real file CONTENT crossed fRead, not zeros.
#   DIAGNOSTIC (printed, NOT hard-required -- they self-diagnose a failed GREEN):
#     p6_w_io_gfsinit   GFS_Init() return (jo's recipe treats <=2 as failure).
#     p6_w_io_fid       GFS_NameToId("P6IO.BIN") return (>=0 == file found on the
#                       ISO; <0 == the mkisofs name recipe did not expose it).
#
# RED vs GREEN (one gate, two builds -- the RED->GREEN demonstration)
#   RED   (link_p6.sh io-red):   Reader.hpp Saturn branch is COMPILED OUT
#         (-DP6_IO_TEST but NOT -DRETRO_SATURN_FILEIO), so LoadFile uses newlib
#         stdio. fOpen -> fopen -> _open (p6_syscalls.c:46) returns -1 -> LoadFile
#         returns false -> p6_w_io_loaded stays 0. The build SUCCEEDS and runs;
#         this is a genuine RUNTIME red (LoadFile reached, returned false), not a
#         link failure. The gate REJECTS it (W1 loaded!=1).
#   GREEN (link_p6.sh io-green):  Core_Reader.o recompiled -DRETRO_SATURN_FILEIO
#         (Reader.hpp redirects FileIO/fOpen/... to Saturn_*), p6_gfs.o + the SGL
#         coupling stubs p6_dma_stubs.o + LIBCD.A linked, p6_gfs_init() runs
#         CDC_CdInit + GFS_Init before the LoadFile. Real CD read -> loaded=1,
#         filesize=256, firstbytes=0xDEADBEEF. The gate ACCEPTS it.
#
# RED-first (CLAUDE.md 4.7 / skill Step 7): on the current tree there is no io map
# and no io savestate, so the gate fires RED ("artifacts missing"). It also fires
# RED if a witness symbol is absent (harness not wired), if the magic anchor does
# not read back (image did not load / wrong bank), if the SH-2 PC is outside core
# .text (crash), or if loaded/filesize/firstbytes disagree with the model. It turns
# GREEN only for a real captured io-green run.
#
#   python tools/_portspike/qa_p6_io.py [savestate.mcs] [link.map]
#   python tools/_portspike/qa_p6_io.py --selftest    # prove the RED path fires
#
# P6 STATUS: this is P6.2 scaffolding (Saturn-only, build-gated; PC build
# untouched). GREEN here is the "engine file I/O over CD works" checkpoint --
# STOP and report; P6.3-P6.7 (real LoadScene, VDP1/VDP2, SCSP, decomp object set,
# hand-port retirement) proceed only under user direction.

import importlib.util
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MAP_DEFAULT = os.path.join(HERE, "_p6", "_p6_io.map")
MCS_DEFAULT = os.path.join(HERE, "_p6", "_p6_io.mcs")
MCS_EXTRACT = os.path.normpath(os.path.join(HERE, "..", "mcs_extract.py"))

TEXT_LOAD = 0x06004000
WRAM_H = (0x06000000, 0x06100000)
WRAM_L = (0x00200000, 0x00300000)

# ---- Deterministic expected witness values (must equal what p6_main.cpp +
#      cd/P6IO.BIN produce) -------------------------------------------------------
EXP_LOADED     = 1
EXP_FILESIZE   = 256          # cd/P6IO.BIN is exactly 256 bytes
EXP_FIRSTBYTES = 0xDEADBEEF   # cd/P6IO.BIN[0:4] = DE AD BE EF, packed big-endian

# Witness symbol names (sh-none-elf one-underscore convention; map_symbol tolerant).
SYM_TEXT_LO    = "__text_start"
SYM_TEXT_HI    = "__text_end"
# Path-A (full-jo build) fallback: COMMON/sgl.linker must NOT be modified
# (CLAUDE.md 10), and it defines neither __text_start nor __text_end. It DOES
# define ___Start (SLSTART at 0x06004000 == the .text load base) and __etext
# (end of the .text output section, which collects *(.text) from EVERY object
# incl. LIBSGL.A/LIBCD.A). [___Start, __etext) brackets all loaded core code, so
# W0 still means "PC is in code, not crashed at the BIOS spin handler 0x06000956".
SYM_TEXT_LO_FB = "___Start"
SYM_TEXT_HI_FB = "__etext"
SYM_MAGIC      = "_p6_w_magic"
SYM_LOADED     = "_p6_w_io_loaded"
SYM_FILESIZE   = "_p6_w_io_filesize"
SYM_FIRSTBYTES = "_p6_w_io_firstbytes"
SYM_GFSINIT    = "_p6_w_io_gfsinit"
SYM_FID        = "_p6_w_io_fid"

REQUIRED_SYMS   = [SYM_MAGIC, SYM_LOADED, SYM_FILESIZE, SYM_FIRSTBYTES]
DIAGNOSTIC_SYMS = [SYM_GFSINIT, SYM_FID]
WITNESS_SYMS    = REQUIRED_SYMS + DIAGNOSTIC_SYMS

MAGIC_VALUE = 0x12345678
MAGIC_BE = bytes([0x12, 0x34, 0x56, 0x78])
PERMS = [
    ("big-endian (identity)", (0, 1, 2, 3)),
    ("16-bit pair-swap", (1, 0, 3, 2)),
    ("full little-endian", (3, 2, 1, 0)),
    ("32-bit word-swap", (2, 3, 0, 1)),
]


def read_text(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def map_symbol(map_text, name):
    """Address of a defined symbol from GNU ld -Map output, tolerating the
    sh-none-elf one-underscore strip on object symbols."""
    bare = name[1:] if name.startswith("_") else name
    pat = re.compile(r"^\s+0x([0-9a-fA-F]+)\s+_?" + re.escape(bare) + r"(?:\s|=|$)",
                     re.M)
    m = pat.search(map_text)
    return (int(m.group(1), 16) & 0xFFFFFFFF) if m else None


def load_harness():
    spec = importlib.util.spec_from_file_location("mcs_extract", MCS_EXTRACT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def calibrate(raw_magic):
    if raw_magic is None or len(raw_magic) < 4:
        return None, None
    raw4 = bytes(raw_magic[:4])
    for label, perm in PERMS:
        if bytes(MAGIC_BE[i] for i in perm) == raw4:
            return label, perm
    return None, None


def decode_u32(raw4, perm):
    inv = [perm.index(j) for j in range(4)]
    true_be = bytes(raw4[inv[j]] for j in range(4))
    return struct.unpack(">I", true_be)[0]


def decode_i32(raw4, perm):
    v = decode_u32(raw4, perm)
    return v - 0x100000000 if v & 0x80000000 else v


def peek_u32(mod, sections, addr, perm, signed=False):
    raw = mod._peek_bytes(sections, addr, 4)
    if raw is None or len(raw) < 4:
        return None
    return decode_i32(bytes(raw[:4]), perm) if signed else decode_u32(bytes(raw[:4]), perm)


def _hx(v):
    return "None" if v is None else ("0x%08X" % (v & 0xFFFFFFFF))


def _dv(v):
    return "None" if v is None else str(v)


def evaluate(pc, text_lo, text_hi, vals):
    """vals: dict symbol->decoded int. Returns (ok, [(name, ok, detail)]).
    Only the REQUIRED witnesses gate; diagnostics are reported separately."""
    checks = []

    c0 = (pc is not None and text_lo is not None and text_hi is not None
          and text_lo <= pc < text_hi)
    checks.append(("W0 SH2-M PC in core .text [%s,%s) (no crash at capture)"
                   % (_hx(text_lo), _hx(text_hi)), c0, "PC=%s" % _hx(pc)))

    got_loaded = vals.get(SYM_LOADED)
    checks.append((
        "W1 RSDK::LoadFile(\"P6IO.BIN\",FMODE_RB) returned true via Saturn FileIO",
        got_loaded is not None and got_loaded == EXP_LOADED,
        "%s = %s (expect %s)" % (SYM_LOADED, _dv(got_loaded), _dv(EXP_LOADED))))

    got_size = vals.get(SYM_FILESIZE)
    checks.append((
        "W2 fSeek(END)+fTell learned the file's EXACT size (info.fileSize==256)",
        got_size is not None and got_size == EXP_FILESIZE,
        "%s = %s (expect %s)" % (SYM_FILESIZE, _dv(got_size), _dv(EXP_FILESIZE))))

    got_first = vals.get(SYM_FIRSTBYTES)
    checks.append((
        "W3 ReadBytes pulled real file CONTENT off the disc (first4==0xDEADBEEF)",
        got_first is not None and (got_first & 0xFFFFFFFF) == EXP_FIRSTBYTES,
        "%s = %s (expect %s)" % (SYM_FIRSTBYTES, _hx(got_first), _hx(EXP_FIRSTBYTES))))

    ok = all(c for _, c, _ in checks)
    return ok, checks


def _print_checks(checks):
    for name, ok, detail in checks:
        print("  [%s] %s" % ("GREEN" if ok else " RED ", name))
        print("          %s" % detail)


def _print_diagnostics(vals):
    gi = vals.get(SYM_GFSINIT)
    fid = vals.get(SYM_FID)
    print("  diagnostics (not gated -- self-diagnose a failed GREEN):")
    print("          %s = %s  (GFS_Init return; jo treats <=2 as failure)"
          % (SYM_GFSINIT, _dv(gi)))
    print("          %s = %s  (GFS_NameToId(\"P6IO.BIN\"); >=0 == found on ISO)"
          % (SYM_FID, _dv(fid)))


def run_selftest():
    print("=" * 72)
    print("P6.2 FILE-I/O GATE -- SELFTEST (prove the RED path fires)")
    print("=" * 72)
    print("  expect (GREEN): loaded=%d filesize=%d firstbytes=%s"
          % (EXP_LOADED, EXP_FILESIZE, _hx(EXP_FIRSTBYTES)))
    print("  RED build: Reader.hpp Saturn branch compiled out -> stdio fOpen ->")
    print("             _open stub returns -1 -> LoadFile false -> loaded stays 0.")
    print("-" * 72)
    text_lo, text_hi = TEXT_LOAD, 0x06030000
    # The RED capture: stdio backend, _open=-1 -> LoadFile false. loaded/filesize/
    # firstbytes all 0; gfs diagnostics 0 (p6_gfs_init not called in io-red). The
    # gate MUST reject this.
    red_vals = {
        SYM_MAGIC: MAGIC_VALUE, SYM_LOADED: 0, SYM_FILESIZE: 0,
        SYM_FIRSTBYTES: 0, SYM_GFSINIT: 0, SYM_FID: 0,
    }
    red_ok, red_checks = evaluate(0x06004F00, text_lo, text_hi, red_vals)
    _print_checks(red_checks)
    _print_diagnostics(red_vals)
    # The GREEN capture: Saturn FileIO over GFS -> real CD read.
    green_vals = {
        SYM_MAGIC: MAGIC_VALUE, SYM_LOADED: EXP_LOADED, SYM_FILESIZE: EXP_FILESIZE,
        SYM_FIRSTBYTES: EXP_FIRSTBYTES, SYM_GFSINIT: 3, SYM_FID: 5,
    }
    green_ok, _ = evaluate(0x06004F00, text_lo, text_hi, green_vals)
    print("-" * 72)
    if (not red_ok) and green_ok:
        print("RESULT: RED (selftest) -- the stdio (io-red build) capture is")
        print("        correctly REJECTED (W1 loaded==0: LoadFile hit the _open=-1")
        print("        stub) while a synthetic GREEN capture (Saturn GFS FileIO)")
        print("        passes. The W0-W3 RED branch is reachable; the gate")
        print("        distinguishes a real CD read from the stubbed stdio path.")
        return 1
    print("RESULT: ERROR -- selftest logic inconsistent (red_ok=%s green_ok=%s)"
          % (red_ok, green_ok))
    return 2


def main(argv):
    if "--selftest" in argv:
        return run_selftest()

    pos = [a for a in argv if not a.startswith("--")]
    mcs = pos[0] if len(pos) >= 1 else MCS_DEFAULT
    mp = pos[1] if len(pos) >= 2 else MAP_DEFAULT

    print("=" * 72)
    print("P6.2 FILE-I/O GATE: engine LoadFile reads a real CD file via GFS (Mednafen)")
    print("=" * 72)
    print("  savestate : %s" % mcs)
    print("  link map  : %s" % mp)
    print("  model     : loaded=%d filesize=%d firstbytes=%s"
          % (EXP_LOADED, EXP_FILESIZE, _hx(EXP_FIRSTBYTES)))
    print("-" * 72)

    if not os.path.isfile(mp):
        print("RESULT: RED -- P6 io link map missing (%s)." % mp)
        print("        Build it:  bash tools/_portspike/_p6/link_p6.sh io-green")
        return 1
    if not os.path.isfile(mcs):
        print("RESULT: RED -- P6 io savestate missing (%s)." % mcs)
        print("        Build the io ISO and capture a post-LoadFile state:")
        print("        bash tools/_portspike/_p6/build_p6_iso.sh io-green")
        print("        pwsh -File tools/qa_savestate.ps1 -Cue <p6io>.cue \\")
        print("             -SaveFrame 50 -FpsScale 2.0 \\")
        print("             -Out tools/_portspike/_p6/_p6_io.mcs")
        print("        (deep capture ~100 emu-s; -SaveFrame 12 was too early --")
        print("         the BIOS had not yet copied the image from CDB to WRAM-H.)")
        return 1

    map_text = read_text(mp)

    # Text bounds: the decoupled io-green link (p6_io.linker) defines
    # __text_start/__text_end; the Path-A full-jo build (COMMON/sgl.linker) does
    # not -- fall back to ___Start (.text load base) / __etext (end of .text).
    text_lo = map_symbol(map_text, SYM_TEXT_LO)
    if text_lo is None:
        text_lo = map_symbol(map_text, SYM_TEXT_LO_FB)
    text_hi = map_symbol(map_text, SYM_TEXT_HI)
    if text_hi is None:
        text_hi = map_symbol(map_text, SYM_TEXT_HI_FB)

    syms = {SYM_TEXT_LO: text_lo, SYM_TEXT_HI: text_hi}
    missing = []
    if text_lo is None:
        missing.append("%s (or fallback %s)" % (SYM_TEXT_LO, SYM_TEXT_LO_FB))
    if text_hi is None:
        missing.append("%s (or fallback %s)" % (SYM_TEXT_HI, SYM_TEXT_HI_FB))
    for s in WITNESS_SYMS:
        a = map_symbol(map_text, s)
        syms[s] = a
        if a is None:
            missing.append(s)
    if missing:
        print("RESULT: RED -- witness symbol(s) absent from the P6 io map:")
        for s in missing:
            print("          %s" % s)
        print("        The io witnesses / text bounds are not wired into the link.")
        print("        Add them, re-link, re-capture.")
        return 1

    print("[1/3] Witness symbols resolved from the link map:")
    for s in (SYM_TEXT_LO, SYM_TEXT_HI, *WITNESS_SYMS):
        print("        %-22s %s" % (s, _hx(syms[s])))

    mod = load_harness()
    sections = mod.parse_savestate(_as_path(mcs))

    raw_magic = mod._peek_bytes(sections, syms[SYM_MAGIC], 4)
    label, perm = calibrate(raw_magic)
    print("[2/3] WRAM byte-order calibration from _p6_w_magic:")
    if perm is None:
        print("        magic raw bytes = %s (expected a permutation of %s)"
              % (raw_magic.hex() if raw_magic else "None", MAGIC_BE.hex()))
        print("RESULT: RED -- the magic anchor (0x%08X) did not read back as any"
              % MAGIC_VALUE)
        print("        known byte permutation. The image did not load / witnesses")
        print("        are not in the expected bank / harness not linked.")
        return 1
    print("        magic raw = %s -> transform: %s" % (raw_magic.hex(), label))

    regs = mod._sh2_regs(sections, "master")
    pc = regs.get("PC") if regs else None

    # loaded/filesize/gfsinit/fid are signed scalars; firstbytes is an address-like
    # 0xDEADBEEF compared unsigned.
    signed = {SYM_LOADED, SYM_FILESIZE, SYM_GFSINIT, SYM_FID}
    vals = {}
    for s in WITNESS_SYMS:
        vals[s] = peek_u32(mod, sections, syms[s], perm, signed=(s in signed))

    print("        peeked witnesses:")
    for s in WITNESS_SYMS:
        show = _hx(vals[s]) if s == SYM_FIRSTBYTES else _dv(vals[s])
        print("          %-22s = %s" % (s, show))

    ok, checks = evaluate(pc, syms[SYM_TEXT_LO], syms[SYM_TEXT_HI], vals)

    print("[3/3] Engine file-I/O witnesses:")
    print("-" * 72)
    _print_checks(checks)
    _print_diagnostics(vals)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the UNMODIFIED engine RSDK::LoadFile opened a real")
        print("        on-disc file through a Saturn-native FileIO backend (SGL GFS")
        print("        over the CD block), learned its exact size via fSeek(END)+")
        print("        fTell (256), and read its real content (0xDEADBEEF) off the")
        print("        disc -- on SH-2. P6.2 done -- STOP and report for the P6")
        print("        checkpoint (P6.3-P6.7 proceed only under user direction).")
        return 0
    print("RESULT: RED -- the captured state does not satisfy all file-I/O")
    print("        witnesses; LoadFile did not open+size+read the real CD file as")
    print("        modelled (stdio-stub io-red build, or a regression).")
    return 1


def _as_path(p):
    try:
        from pathlib import Path
        return Path(p)
    except Exception:
        return p


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
