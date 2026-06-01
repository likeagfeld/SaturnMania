#ifndef RSDK_STRING_H
#define RSDK_STRING_H

/* Phase 1.1 — String module, Saturn port of RSDKv5/RSDK/Storage/Text.
 *
 * Source contracts (read but not reproduced):
 *   tools/_decomp_raw/_RSDKv5_Text.hpp:20-24    String struct
 *   tools/_decomp_raw/_RSDKv5_Text.hpp:150-172  InitString
 *   tools/_decomp_raw/_RSDKv5_Text.hpp:176-208  CopyString, GetCString
 *   tools/_decomp_raw/_RSDKv5_Text.cpp          SetString, AppendText,
 *                                               AppendString, CompareStrings
 *
 * The decomp String is a UTF-16 buffer (uint16 *chars + length + size in
 * uint16 units). Used heavily by:
 *   * SonicMania_Objects_Title_TitleLogo.c — press-start localized text
 *   * Localization (HUD score labels, zone names, menu prompts)
 *   * SetSpriteString (glyph-frame lookup for text rendering)
 *
 * Saturn port:
 *   * Storage uses jo_malloc / jo_free. The decomp's AllocateStorage
 *     (DATASET_STR scope) is a per-scene reset bucket; on Saturn we just
 *     leak the allocation until rsdk_string_reset_dataset() is called
 *     at scene-exit.
 *   * Phase 1.1 ships only the string-data API. SetSpriteString (which
 *     builds a per-glyph frame array) lives in src/rsdk/animation.c
 *     when the title-card / HUD ports land. */

#include <stdint.h>
#include <stdbool.h>

/* String struct, identical layout to _RSDKv5_Text.hpp:20-24. */
typedef struct {
    uint16_t *chars;
    uint16_t  length;
    uint16_t  size;
} rsdk_string_t;

/* === Public API ===================================================== */

void rsdk_string_init(void);

/* Reset the DATASET_STR allocator at scene-exit. Frees every chars
 * buffer allocated via the public API. */
void rsdk_string_reset_dataset(void);

/* InitString: allocate `size`-character buffer; copy `text` (NUL-term'd
 * C string, may be NULL) into chars[]. Mirrors Text.hpp:150-172. */
void rsdk_init_string(rsdk_string_t *s, const char *text, uint32_t text_length);

/* SetString: replace contents from a C string (allocates if needed). */
void rsdk_set_string(rsdk_string_t *s, const char *text);

/* AppendText: concatenate a C-string onto an existing String. */
void rsdk_append_text(rsdk_string_t *s, const char *appendix);

/* AppendString: concatenate another String. */
void rsdk_append_string(rsdk_string_t *dst, const rsdk_string_t *src);

/* CopyString: clone src->chars[] into dst (reallocating if dst->size
 * isn't large enough). */
void rsdk_copy_string(rsdk_string_t *dst, const rsdk_string_t *src);

/* GetCString: copy out as a NUL-terminated C string. `out` must hold
 * at least src->length+1 bytes. */
void rsdk_get_cstring(char *out, const rsdk_string_t *src);

/* CompareStrings: equal-content test. `exact_match` true == case sensitive. */
bool rsdk_compare_strings(const rsdk_string_t *a, const rsdk_string_t *b,
                          bool exact_match);

#endif /* RSDK_STRING_H */
