/* Phase A3 — Animation system, Saturn port of RSDKv5/RSDK/Graphics/Animation.
 *
 * See src/rsdk/animation.h header for §section references and design
 * rationale. The struct definitions live in src/rsdk/storage.h (because
 * A1 Storage owns the on-disk parser); this file owns the tick logic
 * and the animator state machine. */

#include "animation.h"
#include "entity_atlas.h"
#include "string.h"      /* rsdk_string_t */

#include <jo/jo.h>       /* jo_sprite_draw3D */
#include <string.h>      /* memset */

rsdk_sprite_animation_t g_rsdk_sprite_anims[RSDK_SPRITEANIM_LIST_COUNT];

/* Find the first empty slot in the global animation list. Returns -1
 * if the list is full. */
static int _alloc_slot(void)
{
    for (int i = 0; i < RSDK_SPRITEANIM_LIST_COUNT; ++i) {
        if (g_rsdk_sprite_anims[i].anim_count == 0 &&
            g_rsdk_sprite_anims[i].frames == NULL) {
            return i;
        }
    }
    return -1;
}

int rsdk_load_sprite_animation(const char *filename, int slot)
{
    if (slot < 0) slot = _alloc_slot();
    if (slot < 0 || slot >= RSDK_SPRITEANIM_LIST_COUNT) return -1;
    /* Free any pre-existing slot contents to keep slot semantics
     * idempotent. */
    if (g_rsdk_sprite_anims[slot].frames || g_rsdk_sprite_anims[slot].animations) {
        rsdk_sprite_animation_free(&g_rsdk_sprite_anims[slot]);
    }
    if (!rsdk_sprite_animation_load(filename, &g_rsdk_sprite_anims[slot])) {
        return -1;
    }
    return slot;
}

void rsdk_unload_sprite_animation(int slot)
{
    if (slot < 0 || slot >= RSDK_SPRITEANIM_LIST_COUNT) return;
    rsdk_sprite_animation_free(&g_rsdk_sprite_anims[slot]);
    memset(&g_rsdk_sprite_anims[slot], 0, sizeof(rsdk_sprite_animation_t));
}

/* §4.2 SetSpriteAnimation — initialise animator state for a chosen anim.
 *
 * The early-out check matches Animation.cpp ≈L131-138: if the animator
 * is already playing this animation AND force_apply is false, do NOT
 * reset the timer or frame -- that's the idempotency callers depend on
 * for "advance Sonic's walk anim regardless of state-machine churn". */
void rsdk_set_sprite_animation(int list_id, int anim_id,
                               rsdk_animator_t *animator,
                               bool force_apply,
                               int frame_id)
{
    if (!animator) return;
    if (list_id < 0 || list_id >= RSDK_SPRITEANIM_LIST_COUNT) return;
    rsdk_sprite_animation_t *spr = &g_rsdk_sprite_anims[list_id];
    if (anim_id < 0 || anim_id >= spr->anim_count) return;

    /* Phase 1.23 GAP A — propagate list_id through to the draw callback.
     * Always written (even on the early-out idempotency path below) so a
     * re-bind to the same anim still refreshes a stale list_id. */
    animator->list_id = (uint16_t)list_id;

    if (animator->animation_id == anim_id && !force_apply) return;

    rsdk_sprite_animation_entry_t *anim = &spr->animations[anim_id];
    animator->prev_animation_id = animator->animation_id;
    animator->animation_id      = (int16_t)anim_id;
    animator->frames            = anim->frames;
    animator->frame_count       = (int16_t)anim->frame_count;
    animator->speed             = (int16_t)anim->speed;
    animator->loop_index        = anim->loop_index;
    animator->rotation_style    = anim->rotation_style;
    if (frame_id < 0)                       frame_id = 0;
    if (frame_id >= animator->frame_count)  frame_id = animator->frame_count - 1;
    animator->frame_id          = frame_id;
    animator->timer             = 0;
    animator->frame_duration    = animator->frames
                                ? (int16_t)animator->frames[frame_id].duration
                                : 0;
}

/* §4.1 ProcessAnimation — advance the animator timer + frame index.
 *
 * The loop is `while timer >= frame_duration` (not `if`) because the
 * timer can accumulate multi-frame increments when `speed > frame_duration`
 * -- e.g. a 2x-speed Sonic-walk anim with speed=0x200 and per-frame
 * duration=0x100 advances ONE frame per call but the timer still
 * accumulates the residual correctly across ticks. */
void rsdk_process_animation(rsdk_animator_t *animator)
{
    if (!animator || !animator->frames || animator->frame_count == 0) return;
    animator->timer += animator->speed;
    while (animator->timer >= animator->frame_duration &&
           animator->frame_duration > 0) {
        animator->timer -= animator->frame_duration;
        animator->frame_id++;
        if (animator->frame_id >= animator->frame_count) {
            animator->frame_id = (int32_t)animator->loop_index;
            /* One-shot animations set loop_index == frame_count which
             * pins frame_id to frame_count, then the >= check freezes
             * it on the LAST frame indefinitely (matches §4.1 note).
             * Saturn-side: clamp to last real frame so callers reading
             * `frames[frame_id]` don't index past the array. */
            if (animator->frame_id >= animator->frame_count)
                animator->frame_id = animator->frame_count - 1;
        }
        animator->frame_duration = (int16_t)animator->frames[animator->frame_id].duration;
        /* Guard against zero-duration frames (would infinite-loop). */
        if (animator->frame_duration == 0) break;
    }
}

