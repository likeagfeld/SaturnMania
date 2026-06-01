/* Phase 2.1 — engine-layer Green Hill Zone bring-up.
 * Phase 2.3j (2026-05-28) — synchronous scene-load refactor.
 *
 * Port of `src/_archived/main.c.v01-handrolled:200-368, 240-269, 1937`
 * (`setup_ghz_foreground`, `setup_ghz_sky`, `fg_build_page`, `fg_vblank`)
 * into the `src/rsdk/` engine-compat layer. See `scene_ghz.h` for the
 * design rationale and authoritative-doc citations.
 *
 * Phase 2.3j: removed `g_ghz_fg_probe` / `g_ghz_fg_ready` / `g_ghz_sky_ready`
 * volatile async-readiness flags + `ghz_is_active()` helper. Caller now
 * blocks on `ghz_setup_foreground()` + `ghz_setup_sky()` returning. This
 * mirrors the decomp's synchronous scene-load:
 *
 *   rsdkv5-src/RSDKv5/RSDK/Core/RetroEngine.cpp:345-384
 *     ProcessEngine() ENGINESTATE_LOAD: LoadSceneFolder ->
 *       LoadSceneAssets -> InitObjects -> first-frame ProcessObjects.
 *     NO readiness flag. NO deferral. ONE dropped frame budget.
 *
 *   rsdkv5-src/RSDKv5/RSDK/Scene/Scene.cpp:24-282 LoadSceneFolder
 *   rsdkv5-src/RSDKv5/RSDK/Scene/Scene.cpp:283-666 LoadSceneAssets
 *   tools/_decomp_raw/SonicMania_Objects_GHZ_GHZSetup.c GHZSetup_StageLoad
 *
 * Rationale (post Phase 2.3f/g/h/i bug class):
 *   - Cross-TU volatile + GCC 8.2 LTO is unreliable on this codebase;
 *     ready-flag reads got internalized as .lto_priv symbols and
 *     constant-propagated through the gating site. Phase 2.3h added
 *     `-fno-lto` on scene_ghz.c which fixed the LOCAL TU but didn't
 *     help the cross-TU reader in Game.c (which still went through
 *     whole-program LTO).
 *   - Eliminating the async gate eliminates the bug class permanently.
 *   - Saturn-fit deviation: decomp also drops a frame during load; PC
 *     just hides it behind title-card. Saturn drops it visibly inside
 *     TS_TRANSITION_TO_GHZ; acceptable per binding directive.
 *
 * The archived build SHIPPED this exact streaming pattern in a working
 * state — frame-for-frame scrolling GHZ on Saturn hardware. Phase 2.1
 * preserves the call sequence verbatim. */

#include <jo/jo.h>
#include "scene_ghz.h"
#include "storage.h"     /* Phase 2.2c: rsdk_storage_load_to_lwram for FG.TMP */

/* Phase 2.2c — Saturn Work RAM-L (0x00200000-0x002FFFFF, 1 MB) sub-region
 * map for GHZ act 1 resident assets that would otherwise blow jo's 393 KB
 * pool budget. ST-097-R5 §2.1 documents LWRAM as available 1 MB; jo
 * doesn't use it. Phase 2.2b reserved 0x00200000..+0x10000 for GHZ1SURF.
 *
 * Phase 2.3k main (2026-05-28) — buffer size bumped from 0x40000 (256 KB)
 * to 0x50000 (320 KB).
 *
 * ROOT CAUSE of the NBG1 stripe garbage that motivated all of Phase 2.3
 * b/c/d/e/f/g/h/i/j/k bisect work:
 *   - cd/GHZ1FG.TMP is exactly 262148 bytes (header `04 00 00 80` = xs
 *     0x0400 / ys 0x0080 + 1024*128*2 = 262144 + 4 header bytes).
 *   - The old buffer was 0x40000 = 262144 bytes — short by EXACTLY 4
 *     bytes (the file's last sector is 4 used bytes).
 *   - storage.c:673 has a defensive `if (total > max_bytes) return -1`
 *     check; this fired for FG.TMP on every load attempt because
 *     262148 > 262144.
 *   - The helper returned -1, scene_ghz.c::ghz_setup_foreground hit the
 *     `tmp_bytes <= 4` early-return at L255, set
 *     g_ghz_load_error_code |= 0x01, returned false WITHOUT configuring
 *     NBG1 cell mode.
 *   - NBG1 then stayed in stale title-state config (cell-mode bitmap
 *     pointed at title backdrop VRAM), producing the vertical-stripe
 *     garbage pattern users observed.
 *   - GHZ2FG.TMP (245764 bytes) was also subject to this — Act 2 was
 *     equally broken but unobserved because the title→GHZ transition
 *     always loads Act 1 first.
 *
 * The Phase 2.2c "alignment headroom is intentional" comment was wrong:
 * 0x40000 = 262144 < 262148 = actual file size. New 0x50000 (320 KB)
 * provides 58 KB of real headroom (>2 sectors). LWRAM usage map after
 * fix (post-Phase 2.3l):
 *   0x00200000..0x0020FFFF (64 KB)  = GHZ1SURF.BIN (Phase 2.2b)
 *   0x00210000..0x0025FFFF (320 KB) = GHZ?FG.TMP   (Phase 2.2c, 2.3k-fixed)
 *   0x00260000..0x00277FFF (96 KB)  = GHZ?SKY.DAT  (Phase 2.3l, this commit)
 * Total = 480 KB of the 1024 KB LWRAM region. 544 KB still free.
 *
 * Reproducibility:
 *   - Capture: `pwsh tools/qa_savestate.ps1 -Cue game.cue -SaveFrame 35
 *     -Out samples/qa_phase2_3k_main_pre.mcs` (build with
 *     GHZ_AUTOADVANCE_TICKS=480)
 *   - Gate:    `python tools/qa_phase2_3k_main_gate.py
 *     samples/qa_phase2_3k_main_pre.mcs`
 *   - RED-baseline: bit 0 of g_ghz_load_error_code fires; WRAM-L
 *     at 0x00210000 reads all zeros; g_ghz_active_tick_counter == 0;
 *     state machine stuck in TS_TRANSITION_TO_GHZ.
 *
 * Authority: ST-097-R5 §2.1 (LWRAM 1 MB available); DTS-136-R2-093094 §3.1
 * (GFS_GetFileSize/Fread semantics); storage.c:646-690 implements the
 * helper that the size check belongs to. */
