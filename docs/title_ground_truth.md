# Title Scene — Ground Truth (from Mania decomp + Scene1.bin entities)

Source: github.com/RSDKModding/Sonic-Mania-Decompilation/master/SonicMania/Objects/Title/{TitleSetup,TitleLogo,TitleSonic}.c, and `extracted/Data/Stages/Title/Scene1.bin` parsed by `tools/parse_title_entities.py` (consumes 2589/2589 bytes cleanly).

## Entity world coordinates (Y top-down)

| Entity | World X | World Y | jo (X-256, Y-112) | Anim | Render |
|---|---|---|---|---|---|
| EMBLEM (Logo Wings) | 256 | 108 | (0, -4) | 0 | FLIP_NONE + FLIP_X (both wings) |
| RIBBON | 256 | 144 | (0, 32) | 1 (main) + 3 (center) | FLIP_X + FLIP_NONE; center anim 3 only post-flash |
| GAMETITLE (MANIA wordmark) | 256 | 182 | (0, 70) | 4 | Single, no flip, **frame 0 only** (frame 1 = DISCOVERY is Plus-only) |
| POWERLED | 256 | 154 | — | 5 | **Destroyed at electricity frame 31 — NEVER on final title** |
| COPYRIGHT | 432 | 224 | (176, 112) | 6 | Bottom-right corner (off-screen on narrow 320 unless camera adjusted) |
| RINGBOTTOM | 256 | 148 | (0, 36) | 7 | Single, no flip |
| PRESSSTART | 256 | 212 | (0, 100) | 8 | Square wave: drawn only when `!(timer & 0x10)` (16 on / 16 off) |
| TitleSonic | 252 | 104 | (-4, -8) | sonic + finger | Clip Y < 160; finger anim plays after sonic anim ends |

## Camera convention

`TitleSetup_Update` forces `ScreenInfo->position.x = 256 - ScreenInfo->center.x` each frame. For Saturn 320x224, center.x = 160 → camera_x = 96. World→screen: `screen_x = world_x - 96`, `screen_y = world_y` (no Y offset). World X 256 = screen center 160.

For jo's center-anchored draw3D: `jo_x = world_x - 256, jo_y = world_y - 112`.

## State machine timing

```
State_Wait        : 64 frames (timer 1024 → -1024 at step 16)
                    -> at end: Music_PlayTrack(TRACK_STAGE), State_AnimateUntilFlash
                    stateDraw = Draw_DrawRing (electricity ring)
State_AnimateUntilFlash : self->animator advances; at frameID==31:
                          - EMBLEM + RIBBON: visible=true
                          - POWERLED: destroyEntity
                          - state → FlashIn
State_FlashIn     : animator advances to last frame:
                    - GAMETITLE + COPYRIGHT + RINGBOTTOM: visible=true
                    - RIBBON: showRibbonCenter=true; animator swaps to anim 2 (Ribbon Wave)
                    - TitleSonic: visible=true
                    - TitleBG_SetupFX (pal[55]=0x202030 mask 0x00FF00; clouds/island scanline FX)
                    - timer=768, stateDraw=Draw_Flash (white 0xF0F0F0)
                    - state → WaitForSonic
State_WaitForSonic: 768 frames countdown
                  → SetupLogo
State_SetupLogo   : timer counts up to 120
                    - At 120: PRESSSTART visible=true (Plus: SetupPressStart)
                    - state → WaitForEnter
State_WaitForEnter: 800-frame timeout → FadeToVideo (attract video)
                    Any button press → PlaySfx(MenuAccept); FadeToMenu
```

## Per-type Draw() (TitleLogo.c verbatim, lines 35–75)

```c
case TITLELOGO_EMBLEM:
    RSDK.SetClipBounds(0, 0, 0, ScreenInfo->size.x, ScreenInfo->size.y);
    self->direction = FLIP_NONE;
    RSDK.DrawSprite(&self->mainAnimator, NULL, false);
    self->direction = FLIP_X;
    RSDK.DrawSprite(&self->mainAnimator, NULL, false);
    break;

case TITLELOGO_RIBBON:
    self->direction = FLIP_X;
    RSDK.DrawSprite(&self->mainAnimator, NULL, false);
    self->direction = FLIP_NONE;
    RSDK.DrawSprite(&self->mainAnimator, NULL, false);
    if (self->showRibbonCenter)
        RSDK.DrawSprite(&self->ribbonCenterAnimator, NULL, false);
    break;

case TITLELOGO_PRESSSTART:
    if (!(self->timer & 0x10))
        RSDK.DrawSprite(&self->mainAnimator, NULL, false);
    break;

default:
    RSDK.DrawSprite(&self->mainAnimator, NULL, false);
    break;
```

## Hash convention

RSDKv5 Mania scenes store **plain raw MD5** of the ASCII name as 16 bytes, no endian swap. Verified: `hashlib.md5(b"TitleLogo").digest()` == file bytes exactly.

## Critical port gotchas

1. **`filter` is implicit attribute index 0** — NOT stored in attribute table; iterate `varCount - 1` records.
2. POWERLED never renders on final title.
3. GAMETITLE frame 1 = "DISCOVERY" is Plus-only; ship frame 0 = "MANIA".
4. RIBBON center (anim 3 = "Ribbon Center" = SONIC wordmark on red banner) only appears AFTER FlashIn.
5. PRESSSTART blink is a 32-frame square wave (16 on / 16 off), starts ONLY after SetupLogo completes.
6. TitleSonic is clipped to screen Y < 160.
7. COPYRIGHT at world (432, 224) lands off-screen-right on 320-wide Saturn (world 432 - cam 96 = screen 336). Either shift it (e.g. world 304 → screen 208) or accept it's hidden.
8. The intro electricity ring uses `Title/Electricity.bin` (separate asset from Logo.bin) — not yet extracted.
