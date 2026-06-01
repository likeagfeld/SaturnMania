#ifndef RSDK_ANIMATION_H
#define RSDK_ANIMATION_H

/* Phase A3 — Animation system, Saturn port of RSDKv5/RSDK/Graphics/Animation.
 *
 * Per docs/rsdkv5_engine_catalog.md §4 + BIBLE.md Phase A row A3.
 *
 * Authoritative source (cite by §section when touching this code):
 *   §4.1 ProcessAnimation tick (Animation.cpp:111-129)
 *   §4.2 SetSpriteAnimation     (Animation.cpp:131-153)
 *   §4.3 SetSpriteString        (Animation.cpp:155-168)
 *
 * The rsdk_sprite_animation_t / rsdk_sprite_frame_t / rsdk_animator_t
 * structs are already defined in src/rsdk/storage.h (because the
 * file-parser lives in A1 Storage). This module owns the per-tick
 * ANIMATION LOGIC + animator-state-machine API. */

#include "storage.h"
#include "string.h"     /* rsdk_string_t (text trio) */

/* Saturn-port limit. Upstream is SPRITEANIM_COUNT = 0x40; we keep the
 * same value but allocate lazily as animations are loaded. */
#define RSDK_SPRITEANIM_LIST_COUNT 0x40

/* The global sprite-animation list (analogue of upstream
 * `spriteAnimationList[]`). Each slot is either empty (anim_count == 0)
 * or points to a fully-loaded sprite animation. Allocate via
 * rsdk_load_sprite_animation; entries are cleared via
 * rsdk_unload_sprite_animation. */
extern rsdk_sprite_animation_t g_rsdk_sprite_anims[RSDK_SPRITEANIM_LIST_COUNT];

/* === Public API ===================================================== */

/* Load a sprite-animation file into the given slot. Returns the slot
 * index on success, -1 on failure. If `slot` < 0 the function picks the
 * first empty slot. Mirrors upstream LoadSpriteAnimation contract. */
int  rsdk_load_sprite_animation(const char *filename, int slot);
void rsdk_unload_sprite_animation(int slot);

/* SetSpriteAnimation per §4.2 — initialises the animator to play
 * animation `anim_id` from sprite-list `list_id`. If
 * `animator->animation_id == anim_id` and `!force_apply`, returns
 * immediately (idempotent, no timer reset). */
void rsdk_set_sprite_animation(int list_id, int anim_id,
                               rsdk_animator_t *animator,
                               bool force_apply,
                               int frame_id);

/* ProcessAnimation per §4.1 — advance the animator's frame counter.
 * Call once per game tick from the entity's update/late-update. */
void rsdk_process_animation(rsdk_animator_t *animator);

/* Convenience: lookup a frame's pivot+size for sprite-draw routines.
 * Returns NULL if the animator/frame is invalid. */
const rsdk_sprite_frame_t *rsdk_animator_current_frame(const rsdk_animator_t *animator);

/* Phase 1.2 — by-name animation lookups. Mirrors upstream:
 *   _RSDKv5_Graphics_Animation.cpp:131-153  GetSpriteAnimation by name
 *   _RSDKv5_Graphics_Animation.cpp          SetSpriteAnimation by name
 *
 * GetSpriteAnimationIDByName: returns the anim_id within the list, or -1
 *   if the name is not found.
 * SetSpriteAnimationByName: convenience — resolves the name then invokes
 *   the regular rsdk_set_sprite_animation. */
int  rsdk_get_sprite_animation_id_by_name(int list_id, const char *name);
void rsdk_set_sprite_animation_by_name(int list_id, const char *name,
                                       rsdk_animator_t *animator,
                                       bool force_apply,
                                       int frame_id);

/* === Phase 2.4j.1 — text-rendering trio (glyph-string draw) ==========
 *
 * Saturn-fit port of the decomp text path. The decomp routes through the
 * global spriteAnimationList[aniFrames] + animID + frameListOffset:
 *   GetStringWidth   Animation.cpp:179-209
 *   SetSpriteString  Animation.cpp:211-231
 *   DrawString       Drawing.cpp:4312-4391
 *
 * On Saturn the glyph font lives in an entity_atlas_t (one anim per font
 * face). We therefore parameterise by (atlas, anim_id) instead of
 * (aniFrames, animID), and use the per-frame entity_atlas accessors
 * (entity_atlas_frame_for_unicode / _sprite_at / _size_at / _pivot_at)
 * in place of spr->frames[anim->frameListOffset + f].
 *
 * String char convention mirrors the decomp exactly: after
 * rsdk_set_sprite_string, string->chars[c] holds the ANIM-LOCAL frame
 * index (0..frame_count-1) of the glyph, or (uint16)-1 if no glyph
 * matched. GetStringWidth / DrawText then index by that anim-local value.
 *
 * The opaque type is forward-declared so callers don't have to pull in
 * entity_atlas.h just for the prototype. The full definition (tag
 * `struct entity_atlas_s`, typedef `entity_atlas_t`) lives in
 * entity_atlas.h. */
struct entity_atlas_s;

/* Text alignment values (mirror RSDK ALIGN_* from Drawing.hpp). */
#define RSDK_ALIGN_LEFT   0
#define RSDK_ALIGN_RIGHT  1
#define RSDK_ALIGN_CENTER 2

/* SetSpriteString — resolve each char of `string` to the anim-local glyph
 * frame index (writes back into string->chars[]). Animation.cpp:211-231. */
void rsdk_set_sprite_string(const struct entity_atlas_s *atlas, int anim_id,
                            rsdk_string_t *string);

/* GetStringWidth — total pixel width of chars [start_index, length) using
 * the resolved (anim-local) glyph indices + `spacing`. Animation.cpp:179-209. */
int  rsdk_get_string_width(const struct entity_atlas_s *atlas, int anim_id,
                           const rsdk_string_t *string,
                           int start_index, int length, int spacing);

/* DrawString — draw glyphs. `pos_x_fixed`/`pos_y_fixed` are 16.16 fixed
 * world coords (or screen coords when screen_relative != 0). `char_off_x` /
 * `char_off_y` are optional per-glyph pixel-offset arrays (NULL = none),
 * used by TitleCard's per-letter drop animation. Drawing.cpp:4312-4391. */
void rsdk_draw_text(const struct entity_atlas_s *atlas, int anim_id,
                    int32_t pos_x_fixed, int32_t pos_y_fixed,
                    const rsdk_string_t *string,
                    int start_frame, int end_frame, int align, int spacing,
                    const int16_t *char_off_x, const int16_t *char_off_y,
                    int screen_relative);

/* Phase 2.4j.1 (Task #156) — set the SGL Z depth used by subsequent
 * rsdk_draw_text glyph draws. Default 140; SMALLER z = nearer/front
 * (jo convention). Used by TitleCard to layer the zone-name text between
 * the colored strips (behind) and the act number (front). */
void rsdk_set_text_depth(int z);

#endif /* RSDK_ANIMATION_H */
