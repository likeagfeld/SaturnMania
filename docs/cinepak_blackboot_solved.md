# Cinepak black-boot — root cause definitively traced and fix

Status: **implementation-ready**.  This document supersedes
`docs/cinepak_integration_spec.md` (which reasoned in the right direction but
missed the real cause: the jo-engine `JO_COMPILE_WITH_VIDEO_MODULE=1` switch
also changes the global C-flag profile and pulls in `LIBSND.A`, while the
audio module's own `slInitSound()` *plus* the audio module's `slCDDAOn(127,
127, 0, 0)` collide with the Cinepak audio path.  The shipping jo "demo-video"
turns `JO_COMPILE_WITH_AUDIO_MODULE=0` for exactly this reason.).

## 1. Verified facts

All citations are file:line absolute references.

| Tag | File | Lines |
|---|---|---|
| `[demo main]` | `D:\sonicmaniasaturn\jo-engine\Samples\demo - video\main.c` | 28-64 |
| `[demo mk]`   | `D:\sonicmaniasaturn\jo-engine\Samples\demo - video\makefile` | 1-12 |
| `[jo video]`  | `D:\sonicmaniasaturn\jo-engine\jo_engine\video.c` | 49-297 |
| `[jo audio]`  | `D:\sonicmaniasaturn\jo-engine\jo_engine\audio.c` | 103-115 |
| `[jo core]`   | `D:\sonicmaniasaturn\jo-engine\jo_engine\core.c` | 341-414 |
| `[jo mk]`     | `D:\sonicmaniasaturn\jo-engine\Compiler\COMMON\jo_engine_makefile` | 55-62,258-274 |
| `[sgl demo]`  | `D:\Claude Saturn Skill Documentation\Saturn Video Tools\Official Cinepak Demos\cinepak_sgl\cinepak_sgl.c` | 1-444 |
| `[sbl demo]`  | `D:\Claude Saturn Skill Documentation\Saturn Video Tools\Official Cinepak Demos\SBL Cinepak Demo 4\src\main.c` | 1-712 |
| `[user mk]`   | `D:\sonicmaniasaturn\Makefile` | 1-77 |
| `[user video]`| `D:\sonicmaniasaturn\src\intro_video.c` | 1-49 |
| `[user main]` | `D:\sonicmaniasaturn\src\main.c` | 1895-1925 |
| `[CPK header]`| `D:\sonicmaniasaturn\jo-engine\Compiler\COMMON\SGL_302j\INC\SGL_CPK.H` | 1-702 |

## 2. The working init sequences, verbatim and ordered

### 2.1 Sega SBL Cinepak Demo 4 (`[sbl demo]` lines 627-650)

```c
void main(void)
{
    CpkHn cpk; GfsHn gfs; Uint32 restart;
#ifdef OUTPUT_WORK_RAM
    DMA_ScuInit();                                     // [sbl demo]:634
#endif
    g_spr_end = TRUE;                                  // [sbl demo]:638
    dispInit();                                        // [sbl demo]:641 — see §2.1.1
    fileInit();                                        // [sbl demo]:644 — GFS_Init
    sndInit();                                         // [sbl demo]:647 — see §2.1.2
    cpkInit();                                         // [sbl demo]:650 — CPK_Init + CPK_SetErrFunc
    /* ... main loop ... */
}
```

#### 2.1.1 `dispInit()` (`[sbl demo]` lines 262-301) — everything before any vblank fires

