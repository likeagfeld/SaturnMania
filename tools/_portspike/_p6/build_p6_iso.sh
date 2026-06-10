#!/usr/bin/env bash
# =============================================================================
# build_p6_iso.sh -- minimal P6.1 engine-dispatch ISO (Task #205). RED/GREEN.
#
#   bash tools/_portspike/_p6/build_p6_iso.sh red      # 0_red.bin -> _p6_red.iso
#   bash tools/_portspike/_p6/build_p6_iso.sh green     # 0.bin     -> _p6.iso  (default)
#
# Same mkisofs recipe as build_p5_iso.sh (jo_engine_makefile:310-312): the
# COMMON IP.BIN (-generic-boot) loads/execs at 0x06004000 (the p6.linker .text
# base); the staged payload is always named "0.bin" so the Saturn first-read
# mechanism loads it there and jumps. The matching cue is MODE1/2048 (the IP
# "SEGA SEGASATURN" header sits at offset 0).
# =============================================================================
set -u

MODE="${1:-green}"
# P6.2 (Task #206) adds io-red/io-green: same recipe, but the staged tree also
# carries cd/P6IO.BIN (the file RSDK::LoadFile opens on disc) and the binary is
# the io-link output (0_iored.bin / 0_io.bin from link_p6.sh io-red / io-green).
IO=0
case "$MODE" in
  red)      SRCBIN=0_red.bin;   ISONAME=_p6_red.iso        ;;
  green)    SRCBIN=0.bin;       ISONAME=_p6.iso            ;;
  io-red)   SRCBIN=0_iored.bin; ISONAME=_p6_iored.iso; IO=1 ;;
  io-green) SRCBIN=0_io.bin;    ISONAME=_p6_io.iso;    IO=1 ;;
  io-jo)    SRCBIN=0_iojo.bin;  ISONAME=_p6_iojo.iso;  IO=1 ;;
  *) echo "usage: $0 [red|green|io-red|io-green|io-jo]"; exit 2 ;;
esac

P6=/work/tools/_portspike/_p6
IP=/work/jo-engine/Compiler/COMMON/IP.BIN
STAGE=$P6/_stage_$MODE
ISO=$P6/$ISONAME

if [ ! -f "$P6/$SRCBIN" ]; then
  echo "FAIL: $P6/$SRCBIN missing -- run link_p6.sh $MODE first."; exit 1
fi

rm -rf "$STAGE"
mkdir -p "$STAGE"
cp "$P6/$SRCBIN"   "$STAGE/0.bin"
cp /work/cd/ABS.TXT "$STAGE/ABS.TXT"
cp /work/cd/CPY.TXT "$STAGE/CPY.TXT"
cp /work/cd/BIB.TXT "$STAGE/BIB.TXT"
# P6.2: stage the file LoadFile("P6IO.BIN") opens (8 chars < GFS_FNAME_LEN=12).
[ "$IO" = 1 ] && cp /work/cd/P6IO.BIN "$STAGE/P6IO.BIN"

echo "[stage:$MODE] $(ls -la "$STAGE" | grep -E '0.bin|TXT' | awk '{print $5, $9}' | tr '\n' ' ')"

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
echo "[iso:$MODE] $ISO = $SZ bytes ($((SZ/2048)) sectors of 2048)"
echo "[ip ] offset0=$(dd if="$ISO" bs=1 skip=0 count=15 2>/dev/null) loadaddr=$(dd if="$ISO" bs=1 skip=240 count=4 2>/dev/null | od -An -tx1 | tr -d ' ')"
echo "DONE [$MODE]."
