# RSDKv5 → Saturn API Translation Map

Authoritative Rosetta stone for porting Sonic Mania Decompilation game-object
code to Sega Saturn. The decomp source files (cached in `tools/_decomp_raw/`)
call into the RSDK engine via the `RSDK.*`, `API.*`, and `Mod.*` function
tables. This document maps each call to its Saturn-side equivalent.

**Generated from:** `grep RSDK\\. tools/_decomp_raw/*.c | sort -u` (88 distinct
APIs in the cached subset — full game uses ~150)

**Authoritative sources for the RSDK signatures:**
- `D:\sonicmaniasaturn\rsdkv5-src\RSDKv5\RSDK\` (engine implementation)
- `https://github.com/RSDKModding/RSDKv5-Decompilation/tree/master/RSDKv5/RSDK`

**Translation principle:** the Saturn port presents a header `<rsdk_compat.h>`
that declares the RSDK API surface. Ported decomp `.c` files include this
header and call the APIs unchanged. The `src/rsdk_compat/` implementations
translate to Saturn-native (jo / SGL / direct VDP).

---

## 1. Drawing APIs

| RSDK API | Saturn equivalent | Implementation file |
|---|---|---|
| `RSDK.DrawSprite(animator, position, screenRelative)` | `slDispSprite` with `SPR_ATTR` from animator's current frame | `src/rsdk_compat/drawing.c` |
| `RSDK.DrawTile(tiles, count, tileX, tileY, position, screenRelative)` | Manual tile blit into VDP1 sprite buffer (rare; springs/etc. use it) | `src/rsdk_compat/drawing.c` |
| `RSDK.DrawAniTiles(spriteSheet, tileIndex, srcX, srcY, w, h)` | DMA copy into VDP2 NBG character pattern at tile slot | `src/rsdk_compat/drawing.c` |
| `RSDK.DrawRect(x, y, w, h, color, alpha, inkEffect, screenRelative)` | VDP1 polygon command with solid color | `src/rsdk_compat/drawing.c` |
| `RSDK.DrawLine(x1, y1, x2, y2, color, alpha, inkEffect, screenRelative)` | VDP1 line command | `src/rsdk_compat/drawing.c` |
| `RSDK.DrawFace(vertices, count, r, g, b, alpha, inkEffect)` | VDP1 polygon (used by TitleBG 3D effects) | `src/rsdk_compat/drawing.c` |
| `RSDK.DrawText(animator, position, text, ...)` | Render-string via font sprite atlas | `src/rsdk_compat/drawing.c` |
| `RSDK.FillScreen(color, a, b, c)` | Set VDP2 back-color register OR draw fullscreen VDP1 polygon | `src/rsdk_compat/drawing.c` |
| `RSDK.SetClipBounds(layer, x1, y1, x2, y2)` | `slUserClip` (VDP1 system clip) | `src/rsdk_compat/drawing.c` |
| `RSDK.AddDrawListRef(drawGroup, entitySlot)` | Append entity to per-drawGroup linked list | `src/rsdk_compat/drawing.c` |
| `RSDK.SetDrawGroupProperties(group, sorted, callback)` | Internal draw-list metadata setter | `src/rsdk_compat/drawing.c` |

## 2. Animation APIs

| RSDK API | Saturn equivalent | Implementation file |
|---|---|---|
| `RSDK.LoadSpriteAnimation(filename, scope)` | Pre-converted atlas load via `GFS_Fread`; returns atlas handle | `src/rsdk_compat/animation.c` |
| `RSDK.LoadSpriteSheet(filename, scope)` | Atlas load (sheet-only, no anim metadata) | `src/rsdk_compat/animation.c` |
| `RSDK.SetSpriteAnimation(animFramesIdx, animIdx, animator, forceApply, frameID)` | Configure animator struct: which atlas, which anim, starting frame | `src/rsdk_compat/animation.c` |
| `RSDK.ProcessAnimation(animator)` | Per-tick advance: animator.timer -= speed; on underflow, ++frameID; on end, jump to loopIndex | `src/rsdk_compat/animation.c` |
| `RSDK.GetFrameID(animator)` | Returns `animator->frameID` | `src/rsdk_compat/animation.c` |
| `RSDK.SetSpriteString(animFramesIdx, animIdx, string)` | Used for text rendering; computes per-glyph offsets | `src/rsdk_compat/animation.c` |
| `RSDK.GetSpriteAnimation(animFramesIdx, name)` | Look up anim index by name string | `src/rsdk_compat/animation.c` |

