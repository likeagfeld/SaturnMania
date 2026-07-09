#!/usr/bin/env python3
"""qa_netmem.py -- LIVE Saturn memory reads over RetroArch's UDP network command
interface (Beetle Saturn core = Mednafen's Saturn core, same emulation as the
standalone Mednafen baseline but with live READ_CORE_MEMORY / WRITE_CORE_MEMORY).

WHY: standalone Mednafen has no memory API, so all diagnosis went through F5
savestate snapshots (mcs_extract.py) -- a point in time, no watchpoints. This
client reads/writes any mapped address of a RUNNING core, so a value can be
polled as it evolves (e.g. cutscene_state marching through the AIZ beats) and a
hypothesis can be poked live (WRITE_CORE_MEMORY) without a rebuild.

PROTOCOL (RetroArch network_cmd, UDP, default port 55355; ASCII line, no 0x):
  send:  "READ_CORE_MEMORY <addr_hex> <nbytes_dec>\n"
  reply: "READ_CORE_MEMORY <addr_hex> <b0_hex> <b1_hex> ..."   (space-separated)
         "READ_CORE_MEMORY <addr_hex> -1"                       (addr unmapped)
  send:  "WRITE_CORE_MEMORY <addr_hex> <b0_hex> <b1_hex> ...\n"
  reply: "WRITE_CORE_MEMORY <addr_hex> <count>"

BYTE ORDER -- DO NOT ASSUME. The SH-2 is big-endian, so a live read SHOULD
return bytes MSB-first (no swap). mcs_extract's swap32 is a Mednafen-SAVESTATE
serialization artifact (gotcha #10) and very likely does NOT apply to the live
libretro read. `read32` therefore defaults to big-endian, plain -- and
tools/qa_netmem_probe.py cross-checks a KNOWN value (from a savestate) against
this to PROVE the order before any live read is trusted. If the probe shows a
different order, set --order on the CLI / order= in code; the probe prints the
exact bytes so the mapping is measured, never guessed.

ADDRESS MAP -- ALSO DO NOT ASSUME. READ_CORE_MEMORY reads through the memory-map
descriptors the core registers; the virtual address may or may not equal the raw
Saturn physical address, and coverage may be WRAM-only. The probe reads a known
Saturn address (e.g. the Zone struct) and reports whether it is mapped and
whether the value matches the savestate -- establishing the coverage + any
offset empirically.

Usage:
  python tools/qa_netmem.py --peek32 0x00201F88 [--port 55355] [--host 127.0.0.1]
  python tools/qa_netmem.py --peek 0x00243000 --len 64        # raw hex dump
  python tools/qa_netmem.py --raw "READ_CORE_MEMORY 201f88 4"  # send verbatim, print reply
  python tools/qa_netmem.py --poke 0x00201F88 00 00 00 00      # WRITE_CORE_MEMORY
"""
from __future__ import annotations

import argparse
import socket
import sys


