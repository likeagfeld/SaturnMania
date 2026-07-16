# #243 chain-GHZ per-frame banded-fetch attribution + elimination

Date: 2026-07-16. Branch master, base HEAD e6c2219.

## Honest baseline (Mednafen two-capture, emulated-time -- the binding method)

Chain GHZ play phase (SaveFrame 380/395, 15 emulated seconds):
- game speed 23.5 tick/s (39% realtime)
- render 5.9 fps
- p6_w_sht_fetches = 6.84 per rendered frame (banded miniz inflates, zero CD)

The catch-up loop (P6_TICK_CAP=4) multiplies the render wall into game-speed.
Proven lever class: 606fbfb (GHZ landing fetch elimination took 4.1 -> 18 fps).

## Measurement rules (binding, from aiz-cd-fix-ghz-regression-bisect.md)

- NEVER trust live-RA wall-clock speed numbers (fast-forward inflated a false
  "60.2 GREEN"). Mednafen two-capture only: tick/cont/fetch deltas over the
  emulated window; denominator = d(p6_perf_vbl_count)/60 emulated seconds.
- Ratio counters (fetches per rendered frame) are throttle-independent.
- Never measure during the GHZ TitleCard (~40 s wall at ~4 fps).

## Sources read (methodology steps 1-4)

