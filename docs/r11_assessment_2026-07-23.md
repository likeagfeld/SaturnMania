> Authored by R11's seat (second dev machine, macOS) 2026-07-23, from a read-only
> audit of the repo at bac9ed5. Added to docs/ 2026-07-24 with the -2 session.

# SaturnMania — Independent Technical Assessment

*Prepared 2026-07-23 from a read-only audit of likeagfeld/SaturnMania @ `bac9ed5` (master). Three parallel deep-reads: decision history, performance + Jo Engine dependency depth, memory architecture + full-game scaling. All numbers below are the project's own measured figures with file citations.*

---

## 1. What this project actually is

It is **not a demake or a rewrite**. The shipping build compiles the **unmodified RSDKv5 engine** (the real Sonic Mania engine, vendored in `rsdkv5-src/`) plus **verbatim decompiled Mania game objects** to the Saturn's SH-2 with GCC 8.2.0, running against a custom Saturn hardware backend (`platform/Saturn/`, `tools/_portspike/_p6/`). It reads a retail `Data.rsdk` off the disc at runtime.

The project went through two retired approaches first:

1. **Hand-rolled Saturn engine** (May 2026) — justified by the claim "the RSDKv5 C++ engine does not fit" (BIBLE.md §2.1). No measurement backed this; it was an a-priori budget argument (74 MB of engine pools vs 2 MB of RAM).
2. **Hand-port track** (`src/rsdk/` + `src/mania/`) — hand-translated objects against a reimplemented engine API, triggered by a fidelity failure ("wrong Sonic sprite" incident, `docs/WHAT_I_GOT_WRONG.md`).
3. **P6 "true engine" track (current)** — a June 2026 port-spike proved the real engine compiles, links, and *runs* on SH-2. The "does not fit" claim is marked **FALSIFIED by the live build** in BIBLE.md, HANDOFF.md, and the massport plan.

The falsification was earned, not asserted: GHZ Act 1 runs continuously on the unmodified engine at a measured **48.92 fps**, later **56.56 fps** after camera-local entity pools landed (commit `44ed63d`; `WHOLE_GAME_MASSPORT_PLAN.md:734`). The 74 MB pool problem was solved by retargeting pools to Saturn sizes (74 MB → ~224 KB, `Storage.cpp:34-159`), backing overflow with the 4 MB RAM cart, and streaming everything camera-locally.

**Current deliverable**: full boot chain (SEGA/RSDK logos → title with VDP2 mode-7 island → menu → Tornado intro → AIZ cutscene → GHZ title card) into **playable Green Hill Zone Act 1** — real Mania physics, 31 object classes, badniks, CD-DA music. Verified by a live-memory QA harness (UDP reads of emulator RAM, hundreds of automated gates), which is genuinely rigorous engineering.

## 2. Does it work? (correcting the framing)

**Yes — with one giant asterisk.** The engine itself hits 56 fps in plain GHZ. But the *shipping chain build* (the one with the front-end) renders at **~10.3 fps / 41.3 ticks-per-second** (`HANDOFF.md:26-28`). That gap has a **specific, identified, mostly-mundane cause**, not a mystery:

> The "#243 chain VDP1 draw wall": a dangling-`else` bug that only manifests when both `P6_FRAMEDIR` and `P6_FRONTEND_TITLE` are defined (i.e. only in the chain build). It makes the sprite-draw path **re-decompress (zlib-inflate) sheet bands ~6.65 times per frame** instead of using the cached VDP1 upload. Chain draw: 33 ms. Plain draw: 5 ms. (`p6_vdp1.c:1991-2001`, partially fixed already.)

So the game that "doesn't work" is largely one build-flag-conditional caching bug away from ~30+ fps in the current content. The magenta-flicker regression is believed to be the same wall's side effect.

## 3. Likely culprits, ranked

| # | Culprit | Nature | Fixable? |
|---|---------|--------|----------|
| 1 | **Chain re-inflate bug (#243)** — 33 ms vs 5 ms VDP1 draw, 6.65 zlib inflates/frame | Ordinary bug (dangling-else under specific build flags) | Yes — already partially fixed; finish residency/eviction correctness |
| 2 | **VDP1 9.3× overdraw** — every sprite drawn as a fixed 64×64 box (77,824 px drawn vs 8,333 content px); VDP1 ~74% busy | Structural but planned (fix "S3": content-sized slot pools → target ≥58 fps) | Yes — this is normal draw-batching work |
| 3 | **Memory scaling to a full game** — 4 structural mechanisms (S1–S4) don't exist yet: entity-stride tiers (53 classes don't fit the 344 B slot), per-zone anim swap, content-sized draws, residency-swap machine. WRAM-H is frozen at **64 bytes** of headroom; the 4 MB cart is already **241 KB short** at the worst zone (TMZ1), with 28 of 94 scenes overflowing | Architectural debt on the critical path | Partly — each has a designed fix, but they gate *whole classes of content* |
| 4 | **Sheer scale** — GHZ ships ~31 of ~414 object classes (~7.5%). The author's own estimate for the whole game: **~270 build-cycles, "multi-year"** (`WHOLE_GAME_MASSPORT_PLAN.md:48-58`) | Scope, not a bug | Only by descoping |
| 5 | **Asset pipeline not turn-key** — a clean clone cannot rebuild all `cd/` assets; several converters' command lines were never consolidated (SETUP.md admits this) | Process debt | Yes — mechanical consolidation work |

