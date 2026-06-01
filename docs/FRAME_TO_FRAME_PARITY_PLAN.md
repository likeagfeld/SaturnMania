# Frame-to-Frame Parity with PC Steam Sonic Mania — Full-Scope Plan

**Date:** 2026-05-26
**Mandate (user, verbatim):** "you need to fully understand and recreate ALL
SPRITES loading the right time with the right animations in the right place
etc.... so that it is a frame to frame match to the steam version"

**Reference image acquired:** `tools/refs/mania_pc/title_full_archiveorg.jpg`
(Internet Archive, Sonic Team, "Sonic mania title screen.jpg")

This document is the complete-scope master plan. Frame-to-frame parity means:
- Every visible sprite from the original is present
- Every animation runs at the same speed and frame sequence
- Every layered draw order is preserved
- Every palette cycle, scanline deformation, INK_BLEND/ADD effect is
  reproduced or has a documented Saturn-equivalent
- The boot → intro → title → titlecard → gameplay → bossfight sequence
  matches the PC build's tick-by-tick behavior

This is a **substantial multi-iteration effort**. The current Saturn build
implements maybe 15% of the full game. The user wants 100%. The plan below
is the roadmap to get there.

---

## §0 — Reality check on scope

What the original Sonic Mania actually is, in numbers (from
`docs/mania_decomp_catalog.md` §1.1 and the cached decomp in `tools/_decomp_raw/`):

- **~700 RSDK object classes** registered in `Game.c` (every entity, badnik,
  gimmick, platform, item, particle, HUD widget)
- **13+ zones** (GHZ, CPZ, SPZ, FBZ, PGZ, SSZ, HCZ, MMZ, OOZ, LRZ, MSZ, TMZ, ERZ)
  × 2 acts each (typically) + bosses + special stages + ending
- **~30 unique badnik types** (Crabmeat, Buzzbomber, Motobug, Chopper,
  Newtron-Green, Newtron-Blue, BatBrain, Roller, Catakiller, etc.)
- **~25 unique gimmick types** (springs, monitors, signposts, bridges,
  collapsing platforms, loop physics, zip lines, checker balls, water,
  conveyor belts, fans, see-saws, etc.)
- **~6 player abilities** (run, jump, roll, spindash, peelout, drop dash)
- **~7 power-ups** (shield, fire shield, water shield, lightning shield,
  invincibility, super ring, 1-up, speed shoes)
- **~12 background-FX systems** (scanline cloud deformation, palette cycling,
  parallax layers, water reflection, etc.)

The Saturn port currently implements:
- 1 zone (GHZ Act 1) with placeholder gameplay
- 1 badnik (Motobug, static sprite)
- 3 gimmick stubs (spring, monitor, signpost)
- 4 player actions (walk, run, jump, roll — no spindash, peelout, drop dash)
- 0 power-ups
- 0 BG-FX (flat NBG2 only)
- 0 of the title-screen flourish (no intro Cinepak, no electricity arc, no
  white flash, no scanline-deformed clouds, no finger wave, no animated tiles)

So roughly **15% feature completion**.

**Frame-to-frame parity is a multi-month engineering effort.** I will be
honest about that up front. What I CAN do in a single ship-it-first-try
iteration is **Phase 1** below — make the title sequence frame-to-frame
correct. Subsequent phases require their own iterations.

---

## §1 — The TitleSonic frame-to-frame plan (Phase 1)

### 1.1 The shipped problem (current state, post my last edit)

What I shipped: a small `110×120` cropped head sprite (anim 0 frame 48)
with NO finger wave, NO entrance arc beyond what the atlas loaded for,
positioned at world (252, 104).

What it should be (per `tools/_decomp_raw/SonicMania_Objects_Title_TitleSonic.c`
lines 12-37, cross-referenced with `tools/refs/mania_pc/title_full_archiveorg.jpg`):

**Compound sprite:**
1. **Body sprite** = anim 0 current frame (49 frames, plays once 0→48 then
   holds on 48 forever)
