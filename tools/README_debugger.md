# tools/ debugger reference card (Phase 1.30)

The Phase 1.30 tools add Mednafen **savestate-based** register-and-memory
inspection to the existing black-box capture QA. This file is the
permanent reference card for that subsystem.

## Files

| File | Role |
|---|---|
| `tools/mcs_extract.py` | Parse `.mc0`/`.mcs`; expose `--peek`, `--peek16`, `--peek8`, region dumps, SH-2 register dumps, section/var listing. |
| `tools/qa_savestate.ps1` | Boot Mednafen on a CUE, wait N seconds, press F5 to save state, retrieve the `.mc0` from `$env:MEDNAFEN_HOME/mcs/`, pipe through mcs_extract. |
| `tools/qa_register_gate.py` | Read a JSON contract and assert register/memory facts about a captured state. Used by `verify_done.ps1` Gate V-REG. |
| `tools/qa_register_baseline.json` | Don't-regress register contract for the Phase 1.28 settled-title state. |
| `tools/qa_watch.py` | Diff two or more captured states across a memory region; localise which bytes changed. |
| `samples/qa_title_settled.mcs` | Reference settled-title savestate (~539 KB, gzip-wrapped Mednafen format). |
| `tools/qa_phase2_3j_sync_load_gate.py` | Phase 2.3j 4-predicate gate: BGON/VRAM/tick counter/CRAM validate sync-load contract. |
| `samples/qa_phase2_3j_ghz_active.mcs` + `tools/qa_golden/qa_phase2_3j_ghz_active.mcs` | Reference SaveFrame=60 capture showing GHZ-active state under the sync-load architecture (g_ghz_active_tick_counter = 43,780). |

## Phase 2.3j outcome (2026-05-28)

The Phase 2.3h/i probe struct apparatus (`g_ghz_fg_probe.entry/pre/ready/
mirror/post/exit` byte canaries) is RETIRED. The sync-load refactor
eliminated the cross-TU LTO-vs-volatile bug class that motivated those
probes. The new evidence symbol is `g_ghz_active_tick_counter` (volatile
unsigned int, incremented once per `mania_ghz_tick_and_draw` call). Peek
it via `python tools/mcs_extract.py state.mcs --peek8 0x06032250` (4
bytes, big-endian SH-2). A value >5 proves the GHZ tick chain is running.
See `tools/qa_phase2_3j_sync_load_gate.py` for the 4-predicate gate.

## Quick-start recipes

### 1. Peek a single register

```pwsh
python tools/mcs_extract.py samples/qa_title_settled.mcs --peek16 0x25F800E0
# -> peek16 0x25f800e0 = 0x0023
```

Saturn cache-through (`0x20xxxxxx`) and direct (`0x05xxxxxx`) addresses
are interchangeable; both map to the same captured region.

### 2. Capture a fresh state from the current build

```pwsh
pwsh tools/qa_savestate.ps1 -Cue game.cue -SaveFrame 18 `
    -Out samples/qa_new_state.mcs
```

`-SaveFrame 18` waits 18 seconds after Mednafen launch, then injects an
F5 keystroke (Mednafen's default save-state hotkey per the `mednafen.cfg`
`command.save_state` binding, scancode 62 = SDL F5).

### 3. Run the register contract gate

```pwsh
python tools/qa_register_gate.py samples/qa_title_settled.mcs `
    tools/qa_register_baseline.json
```

Per-assertion PASS/FAIL lines plus a final aggregate verdict. Exit 0 on
all-PASS, exit 1 on any FAIL.

### 4. Diff two states to localise an unexpected change

```pwsh
python tools/qa_watch.py state_before.mcs state_after.mcs `
    --region vdp2-regs --start 0x25F800E0 --len 0x10
