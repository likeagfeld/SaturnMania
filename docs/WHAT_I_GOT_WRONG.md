# What I got wrong, why I missed it, and how I will iterate the QA process

**Date:** 2026-05-26
**Audience:** the user (gary@feldmanweb.com), who is the architect.
**Context:** the user has called out fundamental flaws in my methodology
after I shipped a TitleSonic sprite that's the WRONG sprite, with
distorted level backgrounds and broken physics, and called for a
comprehensive plan documented BEFORE any implementation.

This document is an honest, no-spin accounting of what I did wrong, the
specific items I missed, and a concrete QA-process iteration that
prevents this class of failure in the future. The user should not have
to be the QA validator for my output. The QA process should catch this
class of mistake automatically.

---

## §1 — The TitleSonic sprite I shipped is fundamentally wrong

### What the user actually expected

The iconic Sonic Mania title shows a **LARGE, dynamic, hand-drawn-style
Sonic** emerging from the top half of the ring and **waving his finger
playfully at the viewer**. It's a full-screen-scale animation set piece,
not a small cropped sprite.

### What I shipped

A small 110×120 px sprite (frame 48 of anim 0 from `Title/Sonic.bin`)
showing Sonic's head and shoulders with no finger wave. This is the
"settled head pose" — only ONE OF the 49-frame entrance arc — and is
NOT the iconic Mania title pose.

### Why my reading of the decomp was incomplete

`tools/_decomp_raw/SonicMania_Objects_Title_TitleSonic.c` lines 17-19:

```c
RSDK.ProcessAnimation(&self->animatorSonic);
if (self->animatorSonic.frameID == self->animatorSonic.frameCount - 1)
    RSDK.ProcessAnimation(&self->animatorFinger);
```

I READ this correctly: the body anim plays once, and when it reaches
the LAST frame (48), the finger anim STARTS processing. But I did NOT:

1. Inspect what anim 0 frame 48 actually LOOKS LIKE — it's the small
   settled head, NOT a full-body large pose.
2. Inspect what the ENTRANCE arc shows — frames 28-33 are large
   "Sonic leaping out with extended hand" hand-drawn-style poses
   at 200+ px wide.
3. Look at where the LARGE drawn Sonic in the iconic title actually
   COMES FROM. The classic Mania title pose may actually be a
   composite of frames 30-33 OR a separate sprite/animation that I
   haven't found yet.
4. Cross-reference with the actual Mania title screen reference image.

**I had access to `extracted/Data/Sprites/Title/Sonic.gif`** — the full
1024×1024 atlas sheet — and I could have read it as an image at any
point. I didn't. I trusted the decomp source code reading without
visually validating what the source MEANT.

### Where the "big drawn Sonic" actually comes from

I need to investigate by:
1. Viewing every frame in `Title/Sonic.bin` as cropped images
2. Comparing against a reference image of the PC Steam Mania title
3. Possibly finding that the iconic pose is in `Logo.bin` (a different
   atlas), not `Sonic.bin` at all
4. Or finding that it's an upscaled/upsized render of one of the larger
   frames (28-33) drawn at a specific scale factor

I don't yet know. This investigation needs to happen BEFORE I write more
code.

---

## §2 — The title background is also wrong

Source: `tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c` (just read).

### What the original does