#define GHZ_FG_TMP_LWRAM_ADDR  ((unsigned char *)0x00210000)
#define GHZ_FG_TMP_LWRAM_SIZE  0x50000

/* Phase 2.3l (2026-05-28) — SKY.DAT LWRAM bypass.
 *
 * After Phase 2.3k fixed the FG.TMP-size early-return, the SKY.DAT load
 * (jo_fs_read_file path, ~90 KB) became the next bit (5) failure in
 * g_ghz_load_error_code. Root cause: by the time SKY.DAT loads, the jo
 * 393 KB pool already holds FG.PAL (512 B) + FG.CEL (~90 KB) + FG.PAT
 * (~5 KB) + SKY.PAL (512 B), plus any pre-existing residue (player gfx,
 * Phase 2.3 entity SPRs). 90 KB SKY.DAT into that residue exhausts jo's
 * pool — jo_fs_read_file returns NULL, scene_ghz.c:410 trips the
 * <GHZ_SKY_W*GHZ_SKY_H size check, sets bit 5, returns false WITHOUT
 * reaching the NBG2 reconfigure / slScrAutoDisp / SPCTL restore block.
 * Title state machine stays stuck at TS_TRANSITION_TO_GHZ.
 *
 * Fix: mirror the Phase 2.2c FG.TMP pattern exactly. Replace the
 * jo_fs_read_file call with rsdk_storage_load_to_lwram into a pre-
 * reserved LWRAM region. Saturn-fit equivalent of the decomp's
 * engine-owned LoadSceneAssets allocation (RSDKv5/RSDK/Scene/Scene.cpp
 * :283-666 — decomp does its own allocation, never goes through a
 * jo-pool-equivalent).
 *
 * Sizing: cd/GHZ1SKY.DAT = 90112 bytes = exactly GHZ_SKY_W * GHZ_SKY_H
 * (176 * 512). 96 KB region (0x18000) gives 6 KB headroom (3 sectors)
 * for future SKY.DAT layouts or Act 2 variants. Address 0x00260000
 * starts immediately after the 320 KB FG.TMP region ends at 0x0025FFFF.
 *
 * Authority:
 *   - Pattern template: this file lines 286-304 (FG.TMP LWRAM bypass)
 *   - ST-097-R5-072694.pdf §2.1: LWRAM 1 MB available at 0x00200000
 *   - ST-136-R2-093094.pdf §3.1: GFS_NameToId/Open/GetFileSize/Fread
 *   - storage.c:646-690: rsdk_storage_load_to_lwram implementation
 *   - rsdkv5-src/RSDKv5/RSDK/Scene/Scene.cpp:283-666 LoadSceneAssets
 *     (decomp engine-side allocation pattern this mirrors) */
#define GHZ_SKY_DAT_LWRAM_ADDR ((unsigned char *)0x00260000)
#define GHZ_SKY_DAT_LWRAM_SIZE 0x18000

/* jo's NBG1 char base (VRAM A0) + page (VRAM B0). These are owned by
 * jo's VDP2 init; we read them after jo_vdp2_set_nbg1_8bits_image
 * completes to derive the cell-bank offset for pattern-name encoding. */
