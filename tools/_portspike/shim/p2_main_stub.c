/* ===========================================================================
 * p2_main_stub.c -- P2 link-closure ONLY (engine true-port, #201).
 *
 * The real Saturn link gate (qa_p2_saturn_link.sh) links -nostartfiles (no
 * crt0), yet the linked newlib/core object set still references `main` -- the
 * libc init/exit path (__libc_init_array / exit) carries the reference even
 * without a startup file. The P2 gate named it: `main  OTHER`.
 *
 * Provide a trivial definition so symbol closure reaches 0. This stub does
 * NOTHING and is NOT the engine entry point. P3 (#202) REPLACES it with the
 * real entry that constructs RetroEngine and drives it to ENGINESTATE_LOAD;
 * at that point this file is dropped from the link.
 * =========================================================================== */
int main(void) { return 0; }