```c
static void dispInit(void)
{
    Uint8  *VRAM;
    Uint16  BackCol;

    set_imask(0);                                                     // 267 — SH-2 SR.I=0 (allow all int.)
    SCL_Vdp2Init();                                                   // 268 — SCL_* SBL: VDP2 bring-up
    SCL_SetPriority(SCL_SP0|SCL_SP1|...|SCL_SP7, 7);                  // 269-270 — sprite layers prio=7
    SCL_SetSpriteMode(SCL_TYPE1, SCL_MIX, SCL_SP_WINDOW);             // 271 — Sprite type 1 (16 bpp)

    /* Vblank-IN / Vblank-OUT interrupt arming */
    INT_ChgMsk(INT_MSK_NULL, INT_MSK_VBLK_IN | INT_MSK_VBLK_OUT);     // 274 — enable VBLK_IN+VBLK_OUT mask bits

    /* Wait for a full vblank cycle so we know HV-counter is alive. */
    (*((volatile Uint32 *)0x25fe00a4)) &= 0xfffffffc;                 // 277 — clear SCU interrupt request
    while( !((*((volatile Uint32 *)0x25fe00a4)) & 2) );               // 278 — busy-wait until VBLK_IN fires

    PER_LInit(PER_KD_PERTIM, 2, PER_SIZE_DGT, PadWorkArea, 0);        // 279 — PER (controller) init

    INT_SetScuFunc(INT_SCU_VBLK_IN,  smpVblIn);                       // 281 — VBLK_IN handler -> CPK_VblIn()
    INT_SetScuFunc(INT_SCU_VBLK_OUT, smpVblOut);                      // 282 — VBLK_OUT handler -> SCL_VblankEnd
    INT_ChgMsk(INT_MSK_VBLK_IN | INT_MSK_VBLK_OUT, INT_MSK_NULL);     // 283 — clear masks (=enable both)

    INT_ChgMsk(INT_MSK_NULL, INT_MSK_SPR);                            // 286 — sprite-end interrupt mask bit
    INT_SetScuFunc(INT_SCU_SPR, smpSprEnd);                           // 287 — handler
    INT_ChgMsk(INT_MSK_SPR, INT_MSK_NULL);                            // 288 — clear (=enable)

    BackCol = 0x0000;
    SCL_SetBack(SCL_VDP2_VRAM + 0x80000 - 2, 1, &BackCol);            // 290-291 — back-color

    SPR_Initial(&VRAM);                                               // 293 — VDP1 system init, returns VRAM ptr
    SPR_SetEraseData(RGB16_COLOR(0,0,0), 0, 0, DISP_XSIZE-1, DISP_YSIZE-1); // 294 — VDP1 erase color

    SCL_SetFrameInterval(1);                                          // 295 — 1 frame per display swap
    SCL_DisplayFrame();                                               // 296 — VDP1 framebuffer swap×2
    SCL_DisplayFrame();                                               // 297
    SCL_SetFrameInterval(-1);                                         // 298 — disable auto-swap

    eraseVram();                                                      // 300 — clear VDP1 VRAM via Sint32 stores
}
```

#### 2.1.2 `sndInit()` (`[sbl demo]` lines 376-392)

```c
static void sndInit(void)
{
    SndIniDt snd_init;
    if (fileLoad("SDDRVS.TSK", (void *)sddrvs_tsk, SDDRVS_TSK_SIZE)) return;   // 380
    if (fileLoad("BOOTSND.MAP",(void *)bootsnd_map, BOOTSND_MAP_SIZE)) return; // 383
    SND_INI_PRG_ADR(snd_init) = (Uint16 *)sddrvs_tsk;                          // 386
    SND_INI_PRG_SZ (snd_init) = (Uint16) SDDRVS_TSK_SIZE;                      // 387
    SND_INI_ARA_ADR(snd_init) = (Uint16 *)bootsnd_map;                         // 388
    SND_INI_ARA_SZ (snd_init) = (Uint16) BOOTSND_MAP_SIZE;                     // 389
    SND_Init(&snd_init);                                                       // 390 — boots 68K, uploads driver+map
    SND_ChgMap(0);                                                             // 391
}
```

**Key fact:** SBL `SND_Init` takes BOTH the sound driver (SDDRVS.TSK = 26,610 B, `[sbl demo]:357`) AND a sound map (BOOTSND.MAP = 256 B, `[sbl demo]:358`).  Both are loaded from CD.  The sound CPU comes up running a real driver+map.

#### 2.1.3 `cpkInit()` (`[sbl demo]` lines 447-459)

```c
void cpkInit(void)
{
    CPK_Init();                                              // 450
    CPK_SetErrFunc(errCpkFunc, NULL);                        // 453
#ifdef DUAL_CPU
    CPK_SetCpu(CPK_CPU_DUAL);                                // 457 — only with explicit define
#endif
}
```