2. **Finger sprite** = anim 1 current frame (12 frames, loops infinitely
   0→11→0→11→...), ONLY rendered AND processed when body frame == 48

**Clipping:** Y < 160 (sprite is masked off below scanline 160; this prevents
the body from drawing over the title logo + press start which sit below)

**Position:** Entity at world (252, 104). Both body and finger use the
SAME entity origin; their individual per-frame pivots offset the draw
top-left from that origin.

### 1.2 Body anim (anim 0) — 49 frames, total 177 ticks

Per `parse_spr('extracted/Data/Sprites/Title/Sonic.bin')`:
- Frame 0: 89×82, pivot=(-48,28), dur=64 — initial "anticipation" pose
  (off-screen / wound up)
- Frames 1-47: rapid succession dur=2-3 — the entrance arc
- Frame 48: 110×120, pivot=(-50,-91), dur=2, loop_index=48 — settled head
  pose (HOLDS on this frame forever after first hit)

The frames in the middle (frames 28-33 are the largest, ~200×134 px) are
the dramatic "leaping out" poses. The viewer sees: Sonic dynamically
emerges, hand extended, then settles into the small head pose where the
finger wave takes over.

### 1.3 Finger Wave anim (anim 1) — 12 frames, total 30 ticks per cycle, loops forever

Per parse_spr:
- Frame 0: 50×58, pivot=(6,-44), dur=4 — finger up
- Frame 1: 50×60, pivot=(6,-46), dur=3
- Frame 2: 50×62, pivot=(6,-48), dur=2
- Frame 3: 49×63, pivot=(6,-49), dur=2
- Frame 4: 48×63, pivot=(6,-49), dur=2
- Frame 5: 46×62, pivot=(6,-48), dur=2
- Frame 6: 45×60, pivot=(6,-46), dur=2 — finger down
- Frame 7: 46×62, pivot=(6,-48), dur=2
- Frame 8: 48×63, pivot=(6,-49), dur=2
- Frame 9: 49×63, pivot=(6,-49), dur=2
- Frame 10: 50×62, pivot=(6,-48), dur=2
- Frame 11: 50×60, pivot=(6,-46), dur=3 — back to start
- (loops 0→11 forever, loopIndex=0)

The 12-frame cycle is 30 ticks = 0.5 seconds at 60Hz. The finger waggles
twice per second — the iconic Mania title taunt.

### 1.4 Implementation contract (no code yet — design only)

**Atlas format v4** (`cd/TSONIC.ATL` extended):

```
Header:
  u16 magic            'TS' = 0x5453
  u16 version          0x0004                 (bumped from 3)
  u16 anim_count       2                      (Sonic body + Finger Wave)
  u16 palette_size     16
  u16 palette[16]      BGR1555, MSB=1 (opaque), slot 0 = transparent
  u32 total_pixel_bytes

For each anim (anim_count records):
  char  name[16]       'Sonic\0' or 'Finger Wave\0'
  u16   frame_count
  u16   loop_index
  u16   first_frame_index   (offset into the global frame-record table)

Global frame-record table (sum of frame_count across all anims records):
  u16  width            (padded to mult of 8)
  u16  height
  i16  pivot_x
  i16  pivot_y
  u16  duration
  u32  pixel_offset

Pixel pool: concat of all frames' nibble-packed pixel bytes
```

**Memory budget:**
- Anim 0: 49 frames × ~7,500 B/frame avg = ~367 KB
- Anim 1: 12 frames × ~3,000 B/frame avg = ~36 KB
- **Total: ~403 KB** of VDP1 user area (466 KB available) — leaves
  63 KB headroom (down from 94 KB pre-finger-wave, still safe)

**Runtime state in `title_sonic.c`:**
```c
static int g_ts_body_anim_id;   // 0
static int g_ts_finger_anim_id; // 1
static unsigned int g_ts_body_done_tick;   // tick when body first reached frame 48
static int g_ts_atlas_loaded;
```

