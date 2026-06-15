#ifndef RSDK_ENTITY_ATLAS_H
#define RSDK_ENTITY_ATLAS_H

/* Phase 2.4e v2 (Task #144) -- SPR2 + MET entity atlas loader.
 *
 * Canonical pattern for every per-entity asset port from Phase 2.4 onward.
 * Replaces the per-class legacy SPR1 loader (single canvas-per-atlas,
 * uniform tick-per-frame) with a per-frame variable-size + decomp-cadence
 * walker.
 *
 * Authoritative sources cited:
 *   - rsdkv5-src/RSDKv5/RSDK/Graphics/Animation.cpp:150-177
 *     ProcessAnimation -- the per-frame duration walker the Saturn
 *     atlas tick mirrors via rsdk_process_animation (animation.c:96-118).
 *   - docs/anim_completeness_audit.md "SPR2 format" + "MET sidecar format"
 *     -- the on-disk byte layout this loader consumes.
 *   - memory/entity-atlas-must-ship-all-frames.md
 *     -- binding rule that every animator MUST consume per-frame durations.
 *
 * Saturn-side budget:
 *   - One entity_atlas_t per atlas (BSS resident, ~3 KB max).
 *   - Per-frame jo_sprite_id allocated at load time (one slot per kept
 *     atlas frame). MAX_ATLAS_FRAMES caps the per-entity slot draw so
 *     a single bug atlas can't exhaust JO_MAX_SPRITE (255).
 *   - rsdk_animator_t (storage.h:109-118) -- already the shape decomp
 *     ProcessAnimation expects; we wrap it inside entity_atlas_t and
 *     drive it via rsdk_set_sprite_animation + rsdk_process_animation.
 *
 * Memory layout used by tools/qa_phase2_4e_anim_completeness_gate.py P4:
 *   +0x00  ready (u8)
 *   +0x01  anim_count (u8)
 *   +0x02  frame_total (u16)
 *   +0x04  current_anim (u16)
 *   +0x06  current_atlas_frame (u16)  <-- P4 peek target
 *   +0x08  rsdk_animator_t (frames* +0x00, frame_id +0x04, ...)
 *   ...
 *
 * The qa_phase2_4e_anim_completeness_gate.py P4 peeks current_atlas_frame
 * at offset +0x06 from two savestates 30 ticks apart and asserts it
 * advances. */

#include "animation.h"
#include "storage.h"

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ENTITY_ATLAS_MAX_ANIMS    8
/* Phase 2.4c.2 (Task #147) -- shrunk from 72 to 36. BSS-overflow finding:
 * the 10-atlas build at MAX_FRAMES=72 pushed _end to 0x060CAF00 (44 KB
 * past the 0x060C0000 SGL work-area boundary documented in Makefile L41).
 * Largest active atlas across the 10-entity set is SpikeLog at 32 frames;
 * 36 gives 4 headroom while halving each atlas's BSS footprint (~3.4 KB
 * each vs ~6.7 KB). Per-atlas footprint reduced 33 KB across the 10
 * tracked atlases; new _end ~ 0x060BAxxx (10+ KB margin to SGL work area).
 *
 * Phase 2.4j.1 (Task #156) -- bumped 34 -> 40 to seat TitleCard's "Name
 * Letters" font anim (27 frames A-Z + space) plus the Decorations/Zone
 * Letters/Act Numbers anims (9 frames) = 36 flat frames in one atlas.
 * Also added the uint16_t unicode_char[] field below (needed by the text
 * trio SetSpriteString/GetStringWidth/DrawString to map string chars ->
 * frame index via the per-frame .MET unicode record). Post-build _end
 * re-measured from game.map (clean build 2026-05-29): 0x060B16A0 --
 * ~58 KB (0xE960) margin below the 0x060C0000 SGL work-area floor. */
#define ENTITY_ATLAS_MAX_FRAMES   40