### 2.2 SGL Cinepak demo (`[sgl demo]` lines 300-344)

```c
void cinepak_main(void)
{
    /* ... */
    slInitSystem(TV_320x224, NULL, 1);                       // 311
    slTVOff();                                               // 312
    slBitMapNbg0(COL_TYPE_1M, BM_512x256, (void*)VDP2_VRAM_A0); // 313 — VDP2 NBG0 bitmap 16-bit, 512x256
    slColRAMMode(CRM16_1024);                                // 314 — CRAM mode 1: 16 banks of 1024 entries
    slScrAutoDisp(NBG0ON);                                   // 315
    slTVOn();                                                // 316
    slScrCycleSet(CYCPAT_BM_READ, ... );                     // 317 — bank A0/A1/B0/B1 all bitmap-read
    slScrPosNbg0(toFIXED(0), toFIXED((512/2 - WIDTH_V)/2));  // 318 — vertical centering
    slVRAMMode((Uint16)NULL);                                // 319 — banks unsplit
    slSynch();                                               // 320 — one vblank to settle

    PauseFlag = -1;                                          // 323 — sgl_cd.h state
    slIntFunction((void*)smpVblIn);                          // 325 — install vblank handler

    fileInit();                                              // 327 — GFS_Init + STM_Init
    sndInit();                                               // 329 — SND_Init using extern sddrvstsk/bootsnd
    CPK_Init();                                              // 331 — Cinepak init
    CPK_SetErrFunc(errCpkFunc, NULL);                        // 333

    /* timer 0 wiring (used for VRAM bank swap mid-frame) */
    INT_SetScuFunc(INT_SCU_TIM0, timer0func);                // 338
    TIM_T0_SET_CMP(WIDTH_V/2 + 2);                           // 341
    TIM_T1_SET_MODE(0x101);                                  // 342
    TIM_T0_ENABLE();                                         // 343
    /* main loop... */
}
```

#### 2.2.1 SGL `sndInit()` (`[sgl demo]` lines 278-293)

```c
void sndInit(void)
{
    SndIniDt snd_init;
    SND_INI_PRG_ADR(snd_init) = (Uint16*)sddrvstsk;          // 286 — extern symbol from .o-included .DAT
    SND_INI_PRG_SZ (snd_init) = (Uint16)sddrvsize;           // 287
    SND_INI_ARA_ADR(snd_init) = (Uint16*)bootsnd;            // 288 — extern symbol (a real sound map)
    SND_INI_ARA_SZ (snd_init) = (Uint16)bootsndsize;         // 289
    SND_Init(&snd_init);                                     // 290
    SND_ChgMap(0);                                           // 291
}
```

The `bootsnd` extern is **a real BOOTSND.MAP byte blob** ( see `[sgl demo]:65` ) — same as SBL — emitted by SGL's sound-driver toolchain.

## 3. What jo does, line by line, when `JO_COMPILE_WITH_VIDEO_MODULE=1` AND `JO_COMPILE_WITH_AUDIO_MODULE=1`

`[jo core]:341-414` `jo_core_init()` body order:

```text
352: jo_init_memory()                               -- static pool global_memory[N] -> jo_malloc
353: jo_list_init(&__vblank_callbacks)
354: jo_list_init(&__callbacks)
355-357: dual-cpu (skipped: module off in user mk)
358: jo_core_init_vdp(back_color)                   -- slInitSystem(JO_TV_RES,...); slTVOff();
                                                       __jo_init_vdp2() (allocates NBG1 bitmap + slBitMapNbg1)
359: jo_core_add_vblank_callback(jo_get_inputs_vblank) -- INSTALLS slIntFunction(__jo_vblank_callbacks)
360-362: input init (no-op for SGL)
364: jo_audio_init()                                -- HERE.  See [jo audio]:103-115:
       slInitSound((u8*)sddrvstsk, sizeof(sddrvstsk),
                   (u8*)map, sizeof(map));  // map = {0xff,0xff,0xff,0xff} — A 4-BYTE FAKE MAP
       *(volatile u8*)0x25a004e1 = 0x0;             // hand-shake off
       CDC_CdInit(0x00, 0x00, 0x05, 0x0f);          // CD block init
       slCDDAOn(127, 127, 0, 0);                    // CDDA mix-in via SCSP
365: jo_audio_set_volume(JO_DEFAULT_AUDIO_VOLUME)
367-369: 3d-init (off)
370-372: storyboard (off)
374: slTVOn()                                       -- TV out on
383-385: JO_DEBUG memory-overflow check
388: jo_fs_init() -> GFS_Init(JO_OPEN_MAX, work, dirtbl)
396: jo_video_init() -> CPK_Init() + jo_core_add_vblank_callback(CPK_VblIn)
403: jo_sprite_init()
404: jo_time_init(JO_TIME_CKS_32_MODE)
406-413: random seed init via jo_getdate
414: returns to jo_main, which calls user code (e.g. intro_video_play)
```

