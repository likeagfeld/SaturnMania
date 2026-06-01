# Cinepak Integration Specification — Mania.ogv intro inside jo+SGL

**Source authority for every claim in this document:**

| Citation tag | File |
|---|---|
| `[SGL_CPK.H]` | `D:\sonicmaniasaturn\jo-engine\Compiler\COMMON\SGL_302j\INC\SGL_CPK.H` (project copy of Sega's SGL CPK header, version "CPK Version 1.20 1995-10-05") |
| `[cinepak_sgl.c]` | `D:\Claude Saturn Skill Documentation\Saturn Video Tools\Official Cinepak Demos\cinepak_sgl\cinepak_sgl.c` (canonical SGL-side Cinepak sample, "Newest discovered way inspired by SBL samples to get video playback on SGL") |
| `[SBL SMPCPK2]` | `D:\Claude Saturn Skill Documentation\Saturn Video Tools\Official Cinepak Demos\SBL Cinepak Demo 4\CINEPAK\SBL\SEGASMP\CPK\SMPCPK2\SMPCPK2.C` (canonical SBL Cinepak sample) |
| `[SBL Demo 4 main.c]` | `D:\Claude Saturn Skill Documentation\Saturn Video Tools\Official Cinepak Demos\SBL Cinepak Demo 4\src\main.c` (working modern SBL demo) |
| `[jo video.c]` | `D:\sonicmaniasaturn\jo-engine\jo_engine\video.c` |
| `[jo core.c]` | `D:\sonicmaniasaturn\jo-engine\jo_engine\core.c` |
| `[jo_engine_makefile]` | `D:\sonicmaniasaturn\jo-engine\Compiler\COMMON\jo_engine_makefile` |
| `[jo demo-video main.c]` | `D:\sonicmaniasaturn\jo-engine\Samples\demo - video\main.c` |
| `[jo demo-video makefile]` | `D:\sonicmaniasaturn\jo-engine\Samples\demo - video\makefile` |
| `[MANCPK.DOC]` | `D:\Claude Saturn Skill Documentation\Saturn Video Tools\Official Cinepak Demos\SBL Cinepak Demo 4\CINEPAK\SBL\SEGALIB\MAN\MANCPK.DOC` (Shift-JIS; readable via `iconv -f SHIFT_JIS`) |
| `[Saturn Overview]` | `D:\Claude Saturn Skill Documentation\sega_saturn_docs\Saturn_Overview.txt` |
| `[user Makefile]` | `D:\sonicmaniasaturn\Makefile` |
| `[intro_video.c]` | `D:\sonicmaniasaturn\src\intro_video.c` |

## 1. Root cause of the current black-screen lockup

The user's current state (from `[intro_video.c]`) is:

- `[user Makefile]:10` sets `JO_COMPILE_WITH_VIDEO_MODULE = 0`.
- `[user Makefile]:68` manually appends LIBCPK.A to LIBS:
  `LIBS += jo-engine/Compiler/COMMON/SGL_302j/LIB_ELF/LIBCPK.A`.
- `[intro_video.c]:62-64` defines stub `SND_*` functions so the bare LIBCPK.A
  link doesn't fail.
- The "bisect-2" test (called `CPK_Init()` from inside jo_main once init was
  complete) locked the boot to black at ELF=291164 bytes.

**Root cause (definitive):** `JO_COMPILE_WITH_VIDEO_MODULE = 0` strips
`-DJO_COMPILE_WITH_VIDEO_SUPPORT` from CCFLAGS (`[jo_engine_makefile]:55-62`),
which removes `[jo core.c]:395-402` from the build — the `jo_video_init()`
call inside `jo_core_init`. **`jo_video_init` is the function that performs
the canonical SGL `CPK_Init` + `jo_core_add_vblank_callback(CPK_VblIn)`
ordering** (`[jo video.c]:280-297`). Without it, calling `CPK_Init` later
**from arbitrary application code, with jo's vblank already wired through
`__jo_vblank_callbacks`**, leaves the CPK runtime with no `CPK_VblIn` hook
into the vblank — yet `CPK_Init` does internal state setup that expects to
be ticked. The "boots to black" symptom is the classic Cinepak deadlock
where `CPK_Init` arms structures that get serviced only by `CPK_VblIn`.

The three suspect causes listed in `[intro_video.c]:13-29` are diagnosed:

| Suspect | Verdict | Evidence |
|---|---|---|
| (a) Slave SH-2 program upload | **NOT THE CAUSE** | `[MANCPK.DOC]:178-184` and `[SGL_CPK.H]:186-196`: `CPK_CPU_MAIN = 1` is the default; slave SH-2 is touched only after explicit `CPK_SetCpu(CPK_CPU_DUAL)`. None of the canonical samples (`[cinepak_sgl.c]`, `[SBL SMPCPK2]`, `[SBL Demo 4 main.c]`) call `CPK_SetCpu` in single-CPU mode. |
| (b) SCSP / `SND_Init` before `CPK_Init` | **PARTIAL — required when audio is played**; not required when only video matters. The canonical demos call `sndInit()` before `cpkInit()` (`[SBL Demo 4 main.c]:646-650`, `[cinepak_sgl.c]:327-331`), but the prerequisites in `[MANCPK.DOC]:78-94` list error functions, GFS init, STM init — sound init is needed only for the PCM playback path. jo already initialises sound when `JO_COMPILE_WITH_AUDIO_MODULE=1` (the user's current state). |
| (c) V-blank vector | **THIS IS THE CAUSE** | `[cinepak_sgl.c]:325` `slIntFunction((void*)smpVblIn)` where `smpVblIn` calls `CPK_VblIn()`. `[jo video.c]:295` performs the equivalent via `jo_core_add_vblank_callback(CPK_VblIn)`, which internally drives `slIntFunction(__jo_vblank_callbacks)` (`[jo core.c]:432`). **If `jo_video_init` is not run, `CPK_VblIn` is never inserted into jo's vblank dispatch list; `CPK_Init` arms internal counters and event flags that the missing `CPK_VblIn` is supposed to advance every frame, so playback can never start.** |

## 2. The canonical SGL Cinepak boot sequence (Sega-shipped, verbatim)

From `[cinepak_sgl.c]:311-343` — this is the order of operations the SGL
Cinepak sample uses, before any movie file is opened:

```c
slInitSystem(TV_320x224, NULL, 1);          /* line 311 */
slTVOff();                                  /* line 312 */
slBitMapNbg0(COL_TYPE_1M, BM_512x256,       /* line 313 */
             (void *)VDP2_VRAM_A0);
slColRAMMode(CRM16_1024);                   /* line 314 */
slScrAutoDisp(NBG0ON);                      /* line 315 */
slTVOn();                                   /* line 316 */
slScrCycleSet(CYCPAT_BM_READ, CYCPAT_BM_READ,
              CYCPAT_BM_READ, CYCPAT_BM_READ); /* line 317 */
slScrPosNbg0(toFIXED(0),
             toFIXED((512/2 - WIDTH_V)/2)); /* line 318 */
slVRAMMode((Uint16)NULL);                   /* line 319 */
slSynch();                                  /* line 320 */

PauseFlag = -1;                             /* line 323 */
slIntFunction((void*)smpVblIn);             /* line 325  -- vblank handler */

fileInit();                                 /* line 327  -- GFS_Init + STM_Init */

sndInit();                                  /* line 329  -- SND_Init(SDDRVS+BOOTSND) */

CPK_Init();                                 /* line 331  -- CPK runtime init */
CPK_SetErrFunc(errCpkFunc, NULL);           /* line 333 */
```

The smpVblIn handler at `[cinepak_sgl.c]:193-197`:

```c
void smpVblIn(void)
{
    CPK_VblIn();           /* drives Cinepak's per-frame timer */
    Switch_VBL_IN = FALSE; /* sample-specific */
}
```

The SBL-side ordering is identical (`[SBL SMPCPK2]:600-613`,
`[SBL Demo 4 main.c]:637-650`):

```c
DMA_ScuInit();      /* required only for OUTPUT_WORK_RAM path */
g_spr_end = TRUE;
dispInit();         /* installs VBLK_IN -> smpVblIn -> CPK_VblIn (line 256, 312-321) */
fileInit();         /* GFS_Init */
sndInit();          /* SND_Init */
cpkInit();          /* CPK_Init + CPK_SetErrFunc */
```

So the canonical sequence is:
**`slInitSystem` → VDP1/VDP2 mode setup → vblank vector pointing at a
handler that calls `CPK_VblIn` → `GFS_Init` → `SND_Init` → `CPK_Init`.**

Per-handle setup (in `createMovie`, `[cinepak_sgl.c]:220-272`,
`[SBL Demo 4 main.c]:462-513`):

```c
CPK_PARA_WORK_ADDR(&para) = g_movie_work;          /* CPK_15WORK_BSIZE bytes */
CPK_PARA_WORK_SIZE(&para) = CPK_15WORK_BSIZE;
CPK_PARA_BUF_ADDR(&para)  = g_movie_buf;           /* ring buffer */
CPK_PARA_BUF_SIZE(&para)  = RING_BUF_SIZ;
CPK_PARA_PCM_ADDR(&para)  = (void *)0x25A20000;
CPK_PARA_PCM_SIZE(&para)  = 4096L * 16;

GfsHn gfs = GFS_Open(GFS_NameToId("SAMPLE.CPK"));
CpkHn cpk = CPK_CreateGfsMovie(&para, gfs);

CPK_SetTrModePcm(cpk, CPK_TRMODE_CPU);    /* PCM via CPU software copy */
CPK_SetColor(cpk, CPK_COLOR_15BIT);       /* or CPK_COLOR_24BIT for VDP2 32bpp */
CPK_PreloadHeader(cpk);
CpkHeader *h = CPK_GetHeader(cpk);
/* configure sprite/NBG geometry from h->width, h->height */
CPK_SetDecodeAddr(cpk, decode_buf, h->width * bpp_factor);
CPK_SetStartTrgSize(cpk, 100 * 2048);     /* 200 KB prebuffer */
CPK_SetVolume(cpk, 7);
CPK_SetPan(cpk, 0x10);
```

Play loop (`[cinepak_sgl.c]:345-432`, `[SBL Demo 4 main.c]:652-712`):

```c
CPK_Start(cpk);
while (1) {
    CPK_Task(cpk);                              /* advances streaming */
    if (CPK_IsDispTime(cpk) == TRUE) {
        /* transfer decoded frame from work_ram_buf to VDP2/VDP1 VRAM */
        CPK_CompleteDisp(cpk);
    }
    if (CPK_GetPlayStatus(cpk) == CPK_STAT_PLAY_END) {
        CPK_DestroyGfsMovie(cpk);
        GFS_Close(gfs);
        break;
    }
}
```

## 3. What `CPK_Init` requires of pre-init state — definitive

Cross-referenced from `[MANCPK.DOC] §3` (compatibility list, lines 71-83 of
the iconv-converted UTF-8) and the API contract in `[SGL_CPK.H]`:

1. **VDP1/VDP2 brought up by `slInitSystem`.** `CPK_Init` itself does not
   touch VDP registers, but `CPK_CreateGfsMovie` allocates an internal
   handle that relies on SGL having booted (because it calls into SGL's
   memory helpers). Failing to do `slInitSystem` first is undefined behavior.
2. **GFS initialised** (`GFS_Init` with a `GfsDirTbl`) — only required
   before `CPK_CreateGfsMovie`, not for `CPK_Init` itself. Confirmed by
   `[MANCPK.DOC]:71-83` — file system Ver 1.20 or higher is listed as a
   prerequisite for the *create* call, not the init call.
3. **Vblank vector calling `CPK_VblIn`** — this is the runtime contract.
   `CPK_VblIn` updates `status.cnt_vbl_in` and friends
   (`[SGL_CPK.H]:425-433`); `CPK_Task` and `CPK_IsDispTime` consume those
   counters. **If `CPK_VblIn` is never called, every CPK handle remains
   frozen in `CPK_STAT_PLAY_HEADER` / `CPK_STAT_PLAY_START` and
   `CPK_IsDispTime` always returns FALSE.** This is exactly what produces
   the observed black-screen state — `CPK_Start` succeeds, but the play
   loop never sees `IsDispTime == TRUE`.
4. **SCSP/sound only required if PCM is being played.** `[MANCPK.DOC] §5.1`
   compatibility table line 168 says
   `CPK_SetTrModePcm` may use `CPU` or `SCU-DSP`/`CPU-DMA` transfer, OR be
   bypassed entirely if `pcm_size` is zero in the create-para. Setting
   `CPK_PARA_PCM_ADDR` to NULL/`0` and `CPK_PARA_PCM_SIZE` to 0 skips
   sound altogether — useful only if the source video has no audio. (Our
   Mania.ogv has audio; we want it.)
5. **Slave SH-2 reset-held is fine.** Default `CPK_CPU_MAIN` mode never
   uploads or touches the slave (`[MANCPK.DOC]:178-184`, `[SGL_CPK.H]:186`).

## 4. Can `CPK_Init` be called AFTER jo_core_init has already set up vblank?

**Yes, with one constraint: a follow-up `jo_core_add_vblank_callback(CPK_VblIn)`
call.** `jo_core_init` builds a linked list of vblank callbacks
(`__vblank_callbacks`) and once the first callback is registered, jo installs
`__jo_vblank_callbacks` as the SGL vblank handler via
`slIntFunction(__jo_vblank_callbacks)` (`[jo core.c]:432`). The
`__jo_vblank_callbacks` function (the master dispatcher) walks the list every
vblank and calls each registered callback. **Adding `CPK_VblIn` to that list
after `CPK_Init` is the correct integration.** This is precisely what
`jo_video_init` does at `[jo video.c]:295`.

There is no "release back" handoff needed for the video phase. Once the video
finishes, `jo_video_stop` removes `CPK_VblIn` from the list (or you can keep
it: the handler is cheap and harmless when no movie is active).

## 5. Canonical integration plan (jo+SGL) — DEFINITIVE

**The integration is already written by jo-engine.** The user's task is to
turn it on. Specifically:

### 5.1 Enable the video module in `[user Makefile]`

```diff
- JO_COMPILE_WITH_VIDEO_MODULE        = 0
+ JO_COMPILE_WITH_VIDEO_MODULE        = 1
```

Effects (`[jo_engine_makefile]:55-62, 258-265`):

1. Adds `-DJO_COMPILE_WITH_VIDEO_SUPPORT` to CCFLAGS → `jo_video_init` is
   called from `jo_core_init` (`[jo core.c]:395-402`).
2. Adds `$(SGLLDR)/LIBCPK.A $(SGLLDR)/LIBSND.A` to LIBS.
3. Compiles `jo_engine/video.c` into the build.
4. Switches the optimizer profile to the special `-Os` Cinepak-compatible
   options (line 259-265) — `-Wno-strict-aliasing`, `-fno-builtin`, etc.

### 5.2 Remove the manual LIBCPK.A hack in `[user Makefile]:62-68`

```diff
- # LIBCPK.A: SGL Cinepak playback. The library succeeds at link time but
- # ...
- LIBS += jo-engine/Compiler/COMMON/SGL_302j/LIB_ELF/LIBCPK.A
```

It is now added automatically by `[jo_engine_makefile]:57`.

### 5.3 Remove the SND_* stubs and replace `intro_video.c` with a thin wrapper

```c
/* src/intro_video.c — minimal driver around jo's video module */
#include <jo/jo.h>

static volatile int g_intro_done = 0;

static void on_video_stopped(void)
{
    g_intro_done = 1;
}

int intro_video_play(const char *cpk_filename)
{
    if (!jo_video_open_file(cpk_filename)) {
        return 0;
    }
    g_intro_done = 0;
    if (!jo_video_play(on_video_stopped)) {
        return 0;
    }
    /* Block the title state until the video finishes. The internal
     * __jo_internal_cpk callback (jo video.c:125-136) is already registered
     * in jo_core's callback list by jo_video_play; it ticks CPK_Task and
     * calls our on_video_stopped when CPK_STAT_PLAY_END fires. We just
     * pump jo_synch (the engine's per-frame yield, via slSynch internally
     * inside jo_core_run; if we're called BEFORE jo_core_run starts, we
     * need to drive vblanks manually — see §5.4). */
    while (!g_intro_done) {
        slSynch();   /* drives the registered vblank callbacks, including
                      * jo's dispatcher which calls CPK_VblIn and
                      * __jo_internal_cpk. */
    }
    return 1;
}
```

### 5.4 Calling `intro_video_play` at the right moment

The cleanest hookup is from inside `jo_main`, **after** `jo_core_init` has
returned (so vblanks and CPK_Init are wired) but **before** the title state
sets up its own sprites. The user's `src/main.c` already has this shape; the
single new line is:

```c
void jo_main(void)
{
    jo_core_init(JO_COLOR_Black);
    /* ... existing memory zone setup ... */

    intro_video_play("INTRO.CPK");   /* NEW: block until video done */

    /* ...existing title-state setup, e.g. load_title_sonic_anim()... */
    jo_core_add_callback(my_title_draw);
    jo_core_run();
}
```

Note `[jo demo-video main.c]:60-63` is the upstream pattern — `jo_video_play`
is called *concurrently* with the title state via callbacks. Our case wants
**sequential** (video first, then title), hence the explicit while-loop
draining `slSynch()` in §5.3.

### 5.5 Memory budget check

`[jo video.c]` allocates from jo_malloc:
- `__jo_pcm_buffer = jo_malloc(PCM_SIZE)`            32 KB (line 49,145)
- `__jo_ring_buffer = jo_malloc(RING_BUF_SIZ)`       100 KB (line 51,146)
- `__jo_video_cpk.movie_buffer = jo_malloc(RING_BUF_SIZ)` 100 KB (line 163)
- `__jo_video_cpk.work_buffer = jo_malloc(CPK_15WORK_BSIZE * 4)` ≈ 152 KB
  (line 171). Note the `* sizeof(unsigned int)` multiplier is suspect; the
  SGL canonical sample uses `CPK_15WORK_BSIZE` directly without the `*4` —
  see `[cinepak_sgl.c]:83` and `[SBL SMPCPK2]:73`. This is a known
  over-allocation in jo's video module; it works because the user's pool
  is large enough.
- `__jo_video_cpk.decode_buffer = jo_malloc(W*H*2)`  for 320×224×2 = 140 KB
  (line 207).

Total transient allocation: **~520 KB during playback** in the worst case
(with jo's extra `*4` on the work buffer). The user's
`JO_GLOBAL_MEMORY_SIZE_FOR_MALLOC = 589824 = 576 KB`
(`[user Makefile]:47`) **is borderline**. After `jo_video_stop` everything
is freed back.

**Recommended:** for the duration of the intro phase only, also enable
LWRAM expansion (jo's `jo_add_memory_zone` from the demo, `[jo demo-video
main.c]:29-30,59`):

```c
#define LWRAM_HEAP_SIZE 0x40000   /* +256 KB scratch */
#define LWRAM           0x00200000
jo_add_memory_zone((unsigned char *)LWRAM, LWRAM_HEAP_SIZE);
```

This adds the 1 MB Work-RAM-L (low) region to the malloc pool. The demo does
this immediately after `jo_core_init` for the same reason.

### 5.6 Encode `Data/Video/Mania.ogv` to `INTRO.CPK`

`tools/ogv_to_cpk.py` already exists per the user's note. Its output must
satisfy `[Saturn Overview]:1806-1808` and `[MANCPK.DOC]` constraints:

- **Total bitrate ≤ 300 KB/s** sustained (video + audio combined).
- **Video width a multiple of 8** (`[jo video.c]:199-204` enforces this with
  a `JO_MOD_POW2(header->width, 8) != 0` assert in JO_DEBUG builds).
- Codec must be `cvid` (Cinepak), boxed in the SEGA FILM container
  (magic `"FILM"` + `"FDSC"` + `"STAB"` chunks per `[SGL_CPK.H]:294-319`).
- 15-bit RGB color mode (the jo path forces `CPK_COLOR_15BIT` at
  `[jo video.c]:194`).
- Audio (if present) must be PCM at the rates the CPK header expects
  (`[SGL_CPK.H]:305-311`).

If `ogv_to_cpk.py` outputs files conforming to SegaSaturnFilmTools (which it
does per the user's note that "tool already works"), the resulting .CPK is
drop-in compatible with `jo_video_open_file`.

## 6. The exact verification gate to add

Per the codebase's "QA iterative improvement" memory rule
(`memory/qa-iterative-improvement.md`), every bug class needs a new gate.
For this bug class (Cinepak link/init misordering) add to
`tools/verify_done.ps1`:

```powershell
# Gate N: Cinepak module sanity
# Verify the Makefile actually enables the video module rather than the
# manual-LIBCPK hack from the bisect-2 attempt.
$mk = Get-Content "$projectRoot/Makefile" -Raw
if ($mk -notmatch '(?m)^JO_COMPILE_WITH_VIDEO_MODULE\s*=\s*1') {
    Write-Error "GATE FAIL: JO_COMPILE_WITH_VIDEO_MODULE must be 1 (intro video)"
    exit 1
}
if ($mk -match 'LIBS\s*\+=\s*.*LIBCPK\.A') {
    Write-Error "GATE FAIL: LIBCPK.A manual append still present; jo_engine_makefile adds it automatically when video module is on"
    exit 1
}
# Verify INTRO.CPK exists in cd/ and has the FILM header
$cpk = Get-Item "$projectRoot/cd/INTRO.CPK"
if (-not $cpk -or $cpk.Length -lt 4096) {
    Write-Error "GATE FAIL: cd/INTRO.CPK missing or too small"
    exit 1
}
$bytes = [System.IO.File]::ReadAllBytes($cpk.FullName)[0..3]
$magic = [System.Text.Encoding]::ASCII.GetString($bytes)
if ($magic -ne 'FILM') {
    Write-Error "GATE FAIL: cd/INTRO.CPK has wrong magic '$magic' (expected 'FILM')"
    exit 1
}
```

This gate fires on the *broken* state — the current `Makefile` has
`JO_COMPILE_WITH_VIDEO_MODULE = 0` AND the manual `LIBS += LIBCPK.A`, so the
gate correctly reports the broken state before the fix is attempted.

## 7. Summary

| Question | Definitive answer | Citation |
|---|---|---|
| What is the canonical SGL Cinepak init sequence? | `slInitSystem` → VDP setup → `slIntFunction(handler_calling_CPK_VblIn)` → `GFS_Init` → `SND_Init` → `CPK_Init` → `CPK_SetErrFunc` | `[cinepak_sgl.c]:311-333`, `[SBL Demo 4 main.c]:637-650` |
| Per-handle setup? | `CPK_CreateGfsMovie(&para, gfs)` → `CPK_SetColor` → `CPK_PreloadHeader` → `CPK_GetHeader` → `CPK_SetDecodeAddr` → `CPK_SetStartTrgSize` | `[cinepak_sgl.c]:213-272` |
| Play loop? | `CPK_Start` → loop {`CPK_Task` → if `CPK_IsDispTime` then copy & `CPK_CompleteDisp` → check `CPK_GetPlayStatus == CPK_STAT_PLAY_END`} | `[cinepak_sgl.c]:345-432` |
| Does `CPK_Init` upload a slave SH-2 program? | NO in default mode. Only if `CPK_SetCpu(CPK_CPU_DUAL)` is explicitly called. | `[MANCPK.DOC]:178-184`, `[SGL_CPK.H]:186-196` |
| What does `CPK_Init` actually require? | Vblank handler calling `CPK_VblIn`. Without this, every CPK handle stays frozen and `IsDispTime` is always FALSE → black screen forever. | `[SGL_CPK.H]:425-433`, `[cinepak_sgl.c]:325`, `[jo video.c]:295` |
| Can `CPK_Init` be called AFTER jo_core_init? | YES, but you MUST follow it with `jo_core_add_vblank_callback(CPK_VblIn)`. `jo_video_init` does both of these in the right order and is the canonical jo integration. | `[jo video.c]:280-297`, `[jo core.c]:395-402, 432` |
| Why did bisect-2 lock to black? | `JO_COMPILE_WITH_VIDEO_MODULE=0` → `jo_video_init` skipped → `CPK_VblIn` never enters the vblank list → `CPK_Init` sets up state but `CPK_Task` never advances. | `[user Makefile]:10`, `[jo_engine_makefile]:55-62`, `[jo core.c]:395-402` |
| **Definitive fix** | Set `JO_COMPILE_WITH_VIDEO_MODULE = 1`, drop the manual `LIBS += LIBCPK.A`, drop the SND_* stubs, replace `intro_video_play` with thin wrapper around `jo_video_open_file`/`jo_video_play`, optionally `jo_add_memory_zone(LWRAM, 0x40000)` for headroom. | this document §5.1-§5.6 |

There is no need to write a custom Cinepak decoder (Path B in
`[intro_video.c]:42-48`) or to fork `jo_core_init` (Path A in
`[intro_video.c]:33-41`). The right answer is **turn on the existing module
in the Makefile** and the canonical Sega+jo integration runs as-shipped.