## 3. Palette APIs

| RSDK API | Saturn equivalent | Implementation file |
|---|---|---|
| `RSDK.LoadPalette(bank, filename, rowFlags)` | Read .act/.gif palette, write to CRAM bank | `src/rsdk_compat/palette.c` |
| `RSDK.SetPaletteEntry(bank, index, color)` | Direct CRAM write at `CRAM[bank*256 + index]` | `src/rsdk_compat/palette.c` |
| `RSDK.GetPaletteEntry(bank, index)` | Direct CRAM read | `src/rsdk_compat/palette.c` |
| `RSDK.CopyPalette(srcBank, srcIdx, dstBank, dstIdx, count)` | DMA copy CRAM-to-CRAM | `src/rsdk_compat/palette.c` |
| `RSDK.SetActivePalette(bank, startLine, endLine)` | Saturn VDP2 doesn't have per-scanline palette swap natively; emulate via line-color-screen if needed; otherwise just rebind | `src/rsdk_compat/palette.c` |
| `RSDK.RotatePalette(bank, startIdx, endIdx, forward)` | In-CRAM rotate via DMA cycle | `src/rsdk_compat/palette.c` |
| `RSDK.SetLimitedFade(destBank, srcBankA, srcBankB, blendAmount, startIdx, endIdx)` | Compute lerp blend in CRAM | `src/rsdk_compat/palette.c` |
| `RSDK.SetPaletteMask(maskColor)` | Set the transparent-color key for index 0 | `src/rsdk_compat/palette.c` |

## 4. Entity / Object APIs

| RSDK API | Saturn equivalent | Implementation file |
|---|---|---|
| `RSDK.CreateEntity(classID, data, x, y)` | Allocate from per-scene entity pool, call object's Create | `src/rsdk_compat/object.c` |
| `RSDK.GetEntitySlot(entity)` | Return `entity - entityPool` | `src/rsdk_compat/object.c` |
| `RSDK.GetEntityCount(classID, active)` | Walk entity pool, count matches | `src/rsdk_compat/object.c` |
| `RSDK.GetEntityByID(slotID)` | `&entityPool[slotID]` | `src/rsdk_compat/object.c` |
| `RSDK.ResetEntity(entity, classID, data)` | Zero and re-Create | `src/rsdk_compat/object.c` |
| `RSDK.ResetEntitySlot(slotID, classID, data)` | Reset slot-N | `src/rsdk_compat/object.c` |
| `RSDK.CopyEntity(dst, src, clearSrc)` | Memcpy with optional source zero | `src/rsdk_compat/object.c` |
| `RSDK.FindObject(name)` | Resolve class name → ObjectInfo* | `src/rsdk_compat/object.c` |
| `foreach_active(class, var)` macro | Walk entity pool filtering by class+active flags | `src/rsdk_compat/object.h` |
| `foreach_all(class, var)` macro | Walk entity pool filtering by class only | `src/rsdk_compat/object.h` |
| `destroyEntity(self)` macro | `self->classID = TYPE_BLANK` | `src/rsdk_compat/object.h` |
| `CREATE_ENTITY(class, data, x, y)` macro | Wraps `RSDK.CreateEntity` | `src/rsdk_compat/object.h` |

## 5. Math APIs (built around RSDK's 16.16 fixed-point)

| RSDK API | Saturn equivalent | Implementation file |
|---|---|---|
| `RSDK.Sin256(angle)` | LUT or `slSin` | `src/rsdk_compat/math.c` |
| `RSDK.Cos256(angle)` | LUT or `slCos` | `src/rsdk_compat/math.c` |
| `RSDK.Sin512(angle)` | LUT, 512-entry | `src/rsdk_compat/math.c` |
| `RSDK.Cos512(angle)` | LUT, 512-entry | `src/rsdk_compat/math.c` |
| `RSDK.Sin1024(angle)` | LUT, 1024-entry | `src/rsdk_compat/math.c` |
| `RSDK.Cos1024(angle)` | LUT, 1024-entry | `src/rsdk_compat/math.c` |
| `RSDK.ATan2(x, y)` | LUT-based atan2 | `src/rsdk_compat/math.c` |
| `RSDK.Rand(min, max)` | LCG-based | `src/rsdk_compat/math.c` |
| `RSDK.RandSeeded(min, max, seed)` | LCG with explicit seed | `src/rsdk_compat/math.c` |

