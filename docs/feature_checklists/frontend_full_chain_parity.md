# FRONT-END FULL-CHAIN PARITY CONTRACT (binding, 2026-07-03)

User mandate: EVERYTHING -- Logos, Title, Menu, intro animation (AIZ +
GHZCutscene), and Green Hill Zone -- end to end, decomp-authoritative,
PC-equal composition and timing, no detail missed. NOTHING in this chain
claims "done" unless every row below holds its gate AT DEEP/SETTLED FRAMES
(not intro-only) on a single chain build, plus the cross-cutting gates.

Root-cause context (measured 2026-07-02/03): the SGL per-vblank transfer
machinery tears intermittently (title sprites 73% torn post-settle, island/
clouds sticky-dead from flash-end, landing 60-75% empty plans). Foundation
fixes: (F1) direct VDP1 command list for ALL front-end sprites (probe proven:
renders through the dead zone), (F2) title island/cloud VDP2 state moved off
SGL DMA mirrors to direct writes (#276 pattern), (F3) 60Hz catch-up logic
(landed), (F4) runtime per-scene render config replacing compile-time flavor
globals (kills the flavor-interaction class: menu-garble/sky/blank-char bugs).

Object inventories below are the decomp stage_config lists
(docs/scene_objects.json); logic-only objects marked (logic). Per-scene
"PC ref" = capture/source a Steam reference before the scene's port pass
(CLAUDE.md 4.4).

---

## SCENE 1 -- LOGOS (Presentation/Logos; LogoSetup.c, UIPicture.c)

| Item | Decomp source | Current | Gate |
|---|---|---|---|
| SEGA logo splash + timing | LogoSetup states | logo RENDERS but over BLACK bg + full-screen PINK tile-garbage field (AUDIT: _v2__12; PC = clean white bg). Hold duration ~= decomp (logo1 ~4-5s vs 248 ticks = 4.13s) | logo present (band/SSIM), pink-garbage mass == 0, bg white fraction >= 0.5, duration = decomp ticks at 60Hz |
| Logo screen 2 splash + fade | LogoSetup_State_NextLogos (ScreenInfo->position.y += SCREEN_YSIZE, LogoSetup.c:134) | BROKEN placement: logo 2 clipped at the BOTTOM edge under pink garbage (_v2__18) -- camera Y scroll not applied to the draw | logo-2-hold non-bg centroid within the center third of the screen |
| Fade in/out cadence | LogoSetup fade fields (Draw_Fade FillScreen 0x000000, LogoSetup.c:149) | fades run, but the Logos->Title seam frame is FLAT PINK not black (_v2__21) | fade ramp visible; every seam frame max non-black fraction ~0 |
| Logos->Title advance | LogoSetup ++listPos + LoadScene | seam works | folder flips, no swallow, engine path |
| Sega chant SFX (Stage/Sega.wav at fade-in end, LogoSetup.c:102) | LogoSetup_State_ShowLogos | UNVERIFIED -- no witness exists | sfx witness == sfxSega at a logo-window capture |

## SCENE 2 -- TITLE (Presentation/Title; TitleSetup/TitleLogo/TitleSonic/TitleBG/Title3DSprite)

| Item | Decomp source | Current | Gate |
|---|---|---|---|
| Island Mode-7 backdrop ROTATING at settled title | TitleBG_Scanline_Island; Saturn RBG0 #276 | DIES at flash (band 147->0.0 MEASURED) | island band alive AND rotating (frame-pair diff) at t>=60 |
| Clouds band | TitleBG clouds scanline | dead (#290 + tear) | cloud band alive at t>=60 |
| Sky backdrop color | TitleBG palette | dies with backdrop | sky band = ref color at t>=60 |
| Mountains/reflection/sparkle/wingshine | TitleBG entities | sprites default-off (#275 thrash) | present via F1 direct list (no thrash), band checks |
| Title3DSprite billboard islands | Title3DSprite.c | partial (1.32b) | present per decomp coords |
| Emblem+wings, wordmark, ribbon+wave tails, ring bottom, copyright | TitleLogo anims | F1 VERIFIED 2026-07-03: qa_title_deepframe GREEN (10 cmds t=58); viewed _f1__30 full FG clean; RE-VERIFIED _v2__30/36/40 (ribbon tails ANIMATE across pairs) BUT bright-GREEN mask-color fringe rims the wing/emblem edges -- see audit T1 | ALL present every sampled deep frame via F1; 0x00FF00 pixel count == 0 |
| TitleSonic body twirl -> hold + finger wave loop | TitleSonic.c anims 0/1 | F1 VERIFIED; RE-VERIFIED _v2__30 (twirl) -> _v2__36/37/40 (finger wave MOVING across pairs) | present + finger anim MOVING (frame-pair) |
| Press Start blink 16/16 | TitleSetup | blink OBSERVED (_v2__36/37 on, _v2__40 off); 16/16 cadence unmeasurable at 1 Hz | blink cadence measured (savestate timer or sub-second burst) |
| Power LED, electricity ring intro | TitleLogo/Electricity | intro-only ok | intro band check |
| Title BGM + menu SFX | Music.c | plays | str_track gate |
| Full settled composition vs PC | tools/refs/mania_pc/title_full_archiveorg.jpg | FAILS (black backdrop) | SSIM/band-set vs ref at t>=60 -- THE title gate |
| WaitForEnter 800 ticks + auto LoadScene | TitleSetup.c:340-375 | duration ~parity (AUDIT: flash _v2__27 -> fade ~_v2__44-45 = ~17s vs decomp 16.1s incl. SetupLogo+WaitForSonic) BUT timeout DESTINATION diverges: PC = State_FadeToVideo -> Mania.ogv + IntroHP.ogg (TitleSetup.c:362-379); Saturn lands on MENU -- see audit T2 | duration 13.3s ±5% + timeout leg classified "video" |

## SCENE 3 -- MENU (Presentation/Menu; MenuSetup + UI* set)

| Item | Decomp source | Current | Gate |
|---|---|---|---|
| UIBackground animated pattern (per decomp: gold fill + TWO orbiting circle sets + circle outlines, UIBackground_DrawNormal:44-70 -- NOT a diagonal texture) | UIBackground.c | FLAT gold CONFIRMED deep (AUDIT 2026-07-03: _v2__53=56=64=72 pixel-identical; fill matches bgColors[0] 0xF0C800; circles absent) | bg circles present + moving (frame-pair) |
| DrawFace row plates (parallelogram near-white+black pairs) | UIWidgets_DrawParallelogram -> DrawFace | MISSING (#295) CONFIRMED (_v2__53-72: no plates behind any row) | plates present behind each row (F1 polygon cmds) |
| 4 mode rows: MainIcons icon+shadow anims | UIModeButton_Draw:45-74 | PARTIAL (AUDIT: TA+Comp icons render but float below labels; Mania icon MISSING; Options ROW absent -- see audit M1-M3) | qa_chain_menu_clean + icon anim on select |
| Row labels (TextEN) | UIButtonLabel | clean | existing gate |
| Heading/prompts (BACK/CONFIRM) + hand cursor | UIButtonPrompt/UIControl | clean | present |
| Row layout coords vs decomp | UIControl world->screen (M2b) | 320-fit grid | coord witness gate |
| Real Mania-Mode confirm -> No-Save -> SetScene (retire the chain3 dwell-nudge) | UISaveSlot_ProcessButtonCB decomp:871 + MenuSetup.c:1121 | dwell-nudge stand-in (M2 S3 open); AUDIT: no Save-Select/No-Save frame EVER visible (_v2__53-87 static menu then AIZ at 88) | confirm chain fires SetScene; nudge deleted; burst contains >=1 save-select-class frame (teal bgColors[6] 0x38B0C8) |
| UITransition slide on select | UITransition.c (3 parallelograms 0xE48E00/0x1888F0/0xE82858, DrawShapes:96-112) | MISSING (AUDIT: no wipe frame in _v2__53-88) | transition frames present |
| Select/move SFX + menu BGM (MenuSetup_ChangeMenuTrack trackID 0, MenuSetup.c:890-905) | Music/SFX | unverified -- NO witness exists (audio census 2026-07-03: only GHZ str_track gated) | str_track == menu track at settled-menu capture + bleep/accept sfx witness |

## SCENE 4 -- AIZ INTRO (Cutscenes/Angel Island Zone; AIZSetup beats + CutsceneSeq)

| Item | Decomp source | Current | Gate |
|---|---|---|---|
| BG1 distant sky/clouds (NBG2) | AIZ layer 1 | ok | band |
| BG2 sky/water line-scroll (129-band + sine) | AIZ layer 2 | MISSING (#304) | band + line-scroll motion |
| BG3 mountains (NBG3) | AIZ layer 3 | ok | band |
| BG4 jungle (NBG0) | AIZ layer 4 | ok | band |
| FG Low/High complete (no white holes) | AIZ FG layers | white cell gaps CONFIRMED persisting (AUDIT: _v2__119/126/133/142, larger at _v2__140) | FG hole gate (white-mass ~0 in FG) |
| Tornado biplane + pilot + prop anim + path bobbing | AIZTornado/AIZTornadoPath | renders 89+ (_v2__92/100: plane+flame+pilot, coast shadow at 101) BUT _v2__88 = detached pilot head + fragment only, _v2__89 = full BG blackout with plane alive; Sonic rider ABSENT (player-sheet class, AIZTornado.c:129 ANI_RIDE) | present through flight beats + prop MOVING + assembled from first visible frame |
| All 9 beats + timings (fly-in, jungle weave, coast hold, dive, claw, ruby, flash, handoff) | AIZSetup.c:414,896,900 + CutsceneSeq | advance (60Hz) but AUDIT: coast-hold STATIC 14s+ (_v2__119=126=133 identical; PC holds = 60 ticks + visible claw action) | beat timeline duration vs decomp ±10%; no frame-pair identical >3s while beats advance |
| KingClaw + EggRobo + ruby + Platform beats | AIZKingClaw/AIZEggRobo/PhantomRuby/Platform | CONTRADICTED on _v2_ burst: claw/EggRobo/platform/ruby NEVER visible in the 119-144 window (7 spread samples; decomp claw beats = WatchClaw/RubyGrabbed AIZSetup.c:481-573) | presence at beats -- onscreen sprite-mass at claw palette > 0 during WatchClaw |
| FXRuby flash + FXFade | FXRuby/FXFade | CONTRADICTED on _v2_ burst: no visible ruby flash (RubyAppear AIZSetup.c:574-582); _v2__144 = mis-composed gray-band frame then black | fade gate + flash-white presence in the RubyFX window |
| Decoration entities | Decoration.c | unverified | presence census |
| Intro music/ambience | Music | unverified | audio witness |
| Flicker | (tear class) | 0.046 | <=0.02 post-F1 |

## SCENE 5 -- GHZ ARRIVAL CUTSCENE (Cutscenes/Green Hill Zone scene1; GHZCutsceneST)

| Item | Decomp source | Current | Gate |
|---|---|---|---|
| Sky/BG Outside behind FG | GHZ layer 0 recipe (#310) | ok | sky gate |
| Arrival white-wash fade | GHZCutsceneST FadeIn + FXRuby | ok | fade gate |
| 5 Heavies placed + animated | CutsceneHBH.c | ok -- RE-VERIFIED on _v2_ burst (_v2__176/185: King+Shinobi+Mystic+Rider placed, Gunner center descending w/ trails; clean, no garble) | garble gate + all-5 presence |
| KingClaw, PhantomRuby, Platform crate | stage_config | ok | presence at beats |
| Players warp-in (ANI_FAN) -> land -> beats -> SetupGHZ1 | GHZCutsceneST.c:310-327 | ok -- RE-VERIFIED (_v2__176/185: Sonic+Tails VISIBLE descending in fan pose; player sheet IS staged in this leg) | beats + handoff fired |
| Beat timeline at 60Hz | CutsceneSeq | ok (compressed 3x); AUDIT measured: visible content _v2__175-199 ~24s vs decomp warp+ExitHBH ~3-8s (ExitHBH=180 ticks, GHZCutsceneST.c:251) = ~3-4x long | duration vs decomp ±10% |
| FG lit (not black-silhouette) during beats 1-2 | GHZ FG palette / VDP2 color offset | GREEN on _v2_ burst (AUDIT: _v2__176/185 FG + decorations lit through sampled beats; prior _f1__160 RED not reproduced) | FG band lit at every beat |
| Flicker | (tear class) | 0.259 on the F1 burst leg window 118-171 (window includes beat fades + the dark segment -- re-window after the FG fix) | <=0.02 post-F1 |

## SCENE 6 -- GREEN HILL ZONE PLAYABLE (Mania Mode GHZ Scene1)

| Item | Decomp source | Current | Gate |
|---|---|---|---|
| HUD (score/time/rings/lives) | HUD (global) | UNBOUND in chain (pack skip) -- CONFIRMED absent through _v2__360 | HUD present -- close the GHZANIM/GHZOBJ skip |
| TitleCard "GREEN HILL ZONE ACT 1" slide-in on arrival (colored strips + zone words + act number) | TitleCard_Create -> State_SetupBGElements (suppressTitlecard==false on cutscene handoff, TitleCard.c:44-60; strips State_EnterTitle:457-475) | MISSING ENTIRELY (AUDIT 2026-07-03: no card in _v2__200/210/240/300; row did not exist before this audit) | landing window contains strip colors (blue/red/orange/green diagonals) + zone-letter mass; RED today |
| GHZSetup ambient animation: waterfall/water palette shimmer + sunflower/extend-flower aniTiles | GHZSetup_StaticUpdate:18-43 (RotatePalette 1&2 @181-184/197-200 + SetLimitedFade + DrawAniTiles) | FROZEN (AUDIT: _v2__210=240=300 pixel-identical waterfall/sparkle/sunflower over 90s) | waterfall-band + sunflower-tile pixel diff across consecutive shots > 0; RED today |
| Rings placed + spin + collect | Ring.c overlay | unbound in chain | rings present + collect works |
| Badniks (Crabmeat/Batbrain/Chopper/Newtron + break chain) | stage_config + BadnikHelpers | unbound in chain | presence + break effects |
| Springs/monitors/bridges/spikes/platforms/collapsers | registered 18 set | unbound in chain | qa_p6_ghz_regression R-set on the CHAIN build |
| BG parallax layers (all, incl. water band) | GHZ BG layers (#253) | STALE CELL -- AUDIT: crags/bushes/lake/waterfall bands ALL present (_v2__210); parallax MOTION unverifiable this burst (camera static, players invisible) | per-layer bands + parallax motion |
| Players full moveset + camera | Player.c + Camera | wake-up ok; input replay TBD | existing player gates on chain build |
| Player sprites CONTINUOUS post-handoff (F1-R1) | SGL alive-pipeline empty-plan re-transfer stomps the 0x60 trampoline per slSynch (savestate-proven: transitions=4, textures live, halves populated, ~70/30 shot ratio) | RED 0.766 on the F1 burst (qa_chain_landing_sprites 216-360); fix = frame-top re-patch in p6_frontend_frame + p6_w_dl_stomps witness | qa_chain_landing_sprites >=0.9 |
| Landing stability (no blackouts) | (tear class; transitions=4 rules out load-loops) | 0.219 flicker on burst tail | flicker gate <=0.02 |
| Stage BGM CD-DA | PlayStream GreenHill1 | ok | str_track==2 |
| Perf | perf plan | ~30fps | qa_p6_perf floors; 60 via #261 later |

## CROSS-CUTTING (every scene)

| Item | Current | Gate |
|---|---|---|
| F1 direct-VDP1 sprites everywhere; SGL sprite pipeline idle | VERIFIED 2026-07-03 (title deepframe GREEN 10 cmds; menu clean GREEN; sprites survive VDP2 blackouts _f1__211; drops=0 all legs; AIZ flicker 0.033 GREEN) -- one caveat: alive-pipeline legs stomp the trampoline (F1-R1 row, scene 6) | deepframe walker: expected cmd count per scene, deep frames |
| F2 title backdrop state off SGL mirrors | designed | island/cloud bands at t>=60 |
| F3 60Hz logic | landed | tickrate gate (cap-adjusted per leg) |
| F4 runtime per-scene render config (bucket tables, CRAM plan, NBG plan, blank-char) -- no compile-time flavor bleed | flavor globals today | one build serves all scenes; per-scene config table cited to this doc |
| Engine-owned transitions only (seams minimal; menu nudge retired with M2 S3) | 4 seams + 1 nudge | transition witness set |
| No tear-class blackouts anywhere | mixed | qa_chain_flicker <=0.02 all legs |
| Boot->landing hands-free single build | works | full-chain burst classification |
| Whole-chain video vs PC side-by-side review | pending | user review after all gates GREEN |

## AUDIT 2026-07-03 (proactive sweep) -- new findings

Read-only parity audit of the `_v2__1..360` 1 Hz chain burst against decomp
manifests (per `parity-first-verification-binding.md`: reference-anchored,
manifest-gated, no self-deferrals, timing-is-content). Legs re-timed from the
burst: boot blue 1-11, Logos 11-20, seam 21-22, Title 23-44, fade+load 45-51,
Menu 52-87, AIZ 88-143, wash 144-174, GHZCutscene visible 175-~199, GHZ
landing ~200-360. One row per NEW divergence; contradictions of prior "ok"
cells were edited in place above AND are re-listed here for visibility.

| # | Leg | What differs from PC | Evidence | Proposed RED gate (one line) |
|---|---|---|---|---|
| L1 | Logos | SEGA screen bg = BLACK + full-screen pink tile-garbage field (PC: clean white bg, blue SEGA) | _v2__12; UIPicture_Draw draws pictures only (UIPicture.c:39); no garbage source in decomp | qa_logos_clean.py: pink-mass (~RGB 240,150,170) == 0 AND white fraction >= 0.5 at logo-1 hold shot |
| L2 | Logos | Logo screen 2 clipped at bottom edge, mostly off-screen (PC: centered via ScreenInfo->position.y += 224) | _v2__18 vs LogoSetup.c:134 | logo-2 hold: non-bg centroid-y within middle third |
| L3 | Logos | Logos->Title seam frame = flat PINK (PC: black; Draw_Fade = FillScreen 0x000000) | _v2__21 vs LogoSetup.c:149 | seam window: every frame non-black fraction <= 0.01 |
| L4 | Logos | Sega chant SFX state unknown -- no witness | LogoSetup.c:102; audio census: only GHZ str_track gated | sfx witness == GetSfx("Stage/Sega.wav") id fired in logo window |
| T1 | Title | Bright-green (0x00FF00) fringe rims wings/emblem at all settled frames (PC: that palette index is the MASK color, invisible) | _v2__27/30/36/40 vs TitleBG_SetupFX SetPaletteEntry(0,55,0x00FF00)+SetPaletteMask (TitleBG.c:127-128) | settled title: count of pure-0x00FF00 pixels == 0 |
| T2 | Title | 800-tick attract timeout routes to MENU; PC plays Mania.ogv intro video + IntroHP/IntroTee.ogg then returns to title (loops) | fade _v2__44-45 -> menu _v2__52; TitleSetup.c:345-379 | chain classifier: a "video" leg exists after an unpressed 800-tick dwell (RED until Cinepak CP6 lands -- needs user scheduling decision, NOT a silent deferral) |
| M1 | Menu | OPTIONS mode row missing entirely (PC pre-plus main menu = 4 UIModeButtons Mania/TA/Competition/Options + Extras [+ Exit on PC only]) | _v2__53: rows are Mania/TA/Comp/Extras/Exit; MenuSetup_MenuButton_ActionCB case 3 = Options (MenuSetup.c:944-946) | settled menu: Options label mass present / savestate buttonCount+frameID set matches decomp scene |
| M2 | Menu | EXIT button present on a console SKU (decomp destroys buttons[5] Exit when not PC/dev) | _v2__53 bottom-right EXIT vs MenuSetup.c:518-523 | settled menu: no EXIT label (console behavior) |
| M3 | Menu | Mania-Mode row has NO icon (MainIcons anim 0 + shadow anim 1) | _v2__53 vs UIModeButton_SetupSprites UIMODEBUTTON_MANIA (UIModeButton.c:123-128) | icon-mass in Mania-row plate region > threshold |
| M4 | Menu | TA/Competition icons float detached below labels (PC: icon+shadow inline in the row plate, offset by bounce fields) | _v2__53 vs UIModeButton_Draw:40-58 | icon centroid inside plate-relative box per decomp offsets |
| M5 | Menu | No selection focus indicator, no bounce motion, whole menu static for 35s (PC: selected row plates+icon bounce; UIControl processes input every frame) | _v2__53=56=64=72 identical | consecutive settled-menu frames: selected-row band pixel diff > 0 |
| M6 | Menu | Button prompts render text only -- no button GLYPHS (UIButtonPrompt draws prompt text + device button animator) | _v2__53 "BACK"/"CONFIRM" bare | glyph sprite-mass adjacent to each prompt > 0 |
| M7 | Menu | Save-Select/No-Save sub-screen never renders on screen; UITransition wipe never appears (PC: Mania-Mode confirm -> 3-parallelogram wipe -> No-Save screen -> fade) | _v2__53-87 static -> _v2__88 AIZ; UITransition.c:96-112 | between menu + AIZ: >=1 frame with wipe colors (0xE48E00/0x1888F0/0xE82858) or save-select bg (0x38B0C8) |
| M8 | Menu | Menu BGM + bleep/accept SFX unverified -- no witness | MenuSetup.c:890-905 trackID 0 | str_track == menu track at settled-menu capture |
| A1 | AIZ | Claw beats CAST INVISIBLE: KingClaw, EggRobo, Platform, PhantomRuby and the ruby FLASH never appear in any sampled frame of the beat window (contradicts prior "ok (R3.4)" and "FXRuby ok" cells -- edited above) | _v2__119/126/133/137/140/142/144 all show empty coast; AIZSetup.c:481-582 | savestate at WatchClaw: claw position inside camera view AND claw-palette sprite-mass > 0; flash-white frame present in RubyFX window |
| A2 | AIZ | Sea-sparkle palette cycle FROZEN (decomp rotates pal 0 @171-174 every 5 ticks) | _v2__119=126=133 sparkle pixels identical; AIZSetup.c:59-60 | sparkle-band pixel diff across 1s > 0 |
| A3 | AIZ | Sonic absent from the Tornado wing + absent through jungle run (Player object, ANI_RIDE) -- same player-sheet root as the known landing issue, recorded so the AIZ leg is not silently accepted | _v2__92/100/108 vs AIZTornado.c:129 | player sprite-mass on wing region > 0 at fly-in |
| A4 | AIZ | Fly-in entry unstable: first content frame = detached pilot head + fragment; next = full BG blackout with plane alive (tear/#313-class evidence, folded to flicker rows) | _v2__88, _v2__89 | first 3 AIZ shots each: BG band alive AND assembled-tornado mass |
| A5 | AIZ | Coast-hold static 14s+ with zero motion (PC: 60-tick holds between visibly-moving claw beats) | _v2__119=126=133 | no identical frame-pair spanning > 3s while CutsceneSeq stateID advances |
| A6 | AIZ | All AIZ audio unverified: RubyPresence loop, HBHMischief + Eggman1 transitions, drill loop, LedgeBreak3/Impact4/HeliWooshIn | AIZSetup.c:188,372,568,792 | str_track/sfx witnesses at beat windows |
| G1 | GHZCutscene | Leg pacing ~3-4x decomp: visible content 175-199 (~24s) vs warp+ExitHBH ~3-8s (ExitHBH fixed 180 ticks); wash 144-174 ~30s vs decomp fade chain ~5.7s (wash designated CORRECT post-fix -- record a duration waiver or gate it explicitly) | _v2__144-199; GHZCutsceneST.c:142-155,251 | leg duration vs decomp ticks ±10% (or documented waiver row) |
| G2 | GHZCutscene | Ruby-warp SFX (Attack4/Attack1) + Music_FadeOut unverified -- no witness | GHZCutsceneST.c:146,248 | sfx witness at warp beats |
| GL1 | GHZ landing | TitleCard "GREEN HILL ZONE ACT 1" NEVER slides in (PC shows it on this arrival: suppressTitlecard==false; only Encore-swap/death-reload suppress it, Zone.c:799-800/886-887) | _v2__200/210/240/300; TitleCard.c:44-60,457-475 | landing window: strip-color diagonals + zone-letter mass present |

### GL1 IMPLEMENTATION (2026-07-06, this iteration)

RENDER-PATH CONSTRAINT (measured, decisive): the chain runs `P6_DIRECT_VDP1`
(build_shipping.sh:151). Under direct-VDP1 the vblank trampoline redirects the
VDP1 processor from SGL's command buffer to the hand-built list at VRAM
0x2000/0x2800 (p6_vdp1.c:850-963), so `slPutPolygon`/`jo_sprite_draw3D` (the
SGL path the ported `src/mania/.../TitleCard.c` `titlecard_draw_only` uses)
render into a DEAD buffer. The pack RSDK shims confirm this: `DrawRectangle`
is a no-op (p6_pack_stubs.cpp:52) and `DrawFace` is gated OFF because
`slPutPolygon` crashes the pack's first frame (p6_pack_stubs.cpp:67-79). And
`src/mania/` + `entity_atlas.o` are NOT linked in the chain (grep game.map:
absent). So the TitleCard MUST draw into the direct list via `p6_dl_poly`.

DESIGN (chain-native, self-contained, `P6_GHZCUT_BOOT`-gated -> plain GHZ
byte-identical):
- State machine: the faithful decomp vertex math (SetupVertices / SetupTitle-
  Words / HandleWordMovement / HandleZoneCharMovement + the 6 State_* fns)
  lifted verbatim from `src/mania/Objects/Global/TitleCard.c` (which cites
  decomp TitleCard.c line-by-line) into a new self-contained block in
  `p6_wave1_reg.c` (which is C, sees Game.h/ScreenInfo/Zone/Camera/Player).
  No RSDK-draw dependency in the tick.
- Draw: BG bands + 4 diagonal colored strips (blue/red/orange/green) + decor
  boxes + yellow curtains via NEW cross-TU entries `p6_dl_face`/`p6_dl_rect`
  (thin wrappers over file-static `p6_dl_poly` in p6_vdp1.c), emitted inside
  the frontend frame's `p6_dl_begin/end` block for the GHZ folder.
- Spawn: at the GHZCutscene->GHZ handoff (p6_io_main.cpp:7382), the existing
  `p6_titlecard_atl_restore()` FIXME (p6_wave1_reg.c:403 "replace with the
  verbatim TitleCard port") is upgraded to also spawn the card. The ATL
  camera hand-back it already does IS the decomp TitleCard_State_ShowingTitle
  block (TitleCard.c:504-514) -- kept.
- Freeze: the decomp PAUSES the engine while the card slides in
  (SetEngineState PAUSED). On the chain we do NOT freeze ProcessObjects (the
  landing needs the camera/GHZSetup to keep ticking); the card is a pure
  overlay. This is a Saturn-fit deviation (documented at the site).

SCOPE THIS ITERATION: the colored card (BG+strips+decor+curtains) with the
full slide-in->show->slide-away state machine -> turns the GL1 PRIMARY gate
clause ("strip colors blue/red/orange/green diagonals") GREEN. The zone-name
LETTERS ("GREEN HILL"/"ZONE"/act digit) need `Global/TitleCard.gif` extracted
-> a TITLCARD.SHT banded sheet + TITLCARD.PAK + seam staging + a glyph draw
(SaturnSheet FetchRect -> p6_dl_sprite). That is a measured, separable next
increment (boot budget ~0.3s, 1 SaturnSheet slot) -- NOT a silent deferral;
tracked here.

AUDITS (per CLAUDE.md 4.5.1):
- Audit1 Z-order: the card is a full-screen overlay drawn AFTER the entity
  draw-list inside the same direct list; direct list is painter-order (later
  = in front), so BG-first then strips then decor then (future) glyphs, all
  in front of the GHZ FG/entities. GHZ scenery is VDP2 (always behind VDP1).
- Audit2 cadence: the slide timing is the decomp State_* tick math driven
  once per frontend tick (the P6_TICK_CAP catch-up already 60Hz-paces the
  frontend); total slide-in ~= decomp (timer +24/+32/+40 ramps identical).
- Audit3 pivot/flip: N/A for the colored card (flat polys); glyphs deferred.
- Audit4 boot-delay: colored card adds ZERO assets/staging -> 0 boot cost.

GATE: `tools/qa_chain_titlecard.py` -- reads the live structural witness
`p6_w_tc_state`/`p6_w_tc_draw_faces` (card active + N direct-list poly cmds
emitted this frame at the GHZ landing) AND the SCREENSHOT strip-color check
(count of blue/red/orange/green strip pixels in the landing window > 0). RED
today (no card); GREEN after.

RESULT (2026-07-06): GREEN. Built chain (P6_FRONTEND_CHAIN=1 P6_GHZCUT_BOOT=1),
booted GL RA, drove the full intro live (Logos->Title->Menu->AIZ->GHZCutscene->
GHZ all fired). At the GHZ landing the card SPAWNED and ran the complete
decomp choreography: p6_w_tc_state marched 0(SETUPBG)->1(OPENINGBG)->2(ENTER)
->3(SHOWING)->4(SLIDEAWAY)->6(DONE) with p6_w_tc_timer 24->1040 and
p6_w_tc_draw_faces 4-8 (the colored polys emitted into the direct list every
frame). Screenshot gate: RED=1 on the no-card landing (_shots/213437 +
203843, strip mass 0/56); GREEN=0 on the card slide-in (_shots/214050, strip
mass 63625 -- all 4 strips + yellow BG). Viewed the frames: the authentic
Mania GHZ card (yellow BG + blue/red/orange/green diagonal strips + black
decor bar + white zone-name box) renders on top of the GHZ landing. _end =
0x060BD210 (44528 B headroom under the 0x060C8000 chain ceiling; #228 SAFE).
NOTE (cosmetic, separate/pre-existing): the card composites through the GHZ
landing's active VDP2 COLOUR OFFSET (the arrival FXRuby/fade path,
p6_io_main.cpp:7869) -> the strips render PASTEL/washed vs the source RGB. The
strips are hue-distinct + gate-detected; the wash is a VDP2-fade interaction
outside GL1 scope. STILL OPEN (separable next increment, tracked): the
zone-name LETTERS ("GREEN HILL"/"ZONE"/act digit) via Global/TitleCard.gif ->
TITLCARD.SHT/PAK + seam staging + glyph draw.
| GL2 | GHZ landing | Waterfall/water palette shimmer FROZEN (RotatePalette pal 1&2 @181-184/197-200 + SetLimitedFade every frame) | _v2__210=240=300 identical; GHZSetup.c:18-27 | waterfall-band pixel diff across consecutive shots > 0 |
| GL3 | GHZ landing | Sunflower + extend-flower aniTiles animations FROZEN (8/16-frame DrawAniTiles cycles) | sunflower identical _v2__210/240/300; GHZSetup.c:29-43 | sunflower tile-region diff across 2s > 0 |
| GL4 | GHZ landing | GHZ sky hue/gradient vs PC UNVERIFIED -- no PC reference image on disk for GHZ1 | _v2__210 (deep navy) | source PC ref (CLAUDE.md 4.4) then band-color compare |
| X1 | cross | Reference library covers ONLY the title. Missing PC refs: Logos (both screens), Menu (main + save select), AIZ intro keyframes (fly-in/jungle/coast/claw/flash), GHZ arrival cutscene, GHZ1 landing w/ TitleCard+HUD | tools/refs/mania_pc/ has 1 file | ref sourcing task per CLAUDE.md 4.4; every leg pass with no ref = declared UNVERIFIED |
| X2 | cross | Audio witness coverage = GHZ BGM only (qa_p6_music str_track==2). Every other leg's music/SFX is un-witnessed (L4/M8/A6/G2 above) | grep tools/: p6_w_str_track + one ScoreAdd sfx witness only | per-leg str_track + sfx-id witness assertions in the register-gate JSON |
| X3 | cross | Boot latency: 11s of boot-blue before first Logos content (shots 1-11) -- known emulated-CD IO class (#251), recorded here so the chain video review expects it | _v2__5/9 flat blue | qa_frontend_loadtime budget row |

Legs NOT fully verifiable on this burst: Menu input/confirm visuals (harness
dwell-nudge = no visible selection state to audit), GHZ landing parallax
motion + player moveset (camera static, players invisible -- known), Press
Start blink cadence + title flash white-peak (1 Hz sampling too coarse),
GHZCutscene beat-by-beat order (only 2 content samples land inside the 24s
window), all audio (no witnesses). PC references absent for every leg except
the title (X1).

## Execution order (one scene-slice per iteration, each RED->GREEN + screenshots)

1. F1 emit swap (all scenes at once -- one mechanism) + deepframe gates.
2. Title backdrop hardening (F2) + full title gate vs PC ref.
3. GHZ chain-leg pack closure (HUD/rings/badniks) + regression suite on chain.
4. Menu: DrawFace plates + UIBackground pattern + real confirm chain (retire nudge).
5. AIZ: BG2 line-scroll + FG hole fix.
6. F4 runtime config consolidation (retire flavor globals).
7. Full-chain verification burst + all gates + re-record video.
