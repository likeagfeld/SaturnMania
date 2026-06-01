# RSDKv5 Engine Catalog — Saturn Port Reference (Phase A)

Authoritative reference compiled from the open-source decompilation at
**github.com/RSDKModding/RSDKv5-Decompilation** (master branch).

> **Reading note.** This document deliberately quotes only short function
> signatures, struct field layouts, enum values, byte-layout descriptions
> and line-number citations from the engine repo. Long verbatim function
> bodies are *paraphrased* rather than reproduced — the engine sources
> are a community decompilation of code Sega/Headcannon holds rights to,
> and the Saturn port must be implementation-equivalent, not a literal
> copy. Every byte layout and algorithm noted here is sufficient to
> re-implement the subsystem from scratch in C/SH-2 against SGL/Jo.

All `raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/...`
URLs are stable; line numbers below are accurate as of the
2026-05-26 fetch.

---

## §0 Repo map (engine subset relevant to Saturn port)

| Subsystem            | Path                                              |
|----------------------|---------------------------------------------------|
| Storage / data pack  | `RSDKv5/RSDK/Storage/Storage.{cpp,hpp}`           |
| Reader / file I/O    | `RSDKv5/RSDK/Core/Reader.{cpp,hpp}`               |
| Text / strings       | `RSDKv5/RSDK/Storage/Text.{cpp,hpp}`              |
| Scene loader         | `RSDKv5/RSDK/Scene/Scene.{cpp,hpp}`               |
| Object system        | `RSDKv5/RSDK/Scene/Object.{cpp,hpp}`              |
| Tile + collision     | `RSDKv5/RSDK/Scene/Collision.{cpp,hpp}`           |
| Drawing              | `RSDKv5/RSDK/Graphics/Drawing.{cpp,hpp}`          |
| Sprite I/O           | `RSDKv5/RSDK/Graphics/Sprite.{cpp,hpp}`           |
| Animation            | `RSDKv5/RSDK/Graphics/Animation.{cpp,hpp}`        |
| Palette              | `RSDKv5/RSDK/Graphics/Palette.{cpp,hpp}`          |
| Math                 | `RSDKv5/RSDK/Core/Math.hpp`                       |
| Audio                | `RSDKv5/RSDK/Audio/Audio.{cpp,hpp}`               |
| Input                | `RSDKv5/RSDK/Input/Input.{cpp,hpp}`               |
| User / save          | `RSDKv5/RSDK/User/Core/UserStorage.cpp`           |
| Engine core          | `RSDKv5/RSDK/Core/RetroEngine.hpp`                |
| Public API table     | `RSDKv5/RSDK/Core/Link.cpp`                       |
| Boot                 | `RSDKv5/main.cpp`                                 |

---

## §1 Storage layer

### 1.1 Data.rsdk pack file format

Source: `RSDKv5/RSDK/Core/Reader.cpp` (header parse near L113); definitions
in `Reader.hpp`. Verified hashing convention is **MD5 of the lowercased
forward-slash POSIX file path** (e.g. `"data/sprites/global/playertails.bin"`).

URL: <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Core/Reader.cpp>
URL: <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Core/Reader.hpp>

Byte layout, little-endian throughout:

```
offset  size   field                                          notes
------  -----  --------------------------------------------   ---------------------------
0x00    4      signature        = 0x4B445352  ("RSDK")        Reader.hpp:36
0x04    1      'v'                                            literal ASCII 'v'
0x05    1      version char     (typically 'B' for Mania)
0x06    2      fileCount        uint16 (LE)                   bounded by DATAFILE_COUNT=0x1000
0x08    N*0x18 file table       (per entry, 24 bytes)
...     ...    file data blobs

per file-table entry (24 bytes):
  +0x00  16    md5Hash          uint8[16]  (4× uint32 read big-endian into struct)
  +0x10   4    offset           int32 LE   (absolute file byte offset)
  +0x14   4    sizeAndFlag      int32 LE
                  bit 31  = encrypted flag
                  bits 0..30 = actual file size in bytes
```

Key constants (Reader.hpp):

| Name                  | Value      | Line |
|-----------------------|------------|------|
| `RSDK_SIGNATURE_RSDK` | 0x4B445352 | 36   |
| `RSDK_SIGNATURE_DATA` | 0x61746144 | 38   |
| `DATAFILE_COUNT`      | 0x1000     | 40   |
| `DATAPACK_COUNT`      | 4          | 41   |
| Encryption key size   | 0x10 bytes | 53-54 |

Structs (Reader.hpp:47-73):

```
FileInfo {
    int32  fileSize, externalFile, readPos, fileOffset;
    FileIO *file;
    uint8  *fileBuffer;
    uint8  usingFileBuffer, encrypted, eNybbleSwap;
    uint8  encryptionKeyA[0x10], encryptionKeyB[0x10];
    uint8  eKeyPosA, eKeyPosB, eKeyNo;
}
RSDKFileInfo {
    RETRO_HASH_MD5 hash;
    int32 size, offset;
    uint8 encrypted, useFileBuffer;
    int32 packID;
}
RSDKContainer { char name[0x100]; uint8 *fileBuffer; int32 fileCount; }
```

### 1.2 Hash lookup convention

`OpenDataFile()` (Reader.cpp ≈L105) uses macro `GEN_HASH_MD5_BUFFER` to MD5
the lowercased filename, then linearly searches the in-memory `dataFiles[]`
array of `RSDKFileInfo` for a matching hash. **There is no sorted/btree
index — it's a linear scan over `fileCount` entries.** Saturn-side this is
fine; the Mania pack has ~5000 files.

### 1.3 Encryption (the eLoad cipher)