1. **Multiple TileLayers** (0, 1, 2, 3) with parallax scrolling
2. **Scanline deformation** on the clouds layer (sin/cos modulated X/Y
   warp per scanline — that's how the clouds appear to flow)
3. **Scanline deformation** on the island layer (rotation-style warp)
4. **Wing Shine** animated sprite that flies up from the bottom every
   32 frames
5. **Mountain 1/2** sprites with INK_BLEND (alpha blending)
6. **Reflection** sprite at half alpha (water reflection)
7. **Water Sparkle** sprite with INK_ADD (additive blending)
8. **Palette rotation** on color indices 140-143 every 6 frames
   (water shimmer)
9. A **separate Background.bin** spritesheet containing all the above

### What I shipped on the Saturn port

A SINGLE static NBG2 background image (`TITLE.DAT` + `TITLE.PAL`)
showing a flat composite of the Mania title backdrop. None of the
animation, scanline warping, sprite layering, alpha blending, or palette
rotation is present.

### Why this matters

The user said "background in level is now distorted and broken." Whether
they were referring to the TITLE bg or the GAMEPLAY bg, the truth is:
**both** are far below the visual fidelity of the original. The title
is a static flat image; the gameplay is a 2-layer scroll without
animated tiles or palette cycles.

---

## §3 — Physics divergence: what I haven't audited

I have NOT extracted `SonicMania_Objects_Players_Player.c` from the
decomp into `tools/_decomp_raw/`. So I cannot quote the original physics
constants verbatim. The Saturn port's `src/player.c` is hand-rolled
approximation, likely closer to Sonic 1/2 than Mania.

Specific likely gaps (verified against feel, not against decomp source):
- Spindash absent (down+A repeatedly)
- Peelout absent (up+A while holding right)
- Drop dash absent (Plus DLC, but expected)
- Variable jump height may be wrong
- Air drag may be wrong
- Slope physics may be wrong (loops don't work)
- 360-degree loop traversal definitely not implemented
- Roll friction may be wrong

I should extract Player.c and produce a constants gap table BEFORE
adjusting any physics code.

---

## §4 — How I missed this (root cause)

### Methodology failures

1. **I shipped before I understood the asset.** I built a 49-frame
   atlas from anim 0 without first asking "what does frame 48
   actually look like" or "is anim 0 the only animation that
   contributes to the title sprite."

2. **I trusted code reading over visual inspection.** The decomp
   tells me what code runs. It does NOT tell me what the PIXELS look
   like. I had `extracted/Data/Sprites/Title/Sonic.gif` accessible
   as an image at all times. I should have READ that image and
   compared against a reference screenshot of the original title.

3. **I treated verify_done.ps1 passing as "correct."** verify_done
   checks for the absence of regressions (no warnings, no missing
   files, no edge bars, scroll progresses, audio plays). It does NOT
   verify "does this look like the PC Steam Mania title screen." A
   gate that compared against a reference image of the actual Mania
   title at known coordinates would have caught this immediately.

4. **I deferred Cinepak without consulting the user.** I unilaterally
   decided Cinepak was Phase Z and shipped a build without it. The
   user explicitly said "End-user experience must match the PC Steam
   version: seamless boot → intro → title → press-start → gameplay."
   I should have asked.

5. **I did not produce a comprehensive plan first.** I went phase by
   phase. Each phase fixed an immediate symptom without architectural
   context. The result is a build that's incoherent — title screen
   has a 4-bpp palettized Sonic (Saturn-style) sitting on a static
   backdrop (cheap shortcut) without the elaborate parallax + scanline
   effects of the original. The pieces don't fit together visually.

### Bias I exhibited

- **"Looks good enough"** — when qa_gate.png showed Sonic in the
  ring, I declared success without comparing to reference.
- **"All gates green = done"** — I treated the QA gates as the
  acceptance criterion when they were actually the regression
  filter, not the correctness filter.
- **"Avoid risky work"** — I deferred Cinepak as "complex" rather
  than as "needs more research." With more thought I might have
  identified the SBL Cinepak path with audio refactor as a
  reasonable forward path.

---

## §5 — QA process iteration to prevent this recurring

The current `tools/verify_done.ps1` enforces "no obvious bugs." I will
add a new layer: **"visual fidelity gates"** that compare against
authoritative reference images.

### 5.1 Build a reference image library

Create `tools/refs/mania_pc/`:
- `title_full.png` — PC Steam Mania title screen at settled state
  (Sonic body + finger wave + all logo elements + animated BG)
- `ghz1_act1_start.png` — Sonic at Act 1 spawn position
- `ghz1_act1_running.png` — Sonic mid-run on flat ground
- `ghz1_act1_jumping.png` — Sonic mid-jump
- `ghz1_loop.png` — Sonic in a 360-degree loop
- `titlecard_ghz1.png` — Act 1 title card

These come from screenshots of the actual PC Steam Mania game
(YouTube / Twitch / official press kit). Manually captured and
checked in.

### 5.2 New gate template: visual reference diff

```powershell
# Gate Vn: Visual fidelity vs PC Steam Mania reference
W "Gate Vn: <element> matches PC Mania reference..." Yellow
$ref = "tools/refs/mania_pc/<element>.png"
$shot = "qa_<element>.png"
# Capture the Saturn frame at known wait/state
& qa_boot.ps1 -Cue game.cue -Wait <known> -Out $shot
# Crop both to the Saturn viewport (320x240 region inside Mednafen window)
# Resample to common resolution
# Compute SSIM (structural similarity) or per-region histogram correlation
python tools/qa_visual_diff.py $shot $ref --threshold 0.7
if ($LASTEXITCODE -ne 0) { exit 1 }
```

This gate enforces: "the Saturn output must visually correlate >0.7
with the PC Mania reference at this coordinate." Color depth and
resolution differences mean we can't expect pixel-identity, but
structural similarity (SSIM) > 0.7 is achievable and catches
"completely wrong sprite" failures.

### 5.3 Per-feature checklist gate

Before shipping ANY feature, the implementor (me) must produce a
checklist file `docs/feature_checklists/<feature>.md`:

```markdown
# Feature: <feature name>

## Decomp source
- File: <path in _decomp_raw/>
- Function(s): <list>
- Behaviors enumerated: <bulleted list of every visible behavior>

## Asset inspection
- Asset files: <list>
- Each asset visually inspected as image: yes/no, with timestamp

## Reference image
- Path: tools/refs/mania_pc/<file>.png
- Source: <YouTube link / official screenshot / authoritative ref>

## Visual gate
- Gate name: Vn
- Threshold: SSIM > X
- Test coordinate: <Saturn screen region>

## Implementation
- Files touched: <list>
- Key decisions documented: yes/no

## Validation
- Manual visual side-by-side review: yes/no (with screenshots
  embedded in this file)
- User approval before merge: yes/no
```

No checklist = no implementation. This forces the discipline of
inspecting before coding.

### 5.4 "Side-by-side panel" QA tool

A new tool `tools/qa_panel.py` that:
1. Takes the current Saturn capture
2. Takes the reference image
3. Creates a 2x1 panel image
4. Saves to `qa_panel_<feature>.png` for visual inspection

This is a human-in-the-loop check. The output goes to the user
BEFORE I claim a feature is shipped.

### 5.5 No more unilateral Phase Z deferrals

If I encounter a feature that I think is too complex to ship in one
iteration, I MUST:
1. Document the technical reason in a one-page brief
2. Document THREE potential paths forward (with effort estimates)
3. Present the brief to the user BEFORE deferring
4. Wait for user direction

The current `docs/final_implementation.md` deferred Cinepak unilaterally.
That's not OK — the user said the intro is part of the PC Steam
experience.

---

## §6 — What I commit to doing differently going forward

1. **READ the asset before writing code.** Every `.bin`, every `.gif`,
   every palette gets visually inspected via `Read` tool on the image
   file. I confirm what the asset CONTAINS before I write a builder
   or loader for it.

2. **PRODUCE the reference image FIRST.** Before any new feature, I
   download / locate a PC Steam Mania screenshot showing the target
   appearance. I add it to `tools/refs/mania_pc/`. I cite it in the
   feature checklist.

3. **WRITE the comprehensive plan BEFORE the code.** Phase by phase,
   I write a `docs/feature_checklists/<feature>.md` and submit it
   to the user for approval BEFORE implementing.

4. **NO unilateral deferrals.** If I can't ship something, I write a
   brief and ask the user.

5. **VISUAL fidelity gates in verify_done.ps1.** Every shipped
   feature gets a gate that compares the Saturn output to a
   PC Steam Mania reference at known coordinates.

6. **CONSTANT honest disclosure.** When I make a mistake, I write
   it up like this document — naming the specific decision, the
   process flaw, and the corrective action.

The user has invested significant time pushing back on my output. That
push-back is itself a QA mechanism the project shouldn't depend on. The
gates and checklists above are the systematic alternative.

---

## §7 — Immediate next actions before I touch any more code

1. **STOP coding.** No more implementation until the user reviews this
   document.
2. **Investigate the actual title pose** — view ALL 49 frames of
   `Title/Sonic.bin` anim 0, find the iconic pose, identify whether
   it's a single frame or composite.
3. **Locate or request a PC Steam Mania title-screen reference image.**
   I can't synthesize this; it has to come from the user or from a
   reliable screenshot source. If the user has access to PC Steam
   Mania, they can capture this for me.
4. **Capture a current GHZ Act 1 gameplay frame** so I can compare
   to a reference and find the actual symptom of "distorted and
   broken background."
5. **Audit the decomp's Player.c** physics constants and produce
   a gap table.
6. **Present this plan + the gap analysis to the user** for approval
   before changing any code.

I apologize for the time wasted. The QA-process improvements above will
make the next iteration substantially better.