**Draw logic (`title_sonic_draw(g_ticks, z)`):**
1. Compute `body_frame = current_frame_in_anim(0, g_ticks)`. If
   g_ticks ≥ 177, body_frame = 48 (held); record `g_ts_body_done_tick`
   the first time this happens
2. Set Saturn-equivalent of `SetClipBounds(Y < 160)`. For VDP1 sprites
   this means setting the system clip rectangle via slUserClip or
   slClipping for this sprite. The simpler Saturn-native solution is
   to NOT draw the sprite IF its centre_y > 160-h/2 (per-frame check).
3. Draw the body sprite at `(252 + pivot_x + W/2, 104 + pivot_y + H/2)`
4. If body_frame == 48:
   - `finger_anim_tick = g_ticks - g_ts_body_done_tick`
   - Compute `finger_frame = current_frame_in_anim(1, finger_anim_tick)`
     (with wraparound on the 30-tick cycle: `finger_anim_tick % 30`)
   - Draw the finger sprite at `(252 + finger_pivot_x + W/2, 104 + finger_pivot_y + H/2)`

### 1.5 Visual gate (Gate V1)

Threshold: SSIM ≥ 0.55 between Saturn capture and `tools/refs/mania_pc/
title_full_archiveorg.jpg`, masked to the title-ring region only.

