/* spc.h - resident compressed sprite-pack reader (Task #180 step 5).
 *
 * An 'SPC1' pack (built by tools/build_sprite_packs.py) bundles many
 * raw-DEFLATE-compressed SPR2 blobs into one file, loaded ONCE into resident
 * LWRAM at scene start. spc_inflate() locates a blob by name and puff-inflates
 * it RAM->RAM, so the per-frame sprite paths never touch the CD during
 * gameplay (the single CD head stays on the GHZ CD-DA music track).
 *
 * Format: see tools/build_sprite_packs.py docstring. All fields big-endian.
 */
#ifndef RSDK_SPC_H
#define RSDK_SPC_H

#include <stdint.h>
#include <stdbool.h>

/* True if `pack` points at a valid 'SPC1' header. */
bool spc_valid(const uint8_t *pack);

/* Inflate the blob named `name` (NUL-terminated stem, e.g. "RING" / "SONIC07")
 * from resident pack `pack` into `dst` (capacity `dst_cap` bytes). On success
 * returns true and, if out_raw_len != NULL, stores the decompressed length.
 * Returns false if the pack is invalid, the name is absent, the entry's
 * raw_len exceeds dst_cap, or the inflate fails. */
bool spc_inflate(const uint8_t *pack, const char *name,
                 uint8_t *dst, uint32_t dst_cap, uint32_t *out_raw_len);

#endif /* RSDK_SPC_H */
