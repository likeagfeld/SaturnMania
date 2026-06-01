# Archived working build — init sequence (control artifact)

Source: `src/_archived/main.c.v01-handrolled` (1972 lines).
This file SHIPPED visible VDP1 sprite overlays in qa_gate.png.
Below is the EXACT call order from `jo_main` entry to first frame, with file:line.

## jo_main (1910..1972) — ordered Saturn-hardware-state-mutating calls

| # | line | call | args / notes |
|---|------|------|--------------|
| 1 | 1919 | `jo_core_init` | `JO_COLOR_RGB(96,128,224)` — back-color = title sky-blue |
| 2 | 1927 | `slPriorityNbg0` | `0` (hide all NBGs immediately to mask cold-boot VRAM garbage) |
| 3 | 1928 | `slPriorityNbg1` | `0` |
| 4 | 1929 | `slPriorityNbg2` | `0` |
| 5 | 1930 | `slPriorityNbg3` | `0` |
| 6 | 1935 | `intro_video_play("INTRO.CPK")` | Cinepak; takes VDP2, then releases. Side-effects on VDP1/VDP2 state. |
| 7 | 1937 | `setup_ghz_foreground()` | calls jo_create_palette_from, jo_vdp2_set_nbg1_8bits_image, **slPriorityNbg1(6)** (line 309), slDMACopy cel-bank (line 318) |
| 8 | 1938 | `setup_collision_world()` | pure data load (jo_fs_read_file SURF.BIN). NO hardware writes. |
| 9 | 1942 | `setup_title_bg()` | TITLE.PAL→jo_create_palette_from, TITLE.DAT→jo_vdp2_set_nbg2_8bits_image, slScrPosNbg2(0,0) (line 417) |
| 10 | 1943 | `slPriorityNbg1` | `0` (hide GHZ FG during title) |
| 11 | 1947 | `slPriorityNbg2` | `5` (title backdrop visible, but BELOW VDP1 sprite priority 6) |
| 12 | 1953 | `jo_audio_play_cd_track` | track 3 (TitleScreen BGM) |
| 13 | 1955 | `load_sonic_sprites()` | jo_sprite_add for idle + walk frames |
| 14 | 1956 | `load_rings()` | jo_sprite_add ring frames |
| 15 | 1957 | `load_badniks()` | jo_sprite_add motobug |
| 16 | 1958 | `load_objects()` | jo_sprite_add springs/monitors/signpost |
| 17 | 1959 | `load_hud_digits()` | jo_sprite_add DIGITS.SPR |
| 18 | 1960 | `load_title_sprites()` | jo_sprite_add MLOGO/MPRESS/MWINGS/MRIBBON/MRING/MRIBSIDE, title_sonic_load() (TSONIC.ATL) |
| 19 | 1961 | `setup_audio()` | jo_audio_init + PCM load |
| 20 | 1962 | `sms_save_init()` | no-op |
| 21 | 1969 | `jo_core_add_vblank_callback(fg_vblank)` | DMA page to VRAM in vblank |
| 22 | 1970 | `jo_core_add_callback(game_tick)` | per-frame tick |
| 23 | 1971 | `jo_core_run()` | enter main loop |

## Key embedded hardware writes inside callees

| where | line | call | notes |
|-------|------|------|-------|
| setup_ghz_foreground | 309 | `slPriorityNbg1(6)` | initial NBG1 priority |
| setup_ghz_foreground | 318 | `slDMACopy(cel, nbg1_cell, n*64)` | upload cell bank |
| setup_title_bg | 417 | `slScrPosNbg2(0,0)` | reset scroll |
| setup_ghz_sky (gameplay) | 358 | `slScrAutoDisp(NBG1ON)` | drop NBG2ON for swap |
| setup_ghz_sky (gameplay) | 367 | `slScrAutoDisp(NBG1ON|NBG2ON)` | re-enable |
| fg_vblank (vblank cb) | 264 | `slDMAXCopy(...|0x20000000, nbg1_map, ..., Sinc_Dinc_Long)` | SCU DMA, cache-through alias |
| fg_vblank | 268 | `slScrPosNbg1(...)` | per-frame scroll |

## What is NOT called in the archived build (notable absences)

- No explicit `slTVDisp` (jo_core_init handles)
- No explicit `slPrioritySpr0..3` calls (defaults relied on)
- No `slBackColSet` / `slBack1ColSet` (jo_core_init sets back-color via JO_COLOR_RGB)
- No `slColRAMMode` (jo_core_init defaults)
- No `slColorCalcOn` / `slColorCalcMode`
- No `slZBufferUse`
- No `slShadowOn`
- No `slCurUserSystem`
- No `slSpriteBuf` calls
- No `slPerspective` / `slLookR` / `slWindow`

**Key inference:** the archived build relies on `jo_core_init` defaults for VDP1 sprite priority (which default to 6, allowing sprites to render above NBG2 at priority 5). Sprites get added via `jo_sprite_add` BEFORE `jo_core_run`, and drawn from `game_tick` via `jo_sprite_draw3D`. No explicit VDP1 register manipulation is needed.