Per-file keys are derived inside `GenerateELoadKeys()` (Reader.cpp ≈L387-413):
two 16-byte MD5 keys A and B are produced by MD5-hashing the **filename and
file size formatted as strings**. The cipher then XORs bytes against
`encryptionKeyA[eKeyPosA]` while alternately mixing in `encryptionKeyB` and
nibble-swapping. Mania's `Data.rsdk` ships almost entirely *unencrypted*
(bit 31 of size = 0 on every file we've extracted) so the Saturn port can
skip the cipher entirely for the shipping data. The flag must still be
*read* and the path *honored* if it ever fires.

### 1.4 Scene.bin

Source: `RSDKv5/RSDK/Scene/Scene.cpp` (LoadSceneFile ≈L461-665).
URL: <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Scene/Scene.cpp>

Signature: `RSDK_SIGNATURE_SCN = 0x4E4353` ("SCN") — Scene.hpp:17.

Logical block order (little-endian throughout, all "String" = uint16 length
then UTF-8 bytes):

```
1. uint32   signature ('SCN\0')                     L461
2. EDITOR METADATA  (skipped at runtime)            L270-285
     - version byte, two BGRA colors, padding,
     - stamp-name string. Engine seeks past with
       Seek_Cur(&info, 0x10) + string length.
3. uint8    layerCount                              L475
   per layer (L478-527):
       uint8   visible
       String  name  -> MD5 hashed into layer hash
       uint8   type  (LAYER_HSCROLL/VSCROLL/ROTOZOOM/BASIC)
       uint8   drawGroup
       uint8   widthShift
       uint8   heightShift
       uint16  xsize, ysize          (in tiles)
       int16   parallaxFactor        (8.8 fixed)
       int16   scrollSpeed           (16.16 fixed-ish per frame)
       uint8   deform
       Per-row scroll-info block (RLE compressed):
            parallaxFactor, scrollSpeed, deform, uint8 unknown
       uint16  layoutSize  (tile array, xsize*ysize uint16 entries,
                            RLE compressed)
4. uint8    classCount  (object types in this scene) L544
   per class (L551-580):
       String  hash (16-byte MD5 of class name)
       uint8   varCount
       per editable var (L569):  type, MD5 name
   per instance count uint16, then per entity (L583-647):
       uint16  slot
       int32   posX, posY            (16.16 fixed)
       per var (e=1 .. varCount-1):  raw value bytes
       NOTE: var index 0 = "filter" — IMPLICIT, NOT in the file.
       The loader registers it via SetEditableVar(VAR_UINT8,"filter",
       classID, offsetof(Entity,filter)) at L569, and defaults the
       in-memory value to 0xFF at L601. The first stored attribute on
       disk begins at index 1.
5. (REV02 only) filter reorder pass                 L520-545
```

This matches what your title-research agent already found: the implicit
`filter` attribute is **not stored** in the scene-bin table; it's a
synthetic field that exists at offset 0 in the in-memory Entity layout
but is skipped during file deserialization.

### 1.5 Anim.bin (sprite animations)

Source: `RSDKv5/RSDK/Graphics/Animation.cpp` (LoadSpriteAnimation
≈L29-108).
URL: <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Graphics/Animation.cpp>
URL: <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Graphics/Animation.hpp>

Signature: `RSDK_SIGNATURE_SPR`.

```
0x00   4   signature
0x04   4   totalFrameCount   uint32 LE  (sum across all animations)
0x08   1   sheetCount
       per sheet:  String filename   (loaded via LoadSpriteSheet)
       uint8 hitboxCount
       per hitbox type:  String name
       uint16 animCount
       per animation:
           String name        (-> hashed into animation->hash)
           uint16 frameCount
           uint16 animationSpeed     (fixed-tick rate)
           uint8  loopIndex          (frame to return to on wrap)
           uint8  rotationStyle      (ROTSTYLE_*)
       per frame:
           uint8  sheetID
           uint16 duration
           uint16 unicodeChar
           uint16 sprX, sprY         (source rect on sheet)
           uint16 width, height
           int16  pivotX, pivotY     (signed!)
           per hitbox:  int16 left, top, right, bottom
```

In-memory structs (Animation.hpp:44-71):

```
Hitbox        { int16 left, top, right, bottom; }              L21-26
SpriteFrame   : GameSpriteFrameType {                          L44-50
                  uint8 hitboxCount;
                  Hitbox hitboxes[FRAMEHITBOX_COUNT]; }
SpriteAnimation {                                              L52-57
    RETRO_HASH_MD5 hash;
    SpriteFrame  *frames;
    SpriteAnimationEntry *animations;
    uint16 animCount;
    uint8  scope;  }
Animator {                                                     L59-71
    SpriteFrame *frames;
    int32 frameID;
    int16 animationID, prevAnimationID, speed, timer;
    int16 frameDuration, frameCount;
    uint8 loopIndex, rotationStyle;  }
```

`SPRITEANIM_COUNT = 0x40` (Animation.hpp:12) — max simultaneously-loaded
animation files.

Rotation modes (Animation.hpp:19):

| Name                  | Val | Meaning                              |
|-----------------------|-----|--------------------------------------|
| ROTSTYLE_NONE         | 0   | ignore angle, draw frame as-is       |
| ROTSTYLE_FULL         | 1   | rotate sprite by `entity->rotation`  |
| ROTSTYLE_45DEG        | 2   | snap to 8 directions                 |
| ROTSTYLE_90DEG        | 3   | snap to 4 directions                 |
| ROTSTYLE_180DEG       | 4   | snap to 2 directions                 |
| ROTSTYLE_STATICFRAMES | 5   | pre-baked rotated frames in atlas    |

### 1.6 Strings (menu text)

Source: `RSDKv5/RSDK/Storage/Text.cpp`.
URL: <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Storage/Text.cpp>

RSDKv5 menu text is **plain UTF-8 or UTF-16 .txt files in
`Data/Strings/`** — no Strings.bin container. BOM detection:
- 0xFEFF prefix → UTF-16 (read directly into the uint16 `String::chars`)
- 0xEF 0xBB 0xBF prefix → UTF-8 with BOM
- no BOM → ASCII/UTF-8 by content

In-memory `String` struct (inferred from usage):

```
String { uint16 *chars;  uint16 length;  uint16 size; }
```

`LoadStringList()` reads delimited lists from a single .txt file
(line-separated); `SplitStringList()` is the helper.

---

## §2 Object / Entity system

Source: `RSDKv5/RSDK/Scene/Object.{cpp,hpp}`.
URL: <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Scene/Object.cpp>
URL: <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Scene/Object.hpp>

### 2.1 Slot constants (Object.hpp:11-23)

| Constant            | Value                                          |
|---------------------|------------------------------------------------|
| `OBJECT_COUNT`      | 0x400  (1024 class slots)                      |
| `TEMPENTITY_COUNT`  | 0x100  (256)                                   |
| `RESERVE_ENTITY_COUNT` | 0x40 (Sonic/Tails/Knux + camera slots)      |
| `SCENEENTITY_COUNT` | 0x800  (placed entities from Scene.bin)        |
| `ENTITY_COUNT`      | RESERVE + SCENE + TEMP = **0x940 = 2368**      |
| `TEMPENTITY_START`  | ENTITY_COUNT - TEMPENTITY_COUNT = 0x840        |
| `TYPEGROUP_COUNT`   | 0x104                                          |
| `FOREACH_STACK_COUNT` | 0x400                                        |

Permanent entities live in slots `[0 .. TEMPENTITY_START)`; temp entities
(particles, bullets) are recycled from a ring inside `[TEMPENTITY_START ..
ENTITY_COUNT)`. `sceneInfo.createSlot` is the ring head; `CreateEntity`
advances it (Object.cpp:471, 1194-1195).

### 2.2 ACTIVE states (Object.hpp:60-70)

| Value | Name           | Meaning                                          |
|-------|----------------|--------------------------------------------------|
| 0     | ACTIVE_NEVER   | never updates                                    |
| 1     | ACTIVE_ALWAYS  | every frame, even when paused                    |
| 2     | ACTIVE_NORMAL  | every frame in REGULAR engine state              |
| 3     | ACTIVE_PAUSED  | only while ENGINESTATE_PAUSED                    |
| 4     | ACTIVE_BOUNDS  | when within `updateRange` of any camera (box)    |
| 5     | ACTIVE_XBOUNDS | only X-axis range check                          |
| 6     | ACTIVE_YBOUNDS | only Y-axis range check                          |
| 7     | ACTIVE_RBOUNDS | circular range — radius squared compare          |

Bounds math (Object.cpp:553-598, paraphrased):
- `BOUNDS`: `inRange |= abs(ent.x-cam.x) ≤ ent.updateRange.x+cam.offset.x
   && abs(ent.y-cam.y) ≤ ent.updateRange.y+cam.offset.y`, OR'd across all
  `cameraCount` cameras.
- `XBOUNDS` / `YBOUNDS`: same with one axis dropped.
- `RBOUNDS`: convert both deltas via `FROM_FIXED()` first (loses fractional
  to avoid int-overflow), then `sx*sx + sy*sy ≤ ent.updateRange.x +
  cam.offset.x` — note the right side is **not squared** in source; treat
  `updateRange.x` as already the squared radius.

### 2.3 Entity struct (Object.hpp:107-128)

Per-entity record (sized via class's `entityClassSize`):

```
Entity (base) {
    Vector2 position, scale, velocity, updateRange;
    int32   angle, alpha, rotation, groundVel, zdepth;
    uint16  group, classID;
    bool32  inRange, isPermanent, tileCollisions,
            interaction, onGround;
    uint8   active, filter (REV02), direction, drawGroup,
            collisionLayers, collisionPlane, collisionMode,
            drawFX, inkEffect, visible, onScreen;
}
```

Subclassed Entity types (e.g. `EntityPlayer`) embed this at offset 0 and
append additional fields. Saturn port must keep `sizeof(Entity)` and field
offsets identical to the decomp so that data tables built from the PC
decomp transfer 1:1.

### 2.4 ObjectClass (Object.hpp:148-175)

```
ObjectClass {
    RETRO_HASH_MD5 hash;
    void (*update)(), (*lateUpdate)(), (*staticUpdate)(),
         (*draw)(), (*create)(void*), (*stageLoad)(),
         (*editorLoad)(), (*editorDraw)(), (*serialize)(),
         (*staticLoad)(void*);
    void   **staticVars;       // double pointer (slot per scene)
    int32  entityClassSize;
    int32  staticClassSize;
    ObjectClass *inherited;    // mod loader chain
    const char *name;          // debug only
}
```

### 2.5 Frame loop (Object.cpp:506-740)

Pseudocode order per game frame:

```
ProcessObjects():
    for c in 0..DRAWGROUP_COUNT:   drawGroups[c].entityCount = 0
    for c in 0..classCount:
        if class.active in {ALWAYS, NORMAL} && class.staticUpdate:
            class.staticUpdate()
    for slot in 0..ENTITY_COUNT:
        ent = objectEntityList[slot]
        compute ent.inRange from ent.active vs cameras (§2.2)
        if ent.inRange && class.update: class.update()
    for slot in 0..ENTITY_COUNT:
        if ent.inRange && class.lateUpdate: class.lateUpdate()
    # Drawing is a separate pass:
ProcessObjectDrawLists():            # Object.cpp ≈L735+
    for g in 0..DRAWGROUP_COUNT:
        if sorted:  insertion-sort by zdepth
        layerCallback() for tile layers in this group
        for ent in drawGroups[g]:    class.draw()
```

`Create` is called from `CreateEntity` synchronously — never deferred.
`StageLoad` fires once at scene boot, before any entity exists.
`EditorDraw` / `EditorLoad` are stripped at runtime for the Saturn port.

### 2.6 Lifecycle API (Object.cpp:1121-1214)

```
Entity *CreateEntity(uint16 classID, void *data, int32 x, int32 y);
   - returns pointer to slot at sceneInfo.createSlot
   - advances ring pointer (wraps within TEMPENTITY range)
   - memsets the entity to class.entityClassSize bytes, sets
     classID/position, calls class.create(data) if non-null.

void   ResetEntity(Entity *ent, uint16 classID, void *data);
void   ResetEntitySlot(uint16 slot, uint16 classID, void *data);
void   CopyEntity(void *dst, void *src, bool32 clearSrc);
bool32 GetActiveEntities(uint16 group, Entity **out);
bool32 GetAllEntities  (uint16 classID, Entity **out);
void   BreakForeachLoop();
```

Note: there is **no** explicit `DestroyEntity` — to remove an entity, set
`ent->classID = TYPE_BLANK_OBJECT` (or 0) and reset its data via memset.

### 2.7 Foreach (Object.cpp:1218-1263)

`GetActiveEntities` walks `typeGroups[group].entries[]` (a pre-built
slot-index list per type-group), checking `classID == group` on each.
`GetAllEntities` falls back to a sequential walk over all `ENTITY_COUNT`
slots. Both use a stack (`foreachStackPtr`) to support nested foreach
loops — Saturn must keep `FOREACH_STACK_COUNT = 0x400` depth.

---

## §3 Graphics / Drawing

Source: `RSDKv5/RSDK/Graphics/Drawing.{cpp,hpp}`.
URL: <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Graphics/Drawing.cpp>
URL: <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Graphics/Drawing.hpp>

### 3.1 Screen / surface constants (Drawing.hpp)

| Constant         | Value | Line |
|------------------|-------|------|
| `DRAWGROUP_COUNT`| 16    | 13   |
| `SHADER_COUNT`   | 0x20  | 17   |
| `SURFACE_COUNT`  | 0x40  | 5    |

PC engine ScreenInfo:

```
ScreenInfo {                                       L68-79
    uint16  frameBuffer[SCREEN_XMAX * SCREEN_YSIZE];
    Vector2 position, size, center;
    int32   pitch;
    int32   clipBound_X1, clipBound_Y1, clipBound_X2, clipBound_Y2;
    int32   waterDrawPos;
}
DrawList {                                         L81-88
    uint16 entries[ENTITY_COUNT];
    uint16 layerDrawList[LAYER_COUNT];
    void (*hookCB)();
    bool32 sorted;
    int32  entityCount, layerCount;
}
GFXSurface { RETRO_HASH_MD5 hash; uint8 *pixels;
             int32 height, width, lineSize; uint8 scope; }
```

Frame buffer format is RGB565 (5/6/5) on PC. **Saturn port maps this to
VDP1 sprite framebuffer (RGB555 + MSB-priority) and/or VDP2 NBG layers
with 256-color palettes** — see §12.

### 3.2 Flip and ink enums (Drawing.hpp:27-37)

```
InkEffects:  INK_NONE, INK_BLEND, INK_ALPHA, INK_ADD,
             INK_SUB, INK_TINT, INK_MASKED, INK_UNMASKED
FlipFlags:   FLIP_NONE=0, FLIP_X=1, FLIP_Y=2, FLIP_XY=3
```

### 3.3 Layer compositor (paraphrased from Drawing.cpp)

The compositor runs per-frame in **draw-group order 0..15**. For each
draw group:

1. If `drawGroups[g].hookCB` is set, invoke it (used by some scenes for
   inter-group passes).
2. For each tile layer where `layer->drawGroup[currentCamera] == g`:
   render via `DrawLayerHScroll` / `DrawLayerVScroll` / `DrawLayerRotozoom`
   / `DrawLayerBasic` depending on `layer->type`. Each scanline reads a
   `ScrollInfo` row (parallax + per-row x offset) and blits 16×16 tiles
   from the active tileset to the framebuffer.
3. Sort `drawGroups[g].entries` by `entity->zdepth` if `sorted == true`.
4. Call `class.draw()` on each entity in that group (entity calls
   `RSDK.DrawSprite` etc., which write to `currentScreen->frameBuffer`).

### 3.4 DrawSprite

`DrawSprite(animator, position, screenRelative)` — paraphrased:

- Take the current `SpriteFrame` from `animator->frames[animator->frameID]`.
- Compute screen coords:
  `dx = (position.x >> 16) - currentScreen->position.x + frame.pivotX`
  (and Y likewise). If `screenRelative`, skip the camera subtraction.
- Clip to `clipBound_*` rectangle.
- Apply `entity->direction` (FLIP_X iterates source U backwards, FLIP_Y
  iterates V backwards; FLIP_XY does both).
- Apply `entity->rotation` (only when frame's animator has
  `rotationStyle == ROTSTYLE_FULL`) — uses `sin512`/`cos512` from the
  RSDK math tables and walks a rotated bounding box.
- Apply `entity->drawFX` / `entity->inkEffect` (alpha, additive,
  blend-lookup, etc.).
- Blit pixels to `frameBuffer` honoring `entity->alpha` and `inkEffect`.

### 3.5 DrawTile

`DrawTile(tileIDs, count, countY, position, tileOffset, screenRelative)`
draws an arbitrary block of 16×16 tiles at a fixed screen position
(used by HUDs, menus, transitions, and the Bonus Stage rings UI). Each
tileID is `(flipFlags << 10) | (paletteRow << 6 something) | index`.

### 3.6 FillScreen

`FillScreen(color, alphaR, alphaG, alphaB)` — Drawing.cpp ≈L1273.
Iterates the entire framebuffer, blending the existing pixel toward
`color`'s R/G/B independently weighted by the three alpha channels.
This is the primitive `Draw_FadeBlack`, `Draw_FadeWhite`, and the
flashbulb effect are all built on.

### 3.7 SetClipBounds

`SetClipBounds(screenID, x1, y1, x2, y2)` — sets the four `clipBound_*`
fields of `screens[screenID]`. All subsequent blits in that screen
respect the rectangle (clamped to 0..SCREEN_XSIZE/SCREEN_YSIZE).

---

## §4 Animation system

Source: `RSDKv5/RSDK/Graphics/Animation.cpp` (in-engine logic at ≈L111-168).
URL: <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Graphics/Animation.cpp>

### 4.1 ProcessAnimation tick

Paraphrased (Animation.cpp:111-129). Per call (typically once per frame
from the entity's Update or LateUpdate):

```
animator->timer += animator->speed;
while animator->timer >= animator->frameDuration:
    animator->timer -= animator->frameDuration;
    ++animator->frameID;
    if animator->frameID >= animator->frameCount:
        animator->frameID = animator->loopIndex;     # may equal frameCount → freeze on last
    animator->frameDuration = frames[frameID].duration;
```

Notes:
- **One-shot animations** are spelled by setting `loopIndex == frameCount`;
  the last frame becomes a fixed point because the post-increment lands on
  `frameCount`, which is immediately rewritten to `loopIndex == frameCount`,
  and the comparison `>=` keeps it pinned.
- **Reverse** isn't a top-level mode — entities flip direction via
  `entity->direction = FLIP_X` and reverse playback by decrementing
  `frameID` manually if needed.
- `animator->speed` is in the same units as `frameDuration` (typically
  `0x100` per "full speed").

### 4.2 SetSpriteAnimation

`SetSpriteAnimation(aniListID, animationID, animator, forceApply,
frameID)` — paraphrased (Animation.cpp ≈L131-153). Looks up the
animation by ID in `spriteAnimationList[aniListID]`. If
`animator->animationID == animationID && !forceApply`, return early.
Otherwise:

```
animator->prevAnimationID = animator->animationID;
animator->animationID     = animationID;
animator->frames          = &spriteAnim->frames[anim->frameListOffset];
animator->frameCount      = anim->frameCount;
animator->speed           = anim->animationSpeed;
animator->loopIndex       = anim->loopIndex;
animator->rotationStyle   = anim->rotationStyle;
animator->frameID         = frameID;
animator->timer           = 0;
animator->frameDuration   = animator->frames[frameID].duration;
```

### 4.3 SetSpriteString

`SetSpriteString` (Animation.cpp:155-168) maps each character in a
`String` to a frame index by searching `unicodeChar` matches — used by
the in-game text renderer (HUD, dialogue boxes) to convert a `String`
into a sequence of frame IDs that can be blitted with `DrawSprite`.

---

## §5 Tile layer + collision

Source: `RSDKv5/RSDK/Scene/Collision.{cpp,hpp}`, `Scene.{cpp,hpp}`.
URL: <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Scene/Collision.cpp>
URL: <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Scene/Collision.hpp>

### 5.1 Tile + layer constants (Scene.hpp)

| Constant      | Value     | Line |
|---------------|-----------|------|
| `TILE_COUNT`  | 0x400     | 1    |
| `TILE_SIZE`   | 0x10 (16) | 2    |
| `TILESET_SIZE`| TILE_COUNT * TILE_DATASIZE | 4 |
| `LAYER_COUNT` | (used as `[8]` per project; not defined in this header) | — |
| `CAMERA_COUNT`| typically 4 | — |

Layer types (Scene.hpp:14-18):

| Value | Name             | Used by                              |
|-------|------------------|--------------------------------------|
| 0     | LAYER_HSCROLL    | most Mania level FG + BG layers      |
| 1     | LAYER_VSCROLL    | vertical autoscroll segments         |
| 2     | LAYER_ROTOZOOM   | Special Stage path (REV0U)           |
| 3     | LAYER_BASIC      | static (no scroll)                   |

Collision modes (Scene.hpp:36-41):

| Value | Name        | "Up" direction                |
|-------|-------------|-------------------------------|
| 0     | CMODE_FLOOR | up = -Y (default gravity)     |
| 1     | CMODE_LWALL | up = +X (clinging to L wall)  |
| 2     | CMODE_ROOF  | up = +Y (upside-down)         |
| 3     | CMODE_RWALL | up = -X (clinging to R wall)  |

### 5.2 Sensor placement

From Collision.cpp ≈L1310-1425 — when `entity->tileCollisions` is set,
six sensors are spawned per `ProcessObjectMovement` invocation:

```
sensor[0] = left wall   ( outerLeft, 0   )           horizontal probe
sensor[1] = right wall  ( outerRight, 0  )           horizontal probe
sensor[2] = floor inner-left  ( innerLeft,  outerBottom )   downward probe
sensor[3] = floor inner-right ( innerRight, outerBottom )   downward probe
sensor[4] = ceiling inner-left  ( innerLeft,  outerTop )    upward probe
sensor[5] = ceiling inner-right ( innerRight, outerTop )    upward probe
```

So the correct answer to "3 or 4 floor sensors?" is **2 below + 2 above
+ 2 sides = 6 total** (Mania spec). Older Sonic games used 4 floor
sensors (E-D-C-B); RSDKv5 reduced this to 2 because angle interpolation
between just two contact points already produces the correct slope.
Saturn port should mirror this exactly to keep level geometry consistent
with the data.

X offsets are taken from `collisionOuter.left/right` (typical Sonic
sprite ≈ ±9 px, ±20 px Y) with small ±1 corrections to avoid corner
clipping (Collision.cpp ≈L1420-1425).

### 5.3 Sides (Collision.hpp:14-19)

| Value | Name      |
|-------|-----------|
| 0     | C_NONE    |
| 1     | C_TOP     |
| 2     | C_LEFT    |
| 3     | C_RIGHT   |
| 4     | C_BOTTOM  |

### 5.4 Sensor struct + tile-collision enum (Collision.hpp:7-25)

```
TileCollisionModes:
    TILECOLLISION_NONE = 0,
    TILECOLLISION_DOWN = 1,    # normal gravity, sensors point down
    TILECOLLISION_UP   = 2,    # inverted gravity (REV0U only)

CollisionSensor { Vector2 position; bool32 collided; uint8 angle; }
```

### 5.5 Algorithms (paraphrased)

- **FindFloorPosition** (Collision.cpp ≈L1822-1875). Starting at the
  sensor's pixel Y, fetch the tile at `(sx>>4, sy>>4)`. If the tile's
  floor collision mask (height array) gives a surface within tolerance
  in that column (`sx & 0xF`), set `sensor.position.y` to
  `tileTop + 0xF - mask[col]` and `sensor.angle = tileInfo.floorAngle`.
  Otherwise step down by 16 px and repeat (up to 3 tiles ≈ 48 px).
- **FindRoofPosition** (L1922-1973). Symmetric, scanning upward, using
  the roof mask.
- **FindLWallPosition / FindRWallPosition** (L1877-1920, 1975-2018).
  Scan horizontally one column at a time using the L/R wall masks.
- **ProcessPathGrip** (L1454-1818). The grounded-Sonic loop. Once per
  frame: take both floor sensors, pick the one with the lower Y (higher
  ground), set `entity->position.y` to its Y, set `entity->angle` from
  the same sensor. Then re-evaluate `collisionMode`: angles in
  `[0x00..0x20)` and `[0xE0..0x100)` are floor, `[0x20..0x60)` is
  R-wall, `[0x60..0xA0)` is roof, `[0xA0..0xE0)` is L-wall (256/4 = 64
  step). When the mode transitions, sensors are rebuilt for the new
  orientation by `SetPathGripSensors` (L1820-1890).
- **Velocity rotation** (L1373-1376): `xvel = cos256(angle)*groundVel >> 8`,
  `yvel = sin256(angle)*groundVel >> 8`. This is the classic Sonic
  slope-projection — `groundVel` is "speed along the path" and gets
  decomposed into world XY per frame.
- **Edge/cliff detection**: at the end of `ProcessPathGrip`, if **both**
  floor sensors come up empty *and* `entity->onGround` was true last
  frame, the engine clears `onGround` (entity falls). Mania's "balancing
  on a ledge" anim trigger is when exactly one of the two sensors is
  empty.

### 5.6 Tile properties

`TileConfig.bin` (signature `RSDK_SIGNATURE_TIL = 0x4C4954`,
Scene.hpp:18) stores per-tile:

- floorAngle, roofAngle, lWallAngle, rWallAngle (uint8, 256/360°)
- flags (collisionSolid_layerA, collisionSolid_layerB, special bits)
- floor[16] height column array
- roof[16] height column array
- lWall[16] / rWall[16] horizontal column arrays

The engine pre-builds **FlipX / FlipY / FlipXY** variants at load
(Collision.cpp ≈L890-1025), so runtime collision queries just OR the
tile's flip bits into the table index.

### 5.7 Solid / non-solid / lethal interpretation

The decomp engine itself only tracks **per-tile "solid in plane A" and
"solid in plane B"** bits (the two `collisionPlane` bits of the tile).
*Lethal* is **not** an engine flag — it's a game-side decision: most
spike behaviour lives in `obj/Spikes.c` of the Sonic-Mania-Decompilation
side, which uses ObjectTileCollision to find a surface and then applies
hurt logic from the object code, not the tile mask.

This matters for the Saturn port: the engine catalogue here only owes
"is solid in plane X" lookups; hazard logic must come from the
game-decomp catalog the parallel agent is producing.

---

## §6 Audio

Source: `RSDKv5/RSDK/Audio/Audio.{cpp,hpp}`.
URL: <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Audio/Audio.cpp>
URL: <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Audio/Audio.hpp>

### 6.1 Constants (Audio.hpp)

| Constant         | Value     | Line |
|------------------|-----------|------|
| `SFX_COUNT`      | 0x100     | 12   |
| `CHANNEL_COUNT`  | 0x10 (16) | 13   |
| `MIX_BUFFER_SIZE`| 0x800     | 15   |
| `AUDIO_FREQUENCY`| 44100 Hz  | 18   |
| `AUDIO_CHANNELS` | 2 (stereo)| 19   |

### 6.2 Structs (Audio.hpp:17-36)

```
SfxInfo {
    RETRO_HASH_MD5 hash;
    float *buffer;
    size_t length;
    int32 playCount;
    uint8 maxConcurrentPlays;
    uint8 scope;
}
ChannelInfo {
    float *samplePtr;
    float pan;
    float volume;
    int32 speed;
    size_t sampleLength;
    int32 bufferPos;
    int32 playIndex;
    uint32 loop;        # loop frame index, or 0xFFFFFFFF = no loop
    int16 soundID;
    uint8 priority;
    uint8 state;
}
```

ChannelStates (Audio.hpp:38):

```
CHANNEL_IDLE, CHANNEL_SFX, CHANNEL_STREAM,
CHANNEL_LOADING_STREAM, CHANNEL_PAUSED
```

### 6.3 BGM streaming (PlayStream, Audio.cpp:274-319)

`PlayStream(filename, slot, startPos, loopPoint, loadASync)`:

1. Acquire a channel — caller-specified `slot` or first idle channel.
2. Set its state to `CHANNEL_LOADING_STREAM`.
3. Delegate to `AudioDevice::HandleStreamLoad()` which (on PC) opens the
   OGG via stb_vorbis. Loop points are stored as frame indices; when
   playback reaches the end, `stb_vorbis_seek_frame()` repositions to
   the loop point if loop ≠ 0xFFFFFFFF.
4. Returns the channel index.

**There is no engine-side `TRACK_STAGE` enum** — that's a *game-side*
abstraction the Sonic Mania user-code adds. The Saturn port's TRACK_STAGE
slot is just "channel 0" by your existing convention, and the per-track
loop points come from `tools/loops.json` per the "BGM loop points
hand-curated" memory.

### 6.4 SFX (PlaySfx, Audio.cpp:425-483)

`PlaySfx(sfxID, loopPoint, priority)` — picks a channel by:
1. First idle channel, OR
2. If `sfxList[sfxID].playCount >= maxConcurrentPlays`, evict the
   oldest playing instance of the same sfxID, OR
3. Replace the lowest-priority playing channel.

`StopSfx(sfxID)`, `SetChannelAttributes(channel, volume, pan, speed)`,
`StopChannel/PauseChannel/ResumeChannel` round out the API. No
duck/sidechain mixing — volumes are manual.

---

## §7 Input

Source: `RSDKv5/RSDK/Input/Input.{cpp,hpp}`.
URL: <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Input/Input.cpp>
URL: <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Input/Input.hpp>

### 7.1 Constants (Input.hpp:12-26)

```
PLAYER_COUNT = 4
INPUT_DEADZONE = 0.3

InputIDs:        INPUT_UNASSIGNED=-2, INPUT_AUTOASSIGN=-1, INPUT_NONE=0
InputSlotIDs:    CONT_ANY=0, CONT_P1=1, CONT_P2=2, CONT_P3=3, CONT_P4=4
```

### 7.2 KeyMasks (Input.hpp:99-116)

Bit-shift constants `KEYMASK_UP` through `KEYMASK_TRIGGERR`, values
`1<<0` through `1<<17`. Order: UP, DOWN, LEFT, RIGHT, A, B, C, X, Y, Z,
START, SELECT, STICKL, STICKR, BUMPERL, BUMPERR, TRIGGERL, TRIGGERR.

### 7.3 ControllerState (Input.hpp:150-171)

```
struct InputState { bool32 down; bool32 press; }   // edge + level

ControllerState {
    InputState keyUp, keyDown, keyLeft, keyRight;
    InputState keyA, keyB, keyC, keyX, keyY, keyZ;
    InputState keyStart, keySelect;
    # REV01+ additions:
    InputState keyBumperL, keyBumperR, keyTriggerL, keyTriggerR;
    InputState keyStickL, keyStickR;
}
```

Mania uses A/B/X (jump/jump/jump aliases) on every controller, with
START to pause. The Saturn port maps Saturn-pad A/B/C → keyA/B/C and the
Start button → keyStart; X/Y/Z and shoulder triggers are unused by
shipping game-code (but still allocated in struct for forward compat).

### 7.4 Button state machine (paraphrased Input.cpp)

Per frame, per button:

```
if (press && down)        press = false   # press is edge-triggered;
                                          # consumed after one frame
else if (press && !down)  down  = true
else if (!press)          down  = false   # released
```

Raw sample comes from the device backend (`UpdateInput()` per device).
Auto-assign: any slot with `inputSlots[i] == INPUT_AUTOASSIGN` gets
bound to the first device that produces input via
`GetAvaliableInputDevice()` (the typo is in the engine).

---

## §8 Scene management

Source: `RSDKv5/RSDK/Scene/Scene.{cpp,hpp}`.

### 8.1 SceneInfo (Scene.hpp:67-86)

```
SceneInfo {
    Entity         *entity;        # current entity for callback
    SceneListEntry *listData;
    SceneListInfo  *listCategory;
    int32  timeCounter, currentDrawGroup, currentScreenID;
    uint16 listPos, entitySlot, createSlot, classCount;
    bool32 inEditor, effectGizmo, debugMode, useGlobalObjects, timeEnabled;
    uint8  activeCategory, categoryCount, state, filter (REV02),
           milliseconds, seconds, minutes;
}
SceneListEntry { RETRO_HASH_MD5 hash; char name[0x20], folder[0x10], id[8];
                 uint8 filter; }    # filter REV02 only
SceneListInfo  { RETRO_HASH_MD5 hash; char name[0x20];
                 uint16 sceneOffsetStart, sceneOffsetEnd; uint8 sceneCount; }
ScrollInfo     { int32 tilePos, parallaxFactor, scrollSpeed, scrollPos;
                 uint8 deform, unknown; }
```

### 8.2 Engine states (Scene.hpp:44-57)

| Value | Name                       |
|-------|----------------------------|
| 0     | ENGINESTATE_LOAD           |
| 1     | ENGINESTATE_REGULAR        |
| 2     | ENGINESTATE_PAUSED         |
| 3     | ENGINESTATE_FROZEN         |
| 4     | ENGINESTATE_STEPOVER (OR-flag) |
| 8     | ENGINESTATE_DEVMENU        |
| 9     | ENGINESTATE_VIDEOPLAYBACK  |
| 10    | ENGINESTATE_SHOWIMAGE      |
| 11    | ENGINESTATE_ERRORMSG       |
| 12    | ENGINESTATE_ERRORMSG_FATAL |
| 13    | ENGINESTATE_NONE           |

### 8.3 LoadScene flow (Scene.cpp, paraphrased)

```
LoadSceneFolder():
    1. Drain all draw groups; reset entity slots.
    2. Unload previous: scene 3D meshes, sprite atlases, surfaces,
       sound effects, music streams, palettes.
    3. LoadGameConfig() once at boot.
    4. LoadStageConfig(): reads Data/Stages/<folder>/StageConfig.bin
       (sig RSDK_SIGNATURE_CFG). Decodes the global-objects flag, stage
       object list (MD5 hashes), palette banks (per-row activate masks +
       RGB triples → 16-bit), and the per-stage SFX manifest.
    5. LoadTileConfig(): reads Data/Stages/<folder>/TileConfig.bin
       (sig RSDK_SIGNATURE_TIL). Builds the 4 collision masks + 4 flip
       variants per tile (1024 tiles × 8 mask views).
    6. LoadSceneFile(): reads Data/Stages/<folder>/Scene<id>.bin
       (sig RSDK_SIGNATURE_SCN) — see §1.4.
    7. For every registered class with a stageLoad callback, invoke it.
    8. Filter pass (REV02): walk entities, hide/dedup by filter byte.
```

### 8.4 Top frame loop

In `RunRetroEngine()` (main.cpp via Reader.cpp delegation):

```
while !engine.quit:
    sample input
    switch engine.state:
        ENGINESTATE_LOAD:        LoadSceneFolder(); -> REGULAR
        ENGINESTATE_REGULAR:     ProcessObjects(); ProcessParallax();
                                 ProcessObjectDrawLists(); FlipScreen();
        ENGINESTATE_PAUSED:      only ACTIVE_PAUSED + ACTIVE_ALWAYS run;
                                 still re-renders
        ENGINESTATE_FROZEN:      no Update; still re-renders
        ENGINESTATE_VIDEOPLAYBACK / ENGINESTATE_SHOWIMAGE: dedicated paths
    SwapBuffers(); wait for vblank
```

---

## §9 Main loop

Source: `RSDKv5/main.cpp`.
URL: <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/main.cpp>

### 9.1 Boot sequence (paraphrased)

```
Platform entry (WinMain / android_main / SDL_main / main):
    Set up env-vars (working dir, GLES hints).
    RSDK::InitCoreAPI()             # platform layer init
    RSDK::RunRetroEngine(argc, argv)
        -> InitEngine()
            -> ReadSettings("settings.ini")
            -> LoadGameConfig("Data/Game/GameConfig.bin")
                # GameConfig.bin contains: global-object list,
                # palette presets, scene category list, sfx list
            -> InitRenderDevice()    # GL/DX/Vulkan/SDL
            -> InitAudioDevice()
            -> InitInputDevices()
            -> StartGameObjects()    # RegisterObject for every class
            -> sceneInfo.state = ENGINESTATE_LOAD
            -> sceneInfo.activeCategory = startup category
        -> while !quit: tick frame loop (see §8.4)
```

### 9.2 V-blank / frame pacing

PC engine sleeps to maintain 60 Hz on the main thread via the platform
swap (SwappyGL on Android pinning to `SWAPPY_SWAP_60FPS`,
`SDL_GL_SetSwapInterval(1)` on desktop, etc.). All game logic is
deterministic per frame; there is no variable timestep or interpolation —
this is critical for the Saturn port: **60 Hz lock equals NTSC vblank,
50 Hz on PAL means tunables must be scaled. The shipping Mania project
targets NTSC only.**

There is no separate `engine.cfg` — boot reads `settings.ini`
(user/window prefs) + `Data/Game/GameConfig.bin` (asset/scene manifest).

---

## §10 Save system

Source: `RSDKv5/RSDK/User/Core/UserStorage.cpp`.
URL: <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/User/Core/UserStorage.cpp>

### 10.1 LoadUserFile / SaveUserFile (UserStorage.cpp:1290-1350)

Plain binary fopen/fread/fwrite with optional pre/post callbacks. No
encryption applied at this layer.

### 10.2 UserDB binary format (UserStorage.cpp:195-285)

The "save game" is a **UserDB** — a row/column key-value table:

```
offset  size   field
------  ----   ----------------------------------------
0x00    4      signature   RETRO_USERDB_SIGNATURE
0x04    4      usedSize    (bytes of meaningful data)
0x08    2      rowCount
0x0A    1      columnCount
per column:
        1      typeID      (VAR_UINT8/UINT32/INT32/STRING/COLOR)
        16     name        (null-padded ASCII)
per row:
        4      uuid
        N      tm createTime   (platform-sized C struct tm)
        N      tm changeTime
   per column:
        1      valueSize
        size   value bytes
remainder zeroed
```

No encryption; no compression. The signature acts as integrity check
only.

### 10.3 SaveGameProgress (high-level)

The engine layer provides primitives; the *game* (Sonic-Mania-Decomp
side) decides what to persist. Typical contents are: medal count, time
attack rows, character unlocks, last-played zone, blue-spheres rank
table. The Saturn port can emulate this entirely in **backup RAM**
(64 KB per slot), with the UserDB table sized down — fits trivially
within the partition's 1 KB hand-curated record per save.

---

## §11 Public RSDK API table

Source: `RSDKv5/RSDK/Core/Link.cpp` (function-pointer assignment block at
≈L91-467).
URL: <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Core/Link.cpp>

This is the complete API surface that the game code (Sonic-Mania-Decomp
side) calls via `RSDK.<name>`. The Saturn port must provide an
implementation of **every entry** listed here, even if some are stubs
(e.g. video, 3D, leaderboards). Names below are grouped logically.

**Registration & lifecycle**
RegisterGlobalVariables, RegisterObject, RegisterStaticVariables.

**Scene management**
SetScene, SetEngineState, ForceHardReset, CheckValidScene,
CheckSceneFolder, LoadScene, FindObject.

**Entities**
GetActiveEntities, GetAllEntities, BreakForeachLoop, SetEditableVar,
GetEntity, GetEntitySlot, GetEntityCount, GetDrawListRefSlot,
GetDrawListRef, ResetEntity, ResetEntitySlot, CreateEntity, CopyEntity,
CheckOnScreen, CheckPosOnScreen, AddDrawListRef, SwapDrawListEntries,
SetDrawGroupProperties.

**Cameras**
ClearCameras, AddCamera.

**Math**
Sin1024/Cos1024/Tan1024/ASin1024/ACos1024,
Sin512/Cos512/Tan512/ASin512/ACos512,
Sin256/Cos256/Tan256/ASin256/ACos256,
Rand, RandSeeded, SetRandSeed, ArcTanLookup.

**Matrix (only used by 3D scenes — Special Stage)**
SetIdentityMatrix, MatrixMultiply, MatrixTranslateXYZ, MatrixScaleXYZ,
MatrixRotateX/Y/Z/XYZ, MatrixInverse, MatrixCopy.

**Strings**
InitString, CopyString, SetString, AppendString, AppendText,
LoadStringList, SplitStringList, GetCString, CompareStrings.

**Display / video**
GetDisplayInfo, GetWindowSize, SetScreenSize, SetClipBounds,
SetScreenVertices, UpdateGameWindow, LoadVideo, LoadImage.

**Sprites / animation**
LoadSpriteSheet, LoadSpriteAnimation, CreateSpriteAnimation,
SetSpriteAnimation, EditSpriteAnimation, SetSpriteString,
FindSpriteAnimation, GetFrame, GetHitbox, GetFrameID, GetStringWidth,
ProcessAnimation.

**Palette**
SetTintLookupTable, SetPaletteMask, SetPaletteEntry, GetPaletteEntry,
SetActivePalette, CopyPalette, LoadPalette, RotatePalette,
SetPaletteFade, BlendColors.

**Drawing**
DrawRectangle, DrawLine, DrawCircle, DrawCircleOutline, DrawFace,
DrawBlendedFace, DrawSprite, DrawDeformedSprite, DrawString, DrawTile,
CopyTile, DrawAniTile, DrawDynamicAniTile, FillScreen.

**3D (Phase Z — stub for now)**
LoadMesh, Create3DScene, Prepare3DScene, SetDiffuseColor,
SetDiffuseIntensity, SetSpecularIntensity, AddModelToScene,
SetMeshAnimation, AddMeshFrameToScene, Draw3DScene.

**Tile layers**
GetTileLayerID, GetTileLayer, GetLayerSize, GetTile, SetTile,
CopyTileLayer, ProcessParallax, GetScanlines.

**Collision**
CheckObjectCollisionTouch, CheckObjectCollisionCircle,
CheckObjectCollisionBox, CheckObjectCollisionPlatform,
ObjectTileCollision, ObjectTileGrip, ProcessObjectMovement,
SetupCollisionConfig, SetPathGripSensors, FloorCollision, LWallCollision,
RoofCollision, RWallCollision, FindFloorPosition, FindLWallPosition,
FindRoofPosition, FindRWallPosition, GetTileAngle, SetTileAngle,
GetTileFlags, SetTileFlags, CopyCollisionMask, GetCollisionInfo.

**Audio**
GetSfx, PlaySfx, StopSfx, PlayStream, SetChannelAttributes, StopChannel,
PauseChannel, ResumeChannel, SfxPlaying, ChannelActive, GetChannelPos,
StopAllSfx.

**Input**
GetInputDeviceID, GetFilteredInputDeviceID, GetInputDeviceType,
IsInputDeviceAssigned, GetInputDeviceUnknown, InputDeviceUnknown1/2,
GetInputSlotUnknown, InputSlotUnknown1/2, AssignInputSlotToDevice,
IsInputSlotAssigned, ResetInputSlotAssignments, GetUnknownInputValue.

**Save / user**
LoadUserFile, SaveUserFile.

**Debug / editor (strip on Saturn)**
SetActiveVariable, AddEnumVariable, PrintLog, PrintText, PrintString,
PrintUInt32, PrintInt32, PrintFloat, PrintVector2, PrintHitbox,
ClearViewableVariables, AddViewableVariable.

**Misc**
GetVideoSetting, SetVideoSetting, GetAPIFunction, NotifyCallback,
SetGameFinished.

---

## §12 Saturn-port mapping

| Engine concept                | Saturn implementation                                                                        |
|-------------------------------|----------------------------------------------------------------------------------------------|
| `Data.rsdk` pack file         | Stored as a single `DATA.RSD` ISO9660 file; lookup table read into HWRAM on boot. MD5 of path is computed via SH-2 software MD5 (one shot at boot, then cached as 32-bit FNV indices into a sorted secondary table for runtime perf). |
| `OpenFile` by hash            | Saturn `cd_block.c` seek + buffered read into `JO_GLOBAL_MEMORY` heap.                       |
| Scene.bin layers (RLE uint16) | Decompress to `RAM` once on `LoadScene`, then DMA tile-IDs to VDP2 pattern-name table (NBG0/NBG1 for two scrolling planes). |
| LAYER_HSCROLL / LAYER_VSCROLL | VDP2 NBG with `slCellScroll` updated per scanline in V-blank IRQ. **Already proven in this project** via the "VDP2 streaming SOLVED" memory note — `slDMAXCopy` (SCU DMA via cache-through alias) is mandatory to avoid the audio/scroll DMA conflict. |
| LAYER_ROTOZOOM                | VDP2 RBG0 with rotation parameter table updated each frame. (Phase Z — Special Stage only.) |
| LAYER_BASIC                   | VDP2 NBG with fixed scroll = 0.                                                              |
| Entity sprite blit            | VDP1 quad sprite (`slPutSprite` / `slDispSprite4` for distorted, normal sprites at 1× scale via `slPutSprite`). Pivot subtraction done in C, then quad coords pushed to VDP1 cmd list. |
| `FLIP_X` / `FLIP_Y`           | VDP1 character control word bits CL/CV (already supported natively).                         |
| `DrawSprite` rotation         | VDP1 distorted sprite primitive with the 4 vertices computed via `slCos`/`slSin` (256-step table — matches engine's `sin256LookupTable`). |
| `INK_ALPHA` (translucent)     | VDP1 mesh sprite (50% effective) OR VDP2 colour-calculation register set on the destination layer. |
| `INK_ADD`                     | VDP2 colour calc with "additive" mode; for sprites, gradient shading via gouraud-shaded quads on VDP1. |
| `FillScreen` / `Draw_FadeBlack` | VDP2 colour-offset register (per-layer R/G/B add/sub). Fade is a single register write per frame, no per-pixel cost. |
| `SetClipBounds`               | VDP1 user-clip command + per-tile mask. Per-sprite cull done in C before push.               |
| `ProcessAnimation`            | Pure C — identical to PC behaviour.                                                          |
| Animation sheet pixels        | Cached to VDP1 charpat RAM (512 KB) in 16-color paletted format. Mania sprites are 4bpp on disk → 4bpp on Saturn. |
| Palette banks (8×256)         | VDP2 CRAM banks (2048 colours total). Per-layer palette offset register selects bank.        |
| `SetPaletteFade`              | VDP2 colour-calculation gradient OR per-CRAM-row writes from a fade LUT.                     |
| `RotatePalette`               | Plain CRAM write per frame (water shimmer, lava, etc.).                                      |
| `PlayStream` (BGM)            | CD-DA tracks per existing `tools/loops.json` curation. `cdda_play_track()` with loop point.  |
| `PlaySfx`                     | SCSP PCM via `slSndPCMOn`. Up to 16 channels native = matches `CHANNEL_COUNT=0x10`.          |
| `ProcessInput`                | SMPC peripheral read in V-blank IRQ; map Saturn-pad bits → `ControllerState.keyA/B/C/Start/...`. |
| `Sin256` / `Cos256`           | SGL `slSin` / `slCos` (already exposed, exact equivalents).                                  |
| `Sin1024` / `Cos1024`         | Locally generated table; tiny (4 KB) — placed in HWRAM at boot.                              |
| MD5 hashing                   | One-shot SH-2 software MD5 from `tools/`-generated header; runtime never hashes live paths.  |
| Save data (UserDB)            | Backup RAM (2 KB per Saturn save slot). UserDB fields trimmed to: medal-count, character-unlocks, last-played zone, time-attack top-3 per stage. |
| `ProcessObjects` frame loop   | Identical C; SH-2 master CPU runs Update/LateUpdate, slave CPU is reserved for VDP1 cmd-list build + audio mixing. |
| Foreach stack (0x400)         | Identical. Stack lives in LWRAM (1 MB).                                                      |

---

## §13 Phase Z — Saturn-native rewrite scope

Per the project mandate, these subsystems get a **clean-slate Saturn
implementation** in the final phase. Phase A's job is just to stub them
out behind the same API surface so Phase Z can swap in real behaviour.

### 13.1 3D rendering primitives

Used by: Special Stage path (the rotating-orb chase). API surface:
`LoadMesh`, `Create3DScene`, `Prepare3DScene`, `SetDiffuseColor`,
`SetDiffuseIntensity`, `SetSpecularIntensity`, `AddModelToScene`,
`SetMeshAnimation`, `AddMeshFrameToScene`, `Draw3DScene`.

**Phase Z migration path:**
- Replace the engine's software-rasterised mesh pipeline with SGL
  `slPutPolygon`/`slPutPolygon4` calls and a hand-built display list.
- The MDL/BIN mesh format ships as 16-bit indexed verts; Saturn fixed
  point is 12.20 inside `slDispCommand` — convert at load time.
- Lighting reduces to per-vertex gouraud (VDP1 already supports it via
  the colour-gradient table). `SetSpecularIntensity` becomes a stub (no
  Saturn hardware spec channel).
- `AddMeshFrameToScene` is keyframe-based animation; convert PC mesh
  frame indices to Saturn-friendly contiguous-vert arrays in tools, NOT
  at runtime.

### 13.2 Scanline FX

Used by: water reflections, heat haze, certain bosses' rumble FX.
Engine API: per-scanline `ScrollInfo` modulation in `ProcessParallax`,
`SetTintLookupTable` for monochrome bands, `gfxLineBuffer` indirection
for swapped palettes per line.

**Phase Z migration path:**
- VDP2 line-scroll table (`slLineScrollTable` for NBG0 and NBG1) is the
  exact Saturn analog of `layer->lineScroll`.
- VDP2 vertical cell scroll table covers vertical-deform FX.
- For the *waterDrawPos* split (different colour calc above/below a Y
  line), use VDP2 window registers to mask the upper-half of the screen
  out of the colour-calc input. This is hardware free — set once per
  frame from `currentScreen->waterDrawPos`.
- "Tint" (monochrome strip during invulnerability flash) is VDP2
  colour-offset register pulsed once per frame.

### 13.3 Palette FX

Engine API: `SetPaletteFade(destBank, srcBankA, srcBankB, blendAmount,
startIndex, endIndex)`, `RotatePalette(bank, startIndex, endIndex, right)`,
`BlendColors(destBank, srcA, srcB, blendAmount, start, count)`,
`SetTintLookupTable(table)`.

**Phase Z migration path:**
- `SetPaletteFade`: precompute the 0..255 blend ramp into a 16 KB LUT in
  HWRAM at boot. Each frame, VBlank-IRQ does a single DMA copy of the
  current ramp slice into CRAM. Zero-CPU-cost during gameplay.
- `RotatePalette`: SH-2 in-place ring-shift inside CRAM is fast enough
  (< 200 cycles for a typical 16-entry rotation). Currently the
  Saturn port already does this for the water shimmer.
- `BlendColors`: weighted average on R/G/B 5-bit lanes, software loop on
  SH-2; cache the result.
- `SetTintLookupTable` (monochrome): Saturn equivalent is a CRAM bank
  pre-populated with the desaturated palette, swapped in via VDP2 colour
  bank register — instantaneous.

---

## Appendix A — File-format cheat sheet for Saturn loaders

| File              | Signature (LE)             | First useful field                |
|-------------------|----------------------------|------------------------------------|
| `Data.rsdk`       | "RSDK" 0x4B445352          | 'v' + version byte                 |
| `GameConfig.bin`  | 0x474643 ("CFG\0")         | useGlobalObjects + objectCount     |
| `StageConfig.bin` | 0x474643 ("CFG\0") same    | useGlobalObjects + stageObjectCount|
| `TileConfig.bin`  | 0x4C4954 ("TIL\0")         | 0x400 tiles × per-tile config      |
| `Scene<id>.bin`   | 0x4E4353 ("SCN\0")         | editor block then layerCount       |
| `<anim>.bin`      | RSDK_SIGNATURE_SPR         | totalFrameCount uint32             |
| `Strings/*.txt`   | (none — BOM-detected)      | char data                          |

## Appendix B — `tools/dump_data_rsdk.py` (proposed helper, NOT YET created)

The format documented in §1 is sufficient to write a standalone Python
extractor that does NOT depend on the engine binary. Outline:

```python
# tools/dump_data_rsdk.py - proposed; create on demand
import struct, hashlib, sys
SIG = 0x4B445352
def md5_bytes(path: str) -> bytes:
    return hashlib.md5(path.lower().encode()).digest()
def extract(rsdk_path, manifest, outdir):
    with open(rsdk_path, "rb") as f:
        sig, vb, vc, count = struct.unpack("<I B B H", f.read(8))
        assert sig == SIG, "not a Data.rsdk"
        entries = []
        for _ in range(count):
            h = f.read(16)
            off, sz = struct.unpack("<I I", f.read(8))
            entries.append((h, off, sz & 0x7FFFFFFF, bool(sz & 0x80000000)))
        # manifest is list of candidate path strings
        m = {md5_bytes(p): p for p in manifest}
        for h, off, sz, enc in entries:
            path = m.get(h, f"unknown/{h.hex()}.bin")
            f.seek(off); data = f.read(sz)
            if enc:
                raise RuntimeError(f"encrypted entry not implemented: {path}")
            out = f"{outdir}/{path}"
            os.makedirs(os.path.dirname(out), exist_ok=True)
            open(out, "wb").write(data)
```

Existing project script `tools/build_filelist.py` already produces the
manifest (per the "RSDK extract: Mania naming conventions" memory) —
this proposed helper would consume that manifest plus the byte format
documented here.

---

## Citations (raw URLs, verified 2026-05-26)

- Reader.cpp pack format and OpenDataFile:
  <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Core/Reader.cpp>
- Reader.hpp signatures and structs:
  <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Core/Reader.hpp>
- Scene.cpp LoadSceneFile, Scene.bin parse:
  <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Scene/Scene.cpp>
- Scene.hpp constants, EngineStates, SceneInfo:
  <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Scene/Scene.hpp>
- Object.cpp ProcessObjects, CreateEntity, foreach:
  <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Scene/Object.cpp>
- Object.hpp ACTIVE_* enum, Entity, ObjectClass:
  <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Scene/Object.hpp>
- Collision.cpp sensors and slope logic:
  <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Scene/Collision.cpp>
- Collision.hpp sensor struct, TILECOLLISION enums:
  <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Scene/Collision.hpp>
- Animation.cpp LoadSpriteAnimation, ProcessAnimation:
  <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Graphics/Animation.cpp>
- Animation.hpp animator + rotation enums:
  <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Graphics/Animation.hpp>
- Drawing.hpp screen, draw lists, ink/flip:
  <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Graphics/Drawing.hpp>
- Drawing.cpp FillScreen, compositor:
  <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Graphics/Drawing.cpp>
- Palette.hpp banks + dimensions:
  <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Graphics/Palette.hpp>
- Palette.cpp LoadPalette, BlendColors, SetPaletteFade:
  <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Graphics/Palette.cpp>
- Math.hpp TO_FIXED / sin tables:
  <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Core/Math.hpp>
- Audio.hpp ChannelInfo, SFX_COUNT/CHANNEL_COUNT:
  <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Audio/Audio.hpp>
- Audio.cpp PlayStream, PlaySfx:
  <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Audio/Audio.cpp>
- Input.hpp ControllerState, KeyMasks, PLAYER_COUNT:
  <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Input/Input.hpp>
- Input.cpp ProcessInput:
  <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Input/Input.cpp>
- UserStorage.cpp save format:
  <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/User/Core/UserStorage.cpp>
- Text.cpp strings:
  <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Storage/Text.cpp>
- Storage.cpp + Storage.hpp dataset memory pools:
  <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Storage/Storage.cpp>
  <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Storage/Storage.hpp>
- RetroEngine.hpp RetroEngine struct, platform enums:
  <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Core/RetroEngine.hpp>
- Link.cpp public RSDKFunctionTable assignment:
  <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/RSDK/Core/Link.cpp>
- main.cpp boot entry:
  <https://raw.githubusercontent.com/RSDKModding/RSDKv5-Decompilation/master/RSDKv5/main.cpp>