- `C:\Users\gary\.claude\skills\sega-saturn-developer\references\complete-doc-index.md`
  end-to-end. Applicable rows: RSDK/Mania port (`rsdk-mania-port-reference.md`),
  register/memory diagnostics (`mednafen-debug-qa-reference.md` -- savestate
  harness is the primary tool here). No new hardware-register work: the change
  is C-side residency/instrumentation on already-proven paths (SaturnSheet
  banded store per W12, FRD per task #328).
- `platform/Saturn/SaturnSheet.cpp` (FetchRect banded inflate = the choke
  point, line 489 `++p6_w_sht_fetches` inside the per-band loop; resident
  short-circuit at :447).
- `platform/Saturn/SaturnFrameDir.cpp` end-to-end (Lookup bumps
  p6_w_frd_misses ONLY when the slot is valid -> frd_misses==0 with fetches>0
  means the dispatch precondition failed: `p6_frd_slot_for_sheet(sheet) < 0`).
- `tools/_portspike/_p6/p6_vdp1.c` (chain draws route through
  p6_title_pool_for; miss dispatch order = FRD (s_frdByStore by STORE slot,
  #328) -> resident px -> banded s_fetchFn = SaturnSheet_FetchRect).
- `tools/_portspike/_p6/p6_io_main.cpp` GHZ handoff seam :8020-8234 (the
  606fbfb 9-sheet stage + FRD stage + promoteOrder + p6_frd_attach_bound).
- `tools/_portspike/_p6/build_shipping.sh` + `build_p6scene_objs.sh` --
  SaturnSheet.cpp compiles with P6_FRONTEND_MENU/P6_FRAMEDIR but NOT
  P6_FRONTEND_CHAIN; the chain build (P6_GHZCUT_BOOT -> P6_AIZ_TEST ->
  P6_FRONTEND_MENU) sets MENU, plain GHZ does not -> gate the new witnesses
  on P6_FRONTEND_MENU for plain-GHZ byte-identity.

## Step 1 -- attribution instrumentation (existing witnesses cannot attribute)

`platform/Saturn/SaturnSheet.cpp`, gated `#if defined(P6_FRONTEND_MENU)`:
- `p6_w_fetch_hist[32]`  -- per-STORE-slot banded-inflate count, bumped at the
  line-489 fetch site. Store slots are stable across handles (#321/#328).
- `p6_w_sht_slothash0[32]` -- hash[0] per slot (recorded in SetHash), so the
  offline reader names each slot: engine GEN_HASH_MD5 word0 ==
  int.from_bytes(md5(path).digest()[:4], "little") (tools/build_anim_pack.py
  engine_hash, MEASURED convention).
- .bss cost: 256 B, chain flavor only. Chain _end ceiling = GLOBALS
  0x060C8000 (~28 KB headroom per the frontend-cart-map-recarve memory);
  plain GHZ byte-identical (flag unset).

## Gate (RED first)

`tools/qa_ghz_fetch.py` -- Mednafen two-capture:
- capture A at SaveFrame 380, B at 395 (`tools/qa_savestate.ps1`), host quiet;
- validate folder=="GHZ" + cont advancing in both;
- fetches/frame = d(p6_w_sht_fetches)/d(p6_w_cont_frames), GREEN <= 0.5;
- tick/s = d(p6_w_tick_frames)/(d(p6_perf_vbl_count)/60), reported honestly;
- prints the named per-slot fetch-rate histogram (the attribution deliverable).
Expected RED on the pre-fix build: ~6.84 fetches/frame.

## Fix (step 5, after attribution)

Follow 606fbfb: make the identified sheets' per-frame draws stop fetching --
either FRD coverage (rebuild via tools/build_frame_dir.py --all8) or
MakeResident promotion at the GHZ handoff seam (p6_io_main.cpp :8124-8218),
budget-checked against the RES store (SATURNSHEET_RES_BASE 0x22400000 ..
RES_END; 9 FRDs = 1,418,316 B of the 1,703,936 B store already claimed --
any promote must fit the remainder or be justified by a measured reclaim).

## Audits (sec 4.5.1)

- Audit 1 (Z-order) / Audit 2 (cadence) / Audit 3 (pivot+flip): N/A -- no new
  sprite is drawn; the change is residency + witnesses on existing draws.
- Audit 4 (boot-delay budget): no new synchronous CD load is added by the
  instrumentation. Any fix-side staging change must stay within the existing
  handoff-seam load (documented at fix time below).

## Guardrails

- Do NOT touch P6_TICK_CATCHUP / P6_TICK_CAP.
- Plain GHZ byte-identical (all additions behind P6_FRONTEND_MENU/P6_FRAMEDIR).
- `py -3 tools/qa_parity_oracle.py --selftest` must stay GREEN.

## Results

### RED (measured, instrumented build, Mednafen two-capture t=380/395, 16.2 emu-s)

- fetches/frame = 6.65 (d_fetch=592, d_cont=89); 22.0 tick/s (37%); 5.5 fps.
- Attribution (p6_w_fetch_hist deltas over the window):
  - slot 14 Players/Tails1.gif   2.08/frame
  - slot 17 Global/Items.gif     2.02/frame  (rings)
  - slot 18 Global/Display.gif   1.91/frame  (HUD)
  - slot 11 Players/Sonic1.gif   0.64/frame
- Companion witnesses: frd_active=9, frd_misses=0, d(frd_lookups)=280 ==
  d(p6_w_vdp1_evicts)=279 -- EVERY bucket miss consulted the FRD and HIT,
  yet 592 band inflates still ran in the same window.

### ROOT CAUSE (mechanical, code-level)

`p6_title_pool_for` (tools/_portspike/_p6/p6_vdp1.c): with BOTH P6_FRAMEDIR
and P6_FRONTEND_TITLE defined (the chain build), the bare `else` after
`if (fi.pattern) { }` bound to the interposed #326 head-forensic
`if (h >= 100 && w >= 100 && sy >= 400)` statement -- NOT to the
resident/banded sheet-path fallback. So after an FRD HIT the banded
FetchRect still ran and overwrote srcPx: the FRD did full work AND the
miniz inflate ran per miss. Plain GHZ compiles the forensic out (no
P6_FRONTEND_TITLE), binding the else correctly -- chain-only symptom, and
why every plain-GHZ FRD gate stayed GREEN while the chain fetched.
All FRD entries verified mode-0 on disc (12/12 files) -- the mode-reject
path was NOT the cause.

### FIX

Braced the else so it owns the entire fallback (two-line brace addition +
citation comment in p6_title_pool_for). No behavior change for any build
without P6_FRAMEDIR (braces compile out); plain GHZ byte-identical
(function only exists under P6_FRONTEND_TITLE).

### GREEN: (pending)