Dense-zone reality check the author already concedes: even after the S3 fix, PSZ2-class density lands at **~30 fps** — the docs designate 30 fps as the dense-zone fallback, with dual-SH-2 tick-splitting as the last resort (`WHOLE_GAME_MASSPORT_PLAN.md:167-170, 669-674`).

## 4. The Jo Engine question

**Short answer: your instinct is half right — Jo Engine is a liability, but it is *not* why the game is slow, and ripping it out would not rescue anything.**

What the shipping build actually uses, layer by layer:

- **Toolchain** (jo's bundled `sh-none-elf-gcc`): universal, uncontroversial.
- **SGL** (Sega's library, bundled under jo): **load-bearing** — ~450 direct `sl*` call-sites. All VDP2 scroll, all DMA (`slDMAXCopy` with hand-managed cache-through aliases), the slave-SH-2 dispatch, and a *custom-shrunk* SGL work area (`MAX_POLYGONS` 1761→144, freeing 208 KB of WRAM-H).
- **Jo Engine proper**: ~240 call-sites but concentrated in exactly four roles — boot bring-up (`jo_core_init`/`jo_core_run`), CD filesystem init (`GFS_Init` via jo), the VDP1 sprite upload/draw HAL (`jo_sprite_*`, 131 calls in `p6_vdp1.c`), and CD-DA playback.

Crucially, **the author has already migrated every hot path off jo**: the foreground present is a direct-VDP1 vblank-ISR patch bypassing jo entirely, backgrounds are raw SGL, DMA is raw SGL, steady-state file reads go through a custom GFS layer (`p6_gfs.c`), input doesn't touch jo. The measured performance walls (re-inflate bug, VDP1 fill-rate, dense-zone tick cost) are **not attributable to jo**.

Jo's *real* costs are foot-guns, and they have already drawn blood:

- **`JO_FS_MAX_FILES` silent truncation**: adding 9 files pushed the ISO root past jo's 280-entry directory table; `GFS_NameToId` silently returned −1 for the 3 alphabetically-last files, one bad sheet ID corrupted the VDP1 bind table, and **GHZ badniks went invisible** (commit `06d0b12` — fixed *today*, 2026-07-23).
- The jo malloc pool is a static `.bss` array whose sizing can silently overrun the SGL work-area floor at `0x060C0000` (documented recurring trap; resizing doesn't even recompile jo's `core.o` without manual object deletion).
- Jo's sprite-ID allocator has no release-build bounds check.

Removing jo entirely would free modest RAM and eliminate those traps, but requires re-deriving the boot sequence jo currently gets right (a bare-crt0 attempt already crashed on an unset GBR register — `p6_sgl_boot.c` documents it) and rewriting the VDP1 sprite HAL against raw SGL. **Verdict: a gradual jo squeeze-out (the direction already underway) is right; a rip-out is neither necessary nor the bottleneck.**

## 5. Wrong path? — verdict

**The "run the real engine" bet is not the wrong path for what it has been asked to do so far.** The evidence is genuinely on the author's side:

- The original "engine doesn't fit" objections (C++, 74 MB pools, linear framebuffer, 180 MB data) were each individually disproven by a *running build*, not by argument.
- 56 fps engine tick on real GHZ content is a remarkable, verified result.
- The alternative — the hand-port — was tried and produced exactly the failure mode you'd predict: physics divergence "in 50 small ways" and a fidelity incident that triggered the pivot. For frame-parity fidelity (the author's stated goal), the true engine is arguably the *only* honest path.

**Where the wrong-path worry is legitimate is scope, not architecture:**

1. The author's own plan says full Sonic Mania = ~270 more gated build-cycles ("multi-year at one increment per session"), with 383 object classes still unregistered and four structural walls (S1–S4) between GHZ and everything else.
2. Even the plan concedes dense zones cap at ~30 fps and cuts Plus content ("MANIA BASE, no Plus" — Encore, Mighty, Ray, Time-Attack replays are already out).
3. WRAM-H at 64 bytes headroom and a cart that's 241 KB short at the worst zone mean each new zone is a siege, not a port.

**Recommendation to relay to likeagfeld** (ranked):

1. **Finish #243** (chain re-inflate) — it single-handedly converts the "barely runs" impression into a ~30 fps experience and likely kills the magenta flicker. Highest value-per-effort in the repo.
2. **Land S3 (content-sized VDP1 draws)** next — it's the difference between 10 fps perception and the measured 56 fps reality, and it's prerequisite to every dense zone.
3. **Re-scope "done" publicly**: a polished GHZ (both acts) + 2–3 tractable zones at 30–60 fps is an all-time Saturn homebrew achievement and reachable in months; frame-parity full Mania is the multi-year tail. Ship the vertical slice as v1.
4. **Consolidate the asset pipeline** (SETUP.md's admitted gap) so collaborators — like this workspace — can build from a clean clone. This is the cheapest way to get help onto the project.
5. **Keep squeezing jo out incrementally** (sprite HAL → raw SGL next, since `p6_gfs.c` already replaced steady-state FS); don't attempt a big-bang removal.
6. Idle silicon worth noting: the SCU DSP is completely unused, and the slave SH-2 only runs the ~4.6 ms present — both are named fallbacks in his own plan if S3 misses 60 fps.

## 6. Addendum (2026-07-23): what R11's workspace can contribute

### 6a. Emulated oversized cart — recommended as a dev tier

Mednafen 1.32.1 supports `ss.cart cs1ram16`: a **16MiB RAM cart on A-bus CS1**. It *replaces* the 4MB CS0 cart (single `ss.cart` choice), and SaturnMania is welded to CS0 addresses (`p6_cart_probe` at `0x22400000`, plus hardcoded literals in `p6_io_main.cpp`), so using it requires a dev-only `P6_*`-style build flavor that rebases the cart addresses to CS1. Worth it because:
- **Decouples content porting from residency engineering**: zones that overflow the 4MB store today (28 of 94 scenes; TMZ1 by 241KB) could be brought up now, with the S4 band-windowing machinery built later against working content.
- **Converts extrapolated memory demand into measured demand** via the existing live-memory harness.
- **Bug triage**: "does it survive infinite cart space?" cleanly separates residency/eviction bugs (#243-class) from logic bugs.
Discipline: shipping QA gates keep running the `extram4` flavor so nothing silently grows past 4MB.

### 6b. Transferable VDP1/VDP2 techniques from R11's Saturn code

Audited R11's render code (saturn-tools' `saturn/lib/vdp1|vdp2`, the newer `libs/saturn-vdp1|vdp2`, lobby). Honest gap analysis:

**Ahead of SaturnMania (worth adopting):**
- **Raw VDP1 command-table rendering, bypassing the SGL/jo sprite HAL entirely** (`saturn-tools/saturn/lib/vdp1/saturn_vdp1.c`, 629 lines): 32-byte hardware command words written directly to VDP1 VRAM, with an SGL-coexistence trick — let SGL build its slots 0–3, then a `slSynchFunction` callback patches slot 3 from END → LOCAL_COORD so auto-draw walks into user commands at slot 4+. This is one step *past* SaturnMania's stated goal (jo HAL → raw SGL).
- **Content-sized blits via a variable-size VDP1 VRAM bump allocator** with per-color-mode sizing (RGB555/256c/16c). Achieves exactly what the S3 fix is chasing (kill the 9.3× overdraw from fixed 64×64 boxes) — by true variable sizing rather than 16/32/48/64 buckets. Caveat: it's a persistent 32-slot registry with no LRU/eviction, so it complements (doesn't replace) SaturnMania's 40-slot rect-keyed LRU lifecycle.
- **Scaled and distorted-quad sprite encoders, Gouraud tables, half-transparency, mesh PMOD** — none present in SaturnMania.
- **Hard-won VDP1/VDP2 timing gotchas** that become live the moment SaturnMania's command count rises in dense zones: spin on EDSR.CEF before writing VDP1 VRAM (display-period writes race the active draw; ~20-command "transfer-over" tearing symptom); never copy command lists in the vblank callback (VDP1 is already rasterizing — write during display, patch only slot 3 + priority in vblank); write END last, walk low→high; SGL rewrites VDP1 EWDR every frame (re-force for transparency); `slPrioritySpr0` doesn't update SGL's shadow in 3.02j (write PRISA-D/PRINA to hardware each vblank AND after slSynch); wholesale VDP2 PNT wipes during display race scanline fetches (dirty-cell diff instead).

**Behind SaturnMania (don't export):** no measured fill-rate/overdraw/VDP1-busy numbers anywhere in R11's render code; no DMA in the render path (all CPU copies — SaturnMania's vblank cell-page DMA is ahead); simpler CRAM usage; no line scroll.

**Net pitch to likeagfeld:** adopt the raw command-table + variable-size allocator model and the timing gotchas for S3/the jo-HAL replacement; keep his own perf methodology and DMA pipeline — those are already better than anything in R11's workspace.

## 7. Practical notes for this machine

- Run with the 4 MB cart: `mednafen -ss.cart extram4 game.cue` (global `ss.cart auto` left untouched; both BIOSes already in `~/.mednafen/firmware/`).
- Build is Docker (`docker build -t joengine-saturn:latest .`, repo bind-mounted at `/work`); shipping entry point is `tools/_portspike/_p6/build_shipping.sh`, **not** the default `make` (that builds the retired hand-port).
- Blocked on likeagfeld sending `Data.rsdk` (~180 MB, Steam Sonic Mania) — no assets ship in the repo. Expect converter gaps on first clean build (SETUP.md).
- His QA harness is PowerShell-centric; the Python gates should run on macOS, the `.ps1` drivers need `pwsh`.
- He keeps an untracked local `memory/` dir — his live state may be ahead of the committed docs; commit `06d0b12` landed today, so the repo is hot.