SSIM 0.55 is achievable for "same composition, different color depth"
(Saturn uses 16-color quantized palette vs. PC's full RGB). It will FAIL
if:
- Sonic is missing
- Sonic is the wrong sprite (head-only vs. full body)
- Sonic is at the wrong position
- Finger wave is not animating (would show as a sequence of identical
  frames whose pixels differ subtly across captures)

### 1.6 Atlas builder changes (`tools/build_titlesonic_atlas.py`)

1. Iterate ALL anims (not just one)
2. Concatenate frames into a single global frame-record table
3. Track per-anim metadata: name, frame_count, loop_index, first_frame_idx
4. Emit v4 header
5. Verify VDP1 budget across the FULL atlas (both anims combined)

### 1.7 C-side rewrite

Already drafted in the prior iteration (uses
`jo_sprite_add_4bits_image`). Extensions for v4:
- Parse v4 header (read anim_count, per-anim metadata)
- Allocate sprite IDs per anim, store `base_id[anim_count]`
- Implement `current_frame_in_anim(anim_idx, tick)` walking that anim's
  cumulative duration table
- Wrap tick modulo cumulative duration for looping anims

---

## §2 — TitleLogo frame-to-frame plan (Phase 1.b)

### 2.1 What the decomp does — verbatim from
`tools/_decomp_raw/SonicMania_Objects_Title_TitleLogo.c`

There are **8 TitleLogo sub-types** (`TITLELOGO_*`):
- EMBLEM (anim 0) — the SEGA Sonic ring emblem; drawn FLIP_NONE + FLIP_X (mirrored)
- RIBBON (anim 1 = wave, anim 2 = mirror'd wave?, anim 3 = SONIC wordmark
  Center; drawn FLIP_X + FLIP_NONE then center if `showRibbonCenter`)
- GAMETITLE (anim type+2 = anim 4) — "MANIA" wordmark
- POWERLED (destroyed during AnimateUntilFlash) — not visible in final title
- COPYRIGHT (anim type+2 = anim 6) — © SEGA text bottom right
- RINGBOTTOM (anim type+2 = anim 7) — the bottom of the gold ring
- PRESSSTART (anim 8) — "Press Start" prompt with 11 locale variants
- PLUS (Plus DLC only) — skipped for our build (`MANIA_USE_PLUS = 0`)

### 2.2 What the Saturn port has

Five sprite stubs (`MLOGO.SPR`, `MWINGS.SPR`, `MSONIC.SPR`, `MRIBSIDE.SPR`,
`MRIBBON.SPR`, `MRING.SPR`, `MPRESS.SPR`). Each is a static single-frame
extraction. The RIBBON is supposed to be **3 animations** (the wave +
center wordmark). The PRESS START is supposed to have **11 locale variants**.
The COPYRIGHT and the proper RINGBOTTOM are likely wrong or missing.

### 2.3 What needs to change

1. **RIBBON animation:** RIBBON_WAVE (anim 1) is a multi-frame animated
   ribbon that ripples; currently we draw a static frame. The Center
   (anim 3, the "SONIC" wordmark) is enabled only AFTER FlashIn end —
   currently we always show MRIBBON
2. **COPYRIGHT** (anim 6): "© SEGA / 1991, 2017 SEGA" — currently missing
3. **Proper RINGBOTTOM** (anim 7): the gold ring's bottom arc, distinct
   from MRING. Currently we use MRING for both top and bottom which is
   visually wrong
4. **PRESS START** anim 8 — even keeping English, the prompt should be
   "Press [START] to play" not the cropped "PRESS START" sprite I'm
   currently using

### 2.4 Asset reextraction

`extracted/Data/Sprites/Title/Logo.bin` and `Title/Logo.gif` are the
source. The existing `tools/build_titlesonic_atlas.py` only builds the
Sonic atlas — we need a sibling tool `tools/build_titlelogo_atlas.py`
that emits one or more atlases covering all TitleLogo anims.

This will replace the various MLOGO/MWINGS/MRIBBON/MRING/MPRESS/etc.
.SPR files with a single TLOGO.ATL (or per-anim atlases).

---

## §3 — TitleSetup state machine (Phase 1.c)

### 3.1 Replace `STATE_TITLE` linear timeline with the real state machine

Per `tools/_decomp_raw/SonicMania_Objects_Title_TitleSetup.c` —
the 8 sub-states already documented in `docs/COMPREHENSIVE_PLAN.md` §1.3.

### 3.2 Specific deliverables

1. **Fade-from-black** (`State_Wait`, 128 frames). Saturn implementation:
   write an additive RGB ramp to the VDP2 backplane register OR use
   `slBackColRgb()` to ramp the back-color from black (0,0,0) to the
   title sky-blue (96,128,224) over 128 frames at +=8 per channel.

2. **Electricity arc** (`State_AnimateUntilFlash`, ~32 frames). Need to:
   - Extract `Title/Electricity.bin` (small atlas, probably 50 KB)
   - Build `cd/ELECTRA.ATL` with same v4 atlas format
   - Render with both FLIP_NONE and FLIP_X (mirrored about world X=256)

3. **White flash** (`State_FlashIn` end + `State_WaitForSonic`, 48 frames).
   Saturn implementation: switch VDP2 backplane to white (255,255,255) for
   one frame at the trigger moment, then ramp back to sky-blue over 48
   frames

4. **State-driven logo enables**. The TitleLogo entities are
   currently always visible. Need a per-logo `visible` flag that gets
   set at the right state transition.

---

## §4 — TitleBG (Phase 1.d) — animated backdrop with scanline FX

### 4.1 What the original does

Per `tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c`:

- **TileLayer 0**: foreground reflection
- **TileLayer 1**: mountains/islands
- **TileLayer 2**: clouds (with `Scanline_Clouds` callback — sin/cos warp
  per scanline)
- **TileLayer 3**: islands (with `Scanline_Island` callback — rotation warp)
- **WINGSHINE sprite**: flies up vertically every 32 frames
- **MOUNTAIN1, MOUNTAIN2 (INK_BLEND alpha)**: stationary mountain layers
- **REFLECTION (INK_ADD α=0x80)**: water reflection sprite at half alpha
- **WATERSPARKLE (INK_ADD α=0x80)**: animated water sparkle sprite
- **Palette rotation** on CRAM indices 140-143 every 6 frames

### 4.2 Saturn-equivalent implementation

Saturn has NO native scanline-callback for VDP2 layers (the original RSDK
engine does per-scanline programmable scroll, which on Saturn is **VDP2
NBG line-scroll** via the line-scroll table).

Implementation:
1. **NBG3 = clouds layer** with line-scroll table programmed per-frame
   to reproduce the sin/cos cloud warp
2. **NBG2 = static island/mountain backdrop** (current title backdrop)
3. **NBG0 = sky gradient** (line-color screen for the gradient)
4. **VDP1 sprites for WINGSHINE, REFLECTION, WATERSPARKLE** with
   appropriate VDP1 color-calc modes for INK_BLEND/INK_ADD equivalents
   (Saturn VDP1 supports half-transparency and shadow ops via the
   sprite color-calc bit)
5. **VDP2 palette rotation** on CRAM[140..143] every 6 frames via direct
   CRAM writes in vblank

### 4.3 Required asset extraction

`extracted/Data/Sprites/Title/Background.bin` — need to parse this
.bin, identify the WINGSHINE / MOUNTAIN / REFLECTION / WATERSPARKLE
anims, build appropriate atlases.

---

## §5 — Cinepak intro (Phase 1.e)

### 5.1 Existing reference

YouTube: "Sonic Mania Cinepak intro running on native Sega Saturn hardware"
(by RingsOfSaturn, lw3IfCbE1Yw). **This proves it's possible on real
hardware.** A Saturn user has already done this; the question is the audio
trade-off.

### 5.2 Three paths (already documented in `docs/cinepak_blackboot_solved.md`)

I need to actually try Path 2 (SBL Cinepak with audio refactor). The
prior agent (me) deferred this as too complex. The user has explicitly
said "do it." So Phase 1.e is:

1. Disable `JO_COMPILE_WITH_AUDIO_MODULE` in Makefile
2. Copy `BOOTSND.MAP` + `SDDRVS.TSK` from
   `D:\Claude Saturn Skill Documentation\Saturn Video Tools\Official
   Cinepak Demos\SBL Cinepak Demo 4\cd\` into our `cd/`
3. Refactor `setup_audio()` and every `jo_audio_play_sound()` call site
   to use SBL `SND_*` calls with the real driver+map loaded
4. Enable `JO_COMPILE_WITH_VIDEO_MODULE = 1`
5. Implement `intro_video_play("INTRO.CPK")` per the
   cinepak_integration_spec.md §5.3 with jo_video_open_file + jo_video_play
6. Regenerate `cd/INTRO.CPK` from `extracted/Data/Video/Mania.ogv`
   at proper length (current INTRO.CPK is only 2 seconds; Mania.ogv
   is ~30 MB and runs longer)

This is the largest single change because every SFX call site has to
move from jo audio to SBL audio.

---

## §6 — GHZ Act 1 frame-to-frame (Phase 2)

### 6.1 What needs to match

Per `tools/_decomp_raw/SonicMania_Objects_GHZ_GHZSetup.c` + the in-game
walkthrough verified via StrategyWiki:

**Visual elements:**
- **5-7 parallax background layers** (sky, far mountains, mid hills,
  island/palm-trees, water, clouds, etc.)
- **Animated tiles**: sun flower (8 frames, tile 427) and extending
  flower (16 frames, tile 431)
- **Palette cycling**: water-line (CRAM 181-184) and waterfall
  (CRAM 197-200), every ~6 frames
- **TileConfig.bin-driven per-tile collision** with proper slope angles
  for 360-degree loop physics
- **Camera scroll** matching Mania's smooth follow with look-ahead

**Gimmicks (placement per Scene1.bin):**
- Spring (vertical + diagonal)
- Monitor (with item-specific power-ups visible)
- Signpost (Sonic touch animation, spinning)
- Bridge (sagging planks under Sonic's weight)
- Loop-de-loop tubes
- Item boxes (Super Ring, Invincibility, Speed Shoes, shields, 1-up)
- CheckerBall (the giant rolling ball)
- ZipLine (rope swing)

**Badniks (per `extracted/Data/Sprites/GHZ/`):**
- Motobug (already placeholder)
- Crabmeat (claws + 2-shot projectile attack)
- Buzz Bomber (hover + stinger drop attack)
- Chopper (rises from water, snaps)
- Newtron-Green (camouflage chameleon)
- Newtron-Blue (drops + missile)
- Catakiller (caterpillar, optional)
- BatBrain (cave variant — Act 2 only)

**Player sprites:**
- Idle (anim 0)
- Looking up (anim 1)
- Looking down (anim 2)
- Running (anim 3, 8 frames cycle)
- Walking (anim 4, 8 frames cycle)
- Top speed (anim 5, ~4 frames)
- Rolling (anim 6, 4-5 frames, spinball)
- Jumping (anim 7, rolling ball)
- Spindash (charge animation, dust particles)
- Peelout (high-speed run start)
- Drop dash (Plus DLC, defer)
- Insta-shield (one-frame energy ring)
- Hurt (knockback frame)
- Dying (limp pose)
- Drowning (water timer countdown)
- Push (against wall, hands forward)
- Tube/loop sliding (curved)

That's a **lot** of sprites. Each is in `extracted/Data/Sprites/Players/
Sonic.bin` etc., each requires extraction → Saturn atlas → in-game wiring.

### 6.2 Phasing within Phase 2

Phase 2 is itself multi-iteration:
- 2a: Animated tiles + palette cycling (background fidelity)
- 2b: Multi-layer parallax (NBG layer expansion)
- 2c: Full badnik roster (sprite + AI per badnik)
- 2d: Full gimmick roster (springs, monitors, item boxes, etc.)
- 2e: Sonic action sprites + animations
- 2f: Boss fight (Death Egg Robot — phase per `Objects/GHZ/HeavyGunner.c`
  or whatever the GHZ boss object is)

---

## §7 — Frame-to-frame validation methodology

### 7.1 Reference image library

`tools/refs/mania_pc/` must contain:
- `title_full.jpg` (have it ✓ from archive.org)
- `title_t0.png` ... `title_tN.png` — multiple frames across the title
  sequence at known tick offsets for animation validation
- `ghz1_spawn.png` — Sonic at Act 1 spawn position
- `ghz1_act1_*.png` — gameplay frames at key beats (first hill, first
  badnik, first loop, signpost, boss)
- `titlecard_ghz.png` — the Act 1 title-card slide
- `gameover.png`
- `signpost_spin.png`

### 7.2 Visual diff tool (`tools/qa_visual_diff.py`)

```python
# pseudocode
def visual_diff(saturn_capture, reference, mask_path=None, threshold=0.55):
    sat = load_and_crop_to_game_viewport(saturn_capture)
    ref = load_and_resize(reference, sat.size)
    if mask_path: sat, ref = apply_mask(sat, ref, mask_path)
    score = ssim(sat, ref)
    if score < threshold:
        save_panel(sat, ref, diff_heatmap)
        return False
    return True
```

### 7.3 Per-feature gate template

Every shipped feature gets a `Gate Vn` in `verify_done.ps1`:

```powershell
W "Gate Vn: <feature> SSIM vs PC Mania reference >= 0.55..." Yellow
python tools/qa_visual_diff.py qa_<frame>.png \
    --ref tools/refs/mania_pc/<reference>.png \
    --mask tools/refs/masks/<mask>.png \
    --threshold 0.55
```

The threshold 0.55 accounts for Saturn's lower color depth and resolution.
For pixel-exact validation we'd need ≥ 0.9 which isn't achievable on
Saturn given the platform limits.

### 7.4 Animation validation

Static SSIM isn't enough for animations — the finger wave LOOKS like
the body if you sample just once. So animation gates need a sequence
check:

```python
def animation_diff(saturn_captures_list, reference_video_frames_list):
    # Both should have ~10-30 frames
    # Compute frame-to-frame delta sequences and ensure they correlate
    sat_deltas = [delta(sc[i+1], sc[i]) for i in range(len-1)]
    ref_deltas = [delta(rf[i+1], rf[i]) for i in range(len-1)]
    correlation = pearson(sat_deltas, ref_deltas)
    return correlation > 0.5
```

---

## §8 — Honest delivery plan for THIS iteration

The user wants frame-to-frame parity. The honest answer is that's a
multi-iteration journey. What I CAN deliver THIS iteration is:

### Phase 1 minimum viable shipment

1. **Atlas builder v4** — emits both Sonic body + Finger Wave anims
2. **title_sonic.c v2** — drives both animators per the decomp; finger
   wave appears when body settles
3. **Reference image checked in** at `tools/refs/mania_pc/title_full_archiveorg.jpg`
4. **`tools/qa_visual_diff.py`** — SSIM compare tool
5. **`Gate V1` in `verify_done.ps1`** — fires on broken state
6. **Updated golden** for the visual diff test
7. **All previous 11 gates still passing**
8. This document + the gap analysis above

### What I'm NOT delivering this iteration (would need follow-on iterations)

- Cinepak intro with audio coexistence (Phase 1.e — requires full
  audio refactor; multi-day)
- TitleBG scanline-deformed animated backdrop (Phase 1.d — requires
  VDP2 NBG line-scroll programming)
- Electricity arc + white flash (Phase 1.b — needs separate atlas)
- Logo / Ribbon / PressStart full anim coverage (Phase 1.c — needs
  separate TLOGO atlas)
- All GHZ animated tiles, palette cycles, parallax layers (Phase 2)
- All Sonic action sprites + animations (Phase 2e)
- Full badnik roster (Phase 2c)
- Full gimmick roster (Phase 2d)
- Boss fight (Phase 2f)
- Zones 2-13 (Phase 3+)

Each of these requires its own multi-day investment.

### How I will improve communication going forward

Every iteration I will:
1. Read the **decomp source files** for the feature in scope
2. **Visually inspect the assets** I'm building from
3. **Cite a reference image** for what the result should look like
4. **Write the feature checklist** under `docs/feature_checklists/`
5. Present to user for **approval before coding**
6. **Add the visual gate** to verify_done.ps1 FIRST (so it fires on broken)
7. **Implement**
8. **Verify gate passes**
9. **Capture a side-by-side panel image** for user review

The above prevents the "I shipped the wrong sprite" failure mode that
just happened.

---

## §9 — Sources used

- [Sonic Mania - Sonic Wiki Zone (Fandom)](https://sonic.fandom.com/wiki/Sonic_Mania)
- [Sonic Mania Title Screen - Internet Archive](https://archive.org/details/SonicManiaTitleScreen) — reference image saved
- [Sonic Mania / Green Hill - StrategyWiki](https://strategywiki.org/wiki/Sonic_Mania/Green_Hill)
- [Sonic Mania - PC / Computer - The Spriters Resource](https://www.spriters-resource.com/pc_computer/sonicmania/)
- [Sonic Mania Cinepak intro on Saturn (YouTube, RingsOfSaturn)](https://www.youtube.com/watch?v=lw3IfCbE1Yw) — proof of concept
- [Green Hill Zone (Sonic Mania) - Sonic Wiki Zone](https://sonic.fandom.com/wiki/Green_Hill_Zone_(Sonic_Mania))
- [Sega Saturn FILM Tools 3.0.1, Cinepak Movie Disc 1.0](https://www.segasaturnshiro.com/2025/04/28/sega-film-tools-3-0-1-cinepak-movie-disc-1-0/)
- Decompilation source: `D:\sonicmaniasaturn\tools\_decomp_raw\SonicMania_Objects_Title_*.c`
- Asset source: `D:\sonicmaniasaturn\extracted\Data\Sprites\Title\*.bin` + `*.gif`
- Saturn hardware: VDP1 Manual §5.1, VDP2 Manual line-scroll section
