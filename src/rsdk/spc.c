/* spc.c - resident compressed sprite-pack reader (Task #180 step 5).
 * See spc.h + tools/build_sprite_packs.py for the 'SPC1' container format. */

#include "spc.h"
#include "puff.h"

#define SPC_HEADER     8
#define SPC_INDEX_REC  32
#define SPC_NAME_LEN   16

static inline uint16_t _rd_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}
static inline uint32_t _rd_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

/* Exact match of a NUL-terminated query against a fixed 16-byte NUL-padded
 * field: every byte up to and including the query terminator must agree, and
 * the field byte at the terminator position must be NUL. */
static bool _name_eq(const uint8_t *field, const char *name)
{
    int i = 0;
    for (; i < SPC_NAME_LEN; ++i) {
        char c = name[i];
        if ((uint8_t)c != field[i]) return false;
        if (c == '\0') return true;   /* both NUL here -> equal */
    }
    /* query filled all 16 bytes without a terminator -> only equal if the
     * field is also unterminated and identical (already checked above). */
    return name[SPC_NAME_LEN] == '\0';
}

bool spc_valid(const uint8_t *pack)
{
    return pack && pack[0] == 'S' && pack[1] == 'P' &&
           pack[2] == 'C' && pack[3] == '1';
}

bool spc_inflate(const uint8_t *pack, const char *name,
                 uint8_t *dst, uint32_t dst_cap, uint32_t *out_raw_len)
{
    if (!spc_valid(pack) || !name || !dst) return false;

    uint16_t count = _rd_u16(pack + 4);
    const uint8_t *idx = pack + SPC_HEADER;
    for (uint16_t i = 0; i < count; ++i, idx += SPC_INDEX_REC) {
        if (!_name_eq(idx, name)) continue;

        uint32_t comp_off = _rd_u32(idx + SPC_NAME_LEN);
        uint32_t comp_len = _rd_u32(idx + SPC_NAME_LEN + 4);
        uint32_t raw_len  = _rd_u32(idx + SPC_NAME_LEN + 8);
        if (raw_len > dst_cap) return false;

        unsigned long destlen = raw_len;
        unsigned long srclen  = comp_len;
        int rc = puff(dst, &destlen, pack + comp_off, &srclen);
        if (rc != 0 || destlen != raw_len) return false;

        if (out_raw_len) *out_raw_len = raw_len;
        return true;
    }
    return false;   /* name not present */
}
