#!/usr/bin/env bash
# Build the minimal P3 boot-proof ISO (Task #202). Stages ONLY the P3 first-read
# payload 0.bin + the three IP-referenced PVD text files (ABS/CPY/BIB), then runs
# the SAME mkisofs recipe the jo build uses (jo_engine_makefile:310-312):
# -generic-boot IP.BIN embeds the jo COMMON IP.BIN (load/exec 0x06004000, the
# p3.linker .text base) at LBA 0; 0.bin sorts first in the root dir (ASCII '0' <
# letters) so the Saturn first-read mechanism loads it to 0x06004000 and jumps.
# Emits _p3.iso (a 2048-byte/sector image despite -sectype 2352 -- the IP header
# "SEGA SEGASATURN" sits at offset 0, so the matching cue is MODE1/2048).
set -u

P3=/work/tools/_portspike/_p3
IP=/work/jo-engine/Compiler/COMMON/IP.BIN
STAGE=$P3/_stage
ISO=$P3/_p3.iso

rm -rf "$STAGE"
mkdir -p "$STAGE"
cp "$P3/0.bin"     "$STAGE/0.bin"
cp /work/cd/ABS.TXT "$STAGE/ABS.TXT"
cp /work/cd/CPY.TXT "$STAGE/CPY.TXT"
cp /work/cd/BIB.TXT "$STAGE/BIB.TXT"

echo "[stage] $(ls -la "$STAGE" | grep -E '0.bin|TXT' | awk '{print $5, $9}' | tr '\n' ' ')"

mkisofs -quiet \
  -sysid "SEGA SATURN" -volid "SaturnApp" -volset "SaturnApp" \
  -sectype 2352 \
  -publisher "SEGA ENTERPRISES, LTD." -preparer "SEGA ENTERPRISES, LTD." \
  -appid "SaturnApp" \
  -abstract "ABS.TXT" -copyright "CPY.TXT" -biblio "BIB.TXT" \
  -generic-boot "$IP" \
  -full-iso9660-filenames \
  -o "$ISO" "$STAGE"

if [ ! -f "$ISO" ]; then echo "FAIL: mkisofs produced no ISO"; exit 1; fi
SZ=$(wc -c < "$ISO")
echo "[iso] $ISO = $SZ bytes ($((SZ/2048)) sectors of 2048)"

# Boot-witness: IP header magic at offset 0 and load addr at 0xF0.
echo "[ip ] offset0=$(dd if="$ISO" bs=1 skip=0 count=15 2>/dev/null) loadaddr=$(dd if="$ISO" bs=1 skip=240 count=4 2>/dev/null | xxd -p)"
echo "DONE."