## 6. Collision APIs

| RSDK API | Saturn equivalent | Implementation file |
|---|---|---|
| `RSDK.ObjectTileCollision(self, layers, cMode, cPlane, xOff, yOff, setPos)` | Per-tile height/angle lookup vs object's hitbox | `src/rsdk_compat/collision.c` |
| `RSDK.ObjectTileGrip(self, layers, cMode, cPlane, xOff, yOff, tolerance)` | Slope adherence check | `src/rsdk_compat/collision.c` |
| `RSDK.CheckObjectCollisionBox(thisEntity, thisHitbox, otherEntity, otherHitbox, setValues)` | AABB-vs-AABB | `src/rsdk_compat/collision.c` |
| `RSDK.CheckObjectCollisionPlatform(thisEntity, thisHitbox, otherEntity, otherHitbox, setValues)` | Platform (one-way top) | `src/rsdk_compat/collision.c` |
| `RSDK.CheckObjectCollisionTouchBox(thisEntity, thisHitbox, otherEntity, otherHitbox)` | AABB overlap test | `src/rsdk_compat/collision.c` |
| `RSDK.CheckObjectCollisionTouchCircle(thisEntity, thisRadius, otherEntity, otherRadius)` | Circle-vs-circle | `src/rsdk_compat/collision.c` |
| `RSDK.GetHitbox(animator, hitboxID)` | Read hitbox struct from atlas metadata | `src/rsdk_compat/collision.c` |

## 7. Tile Layer APIs

| RSDK API | Saturn equivalent | Implementation file |
|---|---|---|
| `RSDK.GetTileLayer(layerID)` | Return &g_tileLayers[layerID] | `src/rsdk_compat/tilelayer.c` |
| `RSDK.GetTileLayerID(name)` | Look up by name | `src/rsdk_compat/tilelayer.c` |
| `RSDK.GetTile(layer, x, y)` | Lookup tile-index from layer's pattern-name table | `src/rsdk_compat/tilelayer.c` |
| `RSDK.SetTile(layer, x, y, tile)` | Write to layer's pattern-name table (queues VRAM upload via vblank DMA) | `src/rsdk_compat/tilelayer.c` |
| `RSDK.CopyTileLayer(dstLayer, dstX, dstY, srcLayer, srcX, srcY, w, h)` | Block-copy patterns + queue DMA | `src/rsdk_compat/tilelayer.c` |
| `RSDK.GetTileAngle(tileID, cPlane, cMode)` | Read angle from TileConfig.bin entry | `src/rsdk_compat/tilelayer.c` |
| `RSDK.GetTileFlags(tileID, cPlane)` | Read collision flags from TileConfig.bin | `src/rsdk_compat/tilelayer.c` |

## 8. Audio APIs

| RSDK API | Saturn equivalent | Implementation file |
|---|---|---|
| `RSDK.GetSfx(filename)` | Pre-loaded SFX index lookup | `src/rsdk_compat/audio.c` |
| `RSDK.PlaySfx(sfxIdx, loopPoint, priority)` | `jo_audio_play_sound` or `slPCMOn` | `src/rsdk_compat/audio.c` |
| `RSDK.StopSfx(sfxIdx)` | `jo_audio_stop_sound` or `slPCMOff` | `src/rsdk_compat/audio.c` |
| `RSDK.SetChannelAttributes(channel, vol, pan, speed)` | Direct SCSP register set | `src/rsdk_compat/audio.c` |
| `RSDK.ChannelActive(channel)` | `slPCMStat` | `src/rsdk_compat/audio.c` |
| `RSDK.GetChannelPos(channel)` | SCSP playback-position read | `src/rsdk_compat/audio.c` |
| `RSDK.PlayStream(filename, channel, startPos, loopPoint, loadAsync)` | CD-DA track play via `CDC_CdPlay` | `src/rsdk_compat/audio.c` |
| `RSDK.StopChannel(channel)` | Stop one channel | `src/rsdk_compat/audio.c` |