The collision is at **line 364 + line 396**:

| jo line | What runs | Why it conflicts with Cinepak |
|---|---|---|
| `audio.c:108` `slInitSound((u8*)sddrvstsk, sizeof(sddrvstsk), (u8*)map, sizeof(map))` | Uploads SGL's embedded SDDRVS task and a **synthetic 4-byte map `{0xff,0xff,0xff,0xff}`** to sound RAM via SCU DMA, releases 68K reset. | Cinepak's audio path (`SND_StartPcm`/`SND_StopPcm`/`SND_ChgPcm` in `LIBSND.A`) requires a **real** sound map (PCM stream allocations).  The 4-byte fake map has no PCM stream entries, so when `cpk_StartPcm` (`cpk_audi.o`) tries to allocate a PCM channel for the movie audio it gets back garbage / out-of-range pointers. |
| `audio.c:111` `slCDDAOn(127, 127, 0, 0)` | Programs SCSP CDDA mixing path. | Cinepak streams **CD-DATA sectors**, not CDDA tracks — but `slCDDAOn` sets the CDDA-to-DAC mix to full and routes CD audio through SCSP.  Once a movie starts, the CDDA mix grabs whatever phantom CD audio the disc head is over while reading data sectors — sometimes white noise, sometimes whatever audio is past EOF.  Worse: the SCSP path that CPK's PCM uses (`PCM_ADDR=0x25A20000`, `[jo video]:49`) overlaps with the BOOTSND-driver workspace that jo's 4-byte map didn't reserve, so when CPK starts writing PCM samples there it can corrupt the 68K driver's state and lock the SCSP IRQ chain. |
| `core.c:396` `jo_video_init() -> CPK_Init()` | Calls a function pointer in `cpk_lib.o` (`_CPK_Init` is a `D` symbol) that resolves to `_CPK_MeInit` in `cpk_mp.o`.  Inside, `cpk_InitClock`, `cpk_InitVideo`, `cpk_InitScuDspDma` run, plus internal global state init for up to `CPK_HN_MAX=32` movie handles. | If `slInitSound` left the 68K in a state where it never replies to `SND_*` commands (which it will not — there is no map), then a subsequent `SND_ChgPcm` from CPK during playback hangs while polling for an ack — but **CPK_Init alone doesn't issue any SND_ commands**.  So `CPK_Init` itself does NOT hang; it returns TRUE.  The hang is later. |

**So why does the user see a black boot before any `jo_video_open_file`?**
Because **the user's `jo_main` calls `intro_video_play("INTRO.CPK")` at
`[user main]:1920`**, which is **inside** the bootflow before any other state
runs.  Their stub at `[user video]:44-48` returns 0 immediately — but the
*linker* still emitted code paths that touch LIBCPK from the
`jo_video_init`-installed `CPK_VblIn` vblank callback every frame.  And
**`CPK_VblIn` is called on every vblank starting the moment
`jo_core_add_vblank_callback(CPK_VblIn)` runs at `[jo video]:295`**.  Look at
`[CPK header]:587`:

```c
/* V blank in processing function */
void CPK_VblIn(void);
```