```

Reports each byte that differs across the two states. Saturn addresses
and relative offsets both work.

### 5. Dump VDP2 CRAM as a 4 KB blob for offline analysis

```pwsh
python tools/mcs_extract.py samples/qa_title_settled.mcs --cram cram.bin
```

Then inspect with any hex editor or `python -c "import sys; sys.stdout.buffer.write(open('cram.bin','rb').read())"`.

## Savestate format cheat-sheet (citation-backed)

Source: `mednafen-git/src/state.cpp` lines 596-723 + per-subsystem
`ss/<mod>.cpp::StateAction`. Verified empirically against a real
539,375-byte Phase 1.28 `.mc0` capture.

```
file       = gzip(decompressed)
decomp     = header(32B) | preview(W*H*3 B) | chunk-stream
header     = "MDFNSVST"(8B) | timestamp(8B) | version(4B)
           | size_with_endian_flag(4B) | preview_w(4B) | preview_h(4B)
chunk      = name(32B null-padded) | payload_size(4B LE) | payload
payload    = (one or more) variable
variable   = name_length(1B) | name | var_size(4B LE) | var_data
```

Major Saturn regions inside the verified chunk inventory:

| Section / Var | Saturn base | Bytes |
|---|---|---|
| `MAIN/WorkRAML` | 0x00200000 | 1048576 |
| `MAIN/WorkRAMH` | 0x06000000 | 1048576 |
| `MAIN/BackupRAM` | 0x00180000 | 32768 |
| `VDP1/VRAM` | 0x05C00000 | 524288 |
| `VDP1/&FB[0][0]` | 0x05C80000 | 524288 |
| `VDP1/{TVMR,FBCR,PTMR,EWDR,EWLR,EWRR,EDSR,LOPR}` | 0x05D000xx | 2 each |
| `VDP2/VRAM` | 0x05E00000 | 524288 |
| `VDP2/CRAM` | 0x05F00000 | 4096 |
| `VDP2/RawRegs` | 0x05F80000 | 512 |
| `SH2-M`, `SH2-S` | (registers) | varies |
| `SCSP`, `M68K`, `CDB`, `SCU`, `SMPC` | (state) | varies |

VDP2 SPCTL is uint16 at byte offset `0xE0` inside `VDP2/RawRegs`, i.e.
Saturn address `0x05F800E0`. Mednafen serialises uint16 fields in host
LE; `mcs_extract` swaps each 16-bit pair for VDP1/VDP2 array regions so
`--peek16` returns Saturn-visible big-endian values.

## Documented gap: `ss.dbg_mask` does NOT exist in Mednafen 1.32.1

The Phase 1.30 dispatch spec proposed a `qa_debug_gate.py` driven by
`-ss.dbg_mask 0xFF -ss.dbg_break_on_unknown_io 1`. Probing the actual
Mednafen 1.32.1 build at
`C:\Users\gary\AppData\Local\Microsoft\WinGet\Packages\MednafenTeam.Mednafen_Microsoft.Winget.Source_8wekyb3d8bbwe\mednafen.exe`
shows neither setting exists:

```pwsh
Get-Content "$exeDir\mednafen.cfg" | Select-String '^ss\.dbg'
# -> only ss.dbg_exe_cdpath (program-load path for the interactive
#    debugger, NOT a logging mask)
```

The available debug surface is Mednafen's interactive in-window debugger
(Alt-D toggle), accessed by `ss.debugger.disfontsize` and
`ss.debugger.memcharenc`. Driving that interactively from PowerShell is
possible but requires SendInput automation + OCR of the register pane —
a separate effort that should be approved by the user before scheduling.

For now the savestate-based path covers every diagnostic scenario the
original Phase 1.30 dispatch enumerated (BSS overflow bisect, SPCTL
hypothesis chain, register-clobber localisation), because all of those
boil down to "what is the value of memory/register X at time T" — and
that is exactly what `mcs_extract --peek` answers.

## Integration into verify_done.ps1

Gate `V-REG` (see `tools/verify_done.ps1`) runs
`qa_register_gate.py` against `samples/qa_title_settled.mcs` and
`tools/qa_register_baseline.json` at the end of the standard gate run.
The baseline pre-existed when Gate V-REG was added; intentional changes
to the register contract require updating
`tools/qa_register_baseline.json` (with a `_comment` rationale) and
re-capturing the reference state via:

```pwsh
pwsh tools/qa_savestate.ps1 -Cue game.cue -SaveFrame 18 `
    -Out samples/qa_title_settled.mcs
```

