# Whole-Game Mass-Port Plan — Sonic Mania → Sega Saturn (engine-shipping flavor)

Source: multi-agent measurement+validation workflow `wf_81270090-0b7` (2026-06-17). The
12 main zones were agent-deep-measured; the finalize agent then produced this plan and a
self-built adversarial-gap appendix directly from the authoritative data (the 7 parallel
adversarial-lens agents were blocked by a transient server throttle, so the finalize agent
constructed and verified the critique itself). Every number cites
`docs/{scene,object,dropin}_census.json`, `memory/*`, or the cached decomp.

Authority note: this plan supersedes `BIBLE.md` §2.1 ("we do not port the RSDKv5 engine").
That is STALE — Tasks #194–#246 MEASURED-PROVED verbatim RSDKv5 + decomp TUs on SH-2, GHZ1
continuous, 48.92 fps banked.

---

## (0) EXECUTIVE SUMMARY + CRITICAL PATH + BUILD-CYCLE ROLLUP

**Ships today (measured baseline):** `build_shipping.sh` (P6_ENGINE_SHIPPING) boots verbatim
RSDKv5 → GHZ Act 1 continuous, GHZ1→GHZ2 act-advance on real signpost cross (#232/#236),
48.92 fps banked (#246), ~45 s load (#251). 31 objects registered. cart=extram4 hard
requirement (`cart-4mb-extram-measured-map`).

**Gap to whole-game:** `object_census.object_count = 544` TUs; **383 distinct classes still
need registering** across 94 scenes / 42 folders. The work is NOT "write game logic" (the
decomp TUs exist; 1172 cached in `tools/_decomp_raw/`). It is **(a) make each verbatim TU
register+load+draw within the Saturn residency walls, and (b) the four shared walls that
block whole CLASSES of content at once.**

**THE CRITICAL PATH (gates everything; nothing parallelizes around these):**
1. **S1 — Entity-stride wall (344 B).** 53 distinct classes exceed it; Player (556 B, 36
   scenes), SpecialRing (400 B, 27), TitleCard (864 B, 30), CollapsingPlatform (656 B, 19)
   recur in nearly every gameplay scene. UNIVERSAL — until the pool admits wide structs
   generically, no zone past GHZ fully populates. Dual-stride admits 556 today; must rise to
   a **tier model ≥1056 B** (UICreditsText), not a hardcoded 556.
2. **S2 — DATASET_STG anim-pool wall.** STG = 150 KB, ~99 % full at GHZ1 (usedStorage is
   WORDS×4=bytes). Shared pool; adding one object can starve a working one. Per-zone the
   pool must be **swapped, not grown**.
3. **S3 — VDP1 draw-reduction.** 9.3× overdraw (every sprite a 64×64 box; 77,824 box-px vs
   8,333 content-px). Tolerable at 12 GHZ entities (48.9 fps); fatal at HCZ/FBZ/SPZ
   densities (1287–2016 placed). Must land before high-density zones.
4. **S4 — Per-zone residency-swap machine.** The cart is FULLY claimed by GHZ resident
   sheets + GFS windows + VDP1 store. A 2nd zone needs teardown+reload (cart store, band
   stores, anim pool, VDP1 sheet table) keyed on `currentSceneFolder` change.

**Build-cycle rollup** (1 Docker build ≈ 10 min; 1 RED-gate + 1 capture + 1 verify_done per
increment, CLAUDE.md §7):

| Track | Increments | Build-cycles |
|---|---:|---:|
| Shared infra S1–S4 + load-time + boot chain | 9 | ~22 |
| 12 main zones (acts + bosses + gimmicks) | 12 | ~150 |
| Special/bonus (BSS 36-scene shell + UFO 3D) | 2 | ~30 |
| UI/menu/save/cutscene/ending | 4 | ~28 |
| Characters (Tails AI + Knuckles + Mighty/Ray) | 4 | ~22 |
| Whole-game regression + master + real-HW Satiator | — | ~18 |
| **TOTAL** | | **~270 build-cycles** |

Multi-year at one focused increment/session. Value: every increment ships a bootable, gated
artifact, and the critical-path order means each unblocks the next instead of stacking
unfinished scaffold. **Recommended first action: S1 (entity-stride tier model)** — single
highest-leverage unblock (53 classes, every gameplay scene), pure data-structure change with
a clean offline gate.

---

## (1) SHARED-INFRASTRUCTURE PHASES (must precede zone waves)

Each = RED-first gate → implement → GREEN → `verify_done.ps1` exit-0 → `qa_p6_ghz_regression.py`
union GREEN.

**S1 — Entity-stride TIER model.** 53 classes >344 B (`dropin_census.struct_over_344`); max
UICreditsText 1056 B. Implement a 3-tier slot model (narrow 344 / wide 576 / x-wide 1088),
WRAM-L (hot). Gate `qa_stride_tiers.py`: offline-assert every over-344 class ≤ its tier;
runtime-peek CollapsingPlatform spawns without clobbering its neighbor. RED today (656>556).

**S2 — Per-zone DATASET_STG swap + cart anim-pool relocation.** STG contents = 64 KB packed
collision + ~60 KB layers + ~26 KB anims. Make the anim pool zone-scoped (reset on folder
change); relocate the read-mostly slice to the cart free gap 0x22720000–0x227A0000 (~512 KB).
Gate `qa_stg_swap.py`: STG-used resets to baseline after a folder transition, not accumulated.

**S3 — VDP1 content-sized draw.** Size-bucketed slot pools (16/32/48/64) so each blit draws
w×h not 64×64 (DTS ST-013-R3 §5; blocker `jo_sprite_replace` sprites.c:143). Gate
`qa_p6_perf.py` M7 + VDP1 fill-px: RED 77,824 px / ~74 % VDP1 busy → GREEN ≤~12,000 px /
fps≥58 in-motion. (The present already runs on the slave SH-2 per #246; VDP1 is now the wall.)

**S4 — Per-zone residency-swap (teardown+chain-reload, folder-keyed).** Extend the F.2 band-
swap to the FULL residency set (cart sheet store, VDP1 sheet-bind table [9 slots in lockstep],
anim pool, layout band store) via the engine `ENGINESTATE_LOAD` dispatch. Gate `qa_zone_swap.py`:
GHZ→2nd folder→GHZ round-trip; bridge frames>0, loop pscount>0, no stale-tile garble
(screenshot pixel-mass vs clean ref — the #250 lesson that a code-proxy gate lied).

**S5 — Load-time: read-ahead phase-2 + global-SFX pre-pack.** ~45 s load, 94 file-open seeks
dominate; pre-pack ~50 global SFX into one blob + offline-bake TileConfig → ~24–30 s floor.
Gate `qa_p6_loadtime.py`.

**S6 — Boot chain: Logos → Menu → zone.** `Menu/Scene1.bin` = 299 entities, ~30 UI classes
ALL >344 B (depends on S1) + UIControl/UIButton/UISaveSlot; migrate the #145–#148 hand-port UI
foundation onto the engine path. Gate: boot reaches Menu, NEW GAME → GHZ1 via the engine chain.

**S7 — Save (SaveGame.c) over backup RAM.** Wire `save_slot` to BUP; NEW/CONTINUE/zone-unlock.
Gate: power-cycle preserves zone progression.

**S6-adjacent (promoted) — DrawString.** Currently stubbed (`draw_stub_set`) → blocks readable
HUD/tally/cards across ALL zones. NOT optional; land early.

---

## (2) PER-ZONE PORT WAVES (sequenced; the 6-edit recipe + gates)

**6-edit recipe (per object, AFTER `qa_p6_object_preflight.py <Obj>` is GREEN P1–P12):**
1. add verbatim `Game_<Obj>.{c,h}` to the zone overlay TU list;
2. `RSDK_REGISTER_OBJECT`/`register_object_full`;
3. `-u`-root its undefined externals in the pack gc-link (dep-closure);
4. add its anim `.bin` to the cart anim pack + confirm sheet staged;
5. migrate its witness into the overlay entry TU (flat-TU rule);
6. resident per-object anim-load witness (`<obj>->aniFrames`, `p6_saturn_anim_allocfail`).
The build CONFIRMS; it does not DISCOVER.

**Per-build gate:** `qa_p6_ghz_regression.py` union (shared-pool starvation only shows in full
re-validation).

**Wave order** (front-load residency-cheap/already-furthest; back-load densest/novel-gimmick):

| Wave | Zone(s) | max entities | unreg classes | notes |
|---|---|---|---|---|
| W1 | GHZ complete (Act1+2) | 1106 | 26 | template zone; DDWrecker boss establishes the badnik-break→Explosion/Animals/ScoreBonus shared sub-project (closes #252–#255) |
| W2 | AIZ (intro) | 42 | 10 | smallest gameplay folder; proves the recipe at min residency |
| W3 | OOZ1+OOZ2 | 777 | 20/30 | **store_fits=True** — only main zones whose sheets fit the store today; lowest-risk first real new zone |
| W4 | CPZ | 932 | 42 | ChemPool water + CPZBoss/AmoebaDroid |
| W5 | PSZ1+PSZ2 | 2016 | 29/32 | **needs S3** (PSZ2 densest) |
| W6 | SPZ1+SPZ2 | 1768 | 33/34 | LEDPanel/WeatherTV (Z-track FillScreen/DrawString) |
| W7 | HCZ | 1069 | 44 | full Water.c + Whirlpool (needs Z2 scanline FX) |
| W8 | MSZ (3 scenes) | 1331 | 51 | EggLoco train + MSZ/OOZ peek; highest distinct-class count |
| W9 | FBZ | 1879 | 53 | densest + most classes; needs S3+S4 mature |
| W10 | SSZ1+SSZ2 | 1278 | 39/28 | plant gimmicks + EggTower |
| W11 | LRZ1+2+3 | 928 | 34/38/15 | rising-lava + Fireworm + Heavy King |
| W12 | MMZ | 1280 | 38 | scaling FarPlane (needs FX_SCALE/ROTATE) |
| W13 | TMZ1+2+3 + ERZ | 1330 | 28/32/18 | 3 acts + Egg Reverie final |

**Per-wave residency rollup (the wave entry gate):** `sheets_to_stage` union vs the 9-slot VDP1
table + cart store (393216 B) — shared sheets (Global/SuperButtons 33 scenes, Objects3 27,
Objects2 26) are resident-once, only per-zone `<Zone>/Enemies.gif`/`Objects.gif` swap (S4);
anim-pool Σ(frames×36+anims×28) vs the zone-swapped STG (S2); widest class vs the S1 tiers. A
wave that overflows splits (band-window the offending sheet, the W12a pattern).

---

## (3) SPECIAL / BONUS / CUTSCENE / UI TRACKS

- **T1 Blue Spheres (BSS):** 36 `SpecialBS/Scene*.bin`, 1 object each (procedural grid). Saturn
  = VDP2 RBG0 perspective-floor (NOT the UFO mesh). One port, 36 layouts. Sequence after W3.
- **T2 UFO 3D specials (hardest, Z1):** 7 scenes, ~680 entities each; `UFO_Player` 468 B (S1);
  RSDK 3D mesh → Saturn VDP1 textured quads via `slPutPolygon` (SGL ST-238-R1 + EXAMPLES/SGL/FLYING).
  A sub-engine, ~8–10 cycles, LAST among content.
- **T3 UI/Menu/Save:** the full widget set (~30 UI classes >344 B); migrate #145–#148; LSelect/
  Options/save-slot UI. Depends on S1+S6.
- **T4 Cutscenes/Ending/Credits:** GHZCutscene, MSZCutscene, Ending (7), Credits, Thanks,
  TimeTravel; CutsceneHBH 712 B (S1); LoadVideo (Cinepak FMV) stubbed → a Z-track item.

---

## (4) HARD ARCHITECTURE DECISIONS (measured options + recommendation)

- **D1 Code residency:** keep the proven **per-zone CART overlay** (hot code in WRAM-H, swap per
  zone; O1 GREEN). Code-in-cart is an UNPROVEN fps gamble (A-Bus wait-stated). Extend the overlay
  per wave (O1→O2→O3: relocate packed-collision off WRAM-H to grow the window to the ~57 KB set).
- **D2 60 fps:** land **S3 (VDP1 content-draw) first** — VDP1 is the current 74 %-busy wall (the
  present is already on the slave). If S3 doesn't reach 60, dual-SH2 loop1 is the only remaining
  path (deliberate per-zone-density investment). **Do NOT pursue the packed-cache — measured-dead**
  (cart packed-scan 2.07× slower than WRAM strided).
- **D3 Cart:** accept the **4 MB extram4 hard requirement** (no fits-in-2MB whole-game alternative).
- **D4 Scene residency:** keep the camera-local **band-window for layouts** (3 MB FBZ2 can't be
  resident) + cart-resident for sheets/anims (fps-safe −0.5 fps); S4 rebinds both per zone.
- **D5 Phase-Z Saturn-native rewrites** (no verbatim path): Z1 3D mesh (UFO), Z2 scanline FX
  (water deform/heat-haze), Z3 palette FX (fade/rotate). **Schedule each with its first consumer**
  (Z2↔HCZ/W7, Z1↔UFO/T2, Z3 incremental) — NOT a deferred bucket. DrawString promoted early (D5).

---

## (5) RED-FIRST GATE PLAN

- **Per-object (offline, pre-build):** `qa_p6_object_preflight.py <Obj>` P1–P12 GREEN — the gate
  that REPLACES build-cycles.
- **Per-increment:** a named `qa_<obj>_gate.py` RED before / GREEN after, peeking the live entity
  from the `.mcs` (objectEntityList @0x060553d4 ptr → dual-stride pool; classID @entity+54; aniFrames witness).
- **Whole-game regression UNION (binding, grows every wave):** extend `qa_p6_ghz_regression.py` to
  every shipped zone (each object's aniFrames>0 + per-shared-infra: STG-swap resets, VDP1 fill-px ≤
  budget, entity-tier fits, fps ≥ target, load ≤ budget). NO build is "good" until the FULL union is
  GREEN, not the new object alone.
- **verify_done.ps1:** Gate V-REG + every Vn visual gate + the union, exit-0 mandatory.
- **Capture discipline:** host-quiet; validate every capture (cont_frames large + boot-flavor
  witness); pixel-mass/SSIM for visual fixes; savestate-peek for register/memory questions.

---

## (6) ADVERSARIAL-GAP APPENDIX (self-constructed; agents throttled — re-run when clear)

| # | Gap | Verdict | Closed by / rejected because |
|---|---|---|---|
| G1 | 344 B stride treated per-zone; it's universal | REAL | promoted to critical-path S1 (tier ≥1056) before any wave |
| G2 | BIBLE says don't port the engine | REAL but STALE | plan supersedes BIBLE §2.1 with measured P6.7/P6.8 reality |
| G3 | one-by-one adds silently regress shipped zones | REAL | §5 whole-game regression UNION + S2 zone-swapped pool |
| G4 | cart is free bulk space | REJECTED | cart fully claimed; plan uses only the 2 measured free gaps |
| G5 | pack a range-field cache for 60 fps | REJECTED (measured-dead) | cart packed-scan 2.07× slower; D2 routes 60 via S3 + dual-SH2 |
| G6 | usedStorage budget undercounts | REAL | S2 + preflight P5 use usedStorage×4=bytes (99.4 % full, not 24.9 %) |
| G7 | UFO & BSS are one 3D engine | REJECTED | BSS=RBG0 floor, UFO=VDP1 mesh; different sub-engines/waves |
| G8 | load time is host-disk, ignore | REJECTED | memcache gave identical timeline → emulated-CD latency is real; S5 |
| G9 | HUD text just needs a font | REAL | DrawString stubbed → promoted to early shared-infra |
| G10 | zone density doesn't matter | REAL | waves front-load store_fits/low-density, back-load FBZ/PSZ/SPZ |
| G11 | boot chain is trivial UI | REAL but gated | Menu widget set entirely >344 B → S6 depends on S1 |
| G12 | same-folder reload re-uploads safely | REJECTED (#250) | garbles FG; S4 keys on folder CHANGE + screenshot-gates |
| G13 | Phase-Z rewrites all deferred to end | REAL if bucketed | schedule each Z with its first consumer (D5) |
| G14 | 556 B stride already solves it | PARTIAL | 23 classes exceed 556 → S1 tiers to ≥1056 |
| G15 | code-in-cart retires the overlay | REJECTED (unproven fps) | D1 keeps the proven per-zone overlay |

---

---

## (7) SECOND-SYNTHESIS RECONCILIATION (on-disk-verified, 2026-06-17)

The measure+adversarial workflow (`wf_81270090-0b7`) was resumed. The parallel deep-measures
and the 7 adversarial-lens agents were **rate-limited out a second time** (the parallel fan-out
trips the server limit; do NOT re-run it). Its **serial finalize agent broke through** and
produced an INDEPENDENT second synthesis (structure A0–A9 / C1–C6 / its own G-1…G-14). Having
two independent syntheses of the same census is itself an adversarial cross-check — so every
point where they DISAGREE was resolved here against on-disk ground truth (`Object.hpp`, the
census JSONs). **The cross-check caught a real error in the second synthesis** (its G-1) and
confirmed several of its additions; verdicts below are measured, not adjudicated by preference.

### 7.1 Disputed points — resolved against on-disk authority

| Point | Synthesis-1 (S1–S7 above) | Synthesis-2 (resume) | ON-DISK VERDICT (citation) |
|---|---|---|---|
| **ENTITY_COUNT** | 1216 (via memory) | "G-1: 1216 is WRONG -> 1280" | **1216 = 0x4C0.** `Object.hpp:43-45`: RESERVE 0x40 + SCENEENTITY 0x440 + TEMP 0x40 = 0x4C0. The "0x500 (1280)" on line 34 is a STALE comment (pre-dates TEMP halving 0x80->0x40, line 39). **Synthesis-2's correction is itself wrong; memory/Synthesis-1 right.** |
| **Scene-placement slots** | not isolated | folded into "1280" | **1088 = 0x440** (`SCENEENTITY_COUNT`, `Object.hpp:44`). Scene-placed entities occupy this region; the real placement-overflow threshold is ~1088, NOT 1280. |
| **Dense-act entity overflow** | densities in the wave table, not a distinct wall | "G-2: the #1 hard decision, 8 acts >1280, load-time WRAM crisis" | **RESOLVED 7.3 (measured).** NOT a WRAM crash -- the table is FIXED at 1216; the Saturn loader DROPS entities with slotID >= 1152 (witnessed) and SPILLs 1088..1151 into a 64-slot temp buffer. **15 DROP-class scenes** (PSZ2 drops 864 ... LRZ2 drops 23), threshold slotID >= 1152, gate `qa_entity_slots.py`. GHZ/CPZ/OOZ/AIZ SAFE; first bite = SPZ -> fix before order 3. Both syntheses' framing corrected. |
| **Entity-STRIDE wall** | "S1: ≥1056 tier; 53>344, 23>556; RED today" | "C-3: already solved via wide-slot, low-risk" | **Synthesis-1 more right.** `Object.hpp:48-55`: dual-stride admits 556 in reserve/temp ONLY; classes **>556** still don't fit — and `CollapsingPlatform`=656 (GHZ gameplay), `TitleCard`=864 (≈30 scenes), `UICreditsText`=1056 exceed 556. The mechanism EXISTS (Synthesis-2's point) but its WIDTH is insufficient -> **S1 stays critical-path, reframed as a bounded tier-extension of the existing pool, not a from-scratch build.** |
| **VDP1 sheet-store free** | "fully claimed" / effectively unknown | "G-9: 88,081 B free" | **88,081 B free.** `dropin_census.json store_bytes=393216 − staged_banded_bytes=305135`. **Synthesis-2 right** — bake 88,081 as the A9/S2 ceiling constant. (Distinct from the CART, which IS full.) |
| **Render-stub set** | only DrawString enumerated | "G-5: +DrawTile +SwapDrawListEntries; G-6: DrawBlendedFace NOT stubbed" | **Synthesis-2 right.** `object_census.draw_stub_set` (verified) = **7 stubs: DrawTile, DrawAniTile, DrawString, DrawDeformedSprite (water+heat-haze, one entry), FillScreen, SwapDrawListEntries, LoadVideo.** DrawBlendedFace absent -> "latent, not stubbed." Fold all 7 into S3/D5. |
| **object_count / registered** | 544 / 31 | 544 / 31 | **Agree, verified.** `object_count=544`; `registered_objects[]` = 31. |
| **Zone order #2/#3** | OOZ first (store_fits=True, measured) | CPZ -> SPZ | **Not ground-truthable; Synthesis-1's `store_fits` is a measured criterion** -> keep OOZ early as the lowest-residency-risk first new zone; CPZ-class water/AniTile work front-loads the shared render track regardless. |
| **Build-cycle rollup** | ~270 | ~174–190 | Estimate variance (different amortization of the 31 pre-registered classes); not ground-truthable. Treat **~190–270** as the band; both agree it is multi-year, one-port-one-gate. |

### 7.2 Net adjustments folded into the plan above

- **S1 (entity-stride) is CONFIRMED as the highest-leverage first action** and is a tier-extension
  of the EXISTING dual-stride pool (admits 556 today) up to a ≥1056 x-wide tier so the >556 classes
  (`CollapsingPlatform` 656, `TitleCard` 864, `UICreditsText` 1056) fit. The resume's "already
  solved" was checked and rejected on-disk.
- **New critical-path item — dense-act entity DROP wall** (the resume's G-2, corrected + measured
  in 7.3): `ENTITY_COUNT` actual **1216**, scene region **1088**, DROP threshold slotID **>= 1152**.
  NOT a WRAM crash -- graceful drops, gated by `qa_entity_slots.py`. **15 DROP-class scenes**; the
  fix (raise-region / camera-stream / accept-far-drops) must land **before SPZ (order 3)**. GHZ/CPZ/
  OOZ/AIZ are SAFE.
- **S3/D5 render set completed to 7 stubs** (add `DrawTile`, `SwapDrawListEntries`).
- **A9/S2 ceiling constant = 88,081 B** VDP1-sheet-store free.
- **Shared-infra is lighter than Synthesis-1 implied:** SaveGame, Music, ScoreBonus, Shield, Dust,
  GHZSetup are already `reg=True` (in the 31) -> the badnik-cascade / ItemBox / SaveGame / Music
  phases are verify-and-finish, not full builds. ItemBox's real `deps_unreg` = [Crate, Debris,
  Explosion, InvincibleStars, Platform] (heavier than "Debris/Dust/Explosion").

### 7.3 RESOLVED (measured 2026-06-17 -- gate `tools/qa_entity_slots.py`, GREEN on shipping scope)

**Question:** does `scene_census.entity_total` equal live occupancy of the 1088-slot SCENEENTITY
region, or count placed objects the engine filters out of slots?
**Measured answer (engine source + census + the gate):** `entity_total` is a COUNT
(`build_scene_census.py:127`). The engine places each entity by its EXPLICIT `slotID` read from
Scene.bin (`Scene.cpp:622`), and Mania packs slotIDs densely (every dense scene measured:
`max_slot == n_ent-1`). The Saturn `LoadScene` (`Scene.cpp:627-643`) routes: `slotID < 1088` ->
scene region; `1088..1151` -> 64-slot temp buffer (SPILL); **`>= 1152` -> DROPPED + witnessed
`p6_saturn_tempentity_skips`**. `objectEntityList` is a FIXED 1216-slot allocation
(`Object.hpp:43-45`) -- it does NOT grow with density. **So the dense-act issue is NOT a WRAM
crash (this CORRECTS the resume's G-2 "load-time WRAM crisis") -- it is GRACEFUL entity DROPPING
(content parity), threshold slotID >= 1152, already instrumented.**

**Gate result (94 scenes): 77 SAFE, 2 SPILL (0 drops), 15 DROP-class:**

| scene | n_ent | max_slot | dropped (slotID>=1152) |
|---|---:|---:|---:|
| PSZ2/Scene2 | 2016 | 2015 | **864** |
| SPZ1/Scene1 | 1971 | 1970 | 819 |
| SPZ1/Scene1d | 1891 | 1890 | 739 |
| FBZ/Scene2 | 1879 | 1878 | 727 |
| SPZ2/Scene1 | 1768 | 1767 | 616 |
| SSZ1/Scene1 | 1558 | 1557 | 406 |
| HCZ/Scene1 | 1469 | 1468 | 317 |
| MSZ/Scene2 | 1331 | 1330 | 179 |
| TMZ2/Scene1 | 1330 | 1329 | 178 |
| FBZ/Scene1 | 1287 | 1286 | 135 |
| MMZ/Scene2 | 1280 | 1279 | 128 |
| SSZ2/Scene1 | 1278 | 1277 | 126 |
| LRZ1/Scene1 | 1207 | 1206 | 55 |
| PSZ1/Scene1 | 1185 | 1184 | 33 |
| LRZ2/Scene1 | 1175 | 1174 | 23 |

(SPILL, 0 drops: `MSZ/Scene1k` max 1145; `GHZ/Scene2` max 1107.)

**Shipping GHZ is GREEN:** GHZ1 max_slot 1040 (SAFE); **GHZ2 max_slot 1107 -> SPILL (20 entities in
the temp buffer, 0 drops)** -- that is why GHZ2 ships despite 1108 placed. **CPZ (max < 1088), OOZ,
AIZ are SAFE** -> no entity fix needed for them.

**Execution-order verdict (now MEASURED, not asserted):** the wall first bites at **SPZ** (SPZ1
drops 819) -> the dense-act fix must land **before SPZ (order 3)**, exactly the plan's claim. The
fix per dense act is a gate-driven CHOICE: (a) raise `SCENEENTITY_COUNT` for that act (PSZ2 would
need ~2016 slots = +319 KB WRAM-L -- only the worst acts need the full raise); (b) **camera-stream
entities** (instantiate near-camera, stream the rest from the parsed Scene.bin -- avoids the WRAM
cost; the general solution); (c) accept drops for non-critical far objects (tolerable at LRZ2's 23,
NOT at PSZ2's 864). `qa_entity_slots.py` is the per-zone RED-first gate -- it flips RED when a
DROP-class act enters shipping scope and GREEN when that act's fix lands.

### 7.4 Adversarial-validation status

The independent 7-lens parallel pass never completed (throttled twice). The validation actually
delivered is: Synthesis-2's self-run census critique (its G-table) PLUS this on-disk
reconciliation of the two independent syntheses — which is stronger than a single lens pass would
have been, because it caught a measured error (the 1280 misread) that a from-scratch lens would
not have had the second synthesis to diff against. A further independent pass should be run
**serially (one agent), never as the parallel fan-out.** It is not a blocker to starting S1.

### 7.5 S1 + 7.3 UNIFIED: camera-local entity pool (design decision, 2026-06-17)

S1 (entity-stride) and 7.3 (dense-act DROP wall) are the SAME problem -- the size and seating
of the scene-entity region -- and are solved by ONE mechanism (user decision: unify). Measured
basis:
- **S1 wall (gate `qa_stride_tiers.py`, RED):** 53 over-344 classes, biggest **1056**
  (UICreditsText); zero exceed 1088. Tiers NARROW 344 / WIDE 576 / **X-WIDE 1088** cover the
  whole game. **All 17 over-556 classes are SCENE-PLACED** (CollapsingPlatform 656 x203,
  TitleCard 864 x30, TilePlatform, LRZRockPile, ChemicalPool, FarPlane, ...; `qa_stride_tiers`
  + a scene_census cross-ref). So widening only reserve/temp does NOT solve S1 -- the oversize
  objects live in the NARROW 1088-slot scene region; a uniform x-wide scene region is DEAD
  (1088 x 1088 = 1.18 MB > WRAM-L 1 MB).
- **Camera-local sizing (gate `qa_camera_local_pool.py`):** sliding the RSDKv5 camera-active box
  over every scene's real entity positions, the PEAK simultaneously-near population is only
  **~101 total / ~26 WIDE+ / ~11 X-WIDE** (default 680x496 proxy; ~121/43/13 at a generous
  960x640) -- vs the resident **1216-slot** table that holds ALL placed entities.

**The unified design:** instantiate only camera-NEAR entities into a tiered pool of **~256 total
slots** (2x headroom over 121) with **~32 X-WIDE(1088B)** + ~64 WIDE(576) + ~160 NARROW(344) =
**~127 KB**, streaming entities in/out as the camera moves. This single change: (a) admits the 17
oversize classes -> **S1 GREEN**; (b) bounds the live population to the near-set -> the DROP wall
**7.3 GREEN** for all 15 dense acts; (c) **FREES ~318 KB WRAM-L** (127 KB pool vs 445 KB resident
table) -- it pays for itself.

**Implementation = a real engine sub-project** (not a data tweak; ~the plan's ~6-cycle
entity-streaming item, now the #1 execution increment). Roadmap:
- D1 SIZING -- DONE (the two gates above).
- D2 SLOT-REFERENCE STABILITY -- DONE (measured): FEASIBLE. (1) Native RSDKv5 NEVER destroys
  out-of-bounds entities -- ProcessObjects only sets `entity->inRange` (`Object.cpp:499-555`), slots
  stay resident -- so streaming IS a semantic change, handled by a SLOT-STABLE scheme (slotID stays
  1:1 for every placed entity; never reused). (2) ALL slot access already routes through ONE accessor
  `SaturnEntityAt(slot)` / `SaturnEntitySlot(e)` (`Object.hpp:453-454`, def in `Object.cpp`) -- the
  indirection has a SINGLE implementation point, not the 211 `RSDK_GET_ENTITY` call sites. (3) Cross-
  refs are predominantly CONTIGUOUS/ADJACENT slots (`PlatformControl` loops children at
  `platformSlot += platform->childCount + 1`; `GetEntitySlot(self) - 1`) -> referrer+referent are
  co-located -> stream together; reads are shallow (`state`/`speed`/`childCount`/`position`/`classID`),
  satisfiable by a dormant per-slot record. (4) Fixed reserved-slot refs (Player/Zone/HUD, slots
  <0x40) are always resident -> safe.
  RESIDUAL -> D3: manager objects run setup while ACTIVE_NORMAL then drop to ACTIVE_BOUNDS
  (`PlatformControl` Create `active=ACTIVE_NORMAL` -> `ACTIVE_BOUNDS`). Keep always-active managers
  + their contiguous children RESIDENT and stream the bulk ACTIVE_BOUNDS objects (HYBRID). Sizing the
  resident set = an active-type census (parse Create bodies for `active = ACTIVE_*`), feeding D3.
- D3 active-type census -- DONE (gate `tools/qa_active_census.py`): classified by CREATE-time
  active (steady state; the transient ACTIVE_NORMAL most objects set when triggered lives in their
  State_* fns, not Create -- so "has both" is NOT a manager). **125 RESIDENT classes** (always-active
  Setup/HUD/Music/Zone/Camera/boss/UI singletons), 346 STREAMABLE, 60 NONE. Per-scene RESIDENT floor:
  **gameplay <=51** (TMZ2; most zones <=33), UI scenes higher (DAGarden 117, LSelect 97, Title 75) but
  tiny TOTAL (~76-119) so the whole scene fits. Worst materialized = resident_total + camera-near
  streamable ~= **51 + ~100 = ~150** -> the **~256-slot pool (~32 x-wide) holds it with headroom**;
  D1's sizing survives the floor.
  POOL DESIGN (final, data-backed): ~256 tiered slots (NARROW 344 / WIDE 576 / X-WIDE 1088, ~32
  x-wide) ~= 127 KB; a RESIDENT-pinned sub-region holds always-active entities (<=51 gameplay /
  full-scene for small UI scenes), the rest stream the camera-near set; slot-stable `SaturnEntityAt`
  indirection (near=pool EntityBase, far=dormant {classID,pos,flags} record); RegisterObject
  threshold -> 1088. Frees ~318 KB WRAM-L. DESIGN COMPLETE.
- I1..I6 implement (multi-build, NEXT PHASE -- each is one RED-gated build; the entity pool is the
  project's highest-risk subsystem [dual-stride bisects, BSS-overflow class, #249/#250 corruption],
  so each step is additive/reversible and GHZ stays GREEN throughout). Change sites: `Object.hpp:56-67`
  (pool tiers), `Object.cpp` RETRO_SATURN `SaturnEntityAt`/`SaturnEntitySlot` (the single indirection
  point), `Scene.cpp:620-646` (LoadScene entity loop), `p6_io_main.cpp` (pool home + witnesses).
  - **I1 MANIFEST-ENUMERATION** (gate `tools/_portspike/qa_p6_manifest.py` WRITTEN + RED-confirmed;
    source edits TURNKEY below). De-risked scope: prove the enumerate-every-placed-entity path via 3
    count/checksum witnesses, ZERO new allocation (the stored side-table waits for I3 when the
    pool-shrink frees a verified ~24KB home -- placing it today is a guess the BSS-overflow history
    forbids). Additive; touches neither SaturnEntityAt nor the pool -> GHZ byte-identical. THE EXACT
    EDITS (linkage resolved = pattern A, extern "C", so the map symbol is `_p6_w_manifest_*` like the
    other gate witnesses):
    1. `p6_io_main.cpp` -- inside the existing `extern "C" {` block (after `p6_w_io_gfsinit`, ~:65):
       `__attribute__((used)) int32 p6_w_manifest_n=0, p6_w_manifest_maxslot=0, p6_w_manifest_csum=0;`
    2. `Scene.cpp` -- a NAMESPACE-scope decl before `LoadSceneAssets` (block-scope `extern "C"` is
       illegal): `extern "C" { extern int32 p6_w_manifest_n, p6_w_manifest_maxslot, p6_w_manifest_csum; }`
    3. `Scene.cpp` -- RESET before the object loop (after the tempEntityList alloc / `#endif #endif`,
       ~:558, RETRO_SATURN-guarded): zero the 3 witnesses.
    4. `Scene.cpp` -- FOLD in the per-entity loop right after `entity->position.y = ...` (:653,
       RETRO_SATURN): `++n; if(slotID>maxslot)maxslot=slotID; csum = csum*33 + classID; csum=csum*33+slotID;
       csum=csum*33+(entity->position.x>>16); csum=csum*33+(entity->position.y>>16);` (counts ALL placed,
       incl. the slotID>=1152 drops -- the set the manifest must eventually stream).
    5. `build_p6scene_objs.sh` (+ verify `build_shipping.sh`) -- add `-u _p6_w_manifest_n
       -u _p6_w_manifest_maxslot -u _p6_w_manifest_csum` beside the existing `-u _p6_w_*` roots (~:344).
    Build (build_shipping.sh ~10min) -> capture frame 130 -> `qa_p6_manifest` asserts n==1041,
    maxslot==1040, csum!=0 -> GREEN; then commit + the diag sweep stays byte-identical.
  - **I2 INDIRECTION pass-through -- LANDED + GREEN.** qa_p6_i2 resolve_ok==1 (all 1216 slots resolve
    through SaturnSlotToPoolSlot to the byte-identical pre-I2 address); qa_p6_manifest (csum byte-
    identical) + qa_p6_phantom + qa_p6_layout L3 110/110 regression-GREEN; both flavors build clean
    (_end shipping 0x060b5984 / diag 0x060b6004, under the W17 floor). RESOLVED DESIGN
    (the I1 "stored side-table waits for I3" tension is dissolved by making the indirection a FUNCTION,
    not a stored table, in I2 -> ZERO new allocation, no BSS-overflow risk):
    1. `Object.hpp` (RETRO_SATURN block, just before `SaturnEntityAt` ~:429): add
       `inline int32 SaturnSlotToPoolSlot(int32 slot) { return slot; } // I2 identity; I3 -> table lookup`.
    2. `Object.hpp` `SaturnEntityAt(slot)`: compute the SAME dual-stride offset but for
       `SaturnSlotToPoolSlot(slot)` instead of raw `slot` (identity in I2 -> byte-identical). Keep
       `SaturnEntitySlot` as-is (entity ptr -> POOL slot; the inverse map lands with the I3 table).
    3. `p6_io_main.cpp` (extern "C" witness block): `__attribute__((used)) int32 p6_w_i2_resolve_ok=0;`
       + a ONE-SHOT load-time self-check (run it in `p6_scene_load_and_arm` after InitObjects, beside
       `p6_purge_scene_players`): walk `slot` in `[0,ENTITY_COUNT)`, compare `RSDK_ENTITY_AT(slot)` to
       the pre-I2 direct offset (inline the old 3-branch formula as `i2_direct(slot)`); set
       `p6_w_i2_resolve_ok = 1` iff every slot matches, else 0.
    4. `build_p6scene_objs.sh` (+ `build_shipping.sh`): `-u _p6_w_i2_resolve_ok`.
    Build -> capture f130 -> `qa_p6_i2` GREEN (resolve_ok==1) AND `qa_p6_manifest` still n==1041 AND
    `qa_p6_layout` still 110/110 -> the byte-identical proof. The stored remap table is I3's job (placed
    in the pool-shrink-freed ~24KB home), at which point `SaturnSlotToPoolSlot` becomes the lookup and
    the SAME self-check re-gates it. Behaviour-identical; touches only the ONE indirection point, not
    the 211 call sites. Gate: all slots resolve 1:1; GHZ byte-identical.
  - **I3 SHRINK + RESIDENT-PIN**: pool -> ~256 tiered slots; pin always-active classes
    (qa_active_census RESIDENT set) at load; materialize the near-set from the manifest; far slots ->
    dormant {classID,pos,flags} record. Gate: resident + near entities present; GHZ play unchanged.
  - **I4 STREAMING**: instantiate manifest entities entering the near-window (run Create), dormant-ize
    those leaving. Gate: spawn-on-approach witness; GHZ play unchanged.
  - **I5 X-WIDE TIER**: ENTITY_WIDE_SIZE/x-wide -> 1088 + RegisterObject threshold -> 1088. Gate:
    `qa_stride_tiers` GREEN.
  - **I6 OVERSIZE REGISTER**: register the scene-placed oversize classes (CollapsingPlatform first) +
    verify on a dense scene. Gate: `qa_entity_slots` GREEN for that act + whole-game regression union
    + fps no-regress + on-screen parity SSIM.
This sub-project lands BEFORE SPZ (order 3) and is the gate for every dense zone.

---

## (8) END-STATE MEMORY/PERF LAYOUT + EXECUTION SEQUENCING (added 2026-06-19)

> **CORRECTED BY §9 (serial adversarial QA, 2026-06-19) -- read both.** Key reversals:
> CRAM is a WALL (Phase-Z Z3, bites shipping GHZ water), NOT a non-wall; the SGL 144-command
> sortlist is a 4th wall colliding with "WRAM-H frozen"; the camera-local pool's D2/I4 have
> CRITICAL correctness bugs (manager-child corruption + collected-ring-reappears) -> demoted to
> OPEN; whole-game AUDIO (58 BGM tracks) needs a new Phase S-AUDIO; the `_end`/ceiling figures
> are corrected (real enforced ceiling 0x060B8000); load-time = the Saturn-native scene-bake.

Sections 0-7 plan the OBJECT PORT (the 4 walls, the waves, the camera-local pool). This
section adds the dimension that was causing per-feature thrash (the GHZ 64-byte `_end`
fight, #258 cart relocation, the dual-SH2 scan-split's #228 boot trap): **a single
end-state memory + perf MAP, sized to the WORST-CASE zone, with a residency RULE per
resource so every future increment has a defined home and lands by construction.**
Every number is measured (census byte fields, `SaturnMemoryMap.h`, `p6_vdp1/2.c`, the
memory map).

### 8.1 The four real walls (measured) -- and the THREE non-walls

WORST-CASE demand vs capacity, whole game:

| Resource | Capacity | Worst-case zone demand | Verdict |
|---|---|---|---|
| **WRAM-H code/fixed** | 1 MB; `_end` ceiling = ANIMPAK **0x060B6600** | engine core + ANIMPAK 69 KB + GLOBALS 56 KB + PACKEDCOL + layout window + SGL area = FULL (`_end` 0x060b65c0, **64 B** headroom) | **WALL.** New engine code must NOT grow `_end`. |
| **Cart sheet store** | **393,216 B** banded (`dropin_census.store_bytes`) | **TMZ1/Scene1 = 634,388 B banded** (decoded 2.88 MB); **28/94 scenes overflow** | **WALL.** Worst zone is 241 KB over -- needs store growth or per-sheet band-window. |
| **WRAM-L runtime** | 1 MB | entityList **445 KB** + heap 630 KB + DATASET_STG 150 KB = over without care | **WALL today -> SOLVED by the pool:** camera-local pool 445->127 KB **frees ~318 KB**. |
| **VDP1 fill-rate** | ~1 vblank of rasteriser | 9.3x overdraw; FBZ/PSZ/SPZ 1287-2016 placed | **WALL at density (S3).** Not a VRAM-space wall. |
| ~~VDP1 VRAM~~ | 512 KB | 40-slot LRU working set; sheet pixels live in CART | **NON-wall** (sheets stream from cart). |
| ~~VDP2 VRAM~~ | 512 KB | CEL 256 KB (0x25E00000) + MAP 16 KB + band store | **NON-wall** (room; per-zone tile swap). |
| ~~CRAM~~ | 4 KB = 8x 256-color banks | tiles bank 0 + sprites bank 1 = **2 of 8 used** | **NON-wall** (6 banks free; per-scene palette load is free). |

The lesson the GHZ thrash taught: **stop spending effort on the non-walls and on
WRAM-H byte-shaving. The cart is the expansion store; the pool frees WRAM-L; WRAM-H is
frozen.**

### 8.2 RESIDENCY RULES -- where each kind of thing lives in the end state (binding)

So no increment ever has to re-derive a home:

1. **New object CODE -> per-zone CART overlay** (D1; `ovl_ring.ld`, swapped per zone via
   S4). NEVER WRAM-H. The overlay grows per wave (O1->O2->...); only the ACTIVE zone's
   overlay is resident.
2. **New sprite sheet -> CART banded store** (393 KB, S4-swapped per `currentSceneFolder`).
   NEVER WRAM-resident. Shared sheets (Global/SuperButtons 33 scenes, Objects3 27) are
   resident-once; per-zone `<Zone>/Enemies|Objects.gif` swap. A zone over 393 KB
   band-windows its largest sheet (the W12a pattern); TMZ1 (634 KB) is the canonical case.
3. **New anim pack -> CART OBJANIMPAK** (256 KB at 0x22760000, S2-swapped). Player anims
   stay in the WRAM-H ANIMPAK (69 KB, fixed). NEVER grow DATASET_STG resident.
4. **New entity -> camera-local POOL** (~256 tiered slots, I3-I6). NEVER the 1216-resident
   table. The pool bounds live population to the near-set (~150) -> scan + DROP-wall solved.
5. **New ENGINE compute that would grow `_end`** (the scan-split lesson) **-> CART overlay
   or fold into the pool**, NOT WRAM-H. WRAM-H `_end` is frozen at the 64 B headroom.
6. **New palette -> a free CRAM bank** (6 of 8 free). Free.
7. **New tile layer -> camera-local band-window** (D4; VDP2), per-zone swapped.

A gate enforces each: `qa_p6_cart` (store fit), `qa_entity_slots` (pool), `_end<0x060B6600`
(the WRAM-H freeze, now in `qa_p6_scansplit` S0 + `qa_p6_ghz_regression` R0).

### 8.3 Worst-case-zone budget verdict

The binding constraints, in order of how hard they bite:

- **Cart sheet store (HARDEST):** TMZ1 634 KB banded vs 393 KB store. Two fixes, sequenced:
  grow the store into the cart free gap where it fits (measure `qa_p6_cart` first), AND
  band-window the single largest sheet of the 28 overflow scenes. This is S4-adjacent and
  must mature before TMZ (W13) and the SPZ/MSZ band (W5/W6/W8).
- **Entity pool (SECOND):** PSZ2 2016 placed. Solved generically by the camera-local pool
  (I3-I6) -- ~256 near-slots hold the ~150 worst materialized set; far entities are dormant
  records. Lands before SPZ (order 3).
- **VDP1 fill-rate (THIRD):** S3 content-sized draw; needed before the dense zones.
- **WRAM-H (FROZEN, not a moving target):** nothing new goes there; rule 8.2.1/8.2.5.

CRAM, VDP2 VRAM, VDP1 VRAM are confirmed non-binding -- do not budget against them.

### 8.4 END-TO-END EXECUTION SEQUENCING (dependency-ordered; replaces ad-hoc per-feature)

**Phase A -- Memory architecture (unblocks all content; do FIRST):**
- **A1 = camera-local entity pool I3-I6** (§7.5). THE keystone: frees 318 KB WRAM-L, bounds
  the loop1 scan (15.62->~3 ms = locked 60 fps at the SOURCE, **subsuming the dual-SH2
  scan-split**), admits the 17 oversize scene classes (S1), kills the 15-scene DROP wall.
  I1+I2 landed; I3 (shrink+resident-pin) is the next build.
- **A2 = cart residency-swap machine (S4+S2)** -- per-zone teardown+reload of sheet store +
  OBJANIMPAK + VDP1 bind table + layout band store + overlay, keyed on folder change.
- **A3 = cart sheet-store sizing** -- grow store / band-window the 28 overflow scenes
  (TMZ1 634 KB the cap). Gate `qa_p6_cart`.
- **A4 = load-time (S5) -- DEPENDS ON A1+A2** (this is why the boot is ~45 s of blue/red
  loading). The fix (pre-pack ~50 global SFX into one blob + raw-on-disc sheets to kill
  the 7 s miniz + offline-bake TileConfig -> ~24-30 s) needs 64-128 KB of read-ahead/blob
  buffers that are **memory-BLOCKED today** (#251: WRAM-H 3.6 KB margin, WRAM-L full, cart
  = resident store, no guaranteed-free hole across transitions). **A1 frees 318 KB WRAM-L +
  A2's per-zone swap frees cart holes -> S5 lands right after A2, not "whenever."** Phase-1
  adaptive read-ahead already banked 52->45 s (commit 023ab37). IRREDUCIBLE: ~12 s Saturn
  BIOS (not game-controlled) + ~5-7 s genuine CPU decode -> ~24-30 s is the floor, NOT zero.
  Gate `qa_p6_loadtime`.

**Phase B -- Render/perf (before dense zones):**
- **B1 = VDP1 content-sized draw (S3)** -- size-bucketed slots, kill 9.3x overdraw.
- **B2 = the 7 render stubs (D5)** -- DrawString (HUD/cards, ALL zones) first, then DrawTile/
  DrawAniTile/DrawDeformedSprite/FillScreen/SwapDrawListEntries/LoadVideo with their consumer.

**Phase C -- Content waves (§2 W1-W13):** GHZ-complete -> AIZ -> OOZ -> CPZ -> ... -> FBZ ->
TMZ/ERZ, each using A2's swap machine + the 6-edit recipe. Front-load store-fits/low-density,
back-load FBZ/PSZ/SPZ/TMZ.

**Phase D -- Phase-Z native (scheduled WITH first consumer, D5):** Z2 scanline FX (HCZ water
W7) -> Z1 3D mesh (UFO specials T2) -> Z3 palette FX (incremental).

**Phase E -- Shell:** boot chain (Logos->Menu->zone, S6) + UI widgets (T3) + save (S7) +
characters (Tails AI/Knuckles/Mighty/Ray) + cutscenes/ending (T4).

The whole-game regression union (§5) runs THROUGHOUT (load-time S5 is now A4, sequenced
on the memory it depends on, not "whenever").

### 8.5 Disposition of the in-flight dual-SH2 scan-split (2026-06-19)

SHELVED as redundant. Measured: the loop1 inRange scan is 15.62 ms over all 1216 slots;
A1's camera-local pool iterates only ~256 -> scan ~3 ms -> compute-full 24.1->~12 ms = 60 fps
at the source, with NO slave fork and NO WRAM-H growth (which is what trapped it: +608 B
pushed `_end` 0x060b6820, 544 B past the ANIMPAK 0x060B6600 ceiling -> GFS-callback fault,
master PC 0x06000956). KEPT from that work: the decomp **parity audit**
([[ghz-scan-split-parity-audit]]) and the **layout/RED gates** (`qa_p6_scansplit` S0 =
`_end<0x060B6600`). The scan-split code stays gated-off/inert; it is only worth reviving if
A1 measures short of 60 on a specific dense zone (the D2 fallback).

---

## (9) ADVERSARIAL QA VERIFICATION + CORRECTIONS (2026-06-19, serial 6-lens audit)

The independent adversarial validation §7.4 flagged as never-completed (the parallel
fan-out throttled twice) was finally run SERIALLY: 6 lenses (budget / perf / pool-safety /
completeness / sequencing / hardware) + synthesis, each hunting on-disk counterexamples.
**Verdict: HIGH confidence in the FRAMEWORK + arithmetic (8 load-bearing claims CONFIRMED
exact on-disk) -- but 5 CRITICAL + 7 MAJOR defects.** The corrections below SUPERSEDE the
disputed claims in §7-§8 (same authority pattern as §7's reconciliation).

### 9.1 CONFIRMED on-disk (the spine holds)
Cart-store overflow headline (TMZ1 634,388 vs 393,216 B, 28/94 overflow -- exact);
entity-pool arithmetic (ENTITY_COUNT 1216; resident 445,440 B; camera-local 126,720 B;
frees 318,720 B -- exact); GHZ 60 fps from the pool (recompute: scan 15.62->0.24-3.0 ms,
compute-full ~9-11.5 ms < vblank); A1 acyclic + A1-first well-founded; WRAM-H addresses;
census scalars (544 obj / 31 reg / 88,081 B sheet-free / 7 stubs); VDP2 band-store + VDP1
VRAM *space* are genuine capacity non-walls. Shelving the dual-SH2 scan-split is SOUND.

### 9.2 CRITICAL corrections (each gates a wave; do NOT proceed on the old claim)
1. **CRAM is a WALL, not a non-wall.** Mania palettes are PER-SCANLINE (gfxLineBuffer,
   Palette.hpp:26; Drawing.cpp reads fullPalette[*lineBuffer]/scanline); the Saturn backend
   does only fullPalette[0] -- "2/8 banks" measured a MISSING FEATURE. >=2 banks are live in
   SHIPPING GHZ (water cycle + water-line split, Water.c:291); 77 objects use palette-FX.
   FIX: reclassify CRAM as **Phase-Z Z3, FIRST CONSUMER = GHZ water (today)**, then CPZ(W4)/
   HCZ(W7). Tiles via VDP2 per-line CRAM / line-color-screen (ST-058-R2); sprites = per-sprite
   single-bank approx (VDP1 has no per-line palette). RED gate: GHZ below-water-line region
   uses the water palette (histogram vs PC ref), not bank 0.
2. **VDP1 COMMAND-COUNT wall (SGL MAX_POLYGONS=144)** -- distinct from S3 fill-rate, and it
   COLLIDES with rule 8.2.5 "WRAM-H frozen." The SGL work area was shrunk to 144 to FREE
   212 KB WRAM-H (SaturnSGLArea.c:47), lives in WRAM-H, cannot move to cart. The pool's own
   ~150 near entities >= 144 BEFORE multi-sprite + HUD digits. PICK ONE (before W5/PSZ):
   (a) per-frame draw-list cull + HUD-digit batch so cmds<=144, WRAM-H stays frozen
   (preferred); or (b) move the 64 KB packed-collision tenant to cart to fund a bigger
   MAX_POLYGONS. Gate `qa_p6_sortlist` (p6_w_vdp1_cmds<=144, dense scene). [#87 hit this.]
3. **Pool D2 slot-stability is OPEN, not "FEASIBLE/DONE."** PlatformControl walks its child
   slot-span with deep EntityBase read/writes + `platform->speed` is an ABSOLUTE slot index
   (PlatformControl.c:19-63); 1264 Platforms / 23 controllers game-wide; circuits are
   level-wide so a child is dormant while the controller is live -> the walk hits a record
   with no such fields = the silent #249/#250 corruption class, game-wide. FIX (before I3):
   per-scene gate the max manager<->farthest-child span; if > near-window, PIN that
   manager+children RESIDENT as a unit (don't stream) OR widen the dormant record to full
   EntityBase for every manager-reachable slot (enumerate offline from the manifest). Demote
   §7.5 D2/D3 to OPEN.
4. **I4 streaming breaks frame-parity (re-Create).** Ring_Create re-arms unconditionally with
   NO collected-guard -> a collected ring REAPPEARS on backtrack (11,558 rings / 922 ItemBox /
   203 CollapsingPlatform / 31 SignPost game-wide); the dormant {classID,pos,flags} record has
   no lifecycle bit. FIX: the dormant record MUST persist a per-slot lifecycle bit (alive/
   destroyed/triggered/collected); materialize from SERIALIZED live fields, NEVER call Create
   twice; destroyed stays dead. RED-first BACKTRACK gate (collect a ring / break a platform,
   scroll off+back, assert it does NOT reappear). The current I4 gate ("GHZ play unchanged")
   passes while this ships -- it is insufficient.
5. **Whole-game AUDIO is unspecified -> add Phase S-AUDIO (before W2).** 58 BGM .ogg but
   build_cdda ships a 3-track CUE; 0 plan mentions of CD-DA track-budget / per-zone SFX. The
   disc fits 74 min ONLY after stripping ~71 MB OGG from DATA.RSDK -> Red-book CD-DA (~67.6
   min, ~6 min margin). S-AUDIO: (1) rebuild DATA.RSDK music-stripped; (2) 59-track master CUE
   + `qa_disc_capacity` (<74 min); (3) BGM-name->track-index + PlayStream context push-pop
   (ActClear/Invincible/Super/Drowning); (4) **per-zone SFX into the S4 swap set** (~367 files
   / 39 MB today have NO home; manifest + ADPCM budget vs 512 KB Sound RAM) + `qa_zone_sfx`.
   Without this every non-GHZ zone is silent on contact/hazard/boss.

### 9.3 MAJOR corrections
- **SCOPE (binding, add near §0): Saturn ships MANIA BASE, no Plus.** OUT: Encore / Mighty /
  Ray (sheets absent from extracted/) / Time-Attack replays (4 MB buffers impossible in 2 MB;
  packed replay >> 32 KB BUP) / UIReplayCarousel. KEEP: Tails AI + Knuckles (Knux1-3.gif
  exist). Time Attack, if wanted, replay-less (leaderboard times fit BUP).
- **Pool 60 fps is GHZ-ONLY.** Dense zones: (a) the scan is 781 BOUNDS-checks not slots
  (empty slots cheap); (b) I4 adds a per-frame MANIFEST window-entry scan over ALL placed
  (1041 GHZ / 2016 PSZ2) -- spec it PACKED in the freed WRAM-L (reverses #246's cart-only
  "packed-cache dead"); (c) DrawLists (~400 us/sprite master CPU) is untouched -> PSZ2 ~30
  drawn = ~12 ms -> 30 fps even with the scan fixed -> dense zones are the D2 render-pipeline
  fallback. Reword §8.5; add a DENSE (PSZ2/FBZ) RED gate.
- **OBJANIMPAK 256 KB cap is UNPROVEN** (only GHZ 14 KB measured; FBZ 53 / MSZ 51 / HCZ 44
  classes plausibly exceed). Measure per-zone Sigma(frames*36+anims*28) BEFORE W2; if over, S2
  must BAND/STREAM object anims (build_pack hard-asserts <=cap = build FAILURE, not graceful).
- **qa_active_census under-counts managers** (parses only Create; PlatformControl flips to
  NORMAL in its first Update tick). Re-scan State_/Update for unconditional active=NORMAL/
  ALWAYS; hard-pin PlatformControl + every childCount-serialize object RESIDENT; re-derive D3.
- **Phase-Z sequencing contradiction:** SpecialRing (3D mesh) is in GHZ1 x3 (74/scene). SPLIT
  Z1 -> **Z1a (SpecialRing single shaded/wireframe quad via slPutPolygon, BEFORE W1)** + Z1b
  (full UFO mesh, T2/last); interleave Z2 before W3(Smog)/W7(HCZ); EggTower(W10) needs Z1 or a
  sprite fallback. UNION the draw_stub gate with the 3D path (Draw3DScene/AddModelTo3DScene) +
  DrawBlendedFace -- today a wave with un-rendered 3D goes GREEN.
- **UFO 3D specials never sized vs the pool** (qa_camera_local_pool is 2D-box only; UFO is a 3D
  forward tunnel, peak UFO6=960 = 528 spheres + 228 rings, not "~680"). Model a 3D frustum over
  UFO1-7; report near-set + tier breakdown; gate before T2.

### 9.4 LOAD-TIME -- reframed to the Saturn-native bake (A4 target; user-directed 2026-06-19)
The ~45 s boot is our PC-style load (174 MB compressed pack + 94 per-file CD seeks ~13 s +
miniz ~7 s + parse), NOT a Saturn limit -- commercial Saturn games prove it. TARGET is not a
"~30 s floor" but **transitions that FEEL instant**: offline-bake each scene into ONE
Saturn-native CONTIGUOUS blob (VDP1-format sheets, pre-built tile maps + cell patterns,
pre-converted CRAM, pre-parsed entity table, packed collision) -> one big SEQUENTIAL DMA (no
seeks, no decompress; ~2-4 s for ~1 MB at 2x) -> MASK behind the TitleCard the engine already
has. Extends the band-store/resident-sheet bake to the WHOLE scene (a legitimate Phase-Z
native rewrite of the slow RSDK pack-load). IRREDUCIBLE: one-time power-on BIOS (~5-8 s real
HW / ~12 s Mednafen, ONCE not per-level). Depends on A1+A2 (the 64-128 KB bake/DMA buffers are
memory-blocked today, #251). **Promote from "S5 floor" to the A4 goal.**

### 9.5 MINOR (fold in per-wave)
Bind each of the 7 stubs to a first-consumer scene (FillScreen->Title/Pause @S6;
SwapDrawListEntries->Zone @W1; LoadVideo->TitleSetup); orphaned Puyo + DAGarden -> a §3 "T5
secret/bonus" item or an explicit descope note (+ a Competition/2-player scope note); add §8.2
residency rules for PROCEDURAL render output (deform/blended/3D mesh -> Phase-Z path + mesh
store); normalize §8 units (bytes or KiB) + reconcile "318 KB freed" vs the WRAM-L floor in
one unit; cap I4 materialize-burst (N Create/frame) + a dense streaming-cost gate; the BG
band-stream (D4) is UNLANDED (P68_W11_LAYOUTS_OPEN=1) -- RED-gate fast-wide-BG inflate/frame
BEFORE W4; VDP1 40-slot LRU thrash unverified at dense density -- add a distinct-rect witness;
§0 shared-infra 9 -> ~14 increments + a >1 cycle factor on high-risk pool steps.

### 9.6 _end + ANIMPAK-ceiling reconciliation (§8.1 fix)
The on-disk game.map `_end` is **0x060b6660** (the plan's 0x060b65c0 is the clean baseline; the
current map reflects the in-flight scan-split build, reverted with the shelf). The ENFORCED
ceiling is **0x060B8000** via Gate V-MAPOVERLAP (`qa_p6_mapoverlap`, the gate verify_done
actually runs) -> ~6,560 B real headroom, NOT "64 B", and NOT R0 (qa_p6_ghz_regression R0
checks cont_frames only). FOUR inconsistent ANIMPAK ceilings exist (0x060B6600 define +
scansplit S0, 0x060B7800 Animation.hpp comment, 0x060B8000 mapoverlap) -- reconcile to ONE
before any WRAM-H work. (The 64 B "headroom" in §8 was the scan-split's own squeeze, not the
shipping baseline.)
