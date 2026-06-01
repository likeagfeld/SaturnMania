/* Phase 1.1 — String module (Saturn implementation).
 *
 * Port reference (NOT reproduced verbatim — re-implemented per the API
 * contract in the header):
 *   tools/_decomp_raw/_RSDKv5_Text.hpp:150-172   InitString
 *   tools/_decomp_raw/_RSDKv5_Text.hpp:176-208   CopyString / GetCString
 *   tools/_decomp_raw/_RSDKv5_Text.cpp           SetString family
 *
 * Allocation strategy: a simple intrusive linked list of every chars[]
 * pointer we hand out, drained en masse at scene-exit. This mimics the
 * upstream DATASET_STR scope without requiring a real bump allocator. */

#include "string.h"

#include <jo/jo.h>      /* jo_malloc, jo_free */
#include <string.h>     /* memcpy, memset      */

/* Tracker for every chars buffer we own. The buffer holds a back-pointer
 * pair (next, capacity) so we can free in bulk on dataset reset. We use
 * 8 bytes of header before the uint16 chars[] data. */
typedef struct alloc_hdr_s {
    struct alloc_hdr_s *next;
    uint32_t            cap_bytes;
} alloc_hdr_t;

static alloc_hdr_t *s_alloc_head = NULL;

void rsdk_string_init(void)
{
    s_alloc_head = NULL;
}

void rsdk_string_reset_dataset(void)
{
    alloc_hdr_t *h = s_alloc_head;
    while (h) {
        alloc_hdr_t *next = h->next;
        jo_free(h);
        h = next;
    }
    s_alloc_head = NULL;
}

/* Allocate a uint16 buffer for `count` chars + bookkeeping header. */
static uint16_t *str_alloc(uint32_t count)
{
    if (count == 0) count = 1;
    uint32_t bytes = sizeof(alloc_hdr_t) + count * sizeof(uint16_t);
    alloc_hdr_t *h = (alloc_hdr_t *)jo_malloc(bytes);
    if (!h) return NULL;
    h->next = s_alloc_head;
    h->cap_bytes = bytes;
    s_alloc_head = h;
    return (uint16_t *)(h + 1);
}

static uint32_t c_strlen(const char *s)
{
    uint32_t n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

void rsdk_init_string(rsdk_string_t *s, const char *text, uint32_t text_length)
{
    if (!s) return;
    s->length = 0;
    s->size   = 0;
    s->chars  = NULL;
    if (!text) return;

    uint32_t len = c_strlen(text);
    s->length = (uint16_t)len;
    if (text_length != 0 && text_length >= len)
        s->size = (uint16_t)text_length;
    else
        s->size = (uint16_t)len;
    if (s->size == 0) s->size = 1;

    s->chars = str_alloc(s->size);
    if (!s->chars) { s->length = 0; s->size = 0; return; }
    for (uint32_t i = 0; i < len; ++i) s->chars[i] = (uint16_t)(uint8_t)text[i];
}

void rsdk_set_string(rsdk_string_t *s, const char *text)
{
    if (!s) return;
    uint32_t len = c_strlen(text);
    if (s->size < len || !s->chars) {
        s->size  = (uint16_t)(len ? len : 1);
        s->chars = str_alloc(s->size);
        if (!s->chars) { s->length = 0; s->size = 0; return; }
    }
    for (uint32_t i = 0; i < len; ++i) s->chars[i] = (uint16_t)(uint8_t)text[i];
    s->length = (uint16_t)len;
}

void rsdk_append_text(rsdk_string_t *s, const char *appendix)
{
    if (!s || !appendix) return;
    uint32_t add = c_strlen(appendix);
    uint32_t need = (uint32_t)s->length + add;
    if (need > s->size || !s->chars) {
        uint16_t *nbuf = str_alloc(need ? need : 1);
        if (!nbuf) return;
        for (uint16_t i = 0; i < s->length; ++i) nbuf[i] = s->chars[i];
        s->chars = nbuf;
        s->size  = (uint16_t)need;
    }
    for (uint32_t i = 0; i < add; ++i)
        s->chars[s->length + i] = (uint16_t)(uint8_t)appendix[i];
    s->length = (uint16_t)need;
}

void rsdk_append_string(rsdk_string_t *dst, const rsdk_string_t *src)
{
    if (!dst || !src || !src->chars) return;
    uint32_t need = (uint32_t)dst->length + src->length;
    if (need > dst->size || !dst->chars) {
        uint16_t *nbuf = str_alloc(need ? need : 1);
        if (!nbuf) return;
        for (uint16_t i = 0; i < dst->length; ++i) nbuf[i] = dst->chars[i];
        dst->chars = nbuf;
        dst->size  = (uint16_t)need;
    }
    for (uint16_t i = 0; i < src->length; ++i)
        dst->chars[dst->length + i] = src->chars[i];
    dst->length = (uint16_t)need;
}

void rsdk_copy_string(rsdk_string_t *dst, const rsdk_string_t *src)
{
    if (!dst || !src) return;
    if (dst == src) return;
    if (dst->size < src->length || !dst->chars) {
        dst->size  = src->length ? src->length : 1;
        dst->chars = str_alloc(dst->size);
        if (!dst->chars) { dst->length = 0; dst->size = 0; return; }
    }
    for (uint16_t i = 0; i < src->length; ++i) dst->chars[i] = src->chars[i];
    dst->length = src->length;
}

void rsdk_get_cstring(char *out, const rsdk_string_t *src)
{
    if (!out || !src || !src->chars) {
        if (out) *out = 0;
        return;
    }
    for (uint16_t i = 0; i < src->length; ++i)
        out[i] = (char)(src->chars[i] & 0xFF);
    out[src->length] = 0;
}

bool rsdk_compare_strings(const rsdk_string_t *a, const rsdk_string_t *b,
                          bool exact_match)
{
    if (!a || !b) return false;
    if (a->length != b->length) return false;
    for (uint16_t i = 0; i < a->length; ++i) {
        uint16_t ca = a->chars[i];
        uint16_t cb = b->chars[i];
        if (exact_match) {
            if (ca != cb) return false;
        } else {
            /* Case-insensitive ASCII (mirrors Text.hpp StrComp:52-72). */
            if (ca == cb) continue;
            uint16_t la = (ca >= 'A' && ca <= 'Z') ? (uint16_t)(ca + 32) : ca;
            uint16_t lb = (cb >= 'A' && cb <= 'Z') ? (uint16_t)(cb + 32) : cb;
            if (la != lb) return false;
        }
    }
    return true;
}
