#!/usr/bin/env python3
# Parse the P3 measurement-link map's .bss output section into per-object sizes,
# then evaluate the two-bank (WRAM-H + WRAM-L) split arithmetic for p3.linker.
#
# Hard constraints (Saturn physical RAM, DTS96 memory map):
#   WRAM-H 0x06000000..0x06100000 (1 MB); image loads at 0x06004000; SP grows
#          down from 0x06100000. WRAM-H .bss must end <= 0x06100000 - STACK.
#   WRAM-L 0x00200000..0x00300000 (1 MB); holds the rest of .bss + the malloc
#          heap. InitStorage mallocs 608 KB (Storage.cpp:43-47), so the heap
#          window must be >= 622592 B or InitStorage fails -> gate RED.
# The gate REQUIRES engine (Core_RetroEngine.o) + sceneInfo (Scene_Scene.o) in
# WRAM-H so they share the magic's byte-order calibration.
import re, sys

MAP = sys.argv[1] if len(sys.argv) > 1 else "tools/_portspike/_p3/_p3_measure.map"
IMAGE_BASE   = 0x06004000
WRAM_H_TOP   = 0x06100000
WRAM_L_BASE  = 0x00200000
WRAM_L_TOP   = 0x00300000
STACK        = 32 * 1024          # 32 KB reserve at top of WRAM-H. The boot path
                                  # (RunRetroEngine->InitStorage->InitEngine->Start-
                                  # GameObjects) is shallow + non-recursive; 32 KB
                                  # widens the WRAM-H .bss window from ~8.7 KB to
                                  # ~41 KB so the split has real margin on both ends.
HEAP_FLOOR   = 608 * 1024         # InitStorage 608 KB malloc floor

txt = open(MAP).read()
# isolate the "Linker script and memory map" portion
mm = txt[txt.index("Linker script and memory map"):]

# image span: first .text addr .. .bss start addr (flat link; offsets relative)
m_text = re.search(r"^\.text\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)", mm, re.M)
m_bss  = re.search(r"^\.bss\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)", mm, re.M)
text_start = int(m_text.group(1), 16)
bss_start  = int(m_bss.group(1), 16)
bss_total  = int(m_bss.group(2), 16)
image_size = bss_start - text_start

# per-object .bss lines:  " .bss   0x<addr>   0x<size>   <path>/<obj>.o"
objs = {}
for mo in re.finditer(r"^\s+\.bss(?:\.\S+)?\s+0x[0-9a-fA-F]+\s+0x([0-9a-fA-F]+)\s+(\S+)", mm, re.M):
    size = int(mo.group(1), 16)
    path = mo.group(2)
    if "_trueport_obj/" not in path:   # skip libc/libgcc archive members
        continue
    name = path.rsplit("/", 1)[-1]
    objs[name] = objs.get(name, 0) + size

print(f"image (text+rodata+data+tors) = {image_size:>8} B ({image_size/1024:.1f} KB)")
print(f".bss output section total      = {bss_total:>8} B ({bss_total/1024:.1f} KB)")
print(f"sum of per-object .bss          = {sum(objs.values()):>8} B (COMMON = {bss_total - sum(objs.values())} B)")
print("-" * 60)
for name, sz in sorted(objs.items(), key=lambda kv: -kv[1]):
    print(f"  {sz:>8}  0x{sz:06x}  {name}")
print("-" * 60)

# WRAM-H pinned set: engine (Core_RetroEngine.o) + sceneInfo (Scene_Scene.o) are
# gate-required (they share the witness magic's byte-order calibration). Scene_-
# Object.o (296 KB) is added to lift the WRAM-H .bss total above the 726,296 B
# heap floor (so WRAM-L keeps >= 608 KB for InitStorage) while staying under the
# 32 KB stack reserve -- ~27 KB heap margin + ~14 KB stack headroom. Every .bss
# object is position-independent (symbol-relocated), so any object may bank either
# way; only engine+sceneInfo are pinned for the gate's calibration need.
PIN_H = ["Scene_Scene.o", "Core_RetroEngine.o", "Scene_Object.o"]
h_bss = sum(objs[n] for n in PIN_H if n in objs)
l_bss = bss_total - h_bss          # remainder (incl COMMON) sweeps to WRAM-L

# WRAM-H layout: image [IMAGE_BASE, IMAGE_BASE+image_size) then pinned .bss on
# top. The .bss therefore STARTS after the image -- the prior revision omitted
# image_size from the .bss-end address and over-counted stack headroom by the
# full image size (~226 KB), which silently masked a 64 KB-stack overflow.
bss_h_start = IMAGE_BASE + image_size
bss_h_end   = bss_h_start + h_bss
h_floor     = WRAM_H_TOP - STACK                       # SP grows down from WRAM_H_TOP
heap        = (WRAM_L_TOP - WRAM_L_BASE) - l_bss

print("PROPOSED SPLIT")
print(f"  WRAM-H pinned .bss : {h_bss:>8} B  ({', '.join(PIN_H)})")
print(f"  WRAM-H image+.bss  : {image_size + h_bss:>8} B  (base 0x{IMAGE_BASE:08x})")
print(f"  WRAM-H .bss start  : 0x{bss_h_start:08x}")
print(f"  WRAM-H .bss end    : 0x{bss_h_end:08x}   (stack floor 0x{h_floor:08x})")
print(f"  WRAM-H headroom    : {h_floor - bss_h_end:>8} B under stack floor")
print(f"  WRAM-L .bss        : {l_bss:>8} B")
print(f"  WRAM-L heap window : {heap:>8} B  (need >= {HEAP_FLOOR})")
print(f"  heap margin        : {heap - HEAP_FLOOR:>8} B")
ok = (bss_h_end <= h_floor) and (heap >= HEAP_FLOOR)
print("  VERDICT:", "FITS" if ok else "DOES NOT FIT")