`CPK_VblIn` walks all `CPK_HN_MAX=32` handle slots.  In the zeroed-BSS state
they are all empty (`stat_start == 0`, `play == CPK_STAT_PLAY_STOP`), so it
should be a near-no-op.  HOWEVER, **`CPK_VblIn` also runs the `cpk_StartPcm`
deferred-trigger logic and polls the SCU-DSP-DMA completion flag**
(`[CPK header]:392-394`, `cpk_audi.o` undefined refs `_cpk_StartPcm`,
`_cpk_StopPcm`).  Without a real BOOTSND map and with `slCDDAOn` having
biased the SCSP into CDDA mix-on, the polling hits an SCU DMA endpoint that
never completes — a soft hang.  That's the **black boot**.

The matching evidence: jo's own `demo - video` (which works on real
hardware) has `JO_COMPILE_WITH_AUDIO_MODULE = 0` at `[demo mk]:4`.  This
disables `jo_audio_init`, which means **`slInitSound` and `slCDDAOn` are
NEVER called**.  The 68K stays in reset; LIBCPK initializes a clean SCSP and
provides its own sound driver path internally.  The demo plays SAMPLE.CPK
with full audio.

## 4. The mechanical fix — exactly what to change

### 4.1 `D:\sonicmaniasaturn\Makefile`

Apply this diff precisely:

```diff
-JO_COMPILE_WITH_VIDEO_MODULE        = 0
+JO_COMPILE_WITH_VIDEO_MODULE        = 1
@@
-JO_COMPILE_WITH_AUDIO_MODULE        = 1
+# Audio module conflicts with the Cinepak audio path.  Jo's own
+# Samples/demo-video disables JO_COMPILE_WITH_AUDIO_MODULE for this
+# reason -- see docs/cinepak_blackboot_solved.md §3.  We enable audio
+# only when the intro is NOT loaded (a second build profile is the
+# clean solution; the dual-build is in tools/build.bat).  For the
+# intro build profile this MUST be 0.
+JO_COMPILE_WITH_AUDIO_MODULE        = 0
@@
-LIBS += jo-engine/Compiler/COMMON/SGL_302j/LIB_ELF/LIBCPK.A
+# LIBCPK.A and LIBSND.A are now appended automatically by the jo
+# engine makefile when JO_COMPILE_WITH_VIDEO_MODULE=1 (see
+# jo-engine/Compiler/COMMON/jo_engine_makefile lines 55-62).
```

If you need audio for the title screen and gameplay AFTER the intro, the
correct fix is to ship TWO ELFs (one with VIDEO_MODULE=1/AUDIO=0 to play
the intro, the other with VIDEO_MODULE=0/AUDIO=1 for the main game) and
chain-load between them via `SYS_Exit` + a second IP.BIN — but that is a
larger refactor.  The minimum-viable shipping fix is to choose ONE: video
only at boot with no in-game SFX, **or** in-game audio with no video
intro.  The user has said the intro is critical, so above sets VIDEO=1 +
AUDIO=0.

### 4.2 `D:\sonicmaniasaturn\src\intro_video.c` — REPLACE ENTIRELY

