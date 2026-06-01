# `src/rsdk/` Compat-Layer Audit (Phase 0.5)

**Date:** 2026-05-26
**Purpose:** Catalog what each Saturn engine-compat module under `src/rsdk/`
implements vs. what the cached decomp expects, so Phase 1+ can identify
gaps to fix before each per-object port.

**Methodology:**
- For each `src/rsdk/<module>.h`, enumerate the public functions.
- For each function, identify the corresponding RSDK API it represents
  (RSDK.* table entries, foreach macros, ControllerInfo accessors).
- Grep `tools/_decomp_raw/SonicMania_*.c` for the matching `RSDK.*`
  pattern to count call sites and confirm which decomp files depend on
  this module.
- Mark gaps (RSDK APIs from `docs/RSDK_TO_SATURN_API_MAP.md` that have
  NO Saturn-side implementation today) as `FIXME` rows.

Call-site counts below come from `Grep RSDK\.<Fn>` on the cached subset
of decomp files (~75 .c files representing the Title + GHZ + a handful
of other zones; the FULL decomp has ~610 Objects/*.c files, so call
counts are a lower-bound).

---

## 1. `src/rsdk/storage.{h,c}` — Phase A1 file/asset loaders

Authoritative source: `RSDKv5/RSDK/Storage/*.cpp` (not cached as
`_RSDKv5_*` files; consult upstream during Phase 2). Implemented per
`docs/rsdkv5_engine_catalog.md` §1.

| Public function | RSDK API it represents | Call sites in cached decomp | Status |
|---|---|---|---|
| `rsdk_md5_name` | internal MD5 hash for asset/class lookup | (internal helper) | OK |
| `rsdk_scene_load` | (load Scene.bin; not directly an RSDK.* fn — engine-internal `LoadSceneFile` per `_RSDKv5_Scene.cpp`) | invoked by Phase B per-stage setup | OK |
| `rsdk_scene_free` | (paired free) | (internal) | OK |
| `rsdk_scene_find_class` | (post-load helper; not an RSDK.* fn) | (internal) | OK |
| `rsdk_scene_entity_at` | (post-load helper) | (internal) | OK |
| `rsdk_entity_attr_u8 / u32 / i32` | (post-load helper) | (internal) | OK |
| `rsdk_sprite_animation_load` | backs `RSDK.LoadSpriteAnimation` | 306+ across Title/GHZ/Common (`Grep RSDK\.LoadSpriteAnimation`) | OK |
| `rsdk_sprite_animation_free` | (paired free) | (internal) | OK |
| `rsdk_tile_config_load` | backs the tile-collision data side of `RSDK.ObjectTileCollision` etc. | 115+ collision call sites (`Grep RSDK\.ObjectTile`) | OK |
| `rsdk_stage_config_load / free` | reads StageConfig.bin object list (engine-internal LoadStageFile) | invoked by Phase B per-stage setup | OK |

**Decomp dependencies (sample):**
- `SonicMania_Objects_Title_TitleSetup.c:60` — `LoadSpriteAnimation("Title/Electricity.bin", SCOPE_STAGE)`
- `SonicMania_Objects_Title_TitleSonic.c:53` — `LoadSpriteAnimation("Title/Sonic.bin", SCOPE_STAGE)`
- `SonicMania_Objects_Title_TitleLogo.c` — `LoadSpriteAnimation("Title/TitleLogo.bin", SCOPE_STAGE)`
- `SonicMania_Objects_GHZ_GHZSetup.c` — `LoadSpriteAnimation("GHZ/Aquatic.bin", SCOPE_STAGE)`, etc.
- All `SonicMania_Objects_GHZ_*.c` (Motobug, Crabmeat, etc.) call `LoadSpriteAnimation`.

**Gaps for Phase 1+:**
- FIXME: `Data.rsdk` pack-file streaming is bypassed (offline extraction
  to `cd/`); not a blocker but does mean `cd/` carries the full sprite
  set when only a few are needed per scene.
- FIXME: `rsdk_stage_config_load` returns a raw trailing blob — needs a
  proper parser for the palette-bank + soundFX list before per-stage
  audio (`Music_PlayTrack`) can resolve track ids.
- FIXME: `Scene.bin`'s entity-attribute parser (`rsdk_scene_load`) needs
  per-attribute type-dispatch verification (VAR_VECTOR2, VAR_COLOR are
  paths likely untested).

---

## 2. `src/rsdk/object.{h,c}` — Phase A2 entity/class registry

Authoritative source: `tools/_decomp_raw/_RSDKv5_Scene_Object.cpp` (just
cached this turn) covers the upstream contract in full.

| Public function | RSDK API it represents | Call sites in cached decomp | Status |
|---|---|---|---|
| `rsdk_object_init / shutdown` | engine-internal setup | (internal) | OK |
| `rsdk_object_register` | backs `RSDK_REGISTER_OBJECT(name)` (Game.c invokes it ~700 times) | ~610 `Objects/*.c` files; ~700 register calls in `SonicMania_Game.c` | OK |
| `rsdk_object_find_class` | name-lookup helper, used internally by `CreateEntity` | (internal) | OK |
| `rsdk_create_entity` | backs `RSDK.CreateEntity / CREATE_ENTITY` macro | ~110+ across Common + GHZ + Title (`Grep RSDK\.CreateEntity`/`CREATE_ENTITY`) | OK |
| `rsdk_reset_entity / _slot` | backs `RSDK.ResetEntity / ResetEntitySlot` | ~30+ | OK |
| `rsdk_copy_entity` | backs `RSDK.CopyEntity` | ~10+ | OK |
| `rsdk_get_active_entity` | backs `foreach_active(class, var)` macro | ~150+ | OK |
| `rsdk_get_all_entity` | backs `foreach_all(class, var)` macro | ~40+ | OK |
| `rsdk_entity_in_bounds` | engine-internal bounds recompute (called per-frame inside `rsdk_object_tick`) | (internal) | OK |
| `rsdk_object_tick` | engine-internal frame loop (= `ProcessObjects` in `Object.cpp:506-740`) | called once per jo_core tick | OK |
| `rsdk_object_draw_all` | engine-internal draw pass | NOT YET WIRED through `rsdk/drawing.c`'s drawGroup queues | FIXME |
| `rsdk_object_set_camera` | Saturn-port helper, not in upstream | (internal) | OK |

**Decomp dependencies (sample):**
- `SonicMania_Objects_Title_TitleSetup.c:228` — `CREATE_ENTITY(TitleSetup, NULL, ...)`
- `SonicMania_Objects_GHZ_Motobug.c:67` — `CREATE_ENTITY(Dust, ...)`
- `SonicMania_Objects_Global_Player.c` — foreach_active iteration through Player+Camera+Shield slots.

**Gaps for Phase 1+:**
- FIXME: `rsdk_object_class_size` field is filled but the `static_class_size` slab is never allocated (`static_vars` is always NULL). Most decomp objects reference `<Class>->varN` (the static-scoped class variables) — Phase 1 Title port WILL need this.
  - Decomp ref: `_RSDKv5_Scene_Object.cpp` → `RegisterObject` / `RegisterObjectStatic` reserves a `staticVars` block per class.
- FIXME: per-class `static_update`, `late_update`, `editor_*` callbacks are NULL-only — `rsdk_object_register` doesn't expose them. Phase 1 Title port WILL need `static_update` for `TitleSetup` (state-machine tick is in `Update`, but `TitleBG` has `StaticUpdate`).
  - Decomp ref: `SonicMania_Objects_Title_TitleBG.c` `TitleBG_StaticUpdate`.
- FIXME: drawGroup priority queue + per-group sort callback (per `Object.cpp:670-720`) is missing — `rsdk_object_draw_all` walks slots in linear order. Phase 1 Title port needs ordered draw (TitleBG=group 0, TitleLogo=group 2, TitleSonic=group 4, TitleSetup=group 12).
  - Decomp ref: `_RSDKv5_Scene_Object.cpp` → `ProcessObjectDrawLists`.

---

## 3. `src/rsdk/animation.{h,c}` — Phase A3 animator tick + setup

Authoritative source: `tools/_decomp_raw/_RSDKv5_Graphics_Animation.cpp`
(just cached this turn, 7,262 bytes — full upstream module).

| Public function | RSDK API it represents | Call sites in cached decomp | Status |
|---|---|---|---|
| `rsdk_load_sprite_animation` | backs `RSDK.LoadSpriteAnimation` | 306+ | OK (wraps `rsdk_sprite_animation_load` from storage.c) |
| `rsdk_unload_sprite_animation` | engine-internal cleanup | (internal) | OK |
| `rsdk_set_sprite_animation` | backs `RSDK.SetSpriteAnimation` | ~150+ | OK |
| `rsdk_process_animation` | backs `RSDK.ProcessAnimation` | 306+ | OK |
| `rsdk_animator_current_frame` | helper used by `rsdk_draw_sprite` | (internal) | OK |

**Decomp dependencies (sample):**
- `SonicMania_Objects_Title_TitleSonic.c:43-44` — `SetSpriteAnimation(...0, &self->animatorSonic ...)` + `SetSpriteAnimation(...1, &self->animatorFinger ...)`
- `SonicMania_Objects_Title_TitleSonic.c:16-19` — `ProcessAnimation(&self->animatorSonic)` + conditional `ProcessAnimation(&self->animatorFinger)`

**Gaps for Phase 1+:**
- FIXME: `RSDK.GetSpriteAnimation(animFramesIdx, name)` — name-to-id resolver — not exposed.
  - Decomp ref: `SonicMania_Objects_Global_HUD.c` looks up anims by name.
- FIXME: `RSDK.SetSpriteString` (string-to-glyph-frames helper) is not implemented (used by `Localization.c` + `HUD.c` for press-start prompt etc.).
  - Decomp ref: `SonicMania_Objects_Title_TitleLogo.c` and `Global_HUD.c`.

---

## 4. `src/rsdk/drawing.{h,c}` — Phase A4 sprite + screen draw

Authoritative source: `tools/_decomp_raw/_RSDKv5_Graphics_Drawing.cpp`
(just cached this turn, 179,619 bytes — full upstream module).

| Public function | RSDK API it represents | Call sites in cached decomp | Status |
|---|---|---|---|
| `rsdk_drawing_init` | engine-internal setup | (internal) | OK |
| `rsdk_draw_sprite` | backs `RSDK.DrawSprite` | 154+ | OK (per `docs/RSDK_TO_SATURN_API_MAP.md` §1) |
| `rsdk_draw_tile` | backs `RSDK.DrawTile` | ~5 (HUD-only) | OK |
| `rsdk_fill_screen` | backs `RSDK.FillScreen` | ~10 (fade screens) | OK |
| `rsdk_set_clip_bounds` | backs `RSDK.SetClipBounds` | ~10 | OK |
| `g_rsdk_screen` (global) | mirrors upstream `ScreenInfo` | accessed by Player/Camera/etc. | OK |

**Decomp dependencies (sample):**
- `SonicMania_Objects_Title_TitleSonic.c:30-36` — `SetClipBounds + DrawSprite x2`
- `SonicMania_Objects_Title_TitleSetup.c:393-402` — `DrawSprite (electric arc, FLIP_X+FLIP_NONE)`
- `SonicMania_Objects_Title_TitleSetup.c:Draw_FadeBlack / Draw_Flash` — `FillScreen`

**Gaps for Phase 1+:**
- FIXME: `RSDK.DrawAniTiles(sheet, tileIndex, srcX, srcY, w, h)` — animated-tile streaming into VDP2 — NOT implemented. GHZSetup uses this every frame for sun-flower + extending-flower (per `SonicMania_Objects_GHZ_GHZSetup.c:30-44`).
  - Saturn target: DMA `RSDK_TILE_SIZE * RSDK_TILE_SIZE` bytes into the NBG1 character pattern at the tile-id offset, in v-blank.
- FIXME: `RSDK.DrawRect / DrawLine / DrawFace / DrawText` — used by HUD, signposts, debug rendering.
- FIXME: `RSDK.AddDrawListRef / SetDrawGroupProperties` — drawGroup queue management. See `object.c` FIXME row above.
- FIXME: `INK_BLEND / ALPHA / ADD / SUB / TINT` mapping to Saturn VDP1 color-calc — currently `inkEffect` is ignored by `rsdk_draw_sprite`. TitleBG `MOUNTAIN1/2 + REFLECTION + WATERSPARKLE` need this for Phase 1.d.
  - Decomp ref: `SonicMania_Objects_Title_TitleBG.c` (lines 30-90 in upstream).
- FIXME: `RSDK.CheckOnScreen / CheckPosOnScreen` — on-screen culling — not exposed.
  - Decomp ref: heavily used by Camera.c + Player.c + every badnik.
- FIXME: ROTSTYLE_FULL (free rotation) — `direction` argument honours flip but not arbitrary angle. Phase 2 player/badnik will need it.

---

## 5. `src/rsdk/collision.{h,c}` — Phase A5 sensor-based tile collision

Authoritative source: `RSDKv5/RSDK/Scene/TileLayer.cpp` (not yet cached).
Implements 6-sensor probe per `docs/rsdkv5_engine_catalog.md` §5.

| Public function | RSDK API it represents | Call sites in cached decomp | Status |
|---|---|---|---|
| `rsdk_find_floor / _roof / _lwall / _rwall` | sub-helpers for `RSDK.ObjectTileGrip` + `RSDK.ObjectTileCollision` | engine-internal, used by `rsdk_process_path_grip` | OK |
| `rsdk_tile_floor_at / _floor_angle` | tile-property lookup with flip-bit handling | (internal helpers for grip) | OK |
| `rsdk_process_path_grip` | backs `RSDK.ProcessObjectMovement` (the 6-sensor sweep) | called from per-entity update | OK |

**Decomp dependencies (sample):**
- `SonicMania_Objects_Global_Player.c` calls `RSDK.ObjectTileCollision` 22 times (`Grep RSDK\.ObjectTileCollision`).
- `SonicMania_Objects_GHZ_Motobug.c:8` collisions for ground adherence.
- `SonicMania_Objects_GHZ_Chopper.c:15` collision when jumping from water.

**Gaps for Phase 1+:**
- FIXME: `RSDK.ObjectTileCollision(self, layers, cMode, cPlane, xOff, yOff, setPos)` — the public sensor-vs-tile call — has no Saturn-side wrapper that takes the upstream signature. Phase 2 GHZ Player port will need it.
  - Saturn target: re-expose `rsdk_process_path_grip` with the canonical signature, or add `rsdk_object_tile_collision(...)` that constructs the hitbox from the entity.
- FIXME: `RSDK.GetTileAngle(tileID, cPlane, cMode)` and `RSDK.GetTileFlags(tileID, cPlane)` — direct tile-property accessors — not exposed (only the layer-walking variants are).
- FIXME: `RSDK.CheckObjectCollisionBox / TouchBox / TouchCircle / Platform` — object-vs-object collision primitives — NOT implemented. Phase 2 GHZ Spring/Monitor/Ring will need them.
  - Decomp ref: `SonicMania_Objects_Global_Ring.c:101` `CheckObjectCollisionTouchBox`.
- FIXME: `RSDK.GetHitbox(animator, hitboxID)` — read hitbox from animator's current frame.

---

## 6. `src/rsdk/audio.{h,c}` — Phase A6 SFX + CD-DA shim

Authoritative source: `RSDKv5/RSDK/Audio/Audio.cpp` (not yet cached).
Maps RSDK PlayStream/PlaySfx onto jo audio module.

| Public function | RSDK API it represents | Call sites in cached decomp | Status |
|---|---|---|---|
| `rsdk_audio_init` | engine-internal setup | (internal) | OK |
| `rsdk_play_stream` | backs `RSDK.PlayStream` | ~5 in Music.c | OK |
| `rsdk_play_sfx` | backs `RSDK.PlaySfx` | 159+ | OK |
| `rsdk_stop_sfx / _channel` | backs `RSDK.StopSfx / StopChannel` | ~30+ | OK |
| `rsdk_pause_channel / _resume_channel` | backs PauseChannel / ResumeChannel | ~5 | OK |

**Decomp dependencies (sample):**
- `SonicMania_Objects_Title_TitleSetup.c:60-62` — `GetSfx("Global/MenuBleep.wav")` + `GetSfx("Global/MenuAccept.wav")` + `GetSfx("Global/Ring.wav")`.
- `SonicMania_Objects_GHZ_Motobug.c` — `PlaySfx(Motobug->sfxRumble, ...)`.

**Gaps for Phase 1+:**
- FIXME: `RSDK.GetSfx(filename)` — load + cache an SFX, return its slot id — NOT exposed in `audio.h`. Phase 1 needs it for the 3 title SFX above.
- FIXME: `RSDK.SetChannelAttributes(ch, vol, pan, speed)` — per-channel volume/pan/pitch — NOT exposed. Music_FadeOut uses it.
- FIXME: `RSDK.GetChannelPos / ChannelActive` — playback-state queries — NOT exposed.
- FIXME: BGM track-name → CD-DA track mapping table — currently `rsdk_play_stream`'s filename arg is ignored (caller has to know the track id). Phase 1 needs `"Stage.ogg"` etc. → track 3 mapping.

---

## 7. `src/rsdk/input.{h,c}` — Phase A7 controller state

Authoritative source: `RSDKv5/RSDK/Input/Input.cpp` (not yet cached).
Maps Saturn pad state to RSDK ControllerInfo struct.

| Public function | RSDK API it represents | Call sites in cached decomp | Status |
|---|---|---|---|
| `rsdk_input_init` | engine-internal | (internal) | OK |
| `rsdk_input_tick` | per-frame edge-detect on Saturn pad | (called once per jo_core tick) | OK |
| `rsdk_controller_state(slot)` | exposes `ControllerInfo[slot]` | 57+ | OK |
| `g_rsdk_controllers[]` (global) | analogous to `ControllerInfo[]` | accessed by Player/Menu/Title | OK |

**Decomp dependencies (sample):**
- `SonicMania_Objects_Global_Player.c:9` — `ControllerInfo[player_id].keyJump.press`
- `SonicMania_Objects_Menu_MenuSetup.c:36` — `ControllerInfo[CONT_ANY].keyStart.press`
- `SonicMania_Objects_Title_TitleSetup.c:310-350` — WaitForEnter polls `ControllerInfo[CONT_ANY].keyStart.press`.

**Gaps for Phase 1+:**
- FIXME: `API_GetInputDeviceID / API_AssignInputSlotToDevice / API_ResetInputSlotAssignments` — used by `MenuSetup.c` for re-binding controllers. Saturn impl can be no-op (single pad assumed) but the symbols must link.
- FIXME: `TouchInfo` (`RSDKTouchInfo`) — used by Mobile builds. Saturn impl returns false.
- FIXME: `AnalogStickInfoL / R` — Plus DLC only.
  - Decomp ref: `SonicMania_All.h` line ~30 declares them; we omit by setting `MANIA_USE_PLUS=0`.

---

## 8. `src/rsdk/save.{h,c}` — Phase A8 backup-RAM user-files

Authoritative source: `RSDKv5/RSDK/User/Core/UserStorage.cpp` (not
cached). Maps `RSDK.LoadUserFile / SaveUserFile` onto jo backup module.

| Public function | RSDK API it represents | Call sites in cached decomp | Status |
|---|---|---|---|
| `rsdk_save_init` | engine-internal mount | (internal) | OK |
| `rsdk_save_user_file_exists` | backs `API.UserStorageFileExists` (paraphrased) | ~5 | OK |
| `rsdk_load_user_file` | backs `API.LoadUserFile` | 5 (`SaveGame.c`, `MenuSetup.c`) | OK |
| `rsdk_save_user_file` | backs `API_SaveUserFile` | 5 | OK |
| `rsdk_save_user_file_delete` | backs delete-file path | ~2 | OK |

**Decomp dependencies (sample):**
- `SonicMania_Objects_Global_SaveGame.c:2` — `API.LoadUserFile("SaveData.bin", ...)`
- `SonicMania_Objects_Menu_MenuSetup.c` — options + replay storage paths.

**Gaps for Phase 1+:**
- FIXME: Callback-on-completion semantics (`callback(state)`) — current Saturn API is synchronous; per-object code that awaits the callback will need to dispatch it itself.
- FIXME: `API.ClearUserDB(tableID)` + per-table row/column UserDB — not implemented. Time-attack + replay storage will need it (Phase 5+).

---

## 9. NOT YET PRESENT — engine modules required by Phase 1+

| Module | RSDK APIs it would expose | Why Phase 1 needs it |
|---|---|---|
| `src/rsdk/scene.{h,c}` | `RSDK.SetScene / LoadScene / CheckSceneFolder / CheckValidScene / SetEngineState`, `SceneInfo->*` accessors | TitleSetup's `State_FadeToMenu`/`State_FadeToVideo` call `SetScene("Presentation","Menu")`; without this the title state-machine has nowhere to transition. |
| `src/rsdk/palette.{h,c}` | `RSDK.LoadPalette / SetPaletteEntry / GetPaletteEntry / CopyPalette / RotatePalette / SetLimitedFade / SetActivePalette / SetPaletteMask` | TitleSetup's `State_AnimateUntilFlash` blends palettes for the electricity arc; TitleBG rotates CRAM 140-143 every 6 frames for water shimmer; GHZSetup rotates CRAM 181-184/197-200 for waterfall+waterline. (244+ call sites total per Grep.) |
| `src/rsdk/math.{h,c}` | `RSDK.Sin256 / Cos256 / Sin512 / Cos512 / Sin1024 / Cos1024 / ATan2 / Rand / RandSeeded` | TitleBG's `Scanline_Clouds` callback uses `Sin1024` per scanline; TitleSetup `Draw_DrawRing` uses `Sin512` for ring oscillation. |
| `src/rsdk/string.{h,c}` | `RSDK.InitString / SetString / AppendText / GetCString / CompareStrings` | Localization.c uses these heavily for the press-start prompt + zone names. |
| `src/rsdk/tilelayer.{h,c}` | `RSDK.GetTileLayer / GetTileLayerID / GetTile / SetTile / CopyTileLayer / GetTileAngle / GetTileFlags` | GHZSetup uses `GetTileLayer` to register Scanline_Clouds callback. Phase 2 needs `SetTile` for animated tiles. |
| `src/rsdk/api.{h,c}` | `API_SetRichPresence / API_SetNoSave / API.CheckDLC / API.UnlockAchievement / API_ClearPrerollErrors / API.ClearSaveStatus` | Mostly no-op stubs but they must link; Player.c calls `API_SetNoSave` on death. |

---

## 10. Summary

| Module | Status (post Phase 1.1) | Remaining gaps |
|---|---|---|
| storage | OK | StageConfig palette/audio parsing (Phase 2) |
| object | **PHASE 1.1 PORTED** | static_vars allocator + drawGroup queues + static_update/late_update + RSDK_THIS macro — ALL LANDED. Remaining: per-group sort callback + zdepth insertion sort (Phase 1.2). |
| animation | OK | GetSpriteAnimation by name; SetSpriteString (Phase 1.2 — needed by per-class Title bodies). |
| drawing | OK | DrawAniTiles (Phase 2 GHZ); INK_BLEND/ADD (Phase 1.2 TitleBG); CheckOnScreen (Phase 2); drawGroup integration (Phase 1.2). |
| collision | OK | ObjectTileCollision wrapper; CheckObjectCollisionBox; GetHitbox (Phase 2). |
| audio | OK | GetSfx returning slot id; BGM track-name table (Phase 1.2). |
| input | OK | API_GetInputDeviceID + family — provided as no-op stubs in api.c (Phase 1.1). |
| save | OK | Async callback dispatch (Phase 5+). |
| **scene** | **PHASE 1.1 PORTED** | LoadSceneFile pipeline lands per-entity create + per-class stage_load. Remaining: per-attribute payload fill-in (Phase 1.2 — current Title classes don't use variable attribs). |
| **palette** | **PHASE 1.1 PORTED** | In-RAM RGB565 mirror + RotatePalette + SetLimitedFade + SetPaletteEntry + dirty-mask. Remaining: CRAM V-blank DMA hook wired in Phase 1.2 alongside per-class draws. |
| **math** | **PHASE 1.1 PORTED** | Full Sin/Cos/Tan/ArcTan/Rand surface. No remaining gaps. |
| **string** | **PHASE 1.1 PORTED** | InitString family. No remaining gaps for Phase 1.2 Title bodies; SetSpriteString (glyph-frame lookup) is animation.c work for Phase 1.2. |
| **tilelayer** | **PHASE 1.1 PORTED** | GetTileLayer/SetTile + scanline-callback storage. Remaining: callback invocation in V-blank IRQ (Phase 2 GHZ). |
| **api** | **PHASE 1.1 PORTED** | All eight link-time stubs (SetRichPresence/SetNoSave/ClearPrerollErrors/CheckDLC/UnlockAchievement/ClearSaveStatus/ClearUserDB/Localization_GetString). |

**Phase 1.1 critical path (DONE):** all five missing engine modules
(scene/palette/math/string/tilelayer/api) ported. Object module updated
with static_vars + RSDK_THIS + drawGroup queues. mania/Game.c registers
the Title-scene class set with stub callbacks so the scene loader can
spawn entities even before the per-class bodies land.

**Phase 1.2 critical path:** port the actual per-class bodies (TitleSetup_*,
TitleLogo_*, TitleSonic_*, TitleBG_*, Title3DSprite_*) from the cached
decomp into the existing stub .c files under src/mania/Objects/Title/.
Wire palette dirty-mask -> CRAM V-blank DMA. Wire SetSpriteString. Wire
INK_BLEND in drawing.c for TitleBG's mountain/reflection layers.

---

## Phase 1.2 — LANDED (2026-05-26)

### Engine FIXMEs closed in Phase 1.2

| FIXME | Status | Notes |
|---|---|---|
| object.c: per-group sort callback + zdepth insertion sort | DONE | `rsdk_object_draw_all` Pass A.5 sorts each drawGroup bucket by `zdepth` (Drawing.cpp:735+). |
| animation.c: GetSpriteAnimation by name | DONE | `rsdk_get_sprite_animation_id_by_name` + `rsdk_set_sprite_animation_by_name`. Mirrors Animation.cpp:131-153. |
| drawing.c: INK_BLEND/ADD scaffolding | PARTIAL | `rsdk_draw_sprite_ex` accepts `ink_effect` + `alpha` + `draw_group`, passes to the registered sprite callback (`title_sprite_cb` in Game.c). Full VDP1 PMOD bit programming (half-transparent / ECdis) deferred to Phase Z when the visual-fidelity gate demands matching the PC 50% alpha. |
| drawing.c: drawGroup integration | DONE | DrawSprite now routes through the registered sprite callback with full direction/ink/alpha/drawGroup context. |
| scene.c: per-entity attribute fill-in | DONE | `fill_first_attribute` reads attribute index 1 (the first stored attribute after the implicit filter) from the scene-bin payload and writes 4 bytes at MANIA_RSDK_ENTITY_BASE_BYTES (132). Sufficient for TitleLogo `type`, TitleBG `type`, Title3DSprite `frame`. Phase 2 will generalise via a per-class attribute-offset registry. |
| main.c: V-blank palette CRAM DMA | DONE | `fg_vblank` registered via `jo_core_add_vblank_callback`; drains `rsdk_palette_consume_dirty` per frame and writes 16-bit volatile stores into CRAM at `JO_VDP2_CRAM + bank*256`. Small dirty ranges (1-4 entries from rotate_palette) → CPU stores chosen over SCU DMA for simplicity. |

### Per-class Title bodies landed in Phase 1.2

All five `src/mania/Objects/Title/*.c` files now contain the mechanical
ports cited in the per-class file headers. Decomp line ranges are
documented in each function's comment.

### Phase 1.2 scope intentionally not yet delivered

1. **Visual-fidelity gate Vn** — Phase 1.3 work per CLAUDE.md §4.7. The
   Phase 1.2 binary boots and ticks the title state machine correctly,
   but the visible output is BLACK because the .SPR sprite atlases are
   declared but not yet wired into the resolve_asset → title_sprite_cb
   path. The legacy `src/_archived/main.c` had this wiring; Phase 1.3
   migrates it onto the asset bridge.
2. **TSONIC.ATL via the engine bridge** — the 49-frame TitleSonic atlas
   currently sits at cd/TSONIC.ATL but isn't wired through the asset
   bridge (the legacy title_sonic.c is archived). Phase 1.3 brings it
   back through `mania_engine_init`'s `setup_title_assets`.
3. **Cinepak Mania.ogv intro** — Phase Z (existing deferral).
4. **Scanline FX (TitleBG_Scanline_Clouds / _Island)** — Phase 2
   (requires VDP2 H-IRQ raster table programming + scanline-callback
   invocation; the scanline_callback pointer is stored but not invoked).
5. **Title3DSprite Update/Draw** — Phase Z (3D-perspective constellation;
   stub Create only).