const rsdk_sprite_frame_t *rsdk_animator_current_frame(const rsdk_animator_t *animator)
{
    if (!animator || !animator->frames) return NULL;
    if (animator->frame_id < 0 || animator->frame_id >= animator->frame_count) return NULL;
    return &animator->frames[animator->frame_id];
}

/* Phase 1.2 — by-name lookups.
 *
 * §4.2 of upstream Animation.cpp (lines 131-153) shows GetSpriteAnimation
 * resolves by MD5 hash of the name; the per-anim struct stores hash already
 * (see storage.c::rsdk_sprite_animation_load which fills A->hash from the
 * anim's parsed name). We walk the table and memcmp the 16-byte digest. */
int rsdk_get_sprite_animation_id_by_name(int list_id, const char *name)
{
    if (!name) return -1;
    if (list_id < 0 || list_id >= RSDK_SPRITEANIM_LIST_COUNT) return -1;
    rsdk_sprite_animation_t *spr = &g_rsdk_sprite_anims[list_id];
    if (!spr->animations || spr->anim_count == 0) return -1;
    uint32_t target[4];
    rsdk_md5_name(name, target);
    for (int i = 0; i < spr->anim_count; ++i) {
        if (memcmp(spr->animations[i].hash, target, 16) == 0) return i;
    }
    return -1;
}

void rsdk_set_sprite_animation_by_name(int list_id, const char *name,
                                       rsdk_animator_t *animator,
                                       bool force_apply,
                                       int frame_id)
{
    int aid = rsdk_get_sprite_animation_id_by_name(list_id, name);
    if (aid < 0) return;
    rsdk_set_sprite_animation(list_id, aid, animator, force_apply, frame_id);
}

/* === Phase 2.4j.1 — text-rendering trio ==============================
 *
 * Saturn-fit port of the decomp glyph-string path. Where the decomp
 * indexes spr->frames[anim->frameListOffset + f], we index the
 * entity_atlas_t's atlas-flat arrays via the per-frame accessors. The
 * `f` ("anim-local frame index") -> atlas-flat conversion is simply
 * (atlas->anims[anim_id].first + f), exposed by entity_atlas_first_of_anim. */

/* §SetSpriteString — Animation.cpp:211-231.
 *
 * For every char in `string`, treat the current chars[c] value as a
 * unicode codepoint, then replace it with the ANIM-LOCAL frame index of
 * the glyph whose unicode_char matches, or (uint16)-1 if none. We use
 * entity_atlas_frame_for_unicode (returns the ATLAS-FLAT index) and then
 * subtract `first` to obtain the anim-local index the decomp stores. */
void rsdk_set_sprite_string(const struct entity_atlas_s *atlas, int anim_id,
                            rsdk_string_t *string)
{
    const entity_atlas_t *a = (const entity_atlas_t *)atlas;
    if (!a || !string || !string->chars) return;
    if (anim_id < 0 || anim_id >= a->anim_count) return;
    int first = entity_atlas_first_of_anim(a, anim_id);
    if (first < 0) return;

    for (int c = 0; c < string->length; ++c) {
        uint16_t uc = string->chars[c];
        int flat = entity_atlas_frame_for_unicode(a, anim_id, uc);
        string->chars[c] = (flat < 0) ? (uint16_t)-1
                                      : (uint16_t)(flat - first);
    }
}

/* §GetStringWidth — Animation.cpp:179-209.
 *
 * Sums per-glyph widths + `spacing` between glyphs, over chars
 * [start_index, length). Each chars[c] is the resolved anim-local frame
 * index (set by rsdk_set_sprite_string). */
int rsdk_get_string_width(const struct entity_atlas_s *atlas, int anim_id,
                          const rsdk_string_t *string,
                          int start_index, int length, int spacing)
{
    const entity_atlas_t *a = (const entity_atlas_t *)atlas;
    if (!a || !string || !string->chars) return 0;
    if (anim_id < 0 || anim_id >= a->anim_count) return 0;
    int first = entity_atlas_first_of_anim(a, anim_id);
    if (first < 0) return 0;
    int frame_count = a->anims[anim_id].frame_count;

    if (start_index < 0) start_index = 0;
    if (start_index > string->length - 1) start_index = string->length - 1;
    if (length <= 0 || length > string->length) length = string->length;

    int w = 0;
    for (int c = start_index; c < length; ++c) {
        int charFrame = (int)(int16_t)string->chars[c];
        if (charFrame >= 0 && charFrame < frame_count) {
            int gw, gh;
            entity_atlas_size_at(a, first + charFrame, &gw, &gh);
            w += gw;
            if (c + 1 >= length) return w;
            w += spacing;
        }
    }
    return w;
}