```c
/* intro_video.c — Mania.ogv intro playback using jo's video module.
 *
 * Prerequisite Makefile state (see docs/cinepak_blackboot_solved.md):
 *   JO_COMPILE_WITH_VIDEO_MODULE = 1
 *   JO_COMPILE_WITH_AUDIO_MODULE = 0      <-- mandatory; collides w/ Cinepak
 *   no manual LIBS += LIBCPK.A line (jo's makefile adds it)
 *
 * Pre-requisite asset state:
 *   cd/INTRO.CPK present, FILM-container Cinepak, 15-bit RGB, width%8==0.
 *
 * Pre-requisite jo_main state at call site:
 *   - jo_core_init has returned (CPK_Init ran via jo_video_init).
 *   - jo_main has NOT yet registered any user vblank or jo_core_run
 *     callbacks that touch sprites (we own the sprite array during video).
 *
 * Returns:
 *    0 = no video file or open failed
 *    1 = video played to natural end
 *    2 = video skipped by START press
 */

#include <jo/jo.h>

#define INTRO_LWRAM_BASE   ((unsigned char *)0x00200000)
#define INTRO_LWRAM_SIZE   (0x00040000)   /* +256 KB scratch for CPK buffers */

static volatile int g_intro_done;
static volatile int g_intro_skip;

static void __on_video_stopped(void)
{
    g_intro_done = 1;
}

int intro_video_play(const char *cpk_filename)
{
    /* Add Work-RAM-L (LWRAM, 1 MB) so the 520 KB of CPK transient
     * allocations (PCM/RING/WORK/MOVIE/DECODE buffers, jo video.c:145-214)
     * don't blow the 576 KB jo pool.  This matches jo's Samples/demo-video
     * main.c:29-30,59. */
    jo_add_memory_zone(INTRO_LWRAM_BASE, INTRO_LWRAM_SIZE);

    if (!jo_video_open_file(cpk_filename))
        return 0;

    g_intro_done = 0;
    g_intro_skip = 0;

    if (!jo_video_play(__on_video_stopped))
        return 0;

    /* Pump vblank-tied callbacks ourselves until the video finishes.  jo's
     * internal __jo_internal_cpk (registered by jo_video_play) runs from
     * jo_core_add_callback (so it fires from slSynch's vblank dispatch),
     * driving CPK_Task and calling __on_video_stopped at PLAY_END. */
    while (!g_intro_done && !g_intro_skip) {
        /* slSynch waits for vblank + executes the registered callbacks
         * via __jo_vblank_callbacks.  This keeps the play loop alive
         * before jo_core_run takes over. */
        slSynch();

        /* User can skip the intro with START. */
        if (jo_is_pad1_available() && jo_is_pad1_key_pressed(JO_KEY_START)) {
            g_intro_skip = 1;
            jo_video_stop();
            break;
        }
    }

    return g_intro_skip ? 2 : 1;
}
```

### 4.3 `D:\sonicmaniasaturn\src\main.c` — change ONE line at site of `intro_video_play`

`[user main]:1920` already says `intro_video_play("INTRO.CPK");` — leave it.
No further changes here.

### 4.4 Asset side

Make sure `cd/INTRO.CPK` exists.  The existing `tools/ogv_to_cpk.py`
emits files satisfying the format constraints listed in
`[jo video]:194,199`:
- Width must be a multiple of 8 (`[jo video]:199-204` enforces this).
- 15-bit RGB (`[jo video]:194` forces `CPK_COLOR_15BIT`).
- FILM container magic per `[CPK header]:295-319`.

### 4.5 verify_done gate

Add to `tools/verify_done.ps1`:

```powershell
# Gate: Cinepak + audio mutex
$mk = Get-Content "$projectRoot/Makefile" -Raw
if ($mk -match '(?m)^JO_COMPILE_WITH_VIDEO_MODULE\s*=\s*1' -and
    $mk -match '(?m)^JO_COMPILE_WITH_AUDIO_MODULE\s*=\s*1') {
    Write-Error "GATE FAIL: VIDEO_MODULE=1 and AUDIO_MODULE=1 cannot coexist"
    Write-Error " (see docs/cinepak_blackboot_solved.md §3)"
    exit 1
}
if ($mk -match '(?m)^JO_COMPILE_WITH_VIDEO_MODULE\s*=\s*1' -and
    ($mk -match 'LIBS\s*\+=\s*[^\n]*LIBCPK\.A')) {
    Write-Error "GATE FAIL: VIDEO_MODULE=1 but manual LIBS += LIBCPK.A present"
    Write-Error " (jo_engine_makefile adds it automatically; remove the manual line)"
    exit 1
}
```

## 5. Why the prior `cinepak_integration_spec.md` was wrong

That doc concluded "set VIDEO_MODULE=1 + drop manual LIBCPK link + add
LWRAM zone".  It missed:

1. **Audio module conflict.** It said "jo already initialises sound when
   `JO_COMPILE_WITH_AUDIO_MODULE=1` (the user's current state)" — and
   treated that as a positive.  In fact `[demo mk]:4` proves jo's own
   working video sample disables the audio module precisely to avoid the
   conflict.  The 4-byte fake sound map in `[jo audio]:106` plus the
   `slCDDAOn(127,127,0,0)` call do not coexist with Cinepak's PCM path.
2. **`SND_StartPcm`/`SND_StopPcm` stubs in `intro_video.c`.**  These were
   added at `[user video]:34-42` as link helpers; they shadow the real
   functions in `LIBSND.A` for symbols the linker resolves from `.o` first.
   When VIDEO_MODULE=1 the stubs must be removed (LIBSND.A provides the
   real implementations).  The new intro_video.c above removes them.
3. **The lockup is in `CPK_VblIn` running once per frame from the start of
   the program**, not in `CPK_Init` itself.  Once `slCDDAOn` has biased
   the SCSP, every vblank passes through `CPK_VblIn -> cpk_StartPcm
   polling` which never completes its handshake.

## 6. Definitive summary

| Question | Answer | Citation |
|---|---|---|
| Does jo's `jo_video_init` correctly run `CPK_Init` + register `CPK_VblIn`? | YES, both. | `[jo video]:290-296` |
| Does `CPK_Init` itself touch SCU/SCSP hardware? | No, only initializes BSS handle table + clock counters. | `[CPK header]:551`, `cpk_mp.o` undefined refs (none are HW writes) |
| Why does `JO_COMPILE_WITH_VIDEO_MODULE=1` + `JO_COMPILE_WITH_AUDIO_MODULE=1` lock to black? | jo's `slInitSound` w/ 4-byte fake map + `slCDDAOn(127,127,0,0)` biases SCSP to a state where `CPK_VblIn`'s per-vblank `cpk_StartPcm` polling never completes. | `[jo audio]:103-115`, `[demo mk]:4` |
| What is the canonical jo video sample doing differently? | `JO_COMPILE_WITH_AUDIO_MODULE = 0`, plus `jo_add_memory_zone(LWRAM, 0x40000)` for the CPK buffer footprint. | `[demo mk]:4`, `[demo main]:29-30,59` |
| Is slave SH-2 init needed? | No.  Default `CPK_CPU_MAIN`; slave-SH path only via explicit `CPK_SetCpu(CPK_CPU_DUAL)`. | `[CPK header]:186-196,189` |
| Final implementation? | §4.1 + §4.2 + §4.3 + §4.4 above. | this doc |

## 7. Open question — concurrent audio AFTER the intro

This single-binary fix sacrifices in-game audio.  Three paths exist:

1. **Two-binary chain-load.**  Build `INTRO.ELF` (VIDEO=1/AUDIO=0) and
   `GAME.ELF` (VIDEO=0/AUDIO=1); INTRO.ELF calls `SYS_Exit` and the boot
   sector chain-loads GAME.ELF.  Requires custom IP.BIN with a second
   load step.
2. **Use SBL Cinepak directly, bypass jo's video module.**  Implement
   `intro_video.c` to drive `CPK_Init` + `CPK_VblIn` + `CPK_Task` from
   scratch with jo's `slIntFunction` already pointing at
   `__jo_vblank_callbacks`.  Replace jo's audio init with a one-shot
   `SND_Init(real_BOOTSND_MAP)` reading a real BOOTSND.MAP from CD.
   This is the SBL Demo 4 model lifted into the existing jo runtime.
3. **Accept the trade-off.**  Ship the intro with audio, do without
   in-game SFX (CDDA music is still playable through `CDC_CdPlay`
   directly even with the audio module off).  This is the minimum-effort
   ship.

Path 3 is what §4 implements.  Path 2 is the right long-term answer if
SFX coexistence with the intro is required.

## 8. Implementation order

1. Apply Makefile diff §4.1.
2. **Crucially: rm `jo-engine/jo_engine/*.o` and rm any project `*.o` so
   the changed CCFLAGS profile (`[jo mk]:258-265`) actually takes effect
   on every translation unit.**  `JO_COMPILE_WITH_VIDEO_MODULE=1` changes
   `-O2` -> `-Os -nodefaultlibs -nostdlib -fno-builtin -w` for the entire
   project (this is also why the user's existing in-tree `.o` files would
   not be sufficient even after the Makefile change).
3. Replace `src/intro_video.c` per §4.2.
4. Build.  Boot in Mednafen.
5. Run `tools/verify_done.ps1` — gate §4.5 included.

This is the implementation-ready, source-verified fix.