typedef struct entity_atlas_s {
    /* P4-peek-accessible header (DO NOT REORDER). */
    uint8_t  ready;                    /* +0x00 */
    uint8_t  anim_count;               /* +0x01 */
    uint16_t frame_total;              /* +0x02 */
    uint16_t current_anim;             /* +0x04 */
    uint16_t current_atlas_frame;      /* +0x06 */

    /* Decomp animator (ProcessAnimation drives this). */
    rsdk_animator_t animator;

    /* Per-anim cadence + per-frame metadata mirroring the MET sidecar. */
    struct {
        uint16_t frame_count;
        uint16_t speed;
        uint16_t loop_index;
        uint16_t first;                /* index into the atlas-flat list */
    } anims[ENTITY_ATLAS_MAX_ANIMS];

    /* Per-frame metadata (atlas-flat, indexed by atlas_frame_id). */
    int16_t  pivot_x[ENTITY_ATLAS_MAX_FRAMES];
    int16_t  pivot_y[ENTITY_ATLAS_MAX_FRAMES];
    uint16_t duration[ENTITY_ATLAS_MAX_FRAMES];
    uint16_t width[ENTITY_ATLAS_MAX_FRAMES];
    uint16_t height[ENTITY_ATLAS_MAX_FRAMES];
    int16_t  sprite_id[ENTITY_ATLAS_MAX_FRAMES];

    /* Per-frame glyph codepoint (from the .MET unicode_char record, Phase
     * 2.4j.1). 0 for non-font anims. The text trio (rsdk_set_sprite_string
     * / rsdk_get_string_width / rsdk_draw_text in animation.c) matches each
     * string char against this to resolve glyph -> atlas frame index,
     * mirroring decomp Animation.cpp SetSpriteString (211-231). */
    uint16_t unicode_char[ENTITY_ATLAS_MAX_FRAMES];

    /* Storage for the rsdk_animator_t->frames pointer (we synthesize a
     * single rsdk_sprite_frame_t for the current_atlas_frame each tick
     * so ProcessAnimation walks correctly). */
    rsdk_sprite_frame_t scratch_frames[ENTITY_ATLAS_MAX_FRAMES];

    /* FR-2 (lazy residency) -- the byte offset of each frame's
     * (u16 w, u16 h, w*h*2 pixels) record within the atlas's SP2 blob.
     * Computed once at load; used by the on-demand uploader to address a
     * single frame's pixels in the MRU blob pool without re-walking. */
    uint32_t frame_off[ENTITY_ATLAS_MAX_FRAMES];

    /* SP2 basename (no extension), so the residency manager can CD-stream
     * the atlas's pixel blob on demand. */
    char base_name[16];
} entity_atlas_t;

/* === Public API ====================================================== */

/* Load a SP2+MET atlas pair from CD. `base_name` is the file basename
 * WITHOUT extension; the loader probes <base_name>.SP2 + <base_name>.MET
 * (jo_fs_read_file conventions). Pixel data is uploaded via jo_sprite_add
 * (VDP1 VRAM) and freed after upload. Returns true on success.
 *
 * Soft-fail: if SP2 or MET is missing, atlas->ready stays false and the
 * tick/draw helpers no-op. Mirrors the existing per-class soft-fail
 * pattern in Entities.c. */
bool entity_atlas_load(entity_atlas_t *atlas, const char *base_name);

/* Phase 2.4j.2 (Task #157) -- scratch-buffer variant. When `scratch` is
 * non-NULL, the SP2 then MET file is read into the caller-owned buffer via
 * jo_fs_read_file_ptr (fs.c:282 takes the buffer directly, bypassing
 * jo_malloc). Lets a large atlas (TitleCard's 50720-byte SP2) load even
 * when jo's 256 KB pool is exhausted mid-GHZ -- the entity-atlas analogue
 * of the FG.TMP/SKY.DAT LWRAM bypass (memory/ghz-sky-dat-lwram-bypass.md).
 * `scratch_cap` must be >= file size + 1. The buffer is caller-owned
 * (LWRAM-resident) and is never jo_free'd. entity_atlas_load() above is the
 * thin pool-path wrapper (scratch=NULL). */
bool entity_atlas_load_ex(entity_atlas_t *atlas, const char *base_name,
                          void *scratch, int scratch_cap);

/* Switch the atlas to play the given anim. Resets frame to 0 + timer.
 * If anim_id is out of range, no-op. Mirrors the decomp
 * SetSpriteAnimation contract (Animation.cpp:131-153) but operates on
 * the atlas-flat frame indexing so callers don't need to thread
 * rsdk_animator_t through. */
void entity_atlas_play(entity_atlas_t *atlas, int anim_id);

/* Advance the animator by one game tick. Internally calls
 * rsdk_process_animation (animation.c:96-118) which consumes per-frame
 * durations from the scratch_frames array. Updates current_atlas_frame
 * so the P4 gate can peek it. */
void entity_atlas_tick(entity_atlas_t *atlas);

/* Return the jo sprite_id for the currently active frame (suitable for
 * jo_sprite_draw3D). Returns -1 if the atlas isn't ready. */
int  entity_atlas_current_sprite(const entity_atlas_t *atlas);

/* Return the pivot offset (signed pixels) for the current frame. Used
 * by the per-class draw to convert RSDK world-space + pivot to Saturn
 * screen coords. Outputs default to 0 if atlas isn't ready. */
void entity_atlas_pivot(const entity_atlas_t *atlas, int *out_px, int *out_py);

/* Return the (width, height) of the current frame in pixels. */
void entity_atlas_size(const entity_atlas_t *atlas, int *out_w, int *out_h);

/* === Phase 2.4j.1 by-atlas-frame accessors (text trio support) =======
 * Address an arbitrary frame by its atlas-flat index. Used by the text
 * trio (animation.c) to draw glyph strings independent of the animator
 * state. */

/* First atlas-flat index of `anim_id` (-1 if invalid). */
int  entity_atlas_first_of_anim(const entity_atlas_t *atlas, int anim_id);