extern unsigned char  *nbg1_cell;
extern unsigned short *nbg1_map;

/* === FG streaming state (per archived main.c:177-215) =============== */

static unsigned short  g_ghz_page[GHZ_FG_PAGE_LEN];   /* Work-RAM page */
static unsigned short  g_ghz_num_cells;
static unsigned short *g_ghz_fg_pat;                  /* tile id → 4 cell idx */
static unsigned short *g_ghz_fg_tmap;                 /* row*xs + col → tile id */
static int             g_ghz_fg_xs, g_ghz_fg_ys;      /* level size in 16px tiles */
static jo_palette      g_ghz_fg_pal;
static unsigned short  g_ghz_fg_paloff;               /* palette_id << 12 */
static unsigned short  g_ghz_fg_mapoff;               /* char base = cell_off/32 */

static int             g_ghz_cam_x, g_ghz_cam_y;

/* Phase 2.3j (2026-05-28) — synchronous-load tick counter.
 *
 * Replaces the entire Phase 2.3f/g/h/i probe + canary apparatus
 * (g_ghz_fg_probe / g_ghz_fg_ready / g_ghz_sky_ready / ghz_is_active).
 *
 * The new architecture has no readiness flags: `ghz_setup_foreground()`
 * + `ghz_setup_sky()` are called synchronously from the title->GHZ
 * transition state and only return after VDP2 NBG1/NBG2 are committed.
 * Game.c's TS_GHZ_ACTIVE state therefore knows GHZ is ready by virtue
 * of having advanced past TS_TRANSITION_TO_GHZ — no flag needed.
 *
 * This single counter is incremented once per `mania_ghz_tick_and_draw`
 * call. It exists ONLY as a quantitative gate-evidence symbol for the
 * Phase 2.3j register-baseline check (tools/qa_phase2_3j_sync_load_gate.py
 * P3 predicate). Cost: 4 BSS bytes + 1 SH-2 store per frame.
 *
 * `volatile` + `__attribute__((used))` because we want the counter to
 * survive LTO and remain peek-able from a Mednafen savestate. Cost vs
 * value: this is acceptable because the counter is NOT in the gating
 * path — it's pure measurement, no behavior depends on its value. */
__attribute__((used)) volatile unsigned int g_ghz_active_tick_counter = 0;

/* Phase 2.3k-mid (2026-05-28) — sub-asset load-failure bitmask.
 *
 * Each bit records WHICH early-return path inside ghz_setup_foreground
 * or ghz_setup_sky was taken. Read via savestate at any BSS-resident
 * address (game.map: g_ghz_load_error_code). Stays 0 only on a fully
 * successful load.
 *
 * Bit assignments — DO NOT REORDER (gate predicates depend on these):
 *   bit 0 = FG.TMP rsdk_storage_load_to_lwram returned <=4
 *   bit 1 = FG.PAL jo_fs_read_file NULL or len<512
 *   bit 2 = FG.CEL jo_fs_read_file NULL or len<64
 *   bit 3 = FG.PAT jo_fs_read_file NULL or len<2
 *   bit 4 = SKY.PAL jo_fs_read_file NULL or len<512
 *   bit 5 = SKY.DAT jo_fs_read_file NULL or len < GHZ_SKY_W*GHZ_SKY_H
 *
 * Why: pre-2.3k-mid, GHZSetup_StageLoad swallowed the bool return via
 * `(void)`, so a FG.TMP load failure left g_ghz_fg_xs=0 + WRAM-L
 * empty + NBG1 in stale title-state config (cell-mode bitmap), but
 * the state machine still advanced to TS_GHZ_ACTIVE — producing
 * indistinguishable "stripe garbage" output for four distinct root
 * causes. Phase 2.3k iter-2's savestate proved this. The bitmask
 * makes the failure observable + diagnosable in one peek. */
__attribute__((used)) volatile unsigned int g_ghz_load_error_code = 0;

/* Phase 2.3b — VDP2 SPCTL (Sprite Control) snapshot at 0x25F800E0.
 * Per ST-058-R2 §SPCTL: bits[3:0]=SPTYPE, bit[5]=SPCLMD, bit[4]=SPWINEN,
 * bits[13:12]=SPCCCS. jo's NON-SGL default is 0x23 (SPCLMD=1+SPTYPE=3);
 * SGL programs an equivalent default during slInitSystem. Capture
 * pre/post jo_vdp2_set_nbg1_8bits_image to bisect H-A (SPCTL clobber
 * by NBG1 cell-mode setup) per docs/COMPREHENSIVE_PLAN.md §12.3b.
 * Address per jo-engine/jo_engine/jo/sega_saturn.h:357. */
#define GHZ_DIAG_VDP2_SPCTL_ADDR ((volatile unsigned short *)0x25F800E0)
static volatile unsigned short g_diag_spctl_pre_nbg1  = 0;
static volatile unsigned short g_diag_spctl_post_nbg1 = 0;
static volatile unsigned short g_diag_spctl_post_sky  = 0;