/* Phase 2.4j.1 (Task #156) — settable glyph draw depth. Default 140 keeps
 * glyphs in front of the badnik plane (Bridge uses 150). TitleCard sets a
 * larger value so the zone-name + ZONE letters sort BEHIND the act number
 * but IN FRONT of the colored strips, reproducing the decomp's
 * submission-order painter. SMALLER z = nearer/front (jo convention). */
static int s_text_z = 140;
void rsdk_set_text_depth(int z) { s_text_z = z; }

/* §DrawString — Drawing.cpp:4312-4391.
 *
 * Saturn-fit: only the alignment cases TitleCard exercises are wired
 * (ALIGN_LEFT + ALIGN_CENTER; ALIGN_RIGHT is a no-op upstream too). The
 * decomp positions sprites by TOP-LEFT corner via DrawSpriteFlipped; the
 * Saturn jo_sprite_draw3D draws CENTERED on (sx-160, sy-112), so we add
 * half the glyph w/h to convert corner -> center. Depth defaults to 140
 * (in front of the badnik plane; Bridge uses 150) but is settable via
 * rsdk_set_text_depth for TitleCard layering. */
void rsdk_draw_text(const struct entity_atlas_s *atlas, int anim_id,
                    int32_t pos_x_fixed, int32_t pos_y_fixed,
                    const rsdk_string_t *string,
                    int start_frame, int end_frame, int align, int spacing,
                    const int16_t *char_off_x, const int16_t *char_off_y,
                    int screen_relative)
{
    const entity_atlas_t *a = (const entity_atlas_t *)atlas;
    if (!a || !a->ready || !string || !string->chars) return;
    if (anim_id < 0 || anim_id >= a->anim_count) return;
    int first = entity_atlas_first_of_anim(a, anim_id);
    if (first < 0) return;
    int frame_count = a->anims[anim_id].frame_count;

    /* FROM_FIXED: 16.16 -> integer pixels. */
    int x = pos_x_fixed >> 16;
    int y = pos_y_fixed >> 16;
    /* screen_relative != 0 -> coords already screen-space (TitleCard uses
     * screenRelative=true). If a caller ever passes world-space we'd need
     * currentScreen->position here; TitleCard never does. */
    (void)screen_relative;

    if (start_frame < 0) start_frame = 0;
    if (start_frame > string->length - 1) start_frame = string->length - 1;
    if (end_frame <= 0 || end_frame > string->length) end_frame = string->length;

    if (align == RSDK_ALIGN_LEFT) {
        for (; start_frame < end_frame; ++start_frame) {
            int curChar = (int)(int16_t)string->chars[start_frame];
            if (curChar >= 0 && curChar < frame_count) {
                int idx = first + curChar;
                int sid = entity_atlas_sprite_at(a, idx);
                int gw, gh, px, py;
                entity_atlas_size_at(a, idx, &gw, &gh);
                entity_atlas_pivot_at(a, idx, &px, &py);
                int ox = char_off_x ? char_off_x[start_frame] : 0;
                int oy = char_off_y ? char_off_y[start_frame] : 0;
                if (sid >= 0) {
                    /* top-left -> center: + gw/2, + gh/2. pivotY added per
                     * decomp DrawSpriteFlipped(y + frame->pivotY, ...). */
                    int cx = x + ox + gw / 2;
                    int cy = y + py + oy + gh / 2;
                    jo_sprite_draw3D(sid, cx - 160, cy - 112, s_text_z);
                }
                x += spacing + gw;
            }
        }
    }
    else if (align == RSDK_ALIGN_CENTER) {
        /* Walk backward from end_frame-1 (decomp --endFrame). */
        for (int ef = end_frame - 1; ef >= start_frame; --ef) {
            int curChar = (int)(int16_t)string->chars[ef];
            if (curChar >= 0 && curChar < frame_count) {
                int idx = first + curChar;
                int sid = entity_atlas_sprite_at(a, idx);
                int gw, gh, px, py;
                entity_atlas_size_at(a, idx, &gw, &gh);
                entity_atlas_pivot_at(a, idx, &px, &py);
                int ox = char_off_x ? char_off_x[ef] : 0;
                int oy = char_off_y ? char_off_y[ef] : 0;
                if (sid >= 0) {
                    /* decomp: DrawSpriteFlipped(x - gw + offX, y + pivotY + offY).
                     * Convert that top-left to center: + gw/2, + gh/2. */
                    int cx = (x - gw + ox) + gw / 2;
                    int cy = (y + py + oy) + gh / 2;
                    jo_sprite_draw3D(sid, cx - 160, cy - 112, s_text_z);
                }
                x = (x - gw) - spacing;
            }
        }
    }
    /* RSDK_ALIGN_RIGHT: upstream leaves it empty (Drawing.cpp:4360). */
}
