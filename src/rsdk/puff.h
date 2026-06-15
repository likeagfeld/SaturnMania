/* puff.h
 * Copyright (C) 2002-2013 Mark Adler, all rights reserved
 * version 2.3, 21 Jan 2013
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the author be held liable for any damages arising from the
 * use of this software. See the notice in puff.c for the full zlib license.
 *
 * Vendored verbatim (interface only) into the Sonic-Mania-Saturn tree for
 * #180 step 4c: a minimal, self-contained raw-DEFLATE (RFC 1951) inflate so
 * the RAM-resident compressed collision layout (cd/GHZ1COL.BIN 'GCO3') can be
 * decoded column-block by column-block RAM->RAM, with ZERO CD access during
 * gameplay. The Saturn build links no zlib (storage.h:13,779-801), so this is
 * the inflater. We feed it raw DEFLATE blocks (zlib wbits=-15, no header/adler)
 * exactly as tools/build_collayout.py emits them.
 */
#ifndef RSDK_PUFF_H
#define RSDK_PUFF_H

/* Decompress a raw-DEFLATE stream.
 *   dest      : output buffer; if NULL, only computes *destlen (dry run)
 *   destlen   : in = available output bytes; out = bytes written
 *   source    : input raw-DEFLATE bytes
 *   sourcelen : in = available input bytes; out = bytes consumed
 * Returns 0 on success; negative = invalid/incomplete stream; positive =
 * available input/output exhausted. */
int puff(unsigned char *dest, unsigned long *destlen,
         const unsigned char *source, unsigned long *sourcelen);

#endif /* RSDK_PUFF_H */