unsigned short ghz_diag_spctl_pre_nbg1(void)  { return g_diag_spctl_pre_nbg1; }
unsigned short ghz_diag_spctl_post_nbg1(void) { return g_diag_spctl_post_nbg1; }
unsigned short ghz_diag_spctl_post_sky(void)  { return g_diag_spctl_post_sky; }

/* Sky state (per archived main.c:325-368). */
#define GHZ_SKY_W              176
#define GHZ_SKY_H              512
static jo_palette      g_ghz_sky_pal;

/* === Helpers ========================================================= */

/* Build one 16x16 level tile (4 cells) into the Work-RAM page buffer.
 * Mirrors archived main.c:217-236 verbatim. The page wraps every 32
 * tiles; tile (L,R) lands at page cell ((L*2)&63,(R*2)&63), which the
 * VDP2 scroll position re-aligns to the world pixel. */
static void put_tile(int L, int R)
{
    unsigned short        pid = 0;
    const unsigned short *q;
    int                   cc, cr, b;

    if ((unsigned)L < (unsigned)g_ghz_fg_xs && (unsigned)R < (unsigned)g_ghz_fg_ys)
        pid = g_ghz_fg_tmap[R * g_ghz_fg_xs + L];
    q  = &g_ghz_fg_pat[pid << 2];
    cc = (L << 1) & (GHZ_FG_PAGE_CELLS - 1);
    cr = (R << 1) & (GHZ_FG_PAGE_CELLS - 1);
    b  = cr * GHZ_FG_PAGE_CELLS + cc;
    g_ghz_page[b]                        = (unsigned short)(((q[0] << 1) | g_ghz_fg_paloff) + g_ghz_fg_mapoff);
    g_ghz_page[b + 1]                    = (unsigned short)(((q[1] << 1) | g_ghz_fg_paloff) + g_ghz_fg_mapoff);
    g_ghz_page[b + GHZ_FG_PAGE_CELLS]    = (unsigned short)(((q[2] << 1) | g_ghz_fg_paloff) + g_ghz_fg_mapoff);
    g_ghz_page[b + GHZ_FG_PAGE_CELLS + 1]= (unsigned short)(((q[3] << 1) | g_ghz_fg_paloff) + g_ghz_fg_mapoff);
}

/* === Public API ====================================================== */

/* Phase 2.3j: removed `g_ghz_fg_ready` early-return guard. Synchronous
 * load means this function only gets called after ghz_setup_foreground
 * has returned (i.e. g_ghz_fg_pat / g_ghz_fg_tmap are populated). The
 * pre-load guard against a NULL fg_pat / fg_tmap is now provided by the
 * caller (mania_tick gates the entire GHZ pass on TS_GHZ_ACTIVE state). */
void ghz_fg_build_page(void)
{
    int L, R;
    int tx = g_ghz_cam_x >> 4;
    int ty = g_ghz_cam_y >> 4;

    if (!g_ghz_fg_tmap || !g_ghz_fg_pat) return;

    for (R = ty; R < ty + GHZ_FG_WIN_TILES; ++R)
        for (L = tx; L < tx + GHZ_FG_WIN_TILES; ++L)
            put_tile(L, R);
}

/* SCU DMA push of the Work-RAM page to NBG1 page-name VRAM. Per
 * memory/sgl-audio-vs-scroll-cpu-dma-conflict.md (SGL 2.0A release notes
 * §1.2.1): "CPU DMA transfer channel used for a scroll screen data
 * transfer conflicts with this sound data CPU DMA transfer when a
 * V-blank interrupt occurs." SCU DMA (slDMAXCopy) sidesteps the
 * conflict. The cache-through alias (0x20000000 OR) is required because
 * slDMAXCopy is NOT cache-aware. */
void ghz_fg_vblank(void)
{
    /* Phase 2.3j: removed `g_ghz_fg_ready` guard — synchronous load means
     * nbg1_map is populated by the time we get here. Caller (mania_tick)
     * gates the entire GHZ pass on TS_GHZ_ACTIVE. */
    if (!nbg1_map) return;

    slDMAXCopy((void *)((unsigned int)g_ghz_page | 0x20000000),
               (void *)nbg1_map,
               GHZ_FG_PAGE_LEN * sizeof(unsigned short),
               Sinc_Dinc_Long);
    slScrPosNbg1(JO_MULT_BY_65536(g_ghz_cam_x),
                 JO_MULT_BY_65536(g_ghz_cam_y));
}

/* Compose `cd/GHZ<act>FG.<suffix>` into a static buffer. The cd: prefix
 * is implicit in jo_fs_read_file (jo's filesystem is rooted at the disc
 * data partition). */