## 9. Scene / Engine APIs

| RSDK API | Saturn equivalent | Implementation file |
|---|---|---|
| `RSDK.CheckSceneFolder(name)` | Compare current scene folder string | `src/rsdk_compat/scene.c` |
| `RSDK.CheckValidScene()` | Verify scene loaded | `src/rsdk_compat/scene.c` |
| `RSDK.SetScene(category, name)` | Queue scene transition | `src/rsdk_compat/scene.c` |
| `RSDK.LoadScene()` | Actually load the queued scene's StageConfig.bin + Scene.bin + tile data | `src/rsdk_compat/scene.c` |
| `RSDK.SetEngineState(state)` | Set `sceneInfo->state` (LOAD/REGULAR/PAUSED/FROZEN) | `src/rsdk_compat/scene.c` |
| `RSDK.CheckOnScreen(self, range)` | Bounds vs camera | `src/rsdk_compat/scene.c` |
| `RSDK.CheckPosOnScreen(position, range)` | Position vs camera | `src/rsdk_compat/scene.c` |

## 10. String APIs (used by HUD, Localization, Save data)

| RSDK API | Saturn equivalent | Implementation file |
|---|---|---|
| `RSDK.InitString(string, text, length)` | Init a `String` struct | `src/rsdk_compat/string.c` |
| `RSDK.SetString(string, text)` | Copy const char* into String | `src/rsdk_compat/string.c` |
| `RSDK.AppendText(string, text)` | strcat-like | `src/rsdk_compat/string.c` |
| `RSDK.GetCString(buffer, string)` | Copy out as C string | `src/rsdk_compat/string.c` |
| `RSDK.CompareStrings(stringA, stringB, exactMatch)` | strcmp variant | `src/rsdk_compat/string.c` |

## 11. Input APIs (controller, NOT keyboard)

| RSDK API | Saturn equivalent | Implementation file |
|---|---|---|
| `API_GetInputDeviceID(inputSlot)` | Saturn controller is single-port for our build (CONT_P1) | `src/rsdk_compat/input.c` |
| `API_AssignInputSlotToDevice(slot, deviceID)` | No-op (single-controller) | `src/rsdk_compat/input.c` |
| `API_ResetInputSlotAssignments()` | No-op | `src/rsdk_compat/input.c` |
| `ControllerInfo[CONT_P1].keyStart.press` | `jo_is_pad1_key_pressed(JO_KEY_START)` (edge-triggered) | `src/rsdk_compat/input.c` |
| `ControllerInfo[CONT_P1].keyA.down` | `jo_is_pad1_key_down` (level-triggered) | `src/rsdk_compat/input.c` |

## 12. API_ (platform/save layer)

| RSDK API | Saturn equivalent | Implementation file |
|---|---|---|
| `API_SetRichPresence(presence, str)` | No-op (no Discord on Saturn) | `src/rsdk_compat/api.c` |
| `API_SetNoSave(flag)` | Set internal save-disabled flag | `src/rsdk_compat/api.c` |
| `API.LoadUserFile(name, buf, size, callback)` | Read from Backup RAM (jo_backup) | `src/rsdk_compat/api.c` |
| `API_SaveUserFile(name, buf, size, callback, compressed)` | Write to Backup RAM | `src/rsdk_compat/api.c` |
| `API.CheckDLC(id)` | Always returns false (no Plus DLC) | `src/rsdk_compat/api.c` |
| `API.UnlockAchievement(...)` | No-op | `src/rsdk_compat/api.c` |
| `API_ClearPrerollErrors()` | No-op | `src/rsdk_compat/api.c` |
| `API.ClearSaveStatus()` | No-op | `src/rsdk_compat/api.c` |
| `API.ClearUserDB(tableID)` | Clear backup-RAM region | `src/rsdk_compat/api.c` |

## 13. Music_ (the Music object's public methods)

The Music object is itself a Mania game object (`SonicMania_Objects_Global_Music.c`).
Its public methods are called from many sites:

| Method | Saturn equivalent |
|---|---|
| `Music_PlayTrack(trackID)` | Look up trackID → CD-DA track number, call `jo_audio_play_cd_track` |
| `Music_Stop()` | `jo_audio_stop_cd` |
| `Music_FadeOut(speed)` | CDDA volume ramp (SCSP CDDA channel attenuation) |
| `Music_PlayJingle(trackID)` | Same as PlayTrack but flagged as one-shot |
| `Music_IsPlaying()` | Check CDDA play state |
| `Music_Pause()`, `Music_Resume()` | CDC_CdPause / CDC_CdResume |

