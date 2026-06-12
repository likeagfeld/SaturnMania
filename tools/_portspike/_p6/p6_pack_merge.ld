/* p6_pack_merge.ld -- P6.7 W12b root-cause fix (Task #227, 2026-06-12).
 *
 * Second `ld -r` pass over the gc'd pack: MERGE every -fdata-sections /
 * -ffunction-sections named section into the four standard sections. The
 * final jo link (COFF-SH output, COMMON sgl.linker -- do-not-modify tree)
 * treats named .bss.* / .data.* inputs as ORPHAN OUTPUT SECTIONS and
 * assigns them addresses INSIDE the main .bss/.data spans (MEASURED:
 * 179 overlapping pairs, qa_p6_mapoverlap.py; p6_typeGroupsBacking
 * overlapped __jo_fs_work -> typeGroups[126].entryCount=0 zeroed the
 * GfsMng GFCF_Seek pointer -> the W12b GFS_Seek wild-jump crash class).
 * After this merge the pack presents exactly one .text/.rodata/.data/.bss
 * input each, which the COMMON script places disjointly.
 *
 * gc already ran in pass 1 (--gc-sections + -u roots), so collapsing the
 * per-symbol sections here loses nothing.
 */
SECTIONS
{
    .text 0 : { *(.text .text.*) }
    .rodata 0 : { *(.rodata .rodata.* .rdata) }
    .data 0 : { *(.data .data.*) }
    .bss 0 : { *(.bss .bss.*) *(COMMON) }
}