static const char *ghz_path(int act, const char *suffix)
{
    static char buf[16];
    int i = 0;
    buf[i++] = 'G';
    buf[i++] = 'H';
    buf[i++] = 'Z';
    /* Act number (Phase 2.1: 1 or 2). */
    buf[i++] = (char)('0' + (act & 7));
    while (*suffix && i < (int)sizeof(buf) - 1) buf[i++] = *suffix++;
    buf[i] = 0;
    return buf;
}

bool ghz_setup_foreground(int act)
{
    static jo_img_8bits dummy;
    int                 len_pal = 0, len_cel = 0, len_pat = 0, len_tmp = 0;
    unsigned char      *cel = NULL;
    unsigned short     *fgpal = NULL, *tmp = NULL, *pat_raw = NULL;

    /* Phase 2.3j: removed entry-canary probe (g_ghz_fg_probe.entry = 0xA5).
     * The synchronous-load architecture means a savestate captured after
     * TS_GHZ_ACTIVE entry is by definition proof this function ran. */

    /* Phase 2.1 resilience: load ALL assets first, validate, and only
     * THEN call jo_vdp2_set_nbg1_8bits_image. If any load fails we
     * jo_free what we got and return false WITHOUT enabling NBG1 —
     * preventing the cell-mode-garbage symptom (qa_phase2_1_seq_90+
     * on the first build, when FG.TMP allocation failed due to pool
     * pressure).
     *
     * Asset load order picked to surface the failure mode early: TMP
     * is the largest (262 KB), so allocate it first while the pool
     * still has its full free margin. If it fits, everything else
     * (~95 KB combined) will too. */
    /* Phase 2.2c: load FG.TMP to LWRAM (bypass jo's pool entirely).
     * 262 KB FG.TMP residing in jo's 393 KB pool was the dominant
     * cause of the post-2.2 NBG1 vertical-stripe scramble — recovering
     * this 262 KB drops total jo pool draw from ~477 KB to ~215 KB,
     * well within budget. The downstream pointer-arithmetic (tmp[0],
     * tmp[1], tmp+2) is base-pointer-agnostic so swapping the buffer
     * base from jo-heap to LWRAM 0x00210000 is transparent. */
    {
        int tmp_bytes = rsdk_storage_load_to_lwram((char *)ghz_path(act, "FG.TMP"),
                                                    (void *)GHZ_FG_TMP_LWRAM_ADDR,
                                                    GHZ_FG_TMP_LWRAM_SIZE);
        if (tmp_bytes <= 4) {
            g_ghz_load_error_code |= 0x01;   /* FG.TMP failed */
            return false;
        }
        len_tmp = tmp_bytes;
        (void)len_tmp;   /* unused post-LWRAM-move; kept for symmetry */
        tmp     = (unsigned short *)GHZ_FG_TMP_LWRAM_ADDR;
    }
    fgpal = (unsigned short *)jo_fs_read_file((char *)ghz_path(act, "FG.PAL"), &len_pal);
    if (!fgpal || len_pal < 2 * 256) {
        g_ghz_load_error_code |= 0x02;   /* FG.PAL failed */
        /* tmp is LWRAM-resident (Phase 2.2c) — no jo_free. */
        if (fgpal) jo_free(fgpal);
        return false;
    }
    cel = (unsigned char *)jo_fs_read_file((char *)ghz_path(act, "FG.CEL"), &len_cel);
    if (!cel || len_cel < 64) {
        g_ghz_load_error_code |= 0x04;   /* FG.CEL failed */
        jo_free(fgpal);
        if (cel) jo_free(cel);
        return false;
    }
    pat_raw = (unsigned short *)jo_fs_read_file((char *)ghz_path(act, "FG.PAT"), &len_pat);
    if (!pat_raw || len_pat < 2) {
        g_ghz_load_error_code |= 0x08;   /* FG.PAT failed */
        jo_free(fgpal); jo_free(cel);
        if (pat_raw) jo_free(pat_raw);
        return false;
    }

    /* All assets loaded successfully — commit to NBG1 activation. */
    g_ghz_num_cells = (unsigned short)(len_cel / 64);
    jo_create_palette_from(&g_ghz_fg_pal, fgpal, 256);
    g_ghz_fg_paloff = (unsigned short)(g_ghz_fg_pal.id << 12);
    g_ghz_fg_pat    = pat_raw + 1;   /* skip [u16 nPat] header */
    g_ghz_fg_xs     = tmp[0];        /* SH-2 reads big-endian u16 natively */
    g_ghz_fg_ys     = tmp[1];
    g_ghz_fg_tmap   = tmp + 2;

    /* jo sets up NBG1 cell mode + NBG1ON and allocates cell(A0)/page(B0). */
    dummy.data   = cel;
    dummy.width  = 8;
    dummy.height = g_ghz_num_cells * 8;

    /* Phase 2.3b diagnostic: snapshot SPCTL immediately before + after
     * the suspect call to bisect H-A (NBG1 cell-mode setup clobbers
     * SPCTL). Encoded into back-color from main.c::fg_vblank. */
    g_diag_spctl_pre_nbg1 = *GHZ_DIAG_VDP2_SPCTL_ADDR;
    jo_vdp2_set_nbg1_8bits_image(&dummy, g_ghz_fg_pal.id, false);
    g_diag_spctl_post_nbg1 = *GHZ_DIAG_VDP2_SPCTL_ADDR;

    /* char base = byte-offset of cell bank within 0x20000 VRAM bank / 32 */
    g_ghz_fg_mapoff = (unsigned short)(((unsigned int)nbg1_cell & 0x1FFFF) >> 5);

    /* Build a valid initial page BEFORE the upload — without this the
     * first DMA pushes stale BSS-zeroed g_ghz_page bytes which display
     * as a vertical-strip pattern (archived main.c didn't hit this
     * because its fg_vblank ran first and rebuilt before any frame
     * displayed; Phase 2.1's transition-time setup runs once + then the
     * mania_tick loop catches up, leaving 1-2 frames where the page is
     * still zero-bytes). */
    g_ghz_cam_x    = 0;
    g_ghz_cam_y    = GHZ_CAM_Y_PX_DEFAULT;

    /* Phase 2.3j: removed `g_ghz_fg_probe.{pre,ready,mirror,post}` stamps.
     * Synchronous-load architecture: the caller (TS_TRANSITION_TO_GHZ
     * handler in Game.c) waits on this function's return, then advances
     * the state machine to TS_GHZ_ACTIVE. No readiness flag is needed
     * because the call/return contract IS the readiness signal. */

    ghz_fg_build_page();

    /* Upload the resident, row-major cell bank to VRAM via DMA. We
     * KEEP the Work-RAM source allocated — jo_vdp2_set_nbg1_8bits_image
     * may retain internal pointers into it, and freeing would corrupt
     * jo's state (archived main.c:314-319 documents this; Phase 2.2
     * attempt to free verified empirically that the build crashes
     * after free, despite jo's vdp2.c source suggesting the copy is
     * one-shot — there's a downstream consumer not visible in the
     * static read of jo). */
    slDMACopy(cel, (void *)nbg1_cell, g_ghz_num_cells * 64);

    /* Also push the just-built page so the first visible frame already
     * shows valid tile data (rather than the BSS-zero stripe pattern). */
    slDMAXCopy((void *)((unsigned int)g_ghz_page | 0x20000000),
               (void *)nbg1_map,
               GHZ_FG_PAGE_LEN * sizeof(unsigned short),
               Sinc_Dinc_Long);
    slScrPosNbg1(JO_MULT_BY_65536(g_ghz_cam_x),
                 JO_MULT_BY_65536(g_ghz_cam_y));

    /* Phase 2.3j: removed exit canary probe stamp. The caller blocks
     * on the `return true` so a captured-post-load savestate IS proof
     * the function ran to completion. */
    return true;
}