class RetroMem:
    def __init__(self, host: str = "127.0.0.1", port: int = 55355, timeout: float = 2.0):
        self.addr = (host, port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(timeout)

    def _txn(self, line: str) -> str:
        """Send one command line, return the decoded reply (raises on timeout)."""
        self.sock.sendto((line + "\n").encode("ascii"), self.addr)
        data, _ = self.sock.recvfrom(65536)
        return data.decode("ascii", errors="replace").strip()

    def raw(self, line: str) -> str:
        return self._txn(line)

    def read(self, addr: int, nbytes: int) -> bytes:
        """Return `nbytes` raw bytes at `addr` in the order the core reports them.
        Raises ValueError if the core answers -1 (address not mapped)."""
        reply = self._txn(f"READ_CORE_MEMORY {addr:x} {nbytes}")
        toks = reply.split()
        # Expected: ["READ_CORE_MEMORY", "<addr>", "<b0>", "<b1>", ...] or [..., "-1"]
        if len(toks) < 3:
            raise ValueError(f"short/again reply: {reply!r}")
        if toks[2] == "-1":
            raise ValueError(f"address 0x{addr:08X} not mapped (core returned -1)")
        try:
            return bytes(int(t, 16) for t in toks[2:])
        except ValueError:
            raise ValueError(f"unparseable reply: {reply!r}")

    def read32(self, addr: int, order: str = "big") -> int:
        return int.from_bytes(self.read(addr, 4), order)

    def read16(self, addr: int, order: str = "big") -> int:
        return int.from_bytes(self.read(addr, 2), order)

    def write(self, addr: int, data: bytes) -> str:
        payload = " ".join(f"{b:02x}" for b in data)
        return self._txn(f"WRITE_CORE_MEMORY {addr:x} {payload}")

    # ---- Saturn-physical addressing over READ_CORE_RAM (Beetle Saturn) ----
    # MEASURED (2026-07-04, objectEntityList=0x00243000 read back exactly):
    # Beetle Saturn does NOT register a READ_CORE_MEMORY memory map ("no memory
    # map defined"), but DOES expose SYSTEM_RAM via READ_CORE_RAM offsets:
    #   WRAM-L (phys 0x00200000..0x002FFFFF) -> offset (phys - 0x00200000)
    #   WRAM-H (phys 0x06000000..0x060FFFFF) -> offset 0x100000 + (phys - 0x06000000)
    # and the bytes are BYTE-PAIR-SWAPPED per 16-bit halfword -- the SAME quirk as
    # the Mednafen savestate (gotcha #10), so mcs_extract's swap32 applies here too
    # (Beetle Saturn IS Mednafen's core). VDP1/VDP2 VRAM, CRAM, registers are NOT
    # in SYSTEM_RAM -> use the Mednafen savestate path for those.
    _REGIONS = [
        (0x00200000, 0x00300000, 0x00000000),  # WRAM-L
        (0x06000000, 0x06100000, 0x00100000),  # WRAM-H
    ]

    @classmethod
    def _phys_to_off(cls, addr: int):
        for lo, hi, base in cls._REGIONS:
            if lo <= addr < hi:
                return base + (addr - lo)
        return None

    def read_saturn(self, addr: int, nbytes: int) -> bytes:
        """Read nbytes at a Saturn PHYSICAL address via READ_CORE_RAM, returning
        bytes with the 16-bit pair-swap undone (so a big-endian unpack yields the
        real value -- identical convention to mcs_extract). Raises for a non-SYSTEM
        region (use the savestate path for VRAM/CRAM/regs)."""
        off = self._phys_to_off(addr)
        if off is None:
            raise ValueError(f"0x{addr:08X} not in SYSTEM_RAM (WRAM-L/H); use savestate for VRAM/CRAM/regs")
        reply = self._txn(f"READ_CORE_RAM {off:x} {nbytes}")
        toks = reply.split()
        if len(toks) < 3 or toks[2] == "-1":
            raise ValueError(f"READ_CORE_RAM failed: {reply!r}")
        raw = bytearray(int(t, 16) for t in toks[2:])
        for i in range(0, len(raw) - 1, 2):      # undo the per-halfword byte swap
            raw[i], raw[i + 1] = raw[i + 1], raw[i]
        return bytes(raw)

    def read32_saturn(self, addr: int) -> int:
        return int.from_bytes(self.read_saturn(addr, 4), "big")

    def read16_saturn(self, addr: int) -> int:
        return int.from_bytes(self.read_saturn(addr, 2), "big")


def _parse_addr(s: str) -> int:
    return int(s, 16) if s.lower().startswith("0x") else int(s, 16)


def main(argv=None) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=55355)
    p.add_argument("--timeout", type=float, default=2.0)
    p.add_argument("--order", choices=["big", "little"], default="big")
    p.add_argument("--peek32", help="read a 32-bit word (hex addr)")
    p.add_argument("--peek16", help="read a 16-bit word (hex addr)")
    p.add_argument("--peek", help="raw byte dump base addr (hex)")
    p.add_argument("--len", type=int, default=16, help="bytes for --peek")
    p.add_argument("--poke", nargs="+", help="ADDR B0 B1 ... : WRITE_CORE_MEMORY")
    p.add_argument("--raw", help="send a verbatim command line, print the reply")
    a = p.parse_args(argv)

    mem = RetroMem(a.host, a.port, a.timeout)
    try:
        if a.raw is not None:
            print(mem.raw(a.raw))
        elif a.peek32 is not None:
            addr = _parse_addr(a.peek32)
            print(f"0x{addr:08X} = 0x{mem.read32(addr, a.order):08X} "
                  f"({mem.read32(addr, a.order)})")
        elif a.peek16 is not None:
            addr = _parse_addr(a.peek16)
            print(f"0x{addr:08X} = 0x{mem.read16(addr, a.order):04X}")
        elif a.peek is not None:
            addr = _parse_addr(a.peek)
            raw = mem.read(addr, a.len)
            print(f"0x{addr:08X} [{a.len}B]: {raw.hex()}")
        elif a.poke is not None:
            addr = _parse_addr(a.poke[0])
            data = bytes(int(b, 16) for b in a.poke[1:])
            print(mem.write(addr, data))
        else:
            p.error("nothing to do; pass --peek32/--peek16/--peek/--poke/--raw")
    except (socket.timeout, TimeoutError):
        sys.stderr.write(f"qa_netmem: no reply from {a.host}:{a.port} "
                         "(RetroArch network_cmd not enabled, wrong port, or core not running)\n")
        return 2
    except ValueError as e:
        sys.stderr.write(f"qa_netmem: {e}\n")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