This refresh discipline matches the existing
`memory/qa-gate-stale-golden-trap.md` rule for the image-diff gate.

## Phase 2.3i hardening: foreground confirmation + F5 retry + PID-tracked shutdown (2026-05-28)

Phase 2.3h surfaced a hypothesised "Mednafen window-focus race" when
capturing at deep frame indices (`SaveFrame>=60`). Phase 2.3i
diagnostic — 5 consecutive runs at `SaveFrame=60` against game.iso
built with `GHZ_AUTOADVANCE_TICKS=480` — produced 5-of-5 successful
captures with the original implementation, so the race was not the
actual mode of the Phase 2.3h "ready=0x00" reading. The captures
landed in GHZ-active state (BGON=0x0006 = NBG1ON|NBG2ON, PC inside
`slSynch` at 0x06026c94–0x06026c9e) every time. The
`g_ghz_fg_probe.entry / ready / mirror / post / exit` fields read 0x00
in all 5 — a true B2 (caller-TU LTO mangling or upstream symbol
reordering) result, not a harness flake.

Regardless of the prior misdiagnosis, the harness was fragile in
defensible ways and Phase 2.3i hardened it:

1. **Foreground confirmation loop.** Before the F5 stroke, the harness
   now polls `GetForegroundWindow()` until it returns our HWND, with
   `AttachThreadInput` to satisfy Windows' input-queue-share rule for
   `SetForegroundWindow`. Up to 25 attempts at 200ms each (5s total).
   Fail-fast with explicit error if focus never confirms.
2. **F5 retry with mtime verification.** Up to 3 strokes, each
   followed by a 3s poll of `mcs/` for a new `.mc0` whose
   `LastWriteTimeUtc >= stroke-time`. Re-verified once the candidate
   file's size stabilises (200ms x 2 confirmation). Fail-fast after
   3 unsuccessful strokes.
3. **PID-tracked shutdown.** The script only ever stops the Mednafen
   PID it launched. It never enumerates `mednafen.exe` or force-kills
   other instances — Codex / the user may run a parallel one.
4. **Optional `-FpsScale` knob.** Passes `-fps.scale N` to Mednafen so
   the emulator runs at N x realtime. Default `1.0` (real time) keeps
   the historical behaviour byte-identical for all existing callers.
   For very deep frame indices (SaveFrame > 60), the knob compresses
   the wall-clock latency so the harness completes faster.

The hardening is purely diagnostic — no `src/mania/` or `src/rsdk/`
files are modified.

### `Makefile` knob: `GHZ_AUTOADVANCE_TICKS`

Per-build override of the title->GHZ auto-advance timer (ticks at
60Hz). Default (unset) inherits the `Game.c` fallback of 0 so
production ISOs remain release/demo behaviour. Pass on the make CLI
to force a title hold the savestate harness can deterministically park
inside:

```pwsh
docker run --rm -v "D:/sonicmaniasaturn:/work" -w /work `
    joengine-saturn:latest make GHZ_AUTOADVANCE_TICKS=480
```

480 ticks = ~8 seconds. Combined with the harness `-SaveFrame 60`
default, capture lands solidly inside GHZ-active state.

### Reference samples (Phase 2.3i)

- `samples/qa_phase2_3i_b1_b2_state.mcs` — captured at `SaveFrame=60`
  against the `GHZ_AUTOADVANCE_TICKS=480` build. Used as the Phase
  2.3h B1/B2 decision anchor. BGON=0x0006, SPCTL=0x0023, PC in
  slSynch. Probe struct at `0x060b31c8`: entry=0x00, pre=0xA5,
  ready=0x00, mirror=0x00, post=0x00, exit=0x00. Per the Phase 2.3h
  decision matrix, `ready=0x00, mirror=0x00` -> B2 not eliminated by
  per-file `-fno-lto` on `scene_ghz.c`. The 0xA5 at +1 (`pre`,
  whose code-set value is 0x5A) is the smoking gun — suggests
  inter-TU symbol mangling at the link stage swapping `entry` (0xA5)
  with the +1 slot, OR caller TU writes via a stale field-offset
  cached at LTO time. Investigation continues in Phase 2.3j.