bool ghz_setup_sky(int act)
{
    static jo_img_8bits img;
    int                 len_pal = 0, len_dat = 0;
    unsigned short     *spal = NULL;
    unsigned char      *sdat = NULL;

    /* Load both assets up front so we can abort cleanly on partial
     * failure (don't leave NBG2 in a half-swapped state). */
    spal = (unsigned short *)jo_fs_read_file((char *)ghz_path(act, "SKY.PAL"), &len_pal);
    if (!spal || len_pal < 2 * 256) {
        g_ghz_load_error_code |= 0x10;   /* SKY.PAL failed */
        if (spal) jo_free(spal);
        return false;
    }
    /* Phase 2.3l: load SKY.DAT to LWRAM (bypass jo's pool entirely),
     * mirroring the Phase 2.2c FG.TMP pattern. ~90 KB SKY.DAT into a
     * pool already holding ~100 KB of FG.* residue + SKY.PAL + Phase 2.3
     * entity SPRs was the dominant cause of the post-Phase 2.3k bit-5
     * (SKY.DAT) failure in g_ghz_load_error_code. LWRAM-resident at
     * 0x00260000 has no pool dependency. The downstream
     * jo_vdp2_set_nbg2_8bits_image path consumes sdat through img.data
     * verbatim, base-pointer-agnostic so swapping jo-heap -> LWRAM
     * 0x00260000 is transparent. See the GHZ_SKY_DAT_LWRAM_* block at
     * the top of the file for the citation chain. */
    {
        int dat_bytes = rsdk_storage_load_to_lwram((char *)ghz_path(act, "SKY.DAT"),
                                                    (void *)GHZ_SKY_DAT_LWRAM_ADDR,
                                                    GHZ_SKY_DAT_LWRAM_SIZE);
        if (dat_bytes < GHZ_SKY_W * GHZ_SKY_H) {
            g_ghz_load_error_code |= 0x20;   /* SKY.DAT failed */
            jo_free(spal);
            return false;
        }
        len_dat = dat_bytes;
        (void)len_dat;   /* unused post-LWRAM-move; kept for symmetry */
        sdat    = (unsigned char *)GHZ_SKY_DAT_LWRAM_ADDR;
    }

    jo_create_palette_from(&g_ghz_sky_pal, spal, 256);
    img.data   = sdat;
    img.width  = GHZ_SKY_W;
    img.height = GHZ_SKY_H;

    /* Reset NBG2 scroll BEFORE the cell-mode swap so the just-loaded
     * sky lines up at col 0. (Archived main.c:341 + 417.) */
    slScrPosNbg2(0, 0);

    /* Drop NBG2ON cleanly. Per VDP2 manual §3.3 + the archived build's
     * gotcha note (main.c:343-357): VDP2 cannot retarget the NBG2 plane
     * safely while the layer is enabled. The slScrAutoDisp call is
     * REPLACE (not OR/AND), and the screen-flag bitmask must contain
     * ONLY layers that are actually initialized. Use NBG1ON only
     * during the swap window.
     *
     * Phase 2.3 BUG FIX: original Phase 2.1 calls below dropped SPRON
     * from the bitmask, suppressing ALL VDP1 sprite output once GHZ
     * came up (verified empirically: title sprites render fine; GHZ
     * NBG1+NBG2 render fine; but Sonic + entities + HUD all invisible
     * post-transition). slScrAutoDisp is REPLACE semantics — every call
     * must include SPRON or the sprite layer goes dark. Per ST-058-R2
     * §3.3 + SL_DEF.H:548 (SPRON = bit 6). */
    slScrAutoDisp(NBG1ON | SPRON);

    jo_vdp2_set_nbg2_8bits_image(&img, g_ghz_sky_pal.id, false, false);

    /* Re-issue plane config (BIPLANE sample's canonical sequence at
     * NOV96_DTS/EXAMPLES/SGL/BIPLANE/MAIN.C:191-206) so VDP2 latches the
     * new layout. */
    slPlaneNbg2(PL_SIZE_1x1);
    slCharNbg2(COL_TYPE_256, CHAR_SIZE_1x1);

    /* NBG1 ground at 6 (archived value); NBG2 sky at 1 (back).
     * Phase 2.3: explicitly raise sprite priority bank 0 to 7 so
     * VDP1 sprites composite above NBG1 (priority 6) unambiguously.
     * jo's default PRISA = 0x506 sets bank 0 sprite priority = 6 which
     * ties with NBG1, and tie-break behavior on Saturn proved unreliable
     * across emulator + hardware (verified Phase 2.3: sprites invisible
     * post-GHZ transition with default priority). Per ST-058-R2 §3.4
     * Priority register: sprite priority > NBG priority guarantees
     * sprite layer wins. */
    slPriorityNbg1(6);
    slPriorityNbg2(1);
    slPrioritySpr0(7);

    /* Re-enable both layers atomically — INCLUDING SPRON so VDP1
     * sprites continue to render (Phase 2.3 bug fix; see comment above). */
    slScrAutoDisp(NBG1ON | NBG2ON | SPRON);

    /* Phase 2.3b diagnostic: snapshot SPCTL at the end of GHZ bring-up.
     * If this differs from g_diag_spctl_pre_nbg1, H-A is confirmed and
     * we know the writer is between pre_nbg1 and here. */
    g_diag_spctl_post_sky = *GHZ_DIAG_VDP2_SPCTL_ADDR;

    /* Phase 2.3b candidate fix (H-A): restore VDP2 SPCTL to jo's documented
     * default (0x23 = SPCLMD=1 [palette+RGB mix] | SPTYPE=3 [8-bit shadow
     * + priority + color-calc]). Per ST-058-R2 SPCTL register: SPCLMD bit 5
     * MUST be 1 for MSB=1 RGB-direct sprite pixels to composite onto VDP2;
     * SPTYPE in [3:0] selects the sprite framebuffer format. jo's NON-SGL
     * branch writes this exact value at core.c:276 but in the SGL branch
     * it's left to SGL's slInitSystem default. If any of the preceding NBG
     * cell-mode setup calls (jo_vdp2_set_nbg1_8bits_image,
     * jo_vdp2_set_nbg2_8bits_image, slPlaneNbg*, slCharNbg*) clobbered
     * SPCTL, this restore brings sprites back. Cite:
     *  - jo-engine/jo_engine/jo/sega_saturn.h:357 (SPCTL = 0x25F800E0, default 0x23)
     *  - jo-engine/jo_engine/core.c:276 (the dead-in-SGL-mode writer)
     *  - ST-058-R2 SPCTL register field definitions
     *  - docs/COMPREHENSIVE_PLAN.md Sec 12.3b H-A
     *
     * No-op if SPCTL was preserved; sprite-restoring if it was clobbered. */
    *GHZ_DIAG_VDP2_SPCTL_ADDR = 0x23;

    /* Phase 2.3j: removed `g_ghz_sky_ready = true` flag store. The caller
     * blocks on this function's return; the call-return contract IS the
     * readiness signal. ghz_is_active() helper is also removed; callers
     * use `s_ts_state == TS_GHZ_ACTIVE` directly in Game.c / main.c. */
    return true;
}

