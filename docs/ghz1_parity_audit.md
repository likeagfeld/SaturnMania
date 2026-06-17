# GHZ1 Engine-Build Parity Audit + Plan

Authoritative plan for making the **P6 engine-shipping build** (verbatim RSDKv5
decomp + retail Data.rsdk) play GHZ Act 1 *exactly like the original*. Written
2026-06-17 after hands-on testing exposed that the build is an early-stage true
port, not GHZ1 parity.

## Methodology correction (binding)

Per-feature gates ("is Bridge registered? sprite resident? collision golden?")
created false confidence: a feature passed while the level around it was broken
(100 rings, fire shield at spawn, frozen timer, blank sky, no loops, no
crumbling platforms). The correction:

- **Whole-level GHZ1 parity gate.** One capture asserts spawn state + object
  presence + key render layers, RED on the current build, re-run EVERY build so
  a fix to one thing cannot mask a regression in another.
- **Validate the level, never the isolated feature.** Before claiming any GHZ
  item done, confirm GHZ1 still boots to the correct spawn state and the prior
  items still hold.

## Dimension 1 - Objects (dominant gap)

GHZ1 StageConfig declares **28 stage objects**; the pack registers **3**
(GHZSetup, BGSwitch, Bridge). 25 missing, plus their NULL-stubbed Common deps
(`p6_closure_edge.c`). Residency is the gating engineering constraint
(W18/W19 wall, #247/#248) - fitting 30+ sheets is the real problem, not the
registration call.

| Tier | Missing GHZ objects | Cached? | Common deps (also unregistered) |
|---|---|---|---|
| Platforming (loops + brittle ground) | CorkscrewPath, ForceSpin, ForceUnstick, SpinBooster, Platform, CollapsingPlatform, BreakableWall | CorkscrewPath cached; rest `Common/` (pull) | - |
| Badniks | Motobug, BuzzBomber, Crabmeat, Newtron, Splats, Chopper, Batbrain | cached | Animals, Explosion, Debris, ScoreBonus |
| Hazards/props | SpikeLog, CheckerBall, BurningLog, ZipLine, Water, Decoration | GHZ cached; Water/Decoration `Common/` | Spring, Ring (overlay), ItemBox, Spikes |
| Boss + cutscene | DDWrecker, DERobot, Eggman, CutsceneSeq, GHZ2Outro | mostly cached | heavy closure |

## Dimension 2 - Spawn state (P0, small + foundational)

| Value | Build shows | Decomp | Cause |
|---|---|---|---|
| Rings | 100 | 0 | Player.c:643 `self->rings = Player->rings`; the carry-over static is non-zero at fresh start (zeroed only *after* the copy, line 645) |
| Shield | FIRE | SHIELD_NONE | Player.c:654 `if (Player->powerups) self->shield = Player->powerups`; same static-init gap. **This burns the bridge** (SHIELD_FIRE + burnable -> Bridge_Burn -> destroyEntity) |
| Timer | frozen 0'00"00 | counts up | scene timer not ticked in `p6_ghz_frame` (decomp ProcessEngine ticks it at ENGINESTATE_REGULAR) |

Fixing P0 zeroes rings, removes the shield, unfreezes the timer, AND stops the
bridge burning - one small fix, four visible bugs.

## Dimension 3 - Render

GHZ1 layers: `BG Outside` (sky), `BG Cave 1`, `BG Cave 2`, `FG Low`, `FG High`.
The present (`p6_vdp2.c:143,468`) draws only NBG1 = FG ("BG parallax not yet
drawn"). -> blank sky is expected. Needs the 3 BG layers on a second VDP2
scroll (NBG2/RBG) + the `Water` surface.

## Phased plan

- **P0 - spawn-state init** (rings/powerups=0, timer tick). RED-gate-first.
- **P1 - BG parallax + water render** (the sky).
- **P2 - object registration sweep, residency-gated**, tiers above
  (platforming -> badniks+deps -> hazards -> boss/cutscene). The bulk; the
  residency budget gates it throughout. (Bridge registration already staged,
  folds in here; BurningLog must be ported in the hazards tier.)
- **P3 - whole-level parity gate** wired from P0 onward.

## Status

- [x] P0 spawn-state init -- rings/powerups=0, timer ticks (a6af806, user-confirmed)
- [x] Bridge visibility -- planks render (folds into P2). MEASURED root cause: the
      Bridge class was registered and instantiating (collision "present"), but its
      sprite sheet `GHZ/Objects.gif` (sheetID 14) was the 9th staged sheet and the
      VDP1 bind table (`P6_VDP1_NSHEETS`) + the band-store slot count
      (`SATURNSHEET_SLOTS`) were both capped at 8 -> the 9th bind returned -1 ->
      every plank blit dropped silently (`dropbysheet[14]=251` -> "present but
      invisible"). Fix: stage `GHZOBJ.SHT`, bump both caps 8->9. User-confirmed
      planks render.
- [ ] P1 BG/water render (the blank sky)
- [ ] P2 object registration sweep -- the loop (CorkscrewPath+ForceSpin) and the
      brittle/crumbling ground (CollapsingPlatform) are the highest-value missing
      content objects; then badniks+deps, hazards, boss/cutscene.
- [ ] P3 whole-level parity gate