Implemented by porting Music.c with `src/rsdk_compat/audio.c` as the backend.

## 14. StateMachine_

| RSDK API | Saturn equivalent |
|---|---|
| `StateMachine_Run(stateFunc)` | `if (stateFunc) stateFunc()` |
| `StateMachine_None` | NULL function-pointer literal |

These are macros in `RSDKv5/RSDK/StateMachine.h`. Saturn-side: trivial.

## 15. Globals / Engine info

| Decomp construct | Saturn equivalent |
|---|---|
| `SceneInfo->state` | `g_sceneInfo.state` |
| `SceneInfo->entity` | `g_currentEntity` |
| `SceneInfo->position` | Camera position (mirrored to global) |
| `SceneInfo->center` | Screen center constants (160, 112) |
| `SceneInfo->size.x/y` | Screen size (320, 240) |
| `RSDK_THIS(class)` macro | `EntityClass *self = (EntityClass *)g_currentEntity;` |

## 16. `globals` (game-wide state)

| Field | Saturn equivalent |
|---|---|
| `globals->playerID` | `g_globals.playerID` |
| `globals->saveLoaded`, `saveRAM`, `optionsLoaded`, `optionsRAM` | Backup-RAM-resident structs |
| `globals->blueSpheresInit` | Stage init flag (irrelevant pre-special-stage) |

---

## Translation conventions

### File naming

Decomp file `SonicMania/Objects/Title/TitleSonic.c` →
Saturn file `src/mania/Objects/Title/TitleSonic.c`

The directory structure mirrors the decomp so cross-referencing is mechanical.

### Function signatures

Keep the exact decomp function signatures:
- `void TitleSonic_Update(void)`
- `void TitleSonic_LateUpdate(void)`
- `void TitleSonic_StaticUpdate(void)`
- `void TitleSonic_Draw(void)`
- `void TitleSonic_Create(void *data)`
- `void TitleSonic_StageLoad(void)`
- `void TitleSonic_Serialize(void)`

The `RSDK_THIS(class)` macro inside each function gets `self` from the
engine-provided current-entity pointer.

### Object struct layout

The decomp's per-object struct (e.g. `EntityTitleSonic`) is defined in
its `.h` header. Saturn-side: same struct, lives in the entity pool slot.
Pool slot size is the max across all classes (per `mania_decomp_catalog.md`
the upstream pool is ENTITY_COUNT × ~512 bytes; Saturn has 1 MB Work RAM-H
so this fits).

### Fixed-point convention

All `int32` positional values are 16.16 fixed point. Macro:
```c
#define TO_FIXED(x) ((x) << 16)
#define FROM_FIXED(x) ((x) >> 16)
```

### RSDK_REGISTER_OBJECT

`RSDK_REGISTER_OBJECT(TitleSonic)` in `Game.c` → Saturn-side: explicit
function-pointer-table registration:
```c
// In src/mania/Game.c (port of SonicMania/Game.c)
mania_register_object("TitleSonic",
    TitleSonic_Update, TitleSonic_LateUpdate, TitleSonic_StaticUpdate,
    TitleSonic_Draw, TitleSonic_Create, TitleSonic_StageLoad,
    TitleSonic_EditorDraw, TitleSonic_EditorLoad, TitleSonic_Serialize,
    sizeof(EntityTitleSonic), sizeof(ObjectTitleSonic),
    SCOPE_STAGE);
```

---

## Phase 0 deliverables (this iteration)

1. `src/rsdk_compat/` directory with header + skeleton .c files for each API group above
2. Each skeleton compiles cleanly; non-yet-implemented functions are `// FIXME: stub`
3. Build system updated to compile `src/rsdk_compat/*.c`
4. `verify_done.ps1` still passes (skeleton stubs are no-ops; current Saturn code unchanged)
5. This document committed as the API reference

Once Phase 0 is in, Phase 1 ports the Title scene (TitleSetup, TitleLogo,
TitleSonic, TitleBG) by literal translation of the cached decomp files.