int  ghz_camera_x(void) { return g_ghz_cam_x; }
int  ghz_camera_y(void) { return g_ghz_cam_y; }

/* Phase 2.2 — world size accessors. Computed from g_ghz_fg_xs/ys (tile
 * counts) set by ghz_setup_foreground. Returns 0 pre-load. */
int ghz_world_width_px(void)  { return g_ghz_fg_xs * 16; }
int ghz_world_height_px(void) { return g_ghz_fg_ys * 16; }

/* Phase 2.4g.2 (Task #153) — Zone camera-bounds globals, owned by
 * src/mania/Objects/Global/Zone.c. Declared extern here (rather than via
 * Zone.h, which pulls Game.h into the engine layer) so the camera clamp
 * reads the same memory BoundsMarker writes. volatile + the cross-TU
 * read/write contract per memory/sync-load-eliminates-cross-tu-volatile.md;
 * the *value* round-trips through these globals every frame. Index 0 is
 * the single Saturn player. The 4-entry layout mirrors the decomp
 * (Zone.h:65-73 cameraBounds[PLAYER_COUNT]); we only touch [0]. */
extern volatile int g_zone_cameraBoundsL[];
extern volatile int g_zone_cameraBoundsR[];
extern volatile int g_zone_cameraBoundsT[];
extern volatile int g_zone_cameraBoundsB[];
extern volatile int g_zone_bounds_valid;