/* SetSpriteString glyph lookup: return the atlas-flat frame index whose
 * unicode_char == uc within `anim_id`, or -1 if not found. */
int  entity_atlas_frame_for_unicode(const entity_atlas_t *atlas, int anim_id,
                                    uint16_t uc);

/* jo sprite_id / pivot / size for a specific atlas-flat frame index. */
int  entity_atlas_sprite_at(const entity_atlas_t *atlas, int idx);

/* === FR-2 lazy entity residency (per-tick rebuild) ===================
 *
 * The eager loader uploaded EVERY frame of EVERY atlas to VDP1 at scene
 * load, pushing __jo_sprite_id to 407 -- past the fixed JO_MAX_SPRITE=255
 * [255] def/pic arrays (sprites.c has no release-build bounds check), which
 * silently clobbered g_titlecard + the HUD glyph VRAM addresses (#189).
 *
 * Lazy residency uploads ONLY the frames actually displayed on the current
 * tick, into a single dynamic block sitting ABOVE the player's resident
 * block (which is itself above the static scene sprites). Each tick the
 * draw pass rewinds the jo sprite stack to `dyn_base` (freeing last frame's
 * entity/HUD/titlecard sprites, LIFO) and clears every atlas's per-frame
 * sprite_id[]; the accessors (entity_atlas_current_sprite / _sprite_at)
 * then upload-on-demand with per-tick dedup. The pixel source is a 192 KB
 * LWRAM MRU pool of whole-atlas SP2 blobs streamed from CD on first need.
 *
 * Mirrors the FR-1 player_atlas MRU pattern (player_atlas.h). */

/* Flush the MRU blob pool + clear all residency. Call at the top of
 * entities_load_assets (scene (re)load) so no stale blob survives a
 * transition (the pool overlaps the per-scene file-read scratch window). */
void entity_residency_reset(void);

/* Begin a fresh draw frame. Rewinds the jo sprite stack to `dyn_base`
 * (jo_sprite_free_from -> frees every entity/HUD/titlecard sprite uploaded
 * last frame) and clears every atlas's resident sprite_id[] so the
 * accessors re-upload on demand this tick. `dyn_base` MUST be the sprite id
 * just above the player's resident block (player_atlas_top()). A negative
 * dyn_base no-ops the rewind (residency not yet armed). */
void entity_residency_begin_frame(int dyn_base);
void entity_atlas_pivot_at(const entity_atlas_t *atlas, int idx,
                           int *out_px, int *out_py);
void entity_atlas_size_at(const entity_atlas_t *atlas, int idx,
                          int *out_w, int *out_h);

/* === Per-entity BSS globals (used by qa_phase2_4e gate P4) ========= */
extern entity_atlas_t g_ring_atlas;
extern entity_atlas_t g_itembox_atlas;
extern entity_atlas_t g_spring_atlas;
extern entity_atlas_t g_signpost_atlas;
extern entity_atlas_t g_spikes_atlas;
extern entity_atlas_t g_motobug_atlas;
extern entity_atlas_t g_buzz_atlas;
/* Phase 2.4c.2 Task #147 — Platform / SpikeLog / Newtron atlases. */
extern entity_atlas_t g_spikelog_atlas;
extern entity_atlas_t g_platform_atlas;
extern entity_atlas_t g_newtron_atlas;
/* Phase 2.4h — GHZ Act 1 badnik atlases (Chopper/Crabmeat/Batbrain). */
extern entity_atlas_t g_chopper_atlas;
extern entity_atlas_t g_crabmeat_atlas;
extern entity_atlas_t g_batbrain_atlas;
/* Phase 2.4i Task #154 — authentic HUD atlas (replaces fabricated
 * DIGITS.SPR). cd/HUD.SP2 + cd/HUD.MET built from
 * extracted/Data/Sprites/Global/HUD.bin. 3 anims kept: anim 0 HUD
 * Elements (17 frames), anim 1 Numbers (10 = digits 0-9), anim 2 Life
 * Icons (3 = Sonic/Tails/Knuckles). See HUD draw in Entities.c. */
extern entity_atlas_t g_hud_atlas;
/* Phase 2.4-PLAT Task #155 — Bridge planks atlas (cd/BRIDGE.SP2 +
 * cd/BRIDGE.MET built from extracted/Data/Sprites/GHZ/Bridge.bin). */
extern entity_atlas_t g_bridge_atlas;
/* Phase 2.4j.1 Task #156 — TitleCard act-intro atlas (cd/TITLECARD.SP2 +
 * cd/TITLECARD.MET built from extracted/Data/Sprites/Global/TitleCard.bin).
 * 4 anims / 36 frames: Decorations(2), Name Letters(27, per-frame unicode
 * A-Z+space), Zone Letters(4), Act Numbers(3). */
extern entity_atlas_t g_titlecard_atlas;

#ifdef __cplusplus
}
#endif

#endif /* RSDK_ENTITY_ATLAS_H */