/* Phase 2.2 — Player-driven camera. Clamp + commit. The next
 * ghz_fg_build_page() will build the page for the new (cam_x, cam_y),
 * the next ghz_fg_vblank() will push it + update scroll.
 *
 * Phase 2.4g.2 — clamp source is now the Zone camera-bounds globals
 * (decomp Zone->cameraBounds*, written by zone_init_default_bounds +
 * BoundsMarker_ApplyBounds). When g_zone_bounds_valid is set, the
 * viewport right/bottom limits come from cameraBoundsR/B minus the
 * 320x224 screen; otherwise we fall back to the legacy world-size clamp
 * (defense-in-depth so a pre-load / non-GHZ frame never clamps to a
 * zeroed bound). This is the GHZ-side mirror of the decomp camera-bound
 * clamp the BoundsMarker writes feed (BoundsMarker.c:88-116). */
void ghz_set_camera(int cam_x_px, int cam_y_px)
{
    int bound_l, bound_r, bound_t, bound_b;

    if (g_zone_bounds_valid) {
        bound_l = g_zone_cameraBoundsL[0];
        bound_r = g_zone_cameraBoundsR[0];
        bound_t = g_zone_cameraBoundsT[0];
        bound_b = g_zone_cameraBoundsB[0];
    } else {
        bound_l = 0;
        bound_r = g_ghz_fg_xs * 16;
        bound_t = 0;
        bound_b = g_ghz_fg_ys * 16;
    }

    int max_x = bound_r - 320;
    int max_y = bound_b - 224;
    if (max_x < bound_l) max_x = bound_l;
    if (max_y < bound_t) max_y = bound_t;
    if (cam_x_px < bound_l) cam_x_px = bound_l;
    if (cam_y_px < bound_t) cam_y_px = bound_t;
    if (cam_x_px > max_x)   cam_x_px = max_x;
    if (cam_y_px > max_y)   cam_y_px = max_y;
    g_ghz_cam_x = cam_x_px;
    g_ghz_cam_y = cam_y_px;
}

void ghz_sky_scroll(int x_world, int y_world)
{
    /* 1/4 horizontal parallax — sky scrolls at quarter the camera speed
     * (classic Sonic GHZ). Phase 2.2 ships the static-offset variant;
     * Phase 2.3 will add a true VDP2 line-scroll table for vertical
     * depth-of-field bending. The slScrPosNbg2 path is sufficient for
     * the gameplay visible-Sonic milestone.
     *
     * Phase 2.3j: removed `g_ghz_sky_ready` guard — synchronous-load
     * contract means callers gate on TS_GHZ_ACTIVE in Game.c. */
    slScrPosNbg2(JO_MULT_BY_65536(x_world >> 2),
                 JO_MULT_BY_65536(y_world >> 2));
}
