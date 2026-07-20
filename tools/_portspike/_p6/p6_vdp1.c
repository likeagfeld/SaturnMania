/* ============================================================================
 * p6_vdp1.c -- P6.5b2/b3 (Task #208): VDP1 half of the engine render backend.
 *
 * jo-side TU (the engine half lives in p6_io_main.cpp's P6_SCENE_TEST block;
 * the two TUs cannot share headers -- jo.h's SGL C decls clash with the
 * engine's C++ namespace, same split as p6_gfs.c). Compiled INSIDE the jo
 * make (Makefile P6SCENE SRCS +=) so jo.h struct layouts match the project's
 * jo configuration flags exactly.
 *
 * P6.5b3 shape: the Saturn RSDK::DrawSprite backend (p6_io_main.cpp, a
 * mechanical mirror of engine Drawing.cpp:2670-2686 + the FX_NONE arm :2785)
 * calls p6_vdp1_blit(topleftX, topleftY, w, h, sprX, sprY) per draw. Each
 * DISTINCT sheet rect is uploaded to VDP1 exactly once through jo's PROVEN
 * 8bpp path (jo_sprite_add_8bits_image -> __internal_jo_sprite_add(data, w,
 * h, COL_256), sprites.c:237-247) and cached by (sx,sy,w,h); subsequent
 * blits of the same rect only emit the draw command. A per-tick
 * jo_sprite_add would grow __jo_sprite_id without bound -- the #189
 * sprite-table overflow class (jo has NO release-build bounds check).
 * p6_w_vdp1_slots witnesses the cache population (qa_p6_draw.py D5 expects
 * EXACTLY the 16 distinct Ring anim-0 rects after the animator has cycled).
 *
 * Palette: COL_256 sprites read CRAM through jo's palette index. The engine's
 * stage palette already sits in CRAM bank 0 (NBG1's bank, written by
 * p6_vdp2.c); sprites get their OWN copy in bank 1 so jo's palette indexing
 * cannot disturb the proven NBG1 bank. The pixel gates arbitrate color
 * correctness end-to-end.
 *
 * Width rule: VDP1/jo sprite widths MUST be a multiple of 8 (memory rule
 * jo-sgl-sprite-width-mult8-shear; sprites.c:212 truncates char size to
 * width & 0x1F8). The staging copy pads every rect width up to mult-8 with
 * transparent right-columns; content stays left-aligned, and the draw centers
 * on the PADDED width so the content occupies [x, x+w) exactly.
 * ========================================================================== */
#include <jo/jo.h>

/* CP4c BLUE-SCREEN FIX (this session): the VDP1 slot cache stages every sprite
 * into a FIXED box (content top-left, transparent pad). The GHZ gameplay set
 * (Ring/Player/badniks) fits 64x64, but the FRONT-END Logos splash frames are
 * large UI images -- MEASURED from Logos/Logos.bin: Sega 187x58, CW 150x85,
 * HC 92x89, PWG 147x71 (every one > 64 in W and/or H). With a 64x64 box, every
 * Logos blit hit `w > P6_SPR_MAXW || h > P6_SPR_MAXH` in p6_slot_for and DROPPED
 * (MEASURED p6_w_vdp1_drops == draw_calls, vdp1_landed == 0 -> uniform-blue
 * screen). A single VDP1 sprite supports up to 504(W,mult-8) x 255(H) per
 * ST-013-R3 sec 6.6 (CMDSIZE), so the 187x89 max fits ONE sprite each. The
 * FRONT-END flavor therefore uses a 192x96 box (covers the largest frame,
 * width mult-8 per jo-sgl-sprite-width-mult8-shear) with only 8 slots (the
 * Logos working set is the 4 logo frames + a margin; far below 8 -> the LRU
 * eviction path never fires). 8 * 192*96 = 147,456 px < JO_VDP1_USER_AREA_SIZE
 * (0x71D38 = 466,232 B). The DEFAULT (GHZ) build is BYTE-IDENTICAL (64x64x40);
 * only -DP6_FRONTEND_LOGOS enlarges the box. The two staging buffers are also
 * relocated off .bss to a verified-free cart window in the FRONT-END build
 * (192*96*2 = 36 KB would breach the ~1.2 KB WRAM-H headroom under ANIMPAK);
 * the GHZ build keeps them as the original 64x64 .bss arrays. */
#if defined(P6_FRONTEND_TITLE)
/* CP5b.1 (Task #268): the TITLE logo frames are taller than the Logos splash --
 * the EMBLEM (anim 0) is 144x144, Ribbon Center 176x52 -> needs a 192x160 box.
 * CP5b.2 (Task #269): TitleSonic (the ring-center head, Title/Sonic.bin) is WIDER
 * still -- MEASURED (convert_ring_sprite.parse_spr): anim 0 "Sonic" (the head/body
 * pop-up, 49 frames) sweeps up to 241x137 (f30 = 241 wide; f15/f17 = 137 tall;
 * the leaning mid-poses are wide), anim 1 "Finger Wave" (12 frames) up to 50x63.
 * OVERALL MAX frame (logo emblem + sonic head) = 241x137. The CP5b.1 192x160 box
 * would DROP the 241-wide head (p6_slot_for: `w > P6_SPR_MAXW` -> oversize drop ->
 * the head never blits -> black ring interior, the CP5b.1 logo class). So the TITLE
 * flavor grows to a 248x160 box (241->248 mult-8 width per jo-sgl-sprite-width-mult8-
 * shear; 137 fits the 160 that already covered the 144 emblem). A single VDP1 sprite
 * supports 504(W,mult-8)x255(H) per ST-013-R3 sec 6.6 (CMDSIZE), so 241x137 fits ONE
 * sprite each. 10 * 248*160 = 396,800 px < JO_VDP1_USER_AREA_SIZE (0x71D38 = 466,232
 * B). NSLOTS 8->10: the Title working set is now ~6 logo pieces + the electricity ring
 * + the Sonic head + finger (~9 distinct rects); 10 keeps them resident with no
 * eviction (the LRU path still handles any overflow gracefully per
 * ghz-vdp1-sprite-residency-lru). The DEFAULT (GHZ) build is BYTE-IDENTICAL (64x64x40);
 * a Logos-only build keeps 192x96. */
#define P6_SPR_MAXW     248 /* Title: widest frame 241 (sonic head) -> mult-8 pad 248 */
#define P6_SPR_MAXH     160 /* Title: tallest frame 144 (emblem) / 137 (head) -> 160 */
/* Phase 2 (Task #279): with the content-size N-bucket below, s_slots[] is now ONLY
 * bucket 3 -- the 248x160 catch-all for w>160 frames, which is JUST the Sonic body
 * (241x137; every other title sprite + the <=160-wide ring frames route to the
 * smaller buckets). 4 slots cover the body's play-once twirl (LRU). Shrinking 10->4
 * frees 6*sizeof(P6Vdp1Slot)=168 B of .bss so the N-bucket arrays (s_buck0/1/2) are a
 * NET-NEGATIVE WRAM-H change -- the +120 B naive version tripped the #228 boot trap
 * (master PC 0x06000956), the title flavor's ceiling being far tighter than GHZ's. */
#if defined(P6_FRONTEND_MENU)
/* M1b: the MENU scene NEVER reaches bucket b3 (its widest sprite is the 176px Sound
 * label -> routes to b1; the catch-all b3 is only hit for w>192 or h in 65..160, and
 * every menu frame is <=176 wide / <=44 tall). So shrink b3 to 1 slot in the menu --
 * its 39,680 B/slot box dominates the VDP1 user area, and 4 unused b3 slots + the
 * 14/18 b0/b1 counts below would overflow JO_VDP1_USER_AREA_SIZE (466,232). With b3=1:
 * VRAM = 14*5,120 + 18*12,288 + 1*25,600 + 1*39,680 = 358,144 B (108,088 B margin). */
#define P6_VDP1_NSLOTS  1   /* MENU: b3 catch-all unused -> 1 placeholder slot */
#else
#define P6_VDP1_NSLOTS  4   /* Title BIG bucket (Sonic body catch-all); buckets carry the rest */
#endif
#elif defined(P6_FRONTEND_LOGOS)
#define P6_SPR_MAXW     192 /* Logos: widest frame 187 -> mult-8 pad 192 */
#define P6_SPR_MAXH     96  /* Logos: tallest frame 89 -> 96 */
#define P6_VDP1_NSLOTS  8   /* Logos working set = 4 logo frames; no eviction */
#else
#define P6_SPR_MAXW     64 /* W12b: Player frames (Ring needed 32) */
#define P6_SPR_MAXH     64
#define P6_VDP1_NSLOTS  40 /* Ring 16 + Player working set; eviction = a
                            * declared later closer -- overflow DROPS and
                            * counts (p6_w_vdp1_drops), never overwrites */
#endif
/* GHZ1 parity P2 (#181/#247): staged sheets bind here -- SONIC1/2/3, ITEMS,
 * DISPLAY, SHIELDS, TAILS1, GLOBJ, GHZOBJ (GHZ/Objects.gif, the bridge planks +
 * badniks + GHZ content objects), and the Batch 2 effect sheets EXPLODE
 * (Global/Explosions.gif) + ANIMALS (Global/Animals.gif). Must stay >= the actual
 * bind DEMAND: each surface that binds (a staged .SHT via the banded path, OR an
 * unstaged sheet that LoadSpriteSheet gave resident pixels) consumes exactly one
 * bind-table entry. MEASURED root cause of the bridge not drawing (#181): at 8,
 * the 9th bind (GHZOBJ) hit s_sheet_count>=NSHEETS and returned -1 (handle -1 ->
 * plank blits dropped). BADNIK-VIS (2026-06-18): the SAME class re-bit -- Batch 2's
 * Explosions/Animals decoded to resident pixels (Sprite.cpp:994) + bound via the
 * pixels path, so bind_demand=11 > the 9 slots and BOTH surf 13 (Explosions) AND
 * surf 16 (GHZ/Objects.gif = badniks+bridge+SpikeLog) bound -1 -> invisible
 * (MEASURED bind_log16). The GHZ1 scene's bind DEMAND is 11 (the 9 banded .SHT +
 * 2 resident-PIXEL engine/player surfaces, bind_log16 surf 7 + surf 15). Sized to
 * 12 = 11 measured + 1 margin so GHZ/Objects.gif (surf 16, last by index) binds.
 * (12 * 12 B = 144 B .bss; int8 handle table holds 0..11.) NOTE: growing this and
 * SATURNSHEET_SLOTS TOGETHER tripped the #228 orphan-.bss overlap (GFS GfsMng ptr
 * corruption -> boot trap PC=0x06000956); keep the .bss delta minimal -- raise only
 * this table (+24 B), leave SATURNSHEET_SLOTS and the staged-sheet set unchanged. */
#if defined(P6_GHZCUT_BOOT)
/* Task #311: the GHZCutscene direct-boot binds 2 more surfaces (GHCOBJ.SHT =
 * GHZCutscene/Objects.gif for the AIZKingClaw + Platform crate, RUBYOBJ.SHT =
 * Global/PhantomRuby.gif) on top of the HBHOBJ+PLROBJ demand. 14 = 12 + 2.
 * GATED so the plain GHZ table (144 B .bss) is byte-identical; the +24 B lives
 * only in the GHZCUT flavors (same NSHEETS-only rule as the 12 bump below).
 * STEP-3 GHZ CHAIN STAGING (2026-07-03): +9 more for the handoff-staged GHZ
 * gameplay sheets (players/HUD/items/shields/global+GHZ objects). NSHEETS-only
 * per the #243 rule; +216 B .bss safe in front-end flavors (real ceiling =
 * GLOBALS 0x060C8000, ~52 KB headroom post pack-relocation -- see the
 * frontend-cart-map-recarve memory). 23 = 14 + 9.
 * BADNIK-VIS FIX (2026-07-11): 23->25 for the EXPLOS/ANIMALS surfaces bound at the
 * GHZ handoff (2 more VDP1 bind slots). +32 B .bss, same front-end-safe rule. 25 = 14+11.
 * Water M1b: 25->26 for the WATER.SHT surface bound at the GHZ handoff (1 more VDP1 bind
 * slot). +8 B .bss, same front-end-safe rule; the slot is inert without WATER.SHT staged
 * (#if P6_WATER-gated). 26 = 14+12. */
#define P6_VDP1_NSHEETS 26
#else
#define P6_VDP1_NSHEETS 12
#endif

/* qa_p6_draw.py D5 witness: number of distinct rects resident on VDP1.
 * __attribute__((used)) defeats LTO name-collapse so the gate can locate it
 * in game.map (entity-atlas-loader-pattern memory rule). */
__attribute__((used)) int p6_w_vdp1_slots = 0;
/* P6.7a diagnostic: the LAST-uploaded cache key, packed
 * (sx<<20)|(sy<<12)|(w<<6)|h -- identifies an unexpected 17th rect. */
__attribute__((used)) int p6_w_vdp1_lastkey = 0;
#if defined(P6_FRONTEND_TITLE)
/* task #326 HEAD-FETCH forensic: for the TitleSonic head rect (a tall frame from
 * a sheet-slot >= the title's first title-sheet slot), latch whether the fetch
 * served from RESIDENT pixels (headfetch_resident=1) or the banded path (0), and
 * the count of NON-transparent (index != 0) source bytes in the fetched rect.
 * headfetch_nz == 0 -> the staged head is ALL transparent == invisible (root cause
 * of a black ring hole despite a valid, in-front, correctly-placed blit). */
__attribute__((used)) int p6_w_headfetch_resident = -9;
__attribute__((used)) int p6_w_headfetch_nz       = -9;
__attribute__((used)) int p6_w_headfetch_shtslot  = -9;
__attribute__((used)) int p6_w_headfetch_sxsy     = -9;
__attribute__((used)) int p6_w_headfetch_calls    = -9;
__attribute__((used)) int p6_w_headfetch_bucketbw = -9;
__attribute__((used)) int p6_w_headvram_nz        = -9;
__attribute__((used)) int p6_w_headvram_wh        = -9;
__attribute__((used)) int p6_w_headvram_adr       = -9;
__attribute__((used)) int p6_w_headvram_jid       = -9;
__attribute__((used)) int p6_w_headslot_bk        = -9;
__attribute__((used)) int p6_w_headslot_ret       = -9;
__attribute__((used)) int p6_w_headslot_jid       = -9;
__attribute__((used)) int p6_w_headfetch_ret      = -9;
#endif
/* W12b scale-safety: rect-cache exhaustion / oversize-frame drops.
 * Task #241: with LRU eviction this is now ONLY oversize (>64x64) or banded-
 * fetch-fail; a normal cache miss on a full cache EVICTS instead of dropping,
 * so a BOUND sprite rect never drops -> the player no longer blinks. */
__attribute__((used)) int p6_w_vdp1_drops = 0;
/* Task #241: LRU evictions. A cache MISS on a full cache reuses the
 * least-recently-used slot's VRAM in place via jo_sprite_replace (sprites.c:143)
 * instead of dropping the blit. >0 proves the eviction path is live; the working
 * set per frame (~20 distinct rects) is far below P6_VDP1_NSLOTS, so LRU keeps
 * the hot frames resident and only cold rects churn. */
__attribute__((used)) int p6_w_vdp1_evicts = 0;
/* W14: jo_sprite_add_8bits_image failures (the silent no-drop exit). */
__attribute__((used)) int p6_w_vdp1_joaddfail = 0;
#if defined(P6_FRONTEND_TITLE)
/* #313 boundary probe (2026-07-02): the title FG dies in [60,120) capture frames
 * while p6_w_vdp1_landed keeps +15/frame. MEASURED (savestate + LIBSGL disasm):
 * SGL system byte GBR+0x73 (0x060FFC73, set=1 as SpriteEntry's FIRST insn before
 * ANY reject condition, cleared each slSynch at 0x0604bdbe) reads 0 at deep
 * frames, and the CommandBuf mailbox base (0x060F9D38) holds only slSynch's own
 * op=4 -- ZERO sprite commands enter the mailbox all frame. These witnesses read
 * the SGL-side counters IMMEDIATELY AFTER each jo_sprite_draw3D emit: if flag_post
 * stays 0 the call chain jo_sprite_draw3D -> slDispSprite -> SpriteEntry is broken
 * BEFORE SGL; if flag=1 but accept(GBR+0x74) lags attempt(GBR+0xAA), SpriteEntry
 * is REJECTING (Z-window/count) and the snap_* one-shot captures the operands. */
__attribute__((used)) int p6_w_sgl_flag_post = -9; /* GBR+0x73 after the LAST emit */
__attribute__((used)) int p6_w_sgl_aa_post   = -9; /* GBR+0xAA attempts after last emit */
__attribute__((used)) int p6_w_sgl_74_post   = -9; /* GBR+0x74 accepts  after last emit */
__attribute__((used)) int p6_w_sgl_missing   = 0;  /* emits where flag stayed 0 (cumulative) */
__attribute__((used)) int p6_w_sgl_rejgap    = 0;  /* max(attempt-accept) seen (cumulative) */
__attribute__((used)) int p6_w_sgl_snap68    = 0;  /* GBR+0x68 at first reject (Z-window base) */
__attribute__((used)) int p6_w_sgl_snapac    = -9; /* GBR+0xAC at first reject (shift count) */
static void p6_sgl_emit_probe(void)
{
    volatile unsigned char  *f73 = (volatile unsigned char  *)0x060FFC73u;
    volatile unsigned short *aa  = (volatile unsigned short *)0x060FFCAAu;
    volatile unsigned short *s74 = (volatile unsigned short *)0x060FFC74u;
    int gap;
    p6_w_sgl_flag_post = (int)*f73;
    p6_w_sgl_aa_post   = (int)*aa;
    p6_w_sgl_74_post   = (int)*s74;
    if (!*f73) ++p6_w_sgl_missing;
    gap = (int)*aa - (int)*s74;
    if (gap > p6_w_sgl_rejgap) {
        p6_w_sgl_rejgap = gap;
        p6_w_sgl_snap68 = (int)*(volatile unsigned long *)0x060FFC68u;
        p6_w_sgl_snapac = (int)*(volatile unsigned char *)0x060FFCACu;
    }
}
#endif
/* CP4c BLUE-SCREEN diag: WHICH drop branch in p6_slot_for fired last, + the
 * last attempted fetch's (slot,w,h). 1=oversize(w/h>box) 2=no-fetch-fn-or-
 * fetch-failed 3=jo_sprite_add failed. lastfetch packs (shtSlot<<24)|(w<<12)|h ;
 * fetchret = the s_fetchFn return (1 ok / 0 fail). FRONT-END ONLY so the GHZ
 * hot-path p6_slot_for (60 fps-sensitive) stays byte-identical. */
#if defined(P6_FRONTEND_LOGOS) || defined(P6_GHZCUT_BOOT)
__attribute__((used)) int p6_w_vdp1_dropreason = 0;
__attribute__((used)) int p6_w_vdp1_lastfetch  = 0;
__attribute__((used)) int p6_w_vdp1_fetchret   = -1;
__attribute__((used)) int p6_w_vdp1_lastwh     = 0; /* (w<<16)|h of the last slot_for call */
/* #311 mech-4 draw-time bisect (chain rect 258,492,16,16): djb2 of the DRAW-time
 * fetch result + of the same region after the s_stage copy. Reload-time expected
 * = 0x0b35cf05 (measured byte-exact twice). */
__attribute__((used)) int p6_w_draw_fetch_h = -9;
__attribute__((used)) int p6_w_draw_stage_h = -9;
/* v2 ring: last 4 GHCOBJ-slot (11) fetches -- (sx<<16|sy), (w<<16|h), djb2.
 * v3 (build 25): the ring records at the LIVE bucket fetch site
 * (p6_title_pool_for); p6_w_g11_stage[] pairs each entry with the djb2 of the
 * s_stage content-packed box AFTER p6_title_restage_content (pw*h bytes,
 * pw = mult-8 padded w) -- fetch-vs-stage split localises a pack-loop tear. */
__attribute__((used)) int p6_w_g11_rect[4]  = { -9, -9, -9, -9 };
__attribute__((used)) int p6_w_g11_wh[4]    = { -9, -9, -9, -9 };
__attribute__((used)) int p6_w_g11_hash[4]  = { -9, -9, -9, -9 };
__attribute__((used)) int p6_w_g11_stage[4] = { -9, -9, -9, -9 };
__attribute__((used)) int p6_w_g11_n        = 0;
#endif
/* #2a diag: tag WHICH p6_title_pool_for / p6_slot_for drop branch fired (GHZCUT only;
 * every other flavor is a no-op so the hot path stays byte-identical). */
#if defined(P6_GHZCUT_BOOT)
#define P6_DR(n) (p6_w_vdp1_dropreason = (n))
#else
#define P6_DR(n) ((void)0)
#endif
/* W18 (Task #227, qa_p6_entdraw.py): the UNBOUND-SURFACE silent drop. A
 * DrawSprite blit whose surface never bound (handle < 0) returned early
 * WITHOUT counting (the dominant ~5944 unrendered ring/entity blits/run);
 * landed = blits that reached a valid VDP1 slot (handle >= 0, slot cached).
 * Both counted across BOTH blit arms so the gate witnesses the bind+blit. */
__attribute__((used)) int p6_w_vdp1_handle_drops = 0;
__attribute__((used)) int p6_w_vdp1_landed       = 0;
/* W18: the LAST handle passed to a dropped blit (== -1 for unbound surface;
 * identifies whether the drop was unbound vs slot-cache exhaustion). */
__attribute__((used)) int p6_w_vdp1_lastdrop_h   = -2;

/* STEP B (#246/#243): per-frame VDP1 workload, to localise the VDP1-draw
 * bottleneck (A2 showed VDP1 74% BUSY at compute-done). VDP1 rasterises every
 * pixel of a sprite's bbox (transparent texels are READ then skipped), so the
 * cost ~ total drawn pixel area. EVERY sprite is staged into a FIXED 64x64 box
 * (p6_vdp1.c:241 img.width/height = P6_SPR_MAXW/H) -> a 16x16 ring rasterises
 * 4096 px (16x overdraw). These accumulate in the blit arms + reset each frame
 * (p6_vdp1_perf_reset from p6_ghz_frame) so the capture holds the last frame's
 * totals. boxpx = the VDP1 fill workload AS DRAWN; contentpx = the ideal if
 * sprites were drawn at content size -> contentpx/boxpx = the overdraw waste. */
__attribute__((used)) int p6_w_vdp1_cmds      = 0; /* landed sprite cmds this frame */
__attribute__((used)) int p6_w_vdp1_boxpx     = 0; /* sum of 64x64 per cmd (as drawn) */
__attribute__((used)) int p6_w_vdp1_contentpx = 0; /* sum of w*h per cmd (ideal) */
__attribute__((used)) int p6_w_vdp1_maxw      = 0; /* widest single sprite this frame */
__attribute__((used)) int p6_w_vdp1_maxh      = 0; /* tallest single sprite this frame */

#if defined(P6_FRONTEND_MENU)
/* M1b striped-icon ROOT-CAUSE witnesses (this session). The MENU bucket pools
 * (b0/b1/b2/b3) are uniform-box content-size pools; a MISS on a FULL bucket
 * EVICTS the LRU slot and p6_title_restage_content RE-DMAs that slot's jo_id +
 * mutates its __jo_sprite_def CMDSIZE. If a slot that was ALREADY restaged THIS
 * frame is restaged AGAIN (its lastUse stamp is >= this frame's start clock), an
 * EARLIER same-jo_id draw command already in the frame's VDP1 list now reads the
 * NEW rect's pixels/CMDSIZE at flush -> the striped-band garble. p6_w_restage_dbl
 * counts exactly that condition (the garble proof; 0 == every slot restaged at
 * most once/frame == no stale-command corruption). p6_w_buckN_fmax = the max
 * distinct rects requested from bucket N in one frame (the per-frame demand vs
 * the bucket slot count). All reset in p6_vdp1_perf_reset; the savestate holds the
 * captured frame's values. MENU flavor only (the GHZ/Title p6_vdp1.o is unchanged). */
__attribute__((used)) int p6_w_restage_dbl = 0; /* intra-frame double-restage (the garble) */
__attribute__((used)) int p6_w_buck0_fmax  = 0; /* per-frame restages (misses) in bucket idx0 (box per P6_BUCK) */
__attribute__((used)) int p6_w_buck1_fmax  = 0; /* per-frame restages (misses) in bucket idx1 */
__attribute__((used)) int p6_w_buck2_fmax  = 0; /* per-frame restages (misses) in bucket idx2 */
__attribute__((used)) int p6_w_buck3_fmax  = 0; /* per-frame restages (misses) in bucket idx3 */
/* s_buckMiss[bk] accumulates restages (cache misses -> evict+restage) for bucket bk
 * THIS frame; perf_reset latches the per-frame MAX into p6_w_buckN_fmax. A frame whose
 * miss count for a bucket EXCEEDS that bucket's slot count means >=1 slot was restaged
 * MID-FRAME after an earlier draw already referenced its jo_id -> stale-command garble.
 * s_restageEpoch increments per frame; s_buckMiss is the within-frame tally. */
#if defined(P6_GHZCUT_BOOT)
/* #314 punch-list 1: the GHZCUT chain flavor has FIVE buckets (the 176x56 wide bucket
 * below); witness + tally arrays follow. */
__attribute__((used)) int p6_w_buck4_fmax  = 0; /* per-frame restages (misses) in bucket idx4 */
int p6_buckMiss[5] = { 0, 0, 0, 0, 0 }; /* non-static: bumped from p6_title_pool_for */
#else
int p6_buckMiss[4] = { 0, 0, 0, 0 }; /* non-static: bumped from p6_title_pool_for */
#endif
#endif

#if defined(P6_FRONTEND_MENU)
/* #317 restage-split: the per-miss restage (p6_title_restage_content) is the ~200ms
 * front-end draw hog (cb==blit==draw, dma bracket in the DEAD p6_pool_for read 0).
 * Split it into the BLOCKING slDMAWait (SCU-DMA contention w/ the per-frame VDP2
 * scroll DMA -- the sgl-audio-vs-scroll-cpu-dma-conflict class) vs the byte stage-
 * copy + async jo_dma_copy. wait_v>>work_v => slDMAWait is the lever. */
extern volatile unsigned int p6_perf_vbl_count;
__attribute__((used)) int p6_w_rst_wait_v = 0; /* vblanks in slDMAWait() (summed/frame) */
__attribute__((used)) int p6_w_rst_work_v = 0; /* vblanks in stage-copy + jo_dma_copy (summed/frame) */
__attribute__((used)) int p6_w_rst_calls  = 0; /* cumulative restage (miss) count */
/* #324: per-frame FRT ticks inside p6_title_restage_content (the direct VDP1-VRAM
 * pack below). GREEN-side witness for the restage-cost lever; reset with the other
 * per-frame tallies in p6_vdp1_perf_reset. Chain-only (P6_FRONTEND_MENU). */
extern unsigned short p6_perf_frt_get(void);
__attribute__((used)) int p6_w_rst_cyc = 0;
#endif

__attribute__((used)) void p6_vdp1_perf_reset(void) /* called at p6_ghz_frame top */
{
    p6_w_vdp1_cmds = 0; p6_w_vdp1_boxpx = 0; p6_w_vdp1_contentpx = 0;
    p6_w_vdp1_maxw = 0; p6_w_vdp1_maxh = 0;
#if defined(P6_FRONTEND_MENU)
    if (p6_buckMiss[0] > p6_w_buck0_fmax) p6_w_buck0_fmax = p6_buckMiss[0];
    if (p6_buckMiss[1] > p6_w_buck1_fmax) p6_w_buck1_fmax = p6_buckMiss[1];
    if (p6_buckMiss[2] > p6_w_buck2_fmax) p6_w_buck2_fmax = p6_buckMiss[2];
    if (p6_buckMiss[3] > p6_w_buck3_fmax) p6_w_buck3_fmax = p6_buckMiss[3];
    p6_buckMiss[0] = p6_buckMiss[1] = p6_buckMiss[2] = p6_buckMiss[3] = 0;
    p6_w_rst_wait_v = 0; p6_w_rst_work_v = 0; /* #317: per-frame restage split reset */
    p6_w_rst_cyc = 0;                         /* #324: per-frame restage FRT reset */
#if defined(P6_GHZCUT_BOOT)
    if (p6_buckMiss[4] > p6_w_buck4_fmax) p6_w_buck4_fmax = p6_buckMiss[4];
    p6_buckMiss[4] = 0;
#endif
#endif
}

/* W12b: MULTI-SHEET bind table. A handle indexes this table; resident
 * sheets carry their engine surface pixels, banded sheets carry the
 * SaturnSheet store slot (pixels fetched per cache miss). */
/* W12b ROOT-CAUSED (bisect A/A1/A2, 2026-06-11): a STATIC reference from
 * this jo-side (LTO) TU to the pack symbol SaturnSheet_FetchRect re-shapes
 * the mixed LTO/non-LTO link on GCC 8.2 and crashes the GFS pack open
 * (PC 0x06000956, PR in GFS_Tell) -- with the refactor otherwise identical
 * and the reference absent, the boot is GREEN. The fetch therefore arrives
 * as a RUNTIME FUNCTION POINTER set by the pack side (p6_io_main step 1.7),
 * which already references jo symbols in the proven direction. */
static int (*s_fetchFn)(int slot, int sx, int sy, int w, int h,
                        unsigned char *dst) = 0;
void p6_vdp1_set_fetch(int (*fn)(int, int, int, int, int, unsigned char *))
{
    s_fetchFn = fn;
}
#if defined(P6_FRAMEDIR)
/* Sprite-pipeline rework: PRE-CUT frame directory (SaturnFrameDir.cpp, FRD1
 * blobs built by tools/build_frame_dir.py). A slot-cache MISS on an FRD-backed
 * sheet no longer cuts the rect at runtime (banded miniz inflate / resident
 * per-row repack): the pattern is already cut + padded to the ST-013-R3 sec 5.1
 * 8-pixel width unit, rows CONTIGUOUS at the padded stride, 4-aligned in the
 * cart -- the restage copy runs its aligned-u32 fast path over one linear
 * block. Same LTO contract as s_fetchFn above: the pack side hands us the
 * lookup as a RUNTIME FUNCTION POINTER (p6_io_main), never a static ref.
 * Layout mirrors SaturnFrameDir.cpp's P6FrameInfo EXACTLY. */
typedef struct {
    const unsigned char *pattern;
    const unsigned char *lutSrc;
    int pw;      /* padded width, PIXELS (multiple of 8) */
    int mode;    /* 0 = 8bpp (VDP1 color mode 4), 1 = 4bpp LUT (mode 1) */
    int lutIdx;
} P6FrameInfo;
static int (*s_frdFn)(int slot, int sx, int sy, int w, int h,
                      P6FrameInfo *out) = 0;
void p6_vdp1_set_frd(int (*fn)(int, int, int, int, int, P6FrameInfo *))
{
    s_frdFn = fn;
}
/* DRAW-WALL FIX (task #328, 2026-07-13): FRD slot indexed by SaturnSheet STORE slot
 * (NOT the per-VDP1-handle frdSlot). ROOT CAUSE (MEASURED, tools/_disp_probe.py
 * banded-case latch): the Player draws Sonic1/Tails1 through a VDP1 handle whose
 * per-handle frdSlot is -1 (a duplicate handle from the AIZ/cutscene leg that the
 * one-shot + per-frame handle-attach never covered -- the handle index differs
 * from the recorded ghzGslot store slot due to the #321 AIZ-reuse). The banded
 * FetchRect always fetches by STORE slot (s_sheets[sheet].shtSlot), which IS stable
 * and correct (MEASURED store 11=Sonic1, 14=Tails1). So key the FRD dispatch by the
 * store slot: any handle drawing a store slot that has a staged FRD serves from the
 * pre-cut pattern, regardless of that handle's own frdSlot. io_main populates this
 * table once per GHZ frame (p6_vdp1_frd_set_store). -1 = no FRD for that store slot.
 * Chain flavor only (P6_FRAMEDIR) -> plain GHZ byte-identical. Gate: qa_chain_draw.py. */
#define P6_FRD_STORE_MAX 32
static int s_frdByStore[P6_FRD_STORE_MAX];
static int s_frdByStore_init = 0;
void p6_vdp1_frd_set_store(int shtSlot, int frdSlot)
{
    int i;
    if (!s_frdByStore_init) {
        for (i = 0; i < P6_FRD_STORE_MAX; ++i) s_frdByStore[i] = -1;
        s_frdByStore_init = 1;
    }
    if (shtSlot >= 0 && shtSlot < P6_FRD_STORE_MAX)
        s_frdByStore[shtSlot] = frdSlot;
}
void p6_vdp1_frd_clear_store(void)
{
    int i;
    for (i = 0; i < P6_FRD_STORE_MAX; ++i) s_frdByStore[i] = -1;
    s_frdByStore_init = 1;
}
#endif
static struct {
    const unsigned char *px; /* resident surface pixels, or NULL if banded */
    int w;                   /* sheet width (row stride) */
    int shtSlot;             /* SaturnSheet slot for banded sheets (px==NULL) */
#if defined(P6_FRAMEDIR)
    int frdSlot;             /* SaturnFrameDir slot (-1 = no frame directory) */
#endif
} s_sheets[P6_VDP1_NSHEETS];
static int s_sheet_count = 0;

#if defined(P6_FRAMEDIR)
/* DRAW-WALL FIX (task #328): effective FRD slot for a bound handle -- prefer the
 * store-slot table (covers duplicate handles bound to the same store slot across
 * legs), fall back to the per-handle frdSlot (resident px sheets have shtSlot<0). */
static int p6_frd_slot_for_sheet(int sheet)
{
    int ss = s_sheets[sheet].shtSlot;
    if (ss >= 0 && ss < P6_FRD_STORE_MAX && s_frdByStore_init
        && s_frdByStore[ss] >= 0)
        return s_frdByStore[ss];
    return s_sheets[sheet].frdSlot;
}
#endif

typedef struct {
    int sheet;        /* W12b: bind handle joins the cache key */
    int sx, sy, w, h; /* cache key: sheet rect */
    int jo_id;        /* jo sprite id of the uploaded rect */
    int lastUse;      /* Task #241: LRU stamp (s_useclock at last hit/fill) */
} P6Vdp1Slot;
static P6Vdp1Slot s_slots[P6_VDP1_NSLOTS]; /* LARGE box: P6_SPR_MAXW x P6_SPR_MAXH */
#if defined(P6_FRONTEND_TITLE)
/* CP5b.7 -> Phase 2 (Task #279): CONTENT-SIZE N-BUCKET VDP1 pool.
 *
 * MEASURED ROOT CAUSE (qa_p6_perf --scene title, per-section FRT): the title is
 * COMPUTE-bound, not VDP1-fill-bound -- DrawLists 42.3 ms dominates the 48.9 ms
 * master frame; jo-body/slSynch wait = 0.00 ms (the master never waits on VDP1).
 * The DrawLists cost is the per-sprite STAGING (a CPU byte-copy of the fixed box
 * into s_stage) + jo_sprite_replace DMA on every cache MISS. Every sprite paid the
 * one 248x160 box (39,680 B) regardless of content, so a churning animation
 * (ribbon-wave tails, electricity ring, unfurl) re-staged 39,680 B per cycle frame.
 * The earlier "VDP1-fill-bound" read of the P6_TITLE_NODRAW A/B was wrong: skipping
 * the emit also skips this CPU staging. CP5b.7's single 64x64 second pool (8.6 ->
 * 13.3 fps) cut it for tiny sprites; Phase 2 generalises to N content-size buckets
 * so EVERY sprite stages only the smallest box that holds it.
 *
 * Bucket boundaries from the MEASURED Title frame dims (parse_spr on
 * extracted/Data/Sprites/Title/{Logo,Sonic,Electricity}.bin):
 *   b0 64x80   : finger 50x63, ribbon-WAVE tails 56x72, copyright 45x8, small ring
 *   b1 192x64  : ribbon-center 176x52, wordmark 137x46, ring-bottom 120x25,
 *                press-start 174x14
 *   b2 160x160 : emblem 144x144, ribbon-UNFURL 118x85, mid electricity-ring frames
 *   b3 248x160 : Sonic BODY 241x137 + CATCH-ALL (w>160). Box UNCHANGED from the old
 *                large pool, so nothing that drew before can newly drop (every ring
 *                frame is <=160 wide; >160-tall frames already dropped at 248x160).
 * Smallest-first: a sprite takes the first bucket whose box holds BOTH (w,h). Each
 * bucket is UNIFORM-box so jo_sprite_replace's same-size in-place LRU reuse holds
 * (jo's VDP1 allocator is append-only -- mixing sizes in one pool would leak). VRAM
 * (1 B/px, 8bpp paletted): 6*5,120 + 6*12,288 + 6*25,600 + 4*39,680 = 416,768 B <
 * JO_VDP1_USER_AREA_SIZE 466,232 (49,464 B margin). b3 reuses the existing s_slots[]
 * (n_max capped at 4) + the p6_w_vdp1_slots coldn, so the GHZ/Logos builds (no
 * P6_FRONTEND_TITLE) keep the single-pool path byte-identical. */
#if defined(P6_GHZCUT_BOOT)
#define P6_NB 5 /* #314 punch-list 1: + the 176x56 WIDE bucket (see the GHZCUT table below) */
#else
#define P6_NB 4
#endif
/* M1b: the MENU scene draws MANY small sprites (the 4 mode rows = icon+shadow+text
 * each, + headings/prompts) -- far more distinct rects than the Title's ~9. With the
 * Title bucket counts (6/6/6/4 = 22 slots) the menu THRASHED the LRU (MEASURED
 * p6_w_vdp1_evicts=3238): a victim slot re-staged with DIFFERENT content keeps the
 * PREVIOUS sprite's __jo_sprite_def CMDSIZE -> garbage stripes (the title-vdp1-slot-
 * thrash class). The menu sprites are SMALL (text glyphs <=32px, icons <=48px) so they
 * route to bucket 0 (64x80) -- give it MANY slots. VRAM (1 B/px 8bpp):
 * 40*5,120 + 8*12,288 + 3*25,600 + 2*39,680 = 459,264 B < JO_VDP1_USER_AREA_SIZE
 * (466,232; 6,968 B margin). The TITLE/Logos flavor keeps 6/6/6/4 (this #if is the only
 * difference; the GHZ build has no buckets). */
#if defined(P6_GHZCUT_BOOT)
/* Task #311 (GHZCutscene right-pile garble) -- the M1b thrash class, b2 edition.
 * MEASURED (build 18, _late.mcs): the cutscene camera parks at x=0 so the WHOLE
 * dig-site cluster (5 CutsceneHBH 56x95 + claw + ruby + crate, world x 196-364)
 * draws at the right third. The 56x95 Heavies route to b2 (160x160; 95>80 skips
 * b0, 95>64 skips b1) -- and the MENU sizing gives b2 ONE slot -> all Heavy draws
 * in a frame share it: the VDP1 cmd list showed FOUR 56x95 draws with the SAME
 * SRCA 0x057800, and that texture VIEWED = garbage static (mid-frame re-stage
 * mutating __jo_sprite_def after earlier draws queued = the documented M1b
 * red/blue/white horizontal-band garble). Sizing for the cutscene working set:
 * #312 climax re-geometry (build 31) -- MEASURED (_climax30.mcs @ the ExitHBH
 * beat + VIEWED _g30b_11.png stripes): fmax[b0] = 23 MISSES > 22 slots when
 * every actor (claw + 5 Heavies + ruby + platform + players + HUD) restages in
 * ONE frame -> intra-frame slot reuse -> the stale-command stripe garble. And
 * fmax[b1] = 0 across the WHOLE run: NOTHING in this scene is 65-192 px wide x
 * <=64 tall -> the 192x64 bucket was DEAD VRAM (73,728 B). FIX: repurpose b1 as
 * a TINY 16x20 bucket absorbing the small-rect flood (HUD digits 9x14 -- the
 * climax TIME readout alone is ~5 -- world rings 12x16, claw chains 16x14/16,
 * small claw bits 16x13), which drops b0's demand well under capacity; grow b0
 * 22->24 for margin. Router order stays ascending (tiny FIRST). A future
 * 65-192-wide flat sprite routes to b2 (8 slots, measured fmax 3 -- headroom).
 * #314 punch-list 1 (CHAIN menu row-art garble, _chain__46.png) -- the M1b class
 * AGAIN, this time a FLAVOR INTERACTION: the chain build uses THIS (GHZCUT) bucket
 * table for EVERY scene it visits, including the MENU -- and this table had NO
 * bucket for the menu's wide-flat working set. Per the M1b census (measured, see
 * the P6_FRONTEND_MENU branch below): ~10 MainIcons rects 88-120w x 38-44h + 4-6
 * TextEN labels 86-176w x 22h + the 176w Sound label = ~14-16 distinct wide-flat
 * rects PER FRAME. Routing under the old 4-bucket GHZCUT table: w>64 skips the
 * 64x80 bucket -> everything <=160w piled into the 160x160 bucket (8 slots) and
 * the >160w labels into the catch-all (P6_VDP1_NSLOTS==1 slot, GHZCUT implies
 * MENU) -> demand 14-16 vs capacity 9 -> intra-frame LRU reuse -> the documented
 * M1b stale-CMDSIZE band garble on the row icons. (The direct menu boot was clean
 * because its OWN branch below has the 192x64 x18 bucket.) FIX: a FIFTH bucket,
 * 176x56 WIDE, sized 15 -- and it ALSO catches the title's wide-flats (ribbon
 * center 176x52, press-start 174x11, gametitle 137x46, ringbottom 120x25) that
 * previously shared the 1-slot catch-all on the chain's title leg. Funding trims
 * are MEASURED, not guessed (fmax witnesses, climax capture): tiny fmax=6 -> 20
 * becomes 8; 64x80 fmax=12 -> 24 becomes 12 (worst full-restage frame exactly
 * fills 12 distinct slots -> no intra-frame reuse). Heavies bucket unchanged.
 * INDEX ORDER (first-fit router; tiny FIRST, catch-all LAST):
 *   idx0 TINY 16x20  x 8: digits + rings + chains (climax fmax 6)
 *   idx1      64x80  x12: players fan 41x21/48x24 + claw arms + crate 64x60
 *                         + ruby 32x32 + HUD labels 41x14 (climax fmax 12)
 *   idx2 WIDE 176x56 x15: menu icons/labels ~14-16, title ribbon/press-start/
 *                         gametitle/ringbottom (route: w<=176 AND h<=56)
 *   idx3    160x160  x 8: the 5 Heavies 56x95 + emblem 144x144 + fx margin
 *   idx4    248x160  x 1: catch-all (Sonic title body 241x137 -- sole occupant,
 *                         one draw/frame -> 1 slot cannot intra-frame collide)
 * VRAM (8bpp 1 B/px): 8*320 + 12*5,120 + 15*9,856 + 8*25,600 + 1*39,680 =
 * 456,320 B < JO_VDP1_USER_AREA_SIZE 466,232 (9,912 B margin for the FillScreen
 * occluder + fade quads). .bss delta = -24 slots +15 slots (28 B each) = -252 B
 * + 24 B (witness/miss/bucket entries) = NET -228 B (#228-safe, _end DOWN).
 * GHZCUT implies MENU so this branch MUST precede the MENU one. */
/* #324 re-carve (RED-gated, qa_drawcost_gate G1/G2; live chain forensic
 * _drawprof_F1.jsonl 2026-07-09): the 64x80 bucket (idx1) THRASHED -- per-frame
 * miss maxima p6_w_buck1_fmax = 14 (AIZ claw beats), 16-17 (GHZCutscene), 17
 * (GHZ landing) vs 12 slots -> demand > capacity -> the LRU cycled EVERY frame
 * (~11.6 restages/frame at the claw = the measured DrawLists hog). The WIDE
 * 176x56 bucket measured fmax = 10 max across the whole chain (menu leg
 * included) vs 15 slots -> 4 dead slots. Re-carve: 64x80 12 -> 18 (covers the
 * worst measured 17 + 1), WIDE 15 -> 11 (measured 10 + 1). VRAM (8bpp 1 B/px):
 * 8*320 + 18*5,120 + 11*9,856 + 8*25,600 + 1*39,680 = 447,616 B <
 * JO_VDP1_USER_AREA_SIZE 466,232 (18,616 B margin). .bss +2 slots * 28 B =
 * +56 B (front-end flavors only; chain _end headroom ~38 KB). */
/* Fix 2 (user-symptom-map-v2 R3, digits/ball/ring flashing -- RED-gated,
 * tools/qa_vdp1_thrash.py): MEASURED at settled GHZ (live, 2026-07-17):
 * p6_w_vdp1_evicts = 6.10/frame with dl_cmds_max = 43 sprites/frame while the
 * FRD layer was CLEAN (239 lookups, 0 misses) -> the flashing is TINY-bucket
 * LRU thrash: the alternating rects (HUD digits 9x14, placed ring 12x16,
 * StarPost ball 16x16) ALL route to idx0 (16x20), whose 8 slots sat far under
 * the settled-GHZ play demand. Demand enumerated from the ACTUAL bins
 * (parse_spr, gate T1): 10 digit glyphs + lives 'x' + ring frame + 3 collect
 * sparkles + 3 StarPost (bulb + star spins) + dust puff + 2 ScoreBonus = 21
 * distinct concurrent tiny rects -> 26 slots (21 + 20% headroom; the old
 * "climax fmax=6 -> 8" sizing was measured on the CUTSCENE leg, not settled
 * GHZ play with full HUD + ring collection). An intra-frame slot reuse
 * re-stages a live jid's VRAM mid-frame -> the M1b stale-CMDSIZE/content
 * alternation the user sees as digit/ball/ring flashing.
 * FUNDING: 160x160 idx3 8 -> 7 (-25,600 B) -- its measured fmax across the
 * whole chain is 3 (#312 note: "b2 8 slots, measured fmax 3 -- headroom");
 * worst modeled concurrent demand is the 5 GHZCut Heavies + 1 = 6 <= 7. The
 * hot set (player frames idx1, HUD digits + ring idx0) needs no explicit pin:
 * capacity now exceeds worst-case demand in BOTH buckets, so plain LRU keeps
 * it resident (pinning would only mask a future demand growth the fmax
 * witnesses + this gate exist to catch).
 * VRAM (8bpp 1 B/px): 26*320 + 18*5,120 + 11*9,856 + 7*25,600 + 1*39,680 =
 * 427,776 B < JO_VDP1_USER_AREA_SIZE 466,232 -> 38,456 B margin >= the
 * 18,240 B data-driven reserve (TitleCard glyph cache worst 14,656 B, gate
 * T3, + 3 lazy fill sprites 1,536 B + 2 KB safety). .bss: +18 -1 slots *
 * 28 B = +476 B, FRONT-END FLAVORS ONLY (plain-GHZ has no buckets, byte-
 * identical); chain headroom ~38 KB (#324 note) so no cart-window
 * relocation needed -- qa_p6_mapoverlap guards _end < 0x060C8000 post-build. */
#define P6_BK0 26            /* idx0 TINY 16x20 (was 8; settled-GHZ demand 21 + 20%) */
#define P6_BK1 18
#define P6_BKW 11            /* idx2 WIDE 176x56 (menu rows + title wide-flats) */
#define P6_BK2 7             /* idx3 160x160 (was 8; measured fmax 3, modeled worst 6) */
#define P6_GHZCUT_TINY_B1 1  /* idx0 box = 16x20 (see the P6_BUCK table below) */
#elif defined(P6_FRONTEND_MENU)
/* M1b STRIPED-ICON FIX (this session) -- MEASURED root cause + sizing.
 *
 * The UIModeButton rows draw (decomp UIModeButton_Draw:45-74, per frame): the icon
 * (MainIcons.bin anim0) + shadow (anim1) [+ altIcon/altShadow for Competition] of each
 * of the 4 mode rows = ~10 MAINICON rects (88-120px wide, 38-44px tall) -> ALL route to
 * bucket b1 (192x64; b0=64x80 is too narrow for >64px). The 4 mode TEXT labels (TextEN.bin
 * anim1 "Main Menu" 86-148px wide x 22) ALSO route to b1. So b1's per-frame demand is
 * ~14-16 distinct rects. The CRITICAL bug: P6_FRONTEND_MENU was NOT passed to the jo-make
 * compile of THIS TU (Makefile had no ifeq P6_FRONTEND_MENU block), so this #if was FALSE
 * and the build used the TITLE #else branch (6/6/6) -> b1 had only 6 slots -> every frame
 * overflowed -> the LRU EVICTED + p6_title_restage_content RE-DMA'd a slot's jo_id +
 * mutated its __jo_sprite_def CMDSIZE MID-FRAME, after an EARLIER same-id draw command was
 * already queued -> VDP1 rasterised the FINAL (wrong) rect for those earlier icon draws =
 * the RED/BLUE/WHITE horizontal-band garble (the TEXT, drawn LAST per row, won the LRU and
 * stayed clean). MEASURED p6_w_vdp1_evicts = 37,388; qa_menu_icon_clean.py ROW_ALT = 148.
 * FIX: (1) Makefile ifeq P6_FRONTEND_MENU -> -DP6_FRONTEND_MENU reaches this compile (so
 * this branch is taken); (2) size b1 = 18 (> the ~16 demand -> NO eviction). b0 = 14
 * (small nav/heading/prompt glyphs <64px). The menu NEVER reaches b2 (160x160; needs h>64)
 * or b3 (catch-all; needs w>192 -- menu max width 176 routes to b1), so both = 1 placeholder
 * (P6_VDP1_NSLOTS=1 for MENU, set above). VRAM (8bpp, 1 B/px): 14*5,120 + 18*12,288 +
 * 1*25,600 + 1*39,680 = 358,144 B < JO_VDP1_USER_AREA_SIZE (466,232; 108,088 B margin). */
#define P6_BK0 14
#define P6_BK1 18
#define P6_BK2 1
#else
#define P6_BK0 6
#define P6_BK1 6
#define P6_BK2 6
#endif
static P6Vdp1Slot s_buck0[P6_BK0];               /* 64x80 (GHZCUT: 16x20 tiny) */
static P6Vdp1Slot s_buck1[P6_BK1];               /* 192x64 (GHZCUT: 64x80) */
#if defined(P6_GHZCUT_BOOT)
static P6Vdp1Slot s_buckW[P6_BKW];               /* 176x56 WIDE (#314 punch-list 1) */
#endif
static P6Vdp1Slot s_buck2[P6_BK2];               /* 160x160 */
/* final bucket (248x160 catch-all) reuses s_slots[] + p6_w_vdp1_slots. */
static int s_buck0n = 0, s_buck1n = 0, s_buck2n = 0;
#if defined(P6_GHZCUT_TINY_B1)
/* #312: FIRST-FIT order is a router invariant (smallest-suitable select below) --
 * the tiny bucket must come FIRST, the catch-all LAST. Index mapping to the slot
 * arrays is by POSITION (arr[0]=s_buck0 etc), so s_buck0 holds the TINY slots,
 * s_buck1 the 64x80 slots, s_buckW the 176x56 WIDE slots (#314) in this flavor;
 * the fmax witnesses follow the same order (buck0_fmax = tiny, buck2_fmax = wide).
 * ROUTE CHECK (hand-verified per rect class): HUD digit 9x14 / ring 12x16 / chain
 * 16x16 -> tiny; fan 48x24 / ruby 32x32 / crate 64x60 / finger 50x62 -> 64x80;
 * menu icon 120x44 / label 176x22 / ribbon 176x52 / press-start 174x11 -> WIDE
 * (h<=56); Heavy 56x95 (h>56) / emblem 144x144 -> 160x160; Sonic body 241x137
 * (w>176 skips WIDE, w>160 skips 160x160) -> catch-all. */
static const struct { int bw, bh; } P6_BUCK[P6_NB] = {
    { 16, 20 }, { 64, 80 }, { 176, 56 }, { 160, 160 }, { P6_SPR_MAXW, P6_SPR_MAXH }
};
#else
static const struct { int bw, bh; } P6_BUCK[P6_NB] = {
    { 64, 80 }, { 192, 64 }, { 160, 160 }, { P6_SPR_MAXW, P6_SPR_MAXH }
};
#endif
/* smallest-first bucket select; shared by the router AND the stride cull so the
 * box used for placement and the box used for the off-screen-wrap check agree. */
static int p6_bucket_for(int w, int h)
{
    int i;
    for (i = 0; i < P6_NB; ++i)
        if (w <= P6_BUCK[i].bw && h <= P6_BUCK[i].bh)
            return i;
    return -1;
}

/* CP5b.7 content-size step (Task #277): EAGER PRE-ALLOCATION of every bucket slot
 * at its full BOX size, ONCE, before any draw.
 *
 * THE FILL FIX: the N-bucket above cut the single-box waste (3.47 -> 0.92 screens)
 * but each sprite still drew its WHOLE bucket box -- MEASURED 39% of the on-hardware
 * VDP1 fill is transparent padding (e.g. ring-bottom 120x25 in a 192x64 box). VDP1
 * rasterises every pixel of a sprite's CMDSIZE bbox (transparent texels read+skipped),
 * so the fill cost == the drawn CMDSIZE area. jo registers each sprite's CMDSIZE in
 * __jo_sprite_def[id] {width=Hsize, height=Vsize, adr=CGadr, size=HVsize} -- the SGL
 * TEXTURE table fed to slInitSystem (core.c:192 casts __jo_sprite_def to TEXTURE*).
 * slDispSprite (jo SGL path, sprites.c:447, scale 1.0) draws the sprite at that
 * registered hardware size. So to draw a sprite at CONTENT (w mult-8, h) instead of
 * the box, p6_title_restage_content (below) DMAs the content packed at content-width
 * STRIDE into the slot's reserved VRAM and OVERWRITES the slot's __jo_sprite_def
 * width/height/size to content -- exactly the TEXDEF a content-sized add would produce
 * (ST-238-R1 sec, TEXDEF(h,v,presize) HVsize = ((h&0x1f8)<<5)|v == jo
 * __internal_jo_sprite_add:212). VDP1 then rasterises ONLY the content rows/cols.
 *
 * WHY EAGER PRE-ALLOC: jo's VDP1 allocator (__jo_get_next_sprite_address,
 * sprites.c:74) is APPEND-ONLY and computes the NEXT sprite's VRAM address from the
 * PREVIOUS sprite's __jo_sprite_def width*height. If a cold-fill shrank a slot's
 * width/height to content, the next cold-fill would place its sprite into this slot's
 * box tail (overlap) -- and a later eviction restaging a LARGER content into this slot
 * would corrupt the neighbour. Reserving EVERY slot at the BOX footprint up front
 * (contiguously, all jo_sprite_add calls done before any restage) makes the allocator
 * never run again for the buckets, so freely mutating each slot's width/height/size to
 * content per (re)stage is safe -- the reserved box region (boxw*boxh) always holds the
 * content (content fits its bucket). This is jo-pool-stale-core-o-gotcha-clean (no
 * post-init jo_sprite_add) and #189-clean (__jo_sprite_id is bounded by the 22 slots).
 * TITLE flavor only; the GHZ/Logos p6_vdp1.o is byte-identical (this block is #if'd). */
typedef struct { P6Vdp1Slot *slots; int n; int bw, bh; } P6Bucket;
static P6Bucket s_buckets[P6_NB];
static int s_buckets_prealloc = 0;
/* Defined below the s_stage/s_fetch cart-buffer declarations (they DMA through s_stage):
 *   p6_title_alloc_box     -- reserve one box-sized jo sprite (permanent slot VRAM).
 *   p6_title_restage_content -- DMA content packed at content-width stride + set the
 *                              slot's __jo_sprite_def CMDSIZE to content (the fill fix). */
static int p6_title_alloc_box(int bw, int bh);
static int p6_title_restage_content(int jo_id, const unsigned char *srcPx,
                                    int srcStride, int w, int h);
#endif
/* Task #241: monotonic LRU clock; the slot with the smallest lastUse is the
 * eviction victim when the cache is full and a new rect misses. */
static int s_useclock = 0;

/* CP4c BLUE-SCREEN FIX: in the FRONT-END flavor the 192x96 box makes these two
 * buffers 18,432 B each (36 KB total) -- relocate them to a VERIFIED-FREE cart
 * window so .bss (and thus _end vs ANIMPAK) is unchanged. 0x226E0000 is past the
 * camera-local pool / DORM / editableVarList cart structures (highest is
 * 0x226D4000) and well before the GFS windows (0x22700000); the FRONT-END build
 * never loads GHZ so those pool structures are inert anyway, but this address is
 * disjoint regardless. Cache-through alias (0x226E....) -- written by the SH-2,
 * read by jo's DMA into VDP1 VRAM; no coherency purge needed (the existing GHZ
 * staging copies have the same producer/consumer and rely on the same property).
 * The DEFAULT (GHZ) build keeps the original 64x64 .bss arrays (byte-identical). */
#if defined(P6_FRONTEND_TITLE)
/* CP5b.1 (Task #268): the 192x160 TITLE box made each buffer 30,720 B at 0x226E0000.
 * CP5b.2 (Task #269): the 248x160 box makes each buffer P6_SPR_MAXW*P6_SPR_MAXH =
 * 248*160 = 39,680 B (0x9B00); 79,360 B total (8bpp, 1 byte/px -- these are PADDED
 * paletted upload copies, NOT 2 bytes/px). KEPT at the CP5b.1 0x226E0000 window:
 * TSONIC.SHT is staged BANDED but NOT made resident (the 1024-wide MakeResident
 * boundary-case HANGS -- see p6_io_main.cpp's TSONIC stage block), so the cart
 * resident-sheet store stays at its 11-sheet high-water ~0x225E8000, well BELOW these
 * buffers; and the front-end band store is relocated DOWN to 0x22720000 (SaturnSheet.
 * cpp), so 0x226E0000 is clear of it too. s_stage @ 0x226E0000 (0xA000 reserved,
 * covers 39,680), s_fetch @ 0x226EA000 (0xA000) -> ends 0x226F4000, below the GFS
 * windows (0x22700000) and the resident-store END (also 0x22700000) -- disjoint.
 * Cache-through alias (written by SH-2, DMA'd into VDP1 VRAM by jo; same producer/
 * consumer property as the GHZ staging copies -> no coherency purge). */
static unsigned char *const s_stage = (unsigned char *)0x226E0000u; /* 39,680 B */
static unsigned char *const s_fetch = (unsigned char *)0x226EA000u; /* 39,680 B */
#elif defined(P6_FRONTEND_LOGOS)
static unsigned char *const s_stage = (unsigned char *)0x226E0000u; /* 18,432 B */
static unsigned char *const s_fetch = (unsigned char *)0x226E4800u; /* 18,432 B */
#else
static unsigned char s_stage[P6_SPR_MAXW * P6_SPR_MAXH]; /* padded upload copy */
static unsigned char s_fetch[P6_SPR_MAXW * P6_SPR_MAXH]; /* banded-miss fetch */
#endif

#if defined(P6_FRONTEND_TITLE)
/* CP5b.7 content-size (#277): allocate one box-sized jo sprite from a zeroed staging
 * buffer; returns the jo id (the slot's PERMANENT VRAM reservation) or -1. Called only
 * by p6_title_ensure_prealloc -- after prealloc the buckets never jo_sprite_add again,
 * so the append-only allocator (sprites.c:74) runs a fixed 22 times total. */
static int p6_title_alloc_box(int bw, int bh)
{
    jo_img_8bits img;
    int n = bw * bh, i;
    for (i = 0; i < n; ++i) s_stage[i] = 0;   /* transparent box */
    img.width = bw; img.height = bh; img.data = s_stage;
    return jo_sprite_add_8bits_image(&img);
}

/* Restage a slot's jo sprite to draw at CONTENT size: pack the content rows at content-
 * width (mult-8) STRIDE into s_stage, DMA into the slot's reserved VRAM, and set the
 * slot's __jo_sprite_def {width=Hsize, height=Vsize, size=HVsize} to the content TEXDEF.
 * The slot's adr (CGadr, VRAM base) is unchanged -- the box reservation (boxw*boxh) holds
 * the smaller content (content fits its bucket). VDP1 then rasterises ONLY content rows/
 * cols (the 39% box padding is gone). Returns the mult-8 padded width (the drawn width). */
static int p6_title_restage_content(int jo_id, const unsigned char *srcPx,
                                    int srcStride, int w, int h)
{
    int pw = (w + 7) & ~7;          /* content width padded to the VDP1 mult-8 unit */
    int x, y;
#if defined(P6_FRONTEND_MENU)
    unsigned short _rt0 = p6_perf_frt_get(); /* #324 restage FRT bracket */
    ++p6_w_rst_calls;
#endif
    /* #324 (front-end DrawLists hog): SYNCHRONOUS long-word pack STRAIGHT INTO the
     * slot's reserved VDP1 VRAM -- replaces the slDMAWait + s_stage byte-pack +
     * async jo_dma_copy round trip. MEASURED (live chain profile _drawprof.jsonl,
     * 2026-07-09): the AIZ claw beats run ~11.6 restages/frame and the draw()
     * callback FRT bracket (p6_w_draw_cb) reads 36k-66k ticks (43-78 ms @ cks=1);
     * GHZCutscene reads 135k (161 ms) at 17 restages/frame. p6_w_rst_wait_v == 0
     * across the whole chain, so the cost was NOT slDMAWait -- it was the
     * CART->CART byte pack (s_stage lives in the A-bus extended-RAM cart in the
     * front-end flavors: every content byte = 1 wait-stated cart read + 1 cart
     * write, then the DMA re-reads it a third time). The direct pack:
     *   - is doc-legal: "Byte access and word access are both possible from the
     *     CPU" on VDP1 VRAM, and CPU access has priority over drawing
     *     (VDP1 manual / ST-013-R3, VRAM section: lines re "byte access (VRAM)"
     *     p.19); a longword store = two word accesses on the B-bus.
     *   - quarters the cart reads (aligned u32 shift-merge loads) and removes
     *     the cart writes + the DMA's second read pass entirely.
     *   - structurally retires the #311/#312 mid-frame async-DMA tear class for
     *     this path: no shared staging buffer, no in-flight transfer to wait out
     *     (the slDMAWait is gone WITH its hazard, not despite it).
     * dst rows are long-aligned: CGadr*8 is 8-byte aligned (jo TEXDEF) and pw is
     * mult-8. SH-2 is big-endian so a u32 load/store preserves the 4-pixel byte
     * order. TITLE/chain flavor only -- plain GHZ (p6_pool_for) byte-identical. */
    {
        unsigned int dstA = (unsigned int)JO_VDP1_VRAM
                          + (unsigned int)JO_MULT_BY_8(__jo_sprite_def[jo_id].adr);
        for (y = 0; y < h; ++y) {
            volatile unsigned int *dst =
                (volatile unsigned int *)(dstA + (unsigned int)(y * pw));
            const unsigned char *src = srcPx + (unsigned int)(y * srcStride);
            unsigned int al = (unsigned int)src & 3u;
            int nfull = w >> 2;              /* whole content longwords this row */
            if (al == 0) {
                const unsigned int *s32 = (const unsigned int *)src;
                for (x = 0; x < nfull; ++x) dst[x] = s32[x];
            }
            else {
                /* shift-merge: aligned u32 loads at an arbitrary src offset. May
                 * read up to 3 bytes past the row's content end -- always inside
                 * the sheet allocation (rows continue; the resident store / the
                 * 39,680 B s_fetch buffer pad the final row), and cart reads have
                 * no side effects. Big-endian merge: (prev<<sh)|(next>>(32-sh)). */
                const unsigned int *s32 = (const unsigned int *)(src - al);
                unsigned int sh = al << 3, ish = 32u - sh;
                unsigned int prev = s32[0];
                for (x = 0; x < nfull; ++x) {
                    unsigned int nxt = s32[x + 1];
                    dst[x] = (prev << sh) | (nxt >> ish);
                    prev = nxt;
                }
            }
            x = nfull << 2;
            if (x < w) {                     /* 1..3 tail content bytes + zero pad */
                unsigned int v = 0; int k;
                for (k = 0; x + k < w; ++k)
                    v |= (unsigned int)src[x + k] << (24 - (k << 3));
                dst[nfull] = v;
                x = (nfull + 1) << 2;
            }
            for (; x < pw; x += 4)           /* mult-8 right pad transparent */
                dst[x >> 2] = 0;
        }
    }
    __jo_sprite_def[jo_id].width  = (unsigned short)pw;
    __jo_sprite_def[jo_id].height = (unsigned short)h;
    /* HVsize TEXDEF (== jo __internal_jo_sprite_add:212 / SGL ST-238-R1 TEXDEF macro):
     * the hardware CMDSIZE VDP1 rasterises. pw is mult-8 so (pw & 0x1f8) == pw (pw<=504). */
    __jo_sprite_def[jo_id].size   = (unsigned short)(JO_MULT_BY_32(pw & 0x1f8) | (h & 0xff));
#if defined(P6_FRONTEND_MENU)
    p6_w_rst_cyc += (int)(unsigned short)(p6_perf_frt_get() - _rt0);
#endif
    return pw;
}

/* =============================================================================
 * TASK 2 (this session): RSDK::FillScreen on Saturn -- the title INTRO fade/flash.
 *
 * The decomp TitleSetup_Draw_FadeBlack calls RSDK.FillScreen(0x000000, timer,
 * timer-128, timer-256) (black fade-in, timer 1024->0) and Draw_Flash calls
 * RSDK.FillScreen(0xF0F0F0, timer, timer-128, timer-256) (white flash, timer
 * 0x300->0). The PC FillScreen (Drawing.cpp:586) does a per-channel alpha blend
 * of `color` over the software framebuffer -- which on Saturn is the 1-element
 * frameBuffer[1] stub (Drawing.hpp:118) = a NO-OP, so the title pops in with no
 * intro. This is the Saturn implementation: a FULL-SCREEN VDP1 sprite of the
 * fill colour composited ON TOP of the title sprites + VDP2 backdrop.
 *
 * MECHANISM (ST-013-R3 VDP1 + jo SGL sprite path):
 *  - A 16x16 SOLID-colour RGB555 sprite (jo_sprite_add COL_32K; MSB=0x8000 set
 *    so every texel is opaque-visible) is allocated ONCE per colour (black +
 *    white cover both decomp callers). jo's append-only allocator (sprites.c:74)
 *    runs these 2 jo_sprite_add calls at most once each (#189-safe).
 *  - Drawn via jo_sprite_draw3D at Z=450 (the SAME Z the title content draws at,
 *    p6_title_blit:908 -> ~1:1 screen scale) with UNIFORM scale 20.0 -> 320x320,
 *    centred at (0,0) -> covers the whole 320x240 frame. Uniform scale takes the
 *    slDispSprite path (sprites.c:447), avoiding the slDispSpriteHV null-angle bug
 *    (sprites.c:444-445). FillScreen is called from TitleSetup's Draw (drawGroup
 *    12 = the LAST drawGroup), so this command is appended AFTER every other title
 *    sprite -> VDP1 painter's order puts it on top regardless of Z.
 *  - OPACITY ramp from the decomp's timer-derived alphas (CLAMP 0..255 each, the
 *    Drawing.cpp:588-590 clamp). avg = (aR+aG+aB)/3:
 *      avg >= 170 -> OPAQUE (CL_Replace, default) = solid black / solid white
 *                    (the fade-in start frames + the flash peak = the gate's
 *                    "near-black early frames" + "bright flash frame").
 *      avg in [1,170) -> HALF-TRANSPARENT (VDP1 CL_Half via
 *                    jo_sprite_enable_half_transparency, SL_DEF.H:193) = the fade
 *                    transition (50% blend toward the revealing content).
 *      sum <= 0 -> draw NOTHING (fully revealed -> the logo shows through).
 *    VDP1 half-transparency is a fixed 50% blend (it cannot do arbitrary per-
 *    channel alpha), so this is a faithful 3-level quantisation of the PC ramp --
 *    opaque -> 50% -> clear -- which is exactly what the intro pixel-gate asserts
 *    (black -> fade -> reveal, with a white flash before the logo).
 *
 * FRONT-END-ONLY (the whole helper is #if defined(P6_FRONTEND_TITLE)); the GHZ
 * flavor's FillScreen stays the p6_stubs.cpp no-op (it has no intro). The two
 * 16x16 sprites cost 2*512 B of VDP1 VRAM, allocated lazily on first fade frame.
 * ========================================================================== */
static int            s_fillSprBlack = -2; /* -2 = not yet attempted */
static int            s_fillSprWhite = -2;
static int            s_fillSprColor = -2; /* M1b: the menu backdrop solid-colour sprite */
static unsigned short s_fillColorKey = 0;  /* last RGB555 the colour sprite was filled with */
static unsigned short s_fillPx[16 * 16];
/* M1b backdrop diag: count fill draws by class + latch the colour sprite's id/key, so a
 * savestate can localise why the menu backdrop is black (colour-branch reached? sprite
 * alloc ok? what colour?). FRONT-END only (the whole helper is P6_FRONTEND_TITLE). */
__attribute__((used)) int p6_w_fill_calls   = 0; /* total p6_fillscreen_saturn calls */
__attribute__((used)) int p6_w_fill_color   = 0; /* calls that took the "other colour" (menu bg) branch */
__attribute__((used)) int p6_w_fill_drawn   = 0; /* calls that reached jo_sprite_draw3D */
__attribute__((used)) int p6_w_fill_colorid = -3;/* s_fillSprColor (>=0 == colour sprite alloc'd) */
__attribute__((used)) int p6_w_fill_colorkey = 0;/* last colour RGB555 (e.g. gold) */

static int p6_fill_make(unsigned short rgb555)
{
    jo_img img;
    int i;
    for (i = 0; i < 16 * 16; ++i)
        s_fillPx[i] = rgb555; /* MSB(0x8000) already set by the caller -> opaque */
    img.width  = 16;
    img.height = 16;
    img.data   = s_fillPx;
    return jo_sprite_add(&img); /* COL_32K RGB direct-colour sprite */
}

/* M1b: RGB888 (the decomp color, 0xRRGGBB) -> RGB555 with the MSB visible bit. */
static unsigned short p6_rgb888_to_555(unsigned int c)
{
    unsigned int r5 = ((c >> 16) & 0xFF) >> 3;
    unsigned int g5 = ((c >> 8) & 0xFF) >> 3;
    unsigned int b5 = (c & 0xFF) >> 3;
    return (unsigned short)(0x8000 | (b5 << 10) | (g5 << 5) | r5);
}

#if defined(P6_DIRECT_VDP1)
/* #316 F1: forward decl -- the direct-list emitters are defined below the fill. */
static void p6_dl_poly(unsigned short rgb555, int x0, int y0, int x1, int y1,
                       int x2, int y2, int x3, int y3, int half);
#endif
__attribute__((used)) void p6_fillscreen_saturn(unsigned int color, int aR, int aG, int aB)
{
    int sum, avg, spr;
    unsigned int rb = (color >> 16) & 0xFF, gb = (color >> 8) & 0xFF, bb = color & 0xFF;
    int isBlack = (rb == 0 && gb == 0 && bb == 0);
    int isWhite = (rb >= 0xE0 && gb >= 0xE0 && bb >= 0xE0); /* Flash 0xF0F0F0 / near-white */

    ++p6_w_fill_calls;
    /* #313 A/B-3 note (2026-07-02): suppressing THIS draw entirely did NOT stop
     * the flash-end die-off (still 0 sprite cmds at deep frames) -- the scaled
     * fill quad is exonerated as the trigger. Recovery lives in p6_io_main.cpp
     * (slInitSprite re-arm on detected dead pipeline). */
    if (aR < 0) aR = 0; else if (aR > 255) aR = 255;
    if (aG < 0) aG = 0; else if (aG > 255) aG = 255;
    if (aB < 0) aB = 0; else if (aB > 255) aB = 255;
    sum = aR + aG + aB;
    if (sum < 3)
        return; /* avg alpha rounds to 0 -> invisible on PC (per-channel /256 blend),
                 * so draw NOTHING. MEASURED (#310, build 9): the old `sum <= 0` gate
                 * let FXRuby's pinned fadeBlack=1 (alphas 1,0,0) emit a full-screen
                 * HALF-TRANSPARENT black quad EVERY frame (VDP1 cmd list slot 27,
                 * 320x320 SCAL HALF) whose texels persisted as 0x8000 in the
                 * framebuffer -> the sprite layer rendered OPAQUE BLACK over the
                 * upper display = the "black sky" that survived 9 builds of correct
                 * VDP2 state. On PC those alphas change one channel by 1/256 =
                 * invisible; sum<3 (avg==0) reproduces that exactly and leaves
                 * every real fade ramp (sum >= 3) untouched. */

    /* COLOUR SELECT (3 cases):
     *  - black (TitleSetup/MenuSetup FadeBlack 0x000000) -> the lazy black sprite.
     *  - near-white (TitleSetup Flash 0xF0F0F0) -> the lazy white sprite.
     *  - ANY OTHER colour (M1b: UIBackground_DrawNormal's FillScreen(bgColor) -- the
     *    Mania main-menu GOLD 0xF0C800, etc.) -> a single re-tintable colour sprite,
     *    re-filled in place via jo_sprite_replace only when the colour changes (the menu
     *    backdrop colour is constant per menu, so this re-fills ~once). This makes the
     *    UIBackground backdrop render its real colour instead of being thresholded black. */
    if (isBlack) {
        if (s_fillSprBlack == -2)
            s_fillSprBlack = p6_fill_make((unsigned short)(0x8000 | 0x0000));
        spr = s_fillSprBlack;
    }
    else if (isWhite) {
        if (s_fillSprWhite == -2)
            s_fillSprWhite = p6_fill_make((unsigned short)(0x8000 | 0x7FFF));
        spr = s_fillSprWhite;
    }
    else {
        unsigned short key = p6_rgb888_to_555(color);
        ++p6_w_fill_color;
        if (s_fillSprColor == -2) {
            s_fillSprColor = p6_fill_make(key);   /* first colour fill (alloc once) */
            s_fillColorKey = key;
        }
        else if (key != s_fillColorKey && s_fillSprColor >= 0) {
            jo_img img; int i;                    /* re-tint in place (no new alloc) */
            for (i = 0; i < 16 * 16; ++i) s_fillPx[i] = key;
            img.width = 16; img.height = 16; img.data = s_fillPx;
            jo_sprite_replace(&img, s_fillSprColor);
            s_fillColorKey = key;
        }
        spr = s_fillSprColor;
        p6_w_fill_colorid  = s_fillSprColor;
        p6_w_fill_colorkey = (int)key;
    }
    if (spr < 0)
        return; /* alloc failed (VRAM full) -> skip rather than draw garbage */

    avg = sum / 3;
#if defined(P6_DIRECT_VDP1)
    /* #316 F1: the fill is a Comm=4 flat polygon in the direct list (exact,
     * no scale-20 hack). Painter order replaces the Z logic below: the menu
     * backdrop (drawGroup 0) emits FIRST = behind; the black/white fades
     * (last drawGroup) emit LAST = on top -- the decomp's own draw order. */
    {
        unsigned short c = isBlack ? 0x8000
                         : (isWhite ? 0xFFFF : p6_rgb888_to_555(color));
        p6_dl_poly(c, -160, -120, 159, -120, 159, 103, -160, 103, (avg < 170));
        ++p6_w_fill_drawn;
        return;
    }
#endif
    /* 16x16 sprite * uniform 20.0 = 320x320 -> covers the 320x240 frame, centred.
     * Z-DEPTH: the black/white intro fades (TitleSetup/MenuSetup Draw, the LAST
     * drawGroup) must composite ON TOP of the content -> Z=450 (the content Z), where
     * SGL's Z-sort + painter-order put the last-submitted command on top. The MENU
     * BACKDROP (the "other colour" branch, UIBackground drawGroup 0 = FIRST) must sit
     * BEHIND the rows -> Z=460 (farther), so SGL draws it before the Z=450 row sprites.
     * (UIBackground also draws drawGroup 0 first, so painter-order agrees.) */
    jo_sprite_change_sprite_scale(20.0f);
    if (avg < 170)
        jo_sprite_enable_half_transparency(); /* mid-fade: VDP1 CL_Half 50% blend */
    /* avg >= 170 -> opaque (CL_Replace, the default). */
    jo_sprite_draw3D(spr, 0, 0, (isBlack || isWhite) ? 450 : 460);
    jo_sprite_disable_half_transparency();
    jo_sprite_restore_sprite_scale();
    ++p6_w_fill_drawn;
}
#endif

#if defined(P6_DIRECT_VDP1)
/* =============================================================================
 * #316 F1 -- DIRECT VDP1 COMMAND LIST (contract step 1; design + probe results
 * in the direct-vdp1-command-list-design memory + docs/feature_checklists/
 * frontend_full_chain_parity.md). Replaces jo_sprite_draw3D/slDispSprite for
 * EVERY front-end sprite: the SGL slave mailbox/plan machinery carries zero
 * sprites, so the measured transfer tear (title 73% torn post-settle, landing
 * 60-75% empty plans) has nothing left to break.
 *
 * MEASURED FOUNDATION: SGL's transferred preamble chains 0x00 sysclip -> 0x20
 * userclip -> 0x40 localcoord(160,120) -> 0x60 END (probe dump, byte-exact).
 * The vblank trampoline (p6_vdp2.c) rewrites 0x60 each vblank as a duplicate
 * localcoord + JP=assign -> p6_dl_link (probe: renders through 1,652 vblanks
 * including every deep-title frame where the SGL pipeline was dead).
 *
 * Command format per ST-013-R3 sec 6 (vdp1-reference.md): 32-byte commands;
 * CMDCTRL Comm=0 normal sprite / 4 polygon, Dir bits 4-5 = H/V flip, END
 * bit15; CMDPMOD 0x00A0 = ECD + 256-color-bank mode (the byte-identical value
 * the SGL slave used to build, per the #313 SpriteBuf dumps); +0x0003 =
 * CL_Half for fades/ink; CMDCOLR = color bank (palblock*256, matching jo
 * colno semantics incl. the per-Heavy blocks 2-6 + player block 7); CMDSRCA/
 * CMDSIZE copied VERBATIM from __jo_sprite_def[jid].adr/.size -- the content-
 * size restage already maintains the exact TEXDEF, so vertex A is simply the
 * RSDK top-left minus the localcoord origin, flipped or not (HF/VF mirror
 * within the content bbox, which IS the drawn rect).
 *
 * Double buffer: two 62-command halves at VDP1 VRAM 0x2000/0x2800 (inside
 * SGL's reserved command area, beyond its empty-plan transfer reach; jo
 * textures live higher). The frame builds the inactive half in decomp
 * ProcessObjectDrawLists order (painter order == the decomp's drawGroup
 * order -- MORE faithful than SGL's Z-sort); p6_dl_end publishes the half to
 * the vblank trampoline. A LOAD-frame return skips begin/end -> the last
 * completed half keeps displaying (same persistence contract as before). */
#define P6_DL_A    0x2000u
#define P6_DL_B    0x2800u
#define P6_DL_MAX  62
volatile unsigned int p6_dl_link = 0;  /* (half base)>>3 for the vblank trampoline; 0 until first end */
static int s_dl_half = 0;              /* half being BUILT */
static int s_dl_n    = 0;
static int s_dl_ink  = 0;              /* sticky CL_Half (p6_vdp1_set_ink) */
__attribute__((used)) int p6_w_dl_cmds_max = 0; /* per-frame max commands */
__attribute__((used)) int p6_w_dl_drops    = 0; /* emits past P6_DL_MAX */
__attribute__((used)) int p6_w_dl_frames   = 0; /* completed lists */

static volatile unsigned short *p6_dl_next(void)
{
    unsigned int base = 0x25C00000u + (s_dl_half ? P6_DL_B : P6_DL_A);
    return (volatile unsigned short *)(base + (unsigned int)s_dl_n * 32u);
}

void p6_dl_begin(void)
{
    s_dl_n = 0;
}

static void p6_dl_sprite(int jid, int x, int y, int flipX, int flipY, int palblk)
{
    volatile unsigned short *p;
    if (s_dl_n >= P6_DL_MAX) { ++p6_w_dl_drops; return; }
    p = p6_dl_next();
    p[0]  = (unsigned short)((flipX ? 0x0010u : 0) | (flipY ? 0x0020u : 0)); /* Comm=0 + Dir */
    p[1]  = 0;                                                   /* CMDLINK (JP=next) */
    p[2]  = (unsigned short)(0x00A0u | (s_dl_ink ? 0x0003u : 0)); /* PMOD */
    p[3]  = (unsigned short)(palblk << 8);                       /* CMDCOLR = bank */
    p[4]  = __jo_sprite_def[jid].adr;                            /* CMDSRCA */
    p[5]  = __jo_sprite_def[jid].size;                           /* CMDSIZE */
    p[6]  = (unsigned short)(short)(x - 160);                    /* XA */
    p[7]  = (unsigned short)(short)(y - 120);                    /* YA */
    p[8] = p[9] = p[10] = p[11] = p[12] = p[13] = p[14] = 0;
#if defined(P6_GHZCUT_BOOT)
    /* GL1 diag: capture the emitted CMDCOLR|CMDSRCA|CMDPMOD for a glyph draw
     * (palblk 2 == TCH_GLYPH_PALBLK) so the actual block/addr is measured. */
    if (palblk == 2) {
        extern int p6_w_tcg_cmd;
        p6_w_tcg_cmd = ((int)p[3] << 16) | (int)p[4];
    }
#endif
    ++s_dl_n;
}

/* Fix 1 (user-symptom-map-v2: "Sonic invisible on slopes") -- ROTATED sprite
 * emit: a VDP1 DISTORTED SPRITE command (CMDCTRL Comm=0010B = 0x0002,
 * ST-013-R3 sec 7.6 pp.124-125). The character pattern is sampled across the
 * arbitrary quad A..D: A=upper-left, B=upper-right, C=lower-right,
 * D=lower-left (doc-verbatim vertex order). Same PMOD/COLR/SRCA/SIZE as
 * p6_dl_sprite -- the content-size restage already maintains the exact TEXDEF,
 * so the quad just places the four PIVOT-RELATIVE corners of the content rect
 * rotated by the RSDK angle around the entity position.
 *
 * ROTATION MATH (decomp DrawSpriteRotozoom at identity scale 0x200,
 * Drawing.cpp:3515-3588): the software rasterizer maps screen->texture with
 * angle = 0x200 - rotation (:3541), i.e. the FORWARD corner transform is the
 * plain clockwise (y-down) rotation by `rotation`:
 *     dx' = (dx*cos - dy*sin) >> 9      (Sin512/Cos512 are <<9 fixed point)
 *     dy' = (dx*sin + dy*cos) >> 9
 * (derived: posX[0] = x + (cos(-r)*px + sin(-r)*py)>>9 = x + (px*cos r -
 * py*sin r)>>9, Drawing.cpp:3564-3571.) sn/cs arrive AS VALUES from the C++
 * caller (p6_io_main.cpp computes RSDK::Sin512/Cos512(rotation), Math.hpp:72-73
 * over the baked P6.1-fast tables, map symbol RSDK::sin512LookupTable) because
 * this C TU cannot name the C++-mangled table without a brittle asm alias.
 *
 * FLIP_X (decomp FX_ROTATE|FX_FLIP arm, Drawing.cpp:2820-2823, direction
 * masked to FLIP_X only) mirrors the pivot-relative X extents BEFORE rotation
 * (Drawing.cpp:3575-3583: extents become [-pivotX-width, -pivotX]) and sets
 * the VDP1 Dir HF read-direction bit (CMDCTRL bit4). Doc sec 7.6: "inversion
 * ... by the specification of the read direction" -- with HF the character
 * columns read reversed inside the quad, so the mult-8 pad columns (content
 * width padded to pw) mirror to the quad's LEFT edge and the CONTENT lands
 * exactly on [-pivotX-w, -pivotX], matching the RSDK formula; hence the quad
 * spans [dxl, dxl+pw] with dxl = -pivotX - pw.
 *
 * Clipping: no p6_box_in_stride cull here -- the distorted command is bounded
 * by the preamble's system-clip command + VDP1 pre-clipping (sec 7.6 "Set
 * pre-clipping ... in consideration of the clipping area"), and slope draws
 * are on-screen gameplay sprites. ph is the content height (no pad). */
static void p6_dl_sprite_rot(int jid, int x, int y, int pw, int ph,
                             int pivotX, int pivotY, int sn, int cs,
                             int flipX, int palblk)
{
    volatile unsigned short *p;
    int dxl, dxr, dyt, dyb;
    if (s_dl_n >= P6_DL_MAX) { ++p6_w_dl_drops; return; }
    p = p6_dl_next();
    dxl = flipX ? (-pivotX - pw) : pivotX;   /* mirror X extents BEFORE rot */
    dxr = dxl + pw;
    dyt = pivotY;
    dyb = pivotY + ph;
    p[0]  = (unsigned short)(0x0002u | (flipX ? 0x0010u : 0)); /* Comm=2 + Dir HF */
    p[1]  = 0;                                                 /* CMDLINK (JP=next) */
    p[2]  = (unsigned short)(0x00A0u | (s_dl_ink ? 0x0003u : 0)); /* PMOD (== p6_dl_sprite) */
    p[3]  = (unsigned short)(palblk << 8);                     /* CMDCOLR = bank */
    p[4]  = __jo_sprite_def[jid].adr;                          /* CMDSRCA */
    p[5]  = __jo_sprite_def[jid].size;                         /* CMDSIZE */
    /* Vertices A..D (sec 7.6 order: UL, UR, LR, LL), localcoord origin (160,120). */
    p[6]  = (unsigned short)(short)(x - 160 + ((dxl * cs - dyt * sn) >> 9)); /* XA */
    p[7]  = (unsigned short)(short)(y - 120 + ((dxl * sn + dyt * cs) >> 9)); /* YA */
    p[8]  = (unsigned short)(short)(x - 160 + ((dxr * cs - dyt * sn) >> 9)); /* XB */
    p[9]  = (unsigned short)(short)(y - 120 + ((dxr * sn + dyt * cs) >> 9)); /* YB */
    p[10] = (unsigned short)(short)(x - 160 + ((dxr * cs - dyb * sn) >> 9)); /* XC */
    p[11] = (unsigned short)(short)(y - 120 + ((dxr * sn + dyb * cs) >> 9)); /* YC */
    p[12] = (unsigned short)(short)(x - 160 + ((dxl * cs - dyb * sn) >> 9)); /* XD */
    p[13] = (unsigned short)(short)(y - 120 + ((dxl * sn + dyb * cs) >> 9)); /* YD */
    p[14] = 0;                                                 /* CMDGRDA unused */
    ++s_dl_n;
}

static void p6_dl_poly(unsigned short rgb555, int x0, int y0, int x1, int y1,
                       int x2, int y2, int x3, int y3, int half)
{
    volatile unsigned short *p;
    if (s_dl_n >= P6_DL_MAX) { ++p6_w_dl_drops; return; }
    p = p6_dl_next();
    p[0]  = 0x0004;                                              /* Comm=4 polygon */
    p[1]  = 0;
    p[2]  = (unsigned short)(0x00C0u | (half ? 0x0003u : 0));    /* ECD|SPD (+CL_Half) */
    p[3]  = rgb555;                                              /* flat RGB (MSB set) */
    p[4]  = 0; p[5] = 0;
    p[6]  = (unsigned short)(short)x0; p[7]  = (unsigned short)(short)y0;
    p[8]  = (unsigned short)(short)x1; p[9]  = (unsigned short)(short)y1;
    p[10] = (unsigned short)(short)x2; p[11] = (unsigned short)(short)y2;
    p[12] = (unsigned short)(short)x3; p[13] = (unsigned short)(short)y3;
    p[14] = 0;
    ++s_dl_n;
}

void p6_dl_end(void)
{
    volatile unsigned short *p = p6_dl_next();
    p[0] = 0x8000;                                               /* END */
    if (s_dl_n > p6_w_dl_cmds_max) p6_w_dl_cmds_max = s_dl_n;
    p6_dl_link = (unsigned int)((s_dl_half ? P6_DL_B : P6_DL_A) >> 3);
    s_dl_half ^= 1;
    ++p6_w_dl_frames;
}

/* SEGMENT A (#318 visual-arc, Logos pink-band): opaque full-screen black backfill,
 * emitted FIRST in the front-end direct list (right after p6_dl_begin) so the stale
 * VDP1 framebuffer bottom rows -- the Logos pink noise band (#272), left where the
 * 320x224 display geometry differs from the 320x240 fill/erase the front-end assumes
 * -- are painted over BEHIND every UI sprite (VDP1 is painter-order: first command =
 * drawn behind). Oversized quad (-176..176, -136..136 in the direct list's centred
 * VDP1 coords) covers the whole framebuffer + overscan. The caller GATES this to the
 * Logos scene: Title/Menu/AIZ/GHZCutscene composite VDP2 backdrops that an opaque
 * VDP1 quad would occlude. p6_dl_poly is file-static; this is the cross-TU entry.
 * Front-end only (this whole block is #if P6_DIRECT_VDP1). */
void p6_dl_backfill(unsigned short rgb555)
{
    p6_dl_poly(rgb555, -176, -136, 176, -136, 176, 136, -176, 136, 0);
}

/* =============================================================================
 * GL1 (Task, 2026-07-06): TitleCard direct-list primitives -- the chain-native
 * render path for the GHZ-landing act card. The chain runs P6_DIRECT_VDP1, so
 * the ported src/mania TitleCard's SGL draws (slPutPolygon / jo_sprite_draw3D)
 * would emit into the DEAD SGL command buffer (the vblank trampoline redirects
 * VDP1 to the direct list at 0x2000/0x2800). The pack RSDK DrawRectangle is a
 * no-op and DrawFace crashes the pack's first frame (p6_pack_stubs.cpp:52,67).
 * So the card draws through these cross-TU wrappers over the file-static
 * p6_dl_poly, mirroring the p6_dl_backfill seam.
 *
 * Coordinate contract: the caller passes RSDK screen-relative PIXELS (origin
 * top-left 0,0; the decomp TitleCard verts are already screen-relative). The
 * direct list's localcoord origin is (160,120) (p6_dl_poly emits XA=x, the
 * preamble sets localcoord 160,120 per the #316 F1 note), so a screen pixel
 * (px,py) maps to command coord (px-160, py-120). rgb are 8-bit; convert to
 * VDP1 direct RGB555 with the opaque MSB (same as p6_plate_rgb555 / the poly
 * flat-colour field p[3]). half=0 (opaque replace; the card is drawn over a
 * suppressed-gameplay overlay so no blend needed). p6_dl_poly clamps to
 * P6_DL_MAX and counts drops, same as every other emit. */
static unsigned short p6_dl_rgb555_8(int r8, int g8, int b8)
{
    unsigned int r5 = ((unsigned)r8 & 0xFF) >> 3;
    unsigned int g5 = ((unsigned)g8 & 0xFF) >> 3;
    unsigned int b5 = ((unsigned)b8 & 0xFF) >> 3;
    return (unsigned short)(0x8000 | (b5 << 10) | (g5 << 5) | r5);
}

/* Flat-colour 4-vertex polygon at screen-relative pixels. verts are 4 (x,y)
 * pairs in the array order the decomp uses (0-1-2-3). */
void p6_dl_face(const int *px, const int *py, int r8, int g8, int b8)
{
    unsigned short c = p6_dl_rgb555_8(r8, g8, b8);
    p6_dl_poly(c,
               px[0] - 160, py[0] - 120,
               px[1] - 160, py[1] - 120,
               px[2] - 160, py[2] - 120,
               px[3] - 160, py[3] - 120,
               0);
}

/* Axis-aligned filled rect (x,y,w,h) at screen-relative pixels. */
void p6_dl_rect(int x, int y, int w, int h, int r8, int g8, int b8)
{
    int px[4], py[4];
    px[0] = x;     py[0] = y;
    px[1] = x + w; py[1] = y;
    px[2] = x + w; py[2] = y + h;
    px[3] = x;     py[3] = y + h;
    p6_dl_face(px, py, r8, g8, b8);
}

/* GL1 (2026-07-06): draw a SHEET RECT as a VDP1 SPRITE into the open direct list,
 * with a CALLER-SUPPLIED CRAM block. This is the glyph-blit path for the TitleCard
 * zone-name letters (Global/Display.gif rects from the already-staged DISPLAY.SHT):
 * unlike p6_vdp1_blit (which forces P6_BLIT_PALBLOCK = block 1, the GHZ object bank)
 * the glyphs need block 2 (colno 512, where p6_vdp2_titlecard_pal_upload put the
 * Display GCT). Same fetch+emit as p6_vdp1_blit: p6_slot_for does the SaturnSheet
 * FetchRect + LRU and returns a jo sprite id; p6_dl_sprite emits the VDP1 sprite
 * command with CMDCOLR = palblk<<8. (x,y) is the RSDK screen TOP-LEFT already
 * pivot-composed by the caller (pos + pivot, DrawSpriteFlipped Drawing.cpp:2785).
 * No title-stride cull (GHZCUT is not P6_FRONTEND_TITLE). Cross-TU entry over the
 * file-static p6_slot_for/p6_dl_sprite, mirroring p6_dl_face/p6_dl_rect. */
/* GL1 DEDICATED GLYPH CACHE (2026-07-06): the TitleCard glyphs are STATIC (the
 * same ~16 Display.gif rects every frame). Routing them through the shared 40-slot
 * LRU pool (p6_slot_for) THRASHED: at the GHZ landing the card's 15 glyphs compete
 * with the GHZ scene's animating sprites, so glyphs get EVICTED + re-DMA'd every
 * frame -> the documented mid-frame async-slDMACopy tear (p6_vdp1.c:257-275) ->
 * MEASURED non-deterministic magenta garble (consecutive card frames differ by
 * 16.9M px). Fix: a SEPARATE fixed cache that allocates each distinct glyph ONCE
 * (keyed by sx,sy) and NEVER evicts -> after frame 1 every glyph is resident, no
 * per-frame DMA, no tear. Staged CONTENT-EXACT (w x h, not a 64x64 box) from the
 * RESIDENT DISPLAY.SHT (memcpy fetch, no inflate) so pixel-0 transparency handles
 * the letter background. GHZCUT-only; the cache is tiny (24 * 4KB VRAM worst-case,
 * content-exact is far less). */
#define P6_TCGLYPH_SLOTS 24
static struct { int sx, sy, jo_id; } s_tcglyph[P6_TCGLYPH_SLOTS];
static int s_tcglyph_n = 0;

static int p6_tcglyph_slot(int sheet, int sx, int sy, int w, int h)
{
    int i, x, y;
    const unsigned char *srcPx;
    int srcStride;
    int pw; /* VDP1 8bpp sprite WIDTH must be a multiple of 8 (jo sprites.c:296 +
             * CMDSIZE = width&0x1f8, sprites.c:212). A non-mult-8 glyph (ZONE=26/28,
             * 'I'=6, 'V'=20 ...) DMAs its true-width rows but the VDP1 reads a
             * width&~7 stride -> the rows shift -> diagonal garbage. Pad the staged
             * width UP to the next mult-8, filling the pad columns with index 0
             * (VDP1-transparent) so the DMA stride == the CMDSIZE stride. */
    jo_img_8bits img;
    /* cache hit: this glyph rect was already staged -> reuse (no DMA). */
    for (i = 0; i < s_tcglyph_n; ++i)
        if (s_tcglyph[i].sx == sx && s_tcglyph[i].sy == sy)
            return s_tcglyph[i].jo_id;
    if (s_tcglyph_n >= P6_TCGLYPH_SLOTS)
        return -1;
    pw = (w + 7) & ~7;
    if (pw > P6_SPR_MAXW || h > P6_SPR_MAXH)
        return -1;
    /* fetch the rect (resident memcpy or banded inflate) into s_fetch. */
    if (s_sheets[sheet].px) {
        srcPx     = s_sheets[sheet].px + sy * s_sheets[sheet].w + sx;
        srcStride = s_sheets[sheet].w;
    } else {
        if (!s_fetchFn || !s_fetchFn(s_sheets[sheet].shtSlot, sx, sy, w, h, s_fetch))
            return -1;
        srcPx     = s_fetch;
        srcStride = w;
    }
    /* stage into a PADDED-WIDTH (mult-8) box, content left, index-0 pad right. */
    for (y = 0; y < h; ++y) {
        unsigned char *dst = s_stage + y * pw;
        const unsigned char *src = srcPx + y * srcStride;
        for (x = 0; x < w; ++x)  dst[x] = src[x];
        for (; x < pw; ++x)      dst[x] = 0;   /* transparent pad */
    }
    slDMAWait(); /* let any prior transfer finish before this add's DMA. */
    /* diag: hash the FIRST staged glyph's content -> proves the fetch is real
     * (non-zero, varied) vs all-0/all-255 garbage. + count how many of the pw*h
     * staged bytes are index 255 (white). */
    if (s_tcglyph_n == 0) {
        extern int p6_w_tcg_stagehash; extern int p6_w_tcg_white;
        unsigned int hh = 5381; int k, nwhite = 0, tot = pw * h;
        for (k = 0; k < tot; ++k) { hh = ((hh << 5) + hh) ^ s_stage[k]; if (s_stage[k] == 255) ++nwhite; }
        p6_w_tcg_stagehash = (int)hh;
        p6_w_tcg_white = (nwhite << 16) | (tot & 0xFFFF);
    }
    img.width  = pw;
    img.height = h;
    img.data   = s_stage;
    {
        int id = jo_sprite_add_8bits_image(&img);
        if (id < 0) { ++p6_w_vdp1_joaddfail; return -1; }
        /* jo_sprite_add's jo_dma_copy (= slDMACopy) is ASYNC ("terminates soon
         * after DMA is initiated", ST-238-R1). Multiple glyphs stage into the ONE
         * s_stage on the SAME frame, so WITHOUT a wait the next glyph overwrites
         * s_stage while THIS add's DMA is still reading it -> the sprite VRAM gets
         * the WRONG (later) glyph's bytes (MEASURED: the glyph VRAM read back
         * all-0xFF = a later all-white-ish stage, not the staged letter). Wait for
         * THIS add's transfer to finish before returning (so the caller's next
         * glyph re-stage is safe). Only paid on the one-time cold stage (cache hits
         * skip this whole path). Mirrors the pool's slDMAWait (p6_vdp1.c:1396). */
        slDMAWait();
        s_tcglyph[s_tcglyph_n].sx    = sx;
        s_tcglyph[s_tcglyph_n].sy    = sy;
        s_tcglyph[s_tcglyph_n].jo_id = id;
        return s_tcglyph[s_tcglyph_n++].jo_id;
    }
}

/* GL1 glyph diagnostics (read live to root-cause the glyph render). */
__attribute__((used)) int p6_w_tcg_n       = 0;  /* distinct glyphs staged */
__attribute__((used)) int p6_w_tcg_lastjid = -1; /* jo id of the last glyph drawn */
__attribute__((used)) int p6_w_tcg_lastsz  = 0;  /* __jo_sprite_def[jid].size (CMDSIZE) */
__attribute__((used)) int p6_w_tcg_lastwh  = 0;  /* (w<<16)|h requested */
__attribute__((used)) int p6_w_tcg_stagehash = 0;/* djb2 of the FIRST glyph's stage bytes */
__attribute__((used)) int p6_w_tcg_white     = 0;/* (whiteCount<<16)|totalPx of the FIRST glyph */
__attribute__((used)) int p6_w_tcg_cmd       = 0;/* (CMDCOLR<<16)|CMDSRCA of a glyph draw command */

void p6_dl_glyph(int sheet, int x, int y, int w, int h, int sx, int sy, int palblk)
{
    int jid;
    if (sheet < 0 || sheet >= s_sheet_count) {
        ++p6_w_vdp1_handle_drops;
        p6_w_vdp1_lastdrop_h = sheet;
        return;
    }
    jid = p6_tcglyph_slot(sheet, sx, sy, w, h);
    if (jid < 0)
        return;
    p6_w_tcg_n       = s_tcglyph_n;
    p6_w_tcg_lastjid = jid;
    p6_w_tcg_lastsz  = (int)__jo_sprite_def[jid].size;
    p6_w_tcg_lastwh  = (w << 16) | (h & 0xFFFF);
    ++p6_w_vdp1_landed;
    ++p6_w_vdp1_cmds;
    p6_dl_sprite(jid, x, y, 0, 0, palblk);
}
#endif /* P6_DIRECT_VDP1 */

#if defined(P6_FRONTEND_MENU)
/* =============================================================================
 * M3 (Task #295): RSDK::DrawFace on Saturn -- the menu row PLATES.
 *
 * UIWidgets_DrawParallelogram (decomp UIWidgets.c:202-245) builds 4 screen-RELATIVE
 * verts (it subtracts ScreenInfo->position<<16 first, :233-242) + a flat RGB and
 * calls RSDK.DrawFace(verts, 4, r,g,b, 0xFF, INK_NONE). UIModeButton_Draw (:64,69)
 * calls it twice per row: a near-white (0xF0F0F0) plate offset by -buttonBounce and a
 * black (0x000000) plate offset by +buttonBounce -- the row's light/shadow plate pair.
 * On Saturn DrawFace was a p6_pack_stubs.cpp no-op so the rows floated on the gold
 * backdrop. This is the Saturn implementation: a VDP1 FLAT-COLOUR POLYGON.
 *
 * MECHANISM (ST-013-R3 VDP1 sec 5 polygon command + SGL ST-238-R1 slPutPolygon):
 *  - slPutPolygon (jo pulls SGL via JO_COMPILE_USING_SGL) submits one PDATA (a POINT[4]
 *    vertex table + a 1-entry POLYGON + a 1-entry ATTR) into the VDP1 command list. The
 *    ATTR encodes a flat-shaded (No_Gouraud), direct-colour (CL32KRGB) polygon -- the
 *    VDP1 Comm=0x4 polygon-draw command with colour-calc replace (ST-013-R3 sec 5.5.4).
 *    Verbatim the proven hand-port emitter src/rsdk/drawing.c:_emit_polygon4 (Task #148):
 *    Dual_Plane (double-sided so a screen-facing 2D UI quad never back-face culls --
 *    Single_Plane culled every UI polygon, MEASURED), sort SORT_CEN, the sprPolygon
 *    flag-decomposition into sort/atrb/dir.
 *  - Coords: jo's SGL polygon vertex table is screen-CENTRED FIXED (origin 160,112 for
 *    320x224). The decomp verts arrive screen-relative 16.16, so: px = (vert>>16) - 160,
 *    py = (vert>>16) - 112, then <<16 to FIXED. Same conversion as drawing.c:_fixed_to_
 *    sgl_x/y with screen_relative=true.
 *  - Z-DEPTH: the menu icon/text sprites blit at Z=450 (p6_vdp1_blit_flipped's
 *    jo_sprite_draw3D, p6_vdp1.c:1150/1211); the gold backdrop FillScreen at Z=460. The
 *    plate must sit BEHIND the icon+text but ABOVE the backdrop -> Z=455. SGL draws
 *    larger-Z first (farther), so backdrop(460) then plate(455) then row(450) on top.
 *
 * Cross-TU: the pack DrawFace stub (p6_pack_stubs.cpp) cannot call slPutPolygon (the
 * jo/SGL namespace clash, same as the SaturnSheet_FetchRect path). So this jo-side
 * emitter is reached through a runtime function pointer the pack sets after init
 * (p6_vdp1_set_drawface, mirroring p6_vdp1_set_fetch). MENU flavor only; GHZ/Title/Logos
 * p6_vdp1.o is byte-identical (this whole block is #if'd). */
#define P6_PLATE_Z  455   /* between backdrop(460) and rows(450) */
static POINT   s_plate_pnt[4];
/* Screen-facing +Z unit normal (1.0 FIXED) + vertex order 0-1-2-3; Dual_Plane makes
 * winding irrelevant but a non-degenerate normal is required for SGL sort/cull. */
static POLYGON s_plate_pol[1] = { { { 0, 0, 0x00010000 }, { 0, 1, 2, 3 } } };
static ATTR    s_plate_att[1];
static PDATA   s_plate_pdata  = { s_plate_pnt, 4, s_plate_pol, 1, s_plate_att };

static unsigned short p6_plate_rgb555(int r8, int g8, int b8)
{
    unsigned int r5 = ((unsigned)r8 & 0xFF) >> 3;
    unsigned int g5 = ((unsigned)g8 & 0xFF) >> 3;
    unsigned int b5 = ((unsigned)b8 & 0xFF) >> 3;
    return (unsigned short)(0x8000 | (b5 << 10) | (g5 << 5) | r5); /* MSB=opaque */
}

__attribute__((used)) int p6_w_drawface_calls = 0; /* DrawFace invocations (the plate count) */

/* The emitter the pack DrawFace stub forwards to. verts are screen-relative 16.16
 * (screen px = vert>>16). count is 3 or 4 (triangle dups the last vertex). */
__attribute__((used)) void p6_drawface_saturn(const int *vx, const int *vy, int count,
                                              int r8, int g8, int b8)
{
    int i;
    FIXED fx[4], fy[4];
    if (count < 3 || count > 4) return;
    for (i = 0; i < count; ++i) {
        fx[i] = (FIXED)(((vx[i] >> 16) - (JO_TV_WIDTH_2)) << 16);
        fy[i] = (FIXED)(((vy[i] >> 16) - (JO_TV_HEIGHT_2)) << 16);
    }
    if (count == 3) { fx[3] = fx[2]; fy[3] = fy[2]; } /* degenerate quad */

    s_plate_pnt[0][X] = fx[0]; s_plate_pnt[0][Y] = fy[0]; s_plate_pnt[0][Z] = P6_PLATE_Z << 16;
    s_plate_pnt[1][X] = fx[1]; s_plate_pnt[1][Y] = fy[1]; s_plate_pnt[1][Z] = P6_PLATE_Z << 16;
    s_plate_pnt[2][X] = fx[2]; s_plate_pnt[2][Y] = fy[2]; s_plate_pnt[2][Z] = P6_PLATE_Z << 16;
    s_plate_pnt[3][X] = fx[3]; s_plate_pnt[3][Y] = fy[3]; s_plate_pnt[3][Z] = P6_PLATE_Z << 16;

    /* ATTR encoding == src/rsdk/drawing.c:_emit_polygon4 (the proven hand-port path):
     * flat-shaded direct-colour replace polygon, double-sided. */
    s_plate_att[0].flag  = Dual_Plane;
    s_plate_att[0].sort  = (Uint16)(SORT_CEN | ((sprPolygon >> 16) & 0x1c) | No_Option);
    s_plate_att[0].texno = No_Texture;
    s_plate_att[0].atrb  = (Uint16)((CL32KRGB | No_Gouraud | MESHoff) | ((sprPolygon >> 24) & 0xc0));
    s_plate_att[0].colno = p6_plate_rgb555(r8, g8, b8);
    s_plate_att[0].gstb  = 0;
    s_plate_att[0].dir   = (Uint16)(sprPolygon & 0x3f);

    slPutPolygon(&s_plate_pdata);
    ++p6_w_drawface_calls;
}
#endif

/* Mirror the 256-color stage palette into CRAM bank 1 once (engine RGB565,
 * same conversion as p6_vdp2.c bank 0). All Mania global sprites share the
 * stage palette, so the first bind owns the bank. */
static void p6_pal_mirror(const unsigned short *pal565)
{
    volatile Uint16 *cram1 = (volatile Uint16 *)(0x25F00000 + 0x200);
    int i;
    for (i = 0; i < 256; ++i) {
        unsigned short v  = pal565[i];
        unsigned short r5 = (v >> 11) & 0x1F;
        unsigned short g5 = ((v >> 5) & 0x3F) >> 1;
        unsigned short b5 = v & 0x1F;
        cram1[i] = (Uint16)(0x8000 | (b5 << 10) | (g5 << 5) | r5);
    }
}

#if defined(P6_GHZCUT_BOOT)
/* #311 mechanism 5: PC sprites read the LIVE stage palette (RotatePalette
 * cycles included) -- the one-shot first-bind p6_pal_mirror freezes bank1 at
 * load time, so cycled entries diverge (MEASURED _ring26.mcs: 65/256 entries,
 * bank1[255]=0x8000 frozen black vs live bank0[255]=0xf180 water blue -> the
 * dig-site strips sampling the sheet's solid-white block drew SOLID BLACK).
 * The front-end frame calls this per frame with the live fullPalette[0],
 * mirroring the engine's own per-frame bank0 flush. GHZCUT-gated: every other
 * flavor's p6_vdp1.o is byte-identical. */
void p6_vdp1_pal_remirror(const unsigned short *pal565)
{
    p6_pal_mirror(pal565);
}
#endif

#if defined(P6_FRONTEND_TITLE)
static int p6_title_ensure_prealloc(void); /* fwd: eager bucket VRAM reservation */
/* CP5b.7 content-size (#277): the TITLE first-bind init -- mirror the sprite palette
 * then reserve the bucket slots ONCE via the eager prealloc (NOT the per-bind jo_id=-1
 * reset, which would orphan the permanent VRAM reservations and leak jo's append-only
 * allocator). TITLE flavor only; the GHZ/Logos binds keep their verbatim inline reset
 * below (#if'd) so the GHZ p6_vdp1.o stays byte-identical. */
#define P6_VDP1_FIRST_BIND_INIT(pal) do {        \
        p6_pal_mirror(pal);                       \
        s_useclock = 0;                           \
        p6_title_ensure_prealloc();               \
    } while (0)
#else
#define P6_VDP1_FIRST_BIND_INIT(pal) do {        \
        int i;                                    \
        p6_pal_mirror(pal);                       \
        p6_w_vdp1_slots = 0;                      \
        s_useclock = 0;                           \
        for (i = 0; i < P6_VDP1_NSLOTS; ++i) {    \
            s_slots[i].jo_id   = -1;              \
            s_slots[i].sheet   = -1;              \
            s_slots[i].lastUse = 0;               \
        }                                         \
    } while (0)
#endif

/* Bind a RESIDENT engine surface. Returns the sheet handle (or -1). */
int p6_vdp1_sheet_bind(const unsigned char *sheetPixels, int sheetWidth,
                       const unsigned short *pal565)
{
    if (s_sheet_count >= P6_VDP1_NSHEETS)
        return -1;
    if (s_sheet_count == 0)
        P6_VDP1_FIRST_BIND_INIT(pal565);
    s_sheets[s_sheet_count].px      = sheetPixels;
    s_sheets[s_sheet_count].w       = sheetWidth;
    s_sheets[s_sheet_count].shtSlot = -1;
#if defined(P6_FRAMEDIR)
    s_sheets[s_sheet_count].frdSlot = -1;
#endif
    return s_sheet_count++;
}

/* W12b: bind a BANDED sheet (no resident pixels -- SaturnSheet store slot).
 * Same handle space as resident binds. */
int p6_vdp1_sheet_bind_banded(int shtSlot, int sheetWidth,
                              const unsigned short *pal565)
{
    if (s_sheet_count >= P6_VDP1_NSHEETS || shtSlot < 0)
        return -1;
    if (s_sheet_count == 0)
        P6_VDP1_FIRST_BIND_INIT(pal565);
    s_sheets[s_sheet_count].px      = 0;
    s_sheets[s_sheet_count].w       = sheetWidth;
    s_sheets[s_sheet_count].shtSlot = shtSlot;
#if defined(P6_FRAMEDIR)
    s_sheets[s_sheet_count].frdSlot = -1;
#endif
    return s_sheet_count++;
}

#if defined(P6_FRAMEDIR)
/* Attach a staged frame directory (SaturnFrameDir_Stage slot, via p6_io_main)
 * to a bound sheet handle. Once attached, a slot-cache MISS on this sheet is
 * served from the pre-cut FRD pattern FIRST; the banded/resident sheet path
 * remains the fallback for any rect not in the directory (defensive -- the
 * directory holds every anim-referenced rect, so a miss here means a
 * non-anim rect, e.g. a DrawRect-style raw blit). */
void p6_vdp1_sheet_set_frd(int handle, int frdSlot)
{
    if (handle < 0 || handle >= s_sheet_count)
        return;
    s_sheets[handle].frdSlot = frdSlot;
}

/* Water M1b regression fix (2026-07-20, MEASURED): SaturnSheet_BandReset() at the
 * GHZ chain handoff RENUMBERS the band store (slots 24 -> 11 fresh), but handles
 * bound in EARLIER chain legs (Tails1/Sonic1-3/Items... at AIZ) keep their OLD
 * latched shtSlot -> the s_frdByStore dispatch keys a stale slot AND the banded
 * fallback reads a stale slot -> the draw DROPS (live: frd_misses +11.3/s ==
 * drops +11.1/s, fetches +0 -> Tails invisible every frame, his last direct-list
 * quad frozen on screen). Called from the seam remap loop with the surface's
 * re-resolved store slot so persisted handles follow the renumbering. */
void p6_vdp1_sheet_update_slot(int handle, int shtSlot)
{
    if (handle < 0 || handle >= s_sheet_count)
        return;
    if (s_sheets[handle].px)
        return; /* resident-pixel handle: shtSlot unused */
    s_sheets[handle].shtSlot = shtSlot;
}

/* Seam-reclaim companion (feature checklist sec 7): SaturnSheet_ResReset()
 * kills the FRD blobs' cart backing, so every attachment must drop with it
 * -- a stale frdSlot against a re-staged DIFFERENT blob would serve wrong
 * pixels (the #250 stale-binding class). Called by p6_io_main right after
 * each ResReset+SaturnFrameDir_Reset pair; the incoming leg re-attaches. */
void p6_vdp1_frd_detach_all(void)
{
    int i;
    for (i = 0; i < s_sheet_count; ++i)
        s_sheets[i].frdSlot = -1;
}

#endif

#if defined(P6_FRONTEND_CHAIN)
/* CP5c (Task #270) CRAM-PALETTE FIX -- MEASURED root cause of the wrong Title
 * colors after the Logos->Title chain transition (savestate _title_chain.mcs vs
 * the direct-boot golden _title_sonic.mcs): the engine's active Title palette in
 * fullPalette[0] (WorkRAM-L 0x002FAC00) was BYTE-IDENTICAL in both states, but
 * VDP2 CRAM bank 1 (the SPRITE palette VDP1 reads, 0x05F00200) differed in 144 of
 * 256 entries -- the chain's bank 1 held the stale LOGOS palette. CAUSE:
 * p6_pal_mirror (the only writer of CRAM bank 1) runs ONLY when s_sheet_count==0
 * (the very first bind of the WHOLE run). On the direct Title boot, Title is the
 * first scene -> its first bind has s_sheet_count==0 -> the Title palette mirrors
 * correctly. In the CHAIN, the LOGOS scene binds FIRST (s_sheet_count 0->1, mirrors
 * Logos's palette), so when the Logos->Title fire re-binds Title's surfaces
 * s_sheet_count is already >=1 -> p6_pal_mirror is NEVER re-run -> bank 1 keeps the
 * Logos palette -> every Title sprite hue-shifts (the SAME #250-class "loaded once"
 * guard the VDP1-handle-table reset already fixed for GEOMETRY -- this is its CRAM
 * twin, in a SEPARATE static this TU owns).
 *
 * FIX: the front-end CHAIN calls this on the Logos->Title fire (p6_io_main.cpp,
 * alongside the p6_vdp1HandleBySurface[] reset it already does). Resetting
 * s_sheet_count to 0 makes the Title's FIRST re-bind satisfy s_sheet_count==0 ->
 * p6_pal_mirror re-runs with Title's fullPalette[0] -> CRAM bank 1 carries the
 * correct Title sprite palette. The slot rect-cache + sheet table are cleared too
 * (the surfaces re-bind fresh from index 0, matching the handle-table reset). The
 * GHZ same-folder reload never reaches this (front-end CHAIN only); the default
 * GHZ build does not compile it (byte-identical). */
void p6_vdp1_frontend_pal_reset(void)
{
    s_sheet_count   = 0;
    s_useclock      = 0;
#if defined(P6_FRONTEND_TITLE)
    /* CP5b.7 content-size (#277): the bucket slots are PERMANENTLY VRAM-reserved by the
     * eager prealloc -- NEVER reset their jo_id (re-allocating would leak jo's append-only
     * VDP1 allocator on every chain transition). Only clear the rect KEYS so each surface
     * re-stages content fresh after the Logos->Title re-bind; keep s_buckets_prealloc so
     * the allocator is not re-run. (CHAIN implies TITLE.) */
    {
        P6Vdp1Slot *bk[P6_NB];
        int bn, j, n;
#if defined(P6_GHZCUT_BOOT)
        /* #314: 5-bucket flavor -- positional order matches P6_BUCK (tiny, 64x80,
         * WIDE, 160x160, catch-all). */
        static const int bkcnt[P6_NB] = { P6_BK0, P6_BK1, P6_BKW, P6_BK2, P6_VDP1_NSLOTS };
        bk[0] = s_buck0; bk[1] = s_buck1; bk[2] = s_buckW; bk[3] = s_buck2; bk[4] = s_slots;
#else
        static const int bkcnt[P6_NB] = { P6_BK0, P6_BK1, P6_BK2, P6_VDP1_NSLOTS };
        bk[0] = s_buck0; bk[1] = s_buck1; bk[2] = s_buck2; bk[3] = s_slots;
#endif
        for (bn = 0; bn < P6_NB; ++bn) {
            n = bkcnt[bn];
            for (j = 0; j < n; ++j) {
                bk[bn][j].sheet   = -1;
                bk[bn][j].sx = bk[bn][j].sy = bk[bn][j].w = bk[bn][j].h = -1;
                bk[bn][j].lastUse = 0;
            }
        }
        /* keep p6_w_vdp1_slots / s_buck*n at the prealloc'd "full" marks */
    }
#else
    {
        int i;
        p6_w_vdp1_slots = 0;
        for (i = 0; i < P6_VDP1_NSLOTS; ++i) {
            s_slots[i].jo_id   = -1;
            s_slots[i].sheet   = -1;
            s_slots[i].lastUse = 0;
        }
    }
#endif
}
#endif

/* Find (or upload) the VDP1 residency slot for a (sheet, rect).
 *
 * Task #241 (was the "characters blink in and out" bug -- MEASURED: a saturated
 * 40-slot fill-once cache dropped 1,333 bound rects/run): the cache is now LRU,
 * not fill-once. A HIT touches the slot's LRU stamp. A MISS stages the rect into
 * a FIXED P6_SPR_MAXW x P6_SPR_MAXH (64x64) box (content top-left, transparent
 * pad) and either (a) cold-fills a new jo sprite while the cache is below
 * capacity, or (b) EVICTS the least-recently-used slot and overwrites its VDP1
 * VRAM in place via jo_sprite_replace (sprites.c:143 -- which requires identical
 * dimensions, hence the fixed 64x64 slot). A BOUND rect therefore ALWAYS gets a
 * slot; p6_w_vdp1_drops now only fires on an oversize frame (>64x64) or a banded-
 * fetch failure. The per-frame working set (~20 distinct rects) is far below
 * P6_VDP1_NSLOTS, so the player's hot frames stay resident and only cold rects
 * churn through the victim slot. jo_sprite_add still runs at most NSLOTS times
 * total (cold-fill only), so the #189 sprite-table overflow class cannot recur. */
/* CP5b.7: the slot lookup/stage/upload, PARAMETERIZED by the pool + its box size
 * so the same proven LRU logic serves BOTH the large (248x160) and the small
 * (64x64) title pools. Returns the slot INDEX into `slots` (or -1). Each pool is
 * uniform-box, so jo_sprite_replace's same-size in-place VRAM reuse is preserved
 * (no append-only leak). Body is the verbatim pre-CP5b.7 p6_slot_for with
 * s_slots->slots, p6_w_vdp1_slots->*coldn, P6_VDP1_NSLOTS->n_max, P6_SPR_MAXW/H->
 * boxw/boxh. */
#if defined(P6_FRONTEND_MENU)
/* #317: measure the LRU miss-DMA cost (slDMAWait + stage-copy + jo_sprite_replace) in
 * VBLANKS, to split the ~200ms entity-cb hog into blit-DMA vs object-Draw-logic. */
extern volatile unsigned int p6_perf_vbl_count;
extern int p6_w_draw_dma_v;
#endif
static int p6_pool_for(P6Vdp1Slot *slots, int n_max, int *coldn,
                       int boxw, int boxh, int sheet, int sx, int sy, int w, int h)
{
#if defined(P6_FRONTEND_MENU)
    unsigned int _dvdma0;
#endif
    int i, x, y, victim;
    const unsigned char *srcPx;
    int srcStride;
    jo_img_8bits img;

#if defined(P6_FRONTEND_LOGOS)
    p6_w_vdp1_lastwh = (w << 16) | (h & 0xFFFF);
#endif
    for (i = 0; i < *coldn; ++i) {
        if (slots[i].sheet == sheet &&
            slots[i].sx == sx && slots[i].sy == sy &&
            slots[i].w == w && slots[i].h == h) {
            slots[i].lastUse = ++s_useclock; /* LRU touch on hit */
            return i;
        }
    }
#if defined(P6_FRONTEND_MENU)
    _dvdma0 = p6_perf_vbl_count; /* #317: start of the MISS path (fetch+stage+DMA) */
#endif
    /* A fixed boxw x boxh slot cannot hold an oversize frame. */
    if (w > boxw || h > boxh) {
        ++p6_w_vdp1_drops;
#if defined(P6_FRONTEND_LOGOS) || defined(P6_GHZCUT_BOOT)
        p6_w_vdp1_dropreason = 1;
#endif
        return -1;
    }

#if defined(P6_FRAMEDIR)
    /* PRE-CUT frame directory (stage 1, 8bpp): serve the miss from the
     * offline-cut pattern -- rows contiguous at the mult-8 padded stride,
     * 4-aligned in the cart. NO inflate, NO row repack; the box-stage loop
     * below just copies w bytes/row from stride fi.pw. mode==1 (4bpp LUT)
     * is stage 2 (P6_FRAMEDIR_4BPP, feature checklist) -- until then FRDs
     * are built --all8 so every entry is mode 0. Falls through to the
     * resident/banded sheet path on a directory miss (non-anim rect). */
    {
        P6FrameInfo fi;
        int _fslot = p6_frd_slot_for_sheet(sheet); /* store-slot table, dup-handle safe */
        if (_fslot >= 0 && s_frdFn
            && s_frdFn(_fslot, sx, sy, w, h, &fi)
            && fi.mode == 0) {
            srcPx     = fi.pattern;
            srcStride = fi.pw;
        }
        else
            fi.pattern = 0;
        if (fi.pattern) { /* staged from the FRD -- skip the sheet paths */ }
        else
#endif
    if (s_sheets[sheet].px) {
        srcPx     = s_sheets[sheet].px + sy * s_sheets[sheet].w + sx;
        srcStride = s_sheets[sheet].w;
    }
    else {
        /* W12b banded miss: fetch the rect rows from the VDP2 band store
         * through the runtime pointer (see root-cause note above). s_fetch
         * holds the bare rect (stride w); the CACHE KEY keeps sheet sx/sy. */
#if defined(P6_FRONTEND_LOGOS) || defined(P6_GHZCUT_BOOT)
        p6_w_vdp1_lastfetch = ((s_sheets[sheet].shtSlot & 0xFF) << 24)
                            | ((w & 0xFFF) << 12) | (h & 0xFFF);
        p6_w_vdp1_fetchret = s_fetchFn
            ? s_fetchFn(s_sheets[sheet].shtSlot, sx, sy, w, h, s_fetch) : -2;
        if (p6_w_vdp1_fetchret <= 0) {
            ++p6_w_vdp1_drops;
            p6_w_vdp1_dropreason = 2;
            return -1;
        }
#else
        if (!s_fetchFn
            || !s_fetchFn(s_sheets[sheet].shtSlot, sx, sy, w, h, s_fetch)) {
            ++p6_w_vdp1_drops;
            return -1;
        }
#endif
        srcPx     = s_fetch;
        srcStride = w;
        /* #311 mech-4: the g11 fetch ring lives at the BUCKET fetch site
         * (p6_title_pool_for) -- MEASURED (build 24, _ring24.mcs): this
         * p6_pool_for site is DEAD CODE in the GHZCUT flavor (ring n stayed 0;
         * p6_slot_for routes every draw through the P6_FRONTEND_TITLE bucket
         * path, which GHZCUT implies). */
    }
#if defined(P6_FRAMEDIR)
    } /* close the FRD-dispatch block */
#endif

    /* Stage into the FIXED boxw x boxh box: content top-left, the rest transparent
     * (palette index 0 -- VDP1 sprite transparent-pixel processing skips it).
     * jo_sprite_replace re-DMAs exactly boxw*boxh bytes, so the staged box
     * dimensions MUST equal the pre-allocated slot's (boxw x boxh -- guaranteed:
     * same pool = same box). s_stage is 248x160 = big enough for either box. */
    /* #312(e) same-class hardening as p6_title_restage_content: jo_sprite_add/
     * jo_sprite_replace below DMA from s_stage via jo_dma_copy == slDMACopy,
     * which is ASYNC (ST-238-R1: "terminates soon after DMA is initiated") --
     * a PRIOR miss's transfer may still be reading s_stage when this pack
     * overwrites it -> that slot's VRAM tears (the GHZCUT bucket edition was
     * MEASURED, qa_g11_vram build 25; this pool path shares the exact pattern:
     * sprites.c:172-174). Wait out any in-flight transfer before repacking. */
    slDMAWait();
    for (y = 0; y < boxh; ++y) {
        unsigned char *dst = s_stage + y * boxw;
        if (y < h) {
            const unsigned char *src = srcPx + y * srcStride;
            for (x = 0; x < w; ++x) dst[x] = src[x];
            for (; x < boxw; ++x) dst[x] = 0;
        }
        else {
            for (x = 0; x < boxw; ++x) dst[x] = 0;
        }
    }
#if defined(P6_GHZCUT_BOOT)
    /* #311 mech-4 bisect stage-side: hash the SAME 16x16 content region out of
     * the staged box (rows at boxw stride). Match vs p6_w_draw_fetch_h = the
     * stage copy is clean -> the tear is the jo DMA/slot layer. */
    if (sx == 258 && sy == 492 && w == 16 && h == 16 && !s_sheets[sheet].px) {
        unsigned int hh = 5381; int ky, kx;
        for (ky = 0; ky < 16; ++ky)
            for (kx = 0; kx < 16; ++kx)
                hh = ((hh << 5) + hh) ^ s_stage[ky * boxw + kx];
        p6_w_draw_stage_h = (int)hh;
    }
#endif
    img.width  = boxw;
    img.height = boxh;
    img.data   = s_stage;

    if (*coldn < n_max) {
        /* Cold-fill: allocate a fresh fixed-size jo sprite. */
        int id = jo_sprite_add_8bits_image(&img);
        if (id < 0) {
            ++p6_w_vdp1_joaddfail; /* W14: the silent no-drop exit */
#if defined(P6_FRONTEND_LOGOS)
            p6_w_vdp1_dropreason = 3;
#endif
            return -1;
        }
        victim = (*coldn)++;
        slots[victim].jo_id = id;
    }
    else {
        /* Cache full: evict the least-recently-used slot, reuse its VRAM. */
        int oldest = slots[0].lastUse;
        victim = 0;
        for (i = 1; i < n_max; ++i) {
            if (slots[i].lastUse < oldest) {
                oldest = slots[i].lastUse;
                victim = i;
            }
        }
        if (slots[victim].jo_id < 0) { /* defensive: never cold-filled */
            ++p6_w_vdp1_drops;
            return -1;
        }
        jo_sprite_replace(&img, slots[victim].jo_id);
        ++p6_w_vdp1_evicts;
    }
#if defined(P6_FRONTEND_MENU)
    p6_w_draw_dma_v += (int)(p6_perf_vbl_count - _dvdma0); /* #317: MISS fetch+stage+DMA cost */
#endif

    slots[victim].sheet   = sheet;
    slots[victim].sx      = sx;
    slots[victim].sy      = sy;
    slots[victim].w       = w;
    slots[victim].h       = h;
    slots[victim].lastUse = ++s_useclock;
    p6_w_vdp1_lastkey     = (sx << 20) | (sy << 12) | (w << 6) | h;
    return victim;
}

#if defined(P6_FRONTEND_TITLE)
/* CP5b.7 content-size step (Task #277): the TITLE content-tight pool. Every bucket
 * slot is pre-reserved at box size (p6_title_ensure_prealloc), so this never calls
 * jo_sprite_add -- a MISS LRU-evicts a victim slot and RESTAGES it at CONTENT size via
 * p6_title_restage_content (DMA content-packed + set the slot's CMDSIZE to content).
 * VDP1 then rasterises ONLY the sprite's content (w mult-8, h) -- the 39%-padding the
 * box-draw paid is gone. Returns the slot index into b->slots (or -1); sets *out_pw to
 * the drawn mult-8 width (the blit centres on (pw,h), not the box). The HIT path is
 * unchanged (the slot already carries its content TEXDEF from its last restage). */
static int p6_title_ensure_prealloc(void);
static int p6_title_pool_for(P6Bucket *b, int sheet, int sx, int sy, int w, int h,
                             int *out_pw, int *out_ph)
{
    int i, victim;
    const unsigned char *srcPx;
    int srcStride, pw;

    if (!p6_title_ensure_prealloc()) { ++p6_w_vdp1_drops; return -1; }

#if defined(P6_FRONTEND_TITLE)
    /* task #326: does the head rect even REACH the title bucket router? Count every
     * call for a tall rect (the head is the only h>=100 title rect) -- hit or miss.
     * headfetch_calls stays -9 == the head never routes here (dropped upstream or a
     * different frame settled). */
    if (h >= 100 && w >= 100 && sy >= 400) {
        if (p6_w_headfetch_calls < 0) p6_w_headfetch_calls = 0;
        ++p6_w_headfetch_calls;
        p6_w_headfetch_sxsy    = (sx << 16) | (sy & 0xFFFF);
        p6_w_headfetch_bucketbw = b->bw;
    }
#endif

    for (i = 0; i < b->n; ++i) {            /* HIT: same rect already content-staged */
        if (b->slots[i].sheet == sheet &&
            b->slots[i].sx == sx && b->slots[i].sy == sy &&
            b->slots[i].w == w && b->slots[i].h == h) {
            b->slots[i].lastUse = ++s_useclock;
            *out_pw = (w + 7) & ~7; *out_ph = h;
            return i;
        }
    }
    if (w > b->bw || h > b->bh) { ++p6_w_vdp1_drops; P6_DR(3); return -1; } /* oversize for bucket */

#if defined(P6_FRAMEDIR)
    /* PRE-CUT frame directory (stage 1, 8bpp) -- same dispatch as p6_pool_for
     * (see the comment there). With FRD source rows already at the mult-8
     * padded stride AND 4-aligned, p6_title_restage_content's shift-merge
     * branch never runs: srcPx + y*pw stays 4-aligned every row -> the
     * aligned-u32 fast path packs the whole pattern. */
    {
        P6FrameInfo fi;
        int _frdret = 0;
        int _fslot = p6_frd_slot_for_sheet(sheet); /* store-slot table, dup-handle safe */
        if (_fslot >= 0 && s_frdFn) {
            _frdret = s_frdFn(_fslot, sx, sy, w, h, &fi);
            if (_frdret && fi.mode == 0) {
                srcPx     = fi.pattern;
                srcStride = fi.pw;
            } else
                fi.pattern = 0;
        } else
            fi.pattern = 0;
        (void)_frdret;
        if (fi.pattern) { /* staged from the FRD -- skip the sheet paths */ }
        else {
        /* #243 FIX (dangling-else, MEASURED 2026-07-16): with BOTH P6_FRAMEDIR
         * and P6_FRONTEND_TITLE defined (the chain build), the bare `else` here
         * bound to the #326 head-forensic `if (h >= 100 ...)` below -- NOT to
         * the sheet-path fallback -- so after an FRD HIT the resident/banded
         * path STILL ran and the banded miniz fetch overwrote srcPx (MEASURED:
         * d(frd_lookups)=280 hits WITH d(p6_w_sht_fetches)=592 band inflates
         * over the same 16 emu-s GHZ-play window, frd_misses=0 -- the whole
         * 6.65-inflates/frame chain draw wall). Plain GHZ compiles the forensic
         * out, so its else bound correctly -- chain-only symptom. Braces make
         * the else own the entire fallback; closed at the block tail below. */
#endif
#if defined(P6_FRONTEND_TITLE)
    /* task #326: for the head rect, latch whether the sheet is RESIDENT (px!=0). */
    if (h >= 100 && w >= 100 && sy >= 400) {
        p6_w_headfetch_resident = (s_sheets[sheet].px != 0) ? 1 : 0;
        p6_w_headfetch_shtslot  = s_sheets[sheet].shtSlot;
    }
#endif
    if (s_sheets[sheet].px) {
        srcPx     = s_sheets[sheet].px + sy * s_sheets[sheet].w + sx;
        srcStride = s_sheets[sheet].w;
    }
    else {
        P6_DR(4);
#if defined(P6_GHZCUT_BOOT)
        p6_w_vdp1_lastfetch = ((s_sheets[sheet].shtSlot & 0xFF) << 24) | ((w & 0xFFF) << 12) | (h & 0xFFF);
        p6_w_vdp1_fetchret  = s_fetchFn ? s_fetchFn(s_sheets[sheet].shtSlot, sx, sy, w, h, s_fetch) : -2;
#if defined(P6_FRONTEND_TITLE)
        if (h >= 100 && w >= 100 && sy >= 400) p6_w_headfetch_ret = p6_w_vdp1_fetchret;
#endif
        if (p6_w_vdp1_fetchret <= 0) { ++p6_w_vdp1_drops; return -1; }
#else
        if (!s_fetchFn
            || !s_fetchFn(s_sheets[sheet].shtSlot, sx, sy, w, h, s_fetch)) {
            ++p6_w_vdp1_drops;
            return -1;
        }
#endif
        srcPx     = s_fetch;
        srcStride = w;
#if defined(P6_GHZCUT_BOOT) && !defined(P6_PERF_NOSCAN)
        /* #311 mech-4 v3: THIS is the live fetch site (every GHZCUT draw routes
         * through the bucket path). Hash every GHCOBJ-slot (11) draw-time fetch
         * into the g11 ring; offline compares each against the gif crop of the
         * SAME rect (qa_g11_ring.py). #324: a per-draw djb2 over w*h bytes is a
         * hot-loop diagnostic (perf-diagnostic-in-hotloop rule) -- NOSCAN-stripped
         * from shipping; rebuild with P6_NOSCAN= (empty) to re-arm the ring. */
        if (s_sheets[sheet].shtSlot == 11) {
            unsigned int hh = 5381; int k, n = w * h;
            for (k = 0; k < n; ++k) hh = ((hh << 5) + hh) ^ s_fetch[k];
            p6_w_g11_rect[p6_w_g11_n & 3] = (sx << 16) | sy;
            p6_w_g11_wh[p6_w_g11_n & 3]   = (w << 16) | h;
            p6_w_g11_hash[p6_w_g11_n & 3] = (int)hh;
            ++p6_w_g11_n;
        }
#endif
    }
#if defined(P6_FRAMEDIR)
        } /* close the FRD-miss else (#243 dangling-else fix) */
    } /* close the FRD-dispatch block */
#endif

    /* task #326 HEAD-FETCH forensic: the settled TitleSonic head is anim0 frame48
     * = sheet rect (sx=496,sy=636,110x120) -- uniquely the only tall (h>=100) rect
     * fetched from the high sheet rows on the title. Count its non-transparent
     * source bytes: nz==0 == the staged head is all-transparent == the invisible
     * head (a valid, in-front, correctly-placed blit of BLANK content). */
    if (h >= 100 && w >= 100 && sy >= 400) {
        int k, n = w * h, nz = 0;
        for (k = 0; k < n; ++k) {
            /* srcPx has stride srcStride; walk row-by-row so padding is skipped */
            int row = k / w, col = k % w;
            if (srcPx[row * srcStride + col] != 0) ++nz;
        }
        p6_w_headfetch_nz       = nz;
        p6_w_headfetch_resident = (s_sheets[sheet].px != 0) ? 1 : 0;
        p6_w_headfetch_shtslot  = s_sheets[sheet].shtSlot;
        p6_w_headfetch_sxsy     = (sx << 16) | (sy & 0xFFFF);
    }

    /* LRU victim among this bucket's pre-allocated slots (all jo_id >= 0). */
    {
        int oldest = b->slots[0].lastUse;
        victim = 0;
        for (i = 1; i < b->n; ++i)
            if (b->slots[i].lastUse < oldest) { oldest = b->slots[i].lastUse; victim = i; }
    }
    if (b->slots[victim].jo_id < 0) { ++p6_w_vdp1_drops; P6_DR(5); return -1; } /* prealloc failed */
    if (b->slots[victim].sheet >= 0) ++p6_w_vdp1_evicts;             /* reuse of a live slot */

    pw = p6_title_restage_content(b->slots[victim].jo_id, srcPx, srcStride, w, h);
#if defined(P6_GHZCUT_BOOT) && !defined(P6_PERF_NOSCAN)
    /* #311 mech-4 v3 stage-side pair: djb2 of the content-packed box the restage
     * just wrote (pw*h bytes, mult-8 pad zeros included). #324: the restage now
     * packs DIRECTLY into the slot's VDP1 VRAM (s_stage is no longer written), so
     * hash the VRAM itself -- byte reads are doc-legal on VDP1 VRAM (VDP1 manual
     * "Byte access and word access are both possible from the CPU"). Also a
     * hot-loop diagnostic -> NOSCAN-stripped from shipping (P6_NOSCAN= to re-arm). */
    if (s_sheets[sheet].shtSlot == 11 && !s_sheets[sheet].px) {
        const volatile unsigned char *vr = (const volatile unsigned char *)
            (JO_VDP1_VRAM + JO_MULT_BY_8(__jo_sprite_def[b->slots[victim].jo_id].adr));
        unsigned int hh = 5381; int k, n = pw * h;
        for (k = 0; k < n; ++k) hh = ((hh << 5) + hh) ^ vr[k];
        p6_w_g11_stage[(p6_w_g11_n - 1) & 3] = (int)hh;
    }
#endif
#if defined(P6_FRONTEND_MENU)
    /* M1b witness: tally this restage (cache miss) against the owning bucket so
     * perf_reset can latch the per-frame demand (p6_w_buckN_fmax). bk is recovered
     * by matching the bucket pointer into s_buckets[]. A per-frame miss count >
     * the bucket's slot count == an intra-frame slot reuse == the striped garble. */
    {
        int _bk;
        for (_bk = 0; _bk < P6_NB; ++_bk)
            if (b == &s_buckets[_bk]) { ++p6_buckMiss[_bk]; break; }
    }
#endif

    b->slots[victim].sheet   = sheet;
    b->slots[victim].sx      = sx;
    b->slots[victim].sy      = sy;
    b->slots[victim].w       = w;
    b->slots[victim].h       = h;
    b->slots[victim].lastUse = ++s_useclock;
    p6_w_vdp1_lastkey        = (sx << 20) | (sy << 12) | (w << 6) | h;
    *out_pw = pw; *out_ph = h;
    return victim;
}

/* Reserve every bucket slot's VRAM (box footprint) exactly once, before any draw.
 * Returns 1 on success. Builds s_buckets[] (binding each P6Bucket to its slot array +
 * box dims) and content-stages NOTHING yet (the slots start empty: sheet=-1). After
 * this runs, jo_sprite_add is never called again for the buckets (see the eager-
 * prealloc rationale above), so per-(re)stage __jo_sprite_def mutation is safe. */
static int p6_title_ensure_prealloc(void)
{
    int bi, si, id;
    P6Vdp1Slot *arr[P6_NB];
    int cnt[P6_NB];

    if (s_buckets_prealloc) return 1;
#if defined(P6_GHZCUT_BOOT)
    /* #314: 5-bucket flavor -- same positional order as P6_BUCK + pal_reset. */
    arr[0] = s_buck0; arr[1] = s_buck1; arr[2] = s_buckW; arr[3] = s_buck2; arr[4] = s_slots;
    cnt[0] = P6_BK0; cnt[1] = P6_BK1; cnt[2] = P6_BKW; cnt[3] = P6_BK2; cnt[4] = P6_VDP1_NSLOTS;
#else
    arr[0] = s_buck0; arr[1] = s_buck1; arr[2] = s_buck2; arr[3] = s_slots;
    cnt[0] = P6_BK0; cnt[1] = P6_BK1; cnt[2] = P6_BK2; cnt[3] = P6_VDP1_NSLOTS; /* NSLOTS==4 */
#endif
    for (bi = 0; bi < P6_NB; ++bi) {
        s_buckets[bi].slots = arr[bi];
        s_buckets[bi].n     = cnt[bi];
        s_buckets[bi].bw    = P6_BUCK[bi].bw;
        s_buckets[bi].bh    = P6_BUCK[bi].bh;
        for (si = 0; si < cnt[bi]; ++si) {
            id = p6_title_alloc_box(P6_BUCK[bi].bw, P6_BUCK[bi].bh);
            if (id < 0) { ++p6_w_vdp1_joaddfail; return 0; }
            arr[bi][si].jo_id   = id;
            arr[bi][si].sheet   = -1;     /* empty: no rect staged yet */
            arr[bi][si].sx = arr[bi][si].sy = arr[bi][si].w = arr[bi][si].h = -1;
            arr[bi][si].lastUse = 0;
        }
    }
    /* p6_w_vdp1_slots tracks bucket-3 occupancy in the witnesses; the slots are now all
     * reserved, so mark it full (the LRU victim path -- not cold-fill -- serves it). */
    s_buck0n = P6_BK0; s_buck1n = P6_BK1; s_buck2n = P6_BK2; p6_w_vdp1_slots = P6_VDP1_NSLOTS;
    s_buckets_prealloc = 1;
    return 1;
}
#endif

/* CP5b.7 ROUTER: pick the SMALLEST box pool that holds (w,h). Returns the jo
 * sprite ID (NOT a slot index) + sets s_last_box_w/h for the blit's box-center
 * placement. TITLE only -- the GHZ/Logos build has a single pool (byte-identical:
 * the small-pool branch is #if'd out and the large call mirrors the old code). */
static int s_last_box_w = P6_SPR_MAXW, s_last_box_h = P6_SPR_MAXH;
static int p6_slot_for(int sheet, int sx, int sy, int w, int h)
{
    int s;
#if defined(P6_FRONTEND_TITLE)
    /* Phase 2 + content-size (#277): route to the smallest bucket, then DRAW at content
     * size. s_last_box_w/h become the drawn (mult-8 w, h) so the blit centres the
     * content -- NOT the box -- and the fill witnesses sum the real CMDSIZE area. */
    int bk = p6_bucket_for(w, h);
    int pw = (w + 7) & ~7, ph = h;
    if (bk < 0) { ++p6_w_vdp1_drops; P6_DR(6); return -1; } /* oversize (w>248 or h>160) */
    s = p6_title_pool_for(&s_buckets[bk], sheet, sx, sy, w, h, &pw, &ph);
#if defined(P6_FRONTEND_TITLE)
    /* task #326: latch the router result for the head rect -- bk, the returned slot
     * s, and slots[s].jo_id. slotret<0 == p6_title_pool_for dropped the head; jid<0
     * == the slot exists but its jo sprite was never allocated (prealloc short). */
    if (h >= 100 && w >= 100 && sy >= 400) {
        p6_w_headslot_bk  = bk;
        p6_w_headslot_ret = s;
        p6_w_headslot_jid = (s >= 0) ? s_buckets[bk].slots[s].jo_id : -100;
    }
#endif
    if (s < 0) return -1;
    s_last_box_w = pw; s_last_box_h = ph;
    return s_buckets[bk].slots[s].jo_id;
#else
    s = p6_pool_for(s_slots, P6_VDP1_NSLOTS, &p6_w_vdp1_slots,
                    P6_SPR_MAXW, P6_SPR_MAXH, sheet, sx, sy, w, h);
    if (s < 0) return -1;
    s_last_box_w = P6_SPR_MAXW;
    s_last_box_h = P6_SPR_MAXH;
    return s_slots[s].jo_id;
#endif
}

#if defined(P6_FRONTEND_TITLE)
/* CP5b.4 (Task #272): VDP1 half-transparency for the TitleBG INK_BLEND (Mountain2)
 * + INK_ADD (Reflection/WaterSparkle, alpha 0x80) sprites. jo's effect bits OR into
 * cmd->pmod (sprites.c:363: pmod = 0x0080 | effect); effect 0x3 == SGL CL_Trans
 * translucent color-calc (SL_DEF.H:194; ST-013-R3 sec 5.5.4 PMOD bits 2:0).
 * HARDWARE TRUTH (REPORTED): VDP1 PMOD half-transparency blends with what is
 * already in the VDP1 FRAMEBUFFER (other sprites), NOT the VDP2 backdrop -- so a
 * mountain's translucency over the VDP2 island is NOT reproduced by PMOD alone
 * (that needs VDP2 color-calc CCRTL on the sprite layer, ST-058-R2 sec 12). Set
 * before the blit, clear after (sticky jo attribute -- same pattern as the flips).
 * Title flavor only (GHZ p6_vdp1.o byte-identical). */
__attribute__((used)) int p6_w_ink_half_blits = 0;
void p6_vdp1_set_ink(int half)
{
#if defined(P6_DIRECT_VDP1)
    /* #316 F1: the sticky ink threads into the direct emit's PMOD CL_Half. */
    s_dl_ink = half;
    if (half) ++p6_w_ink_half_blits;
#else
    if (half) { jo_sprite_enable_half_transparency(); ++p6_w_ink_half_blits; }
    else      { jo_sprite_disable_half_transparency(); }
#endif
}
#endif

#if defined(P6_FRONTEND_TITLE)
/* EDGE-GLITCH FIX (this session): the TitleBG parallax band (MountainTop1/2,
 * Reflection, WaterSparkle -- Title/BG.gif, 176-192 px wide) is scrolled +
 * horizontally wrapped by the verbatim decomp TitleBG_Update
 * (position.x -= 0x10000; if (position.x < -0x800000) position.x += 0x3000000)
 * for a band that spans a WIDER PC screen. On Saturn's 320 px screen those wide
 * sprites land (MEASURED via the per-blit ring on a settled-title savestate:
 * x=283 content [283,475], and x=-77 content [-77,115]) so their drawn box
 * extends far past both screen edges. The Saturn VDP1 path stages each sprite
 * into a FIXED P6_SPR_MAXW(248)-wide box (content at the box top-left) and
 * slDispSprite-places the box CENTER at framebuffer x + 124. For an off-screen
 * sprite the box CROSSES the 512 px VDP1 framebuffer LINE STRIDE (e.g. x=283 ->
 * box [283,531]; 531 > 512), and the part past 512 WRAPS to the next line's
 * left columns -- the "fragment at the opposite edge". The PC engine never sees
 * this because DrawSpriteFlipped (Drawing.cpp:2882-2905) clips the sprite to
 * currentScreen->clipBound_* PER PIXEL; the Saturn VDP1 has no such per-pixel
 * clip on a normal-sprite command. MIRROR that clip here: clip the source rect
 * to the on-screen span [0, JO_TV_WIDTH) so only the visible columns are staged
 * + drawn and the box can never cross the framebuffer boundary. Title flavor
 * only (default GHZ p6_vdp1.o stays byte-identical -- the GHZ object set draws
 * within-screen and never triggers this).
 *
 * The VDP1 stages every sprite into a FIXED P6_SPR_MAXW-wide box (content at the
 * box top-left) and slDispSprite-places the box CENTER at framebuffer x + 124, so
 * the box spans framebuffer [x, x + P6_SPR_MAXW]. When a sprite is positioned so
 * its box crosses the 320-mode VDP1 framebuffer LINE STRIDE (512 px) -- box-left
 * < 0 (a sprite scrolled off the LEFT) or box-right > 512 (off the RIGHT) -- the
 * crossing columns WRAP to the opposite edge as a visible "duplicate fragment".
 * MEASURED root cause of the title edge glitch: the verbatim decomp TitleBG_Update
 * scrolls + horizontally wraps the TitleBG parallax band (MountainTop1/2, Reflection,
 * WaterSparkle) for a WIDER PC screen; on Saturn's 320 px screen those wide sprites
 * land off both edges and their box crosses the stride.
 *
 * The engine clips partly-off-screen sprites per-pixel (DrawSpriteFlipped clipBound),
 * but the Saturn VDP1 normal-sprite command has no per-pixel clip, and re-staging a
 * clipped sub-rect would thrash the 10-slot LRU cache (the per-scroll-position rects
 * explode the key space -> evictions -> stale-slot garbage). So instead CULL any
 * sprite whose fixed box would cross the stride. The culled content is only the few
 * pixels of the DISTANT-mountain band right at the screen edge -- imperceptible vs a
 * wrapped duplicate, and the cull touches neither the source rect nor the cache.
 * Returns 1 to draw (box fully in [0,512)), 0 to cull. Title flavor only (GHZ
 * p6_vdp1.o is byte-identical -- its object set draws within-screen). */
#define P6_VDP1_FB_STRIDE 512  /* 320-mode VDP1 framebuffer line width (px) */
static int p6_box_in_stride(int x, int flipX, int w, int h)
{
    int box_left;
    /* Phase 2: the box width is the ROUTED content-size bucket -- MUST match the
     * p6_slot_for routing exactly, or a sprite near the right edge is wrongly
     * culled/passed (box-right = x+boxw vs the 512 px stride). p6_bucket_for(-1)
     * (oversize) -> the big box; p6_slot_for drops it anyway. */
    int b    = p6_bucket_for(w, h);
    int boxw = (b < 0) ? P6_SPR_MAXW : P6_BUCK[b].bw;
    /* Box-left in the framebuffer for the content-at-box-left staging:
     *   FLIP_NONE: box center FB = x + boxw/2  -> box-left = x.
     *   FLIP_X:    box center FB = x + w - boxw/2 + JO_TV_WIDTH_2 ... reduces
     *              to box-left = x + w - boxw. */
    box_left = flipX ? (x + w - boxw) : x;
    return (box_left >= 0 && box_left + boxw <= P6_VDP1_FB_STRIDE);
}
#endif

#if defined(P6_GHZCUT_BOOT)
/* Task #309 Tier-B.2: per-draw VDP1 palette-block selector. Normal sprites use
 * block 1 (jo colno=256 -> CRAM[256] bank1, p6_pal_mirror). The 5 GHZCutscene
 * Heavies each route to their OWN 256-entry CRAM block via colno = block*256
 * (DOC-CITED: live SPCTL=0x23 Sprite Type 3 = full 11-bit DC, SPCAOS=0, CRAM
 * mode 1 -> a VDP1 8bpp sprite's CRAM address = jo colno (CMDCOLR high byte) +
 * charpixel; ST-013-R3 sec 6.4 + ST-058-R2 sec 10.1). The overlay's CutsceneHBH
 * Draw shim sets this to 2+characterID (-> colno 512..1536) before DrawSprite and
 * resets it to 1 after. Default 1 -> every non-Heavy draw is byte-identical to the
 * hardcoded jo_sprite_set_palette(1). GHZCUT-only (#else keeps p6_vdp1.o the same). */
__attribute__((used)) int p6_heavy_palblock = 1;
#define P6_BLIT_PALBLOCK p6_heavy_palblock

/* Task #309 caveat #2a (cutscene PLAYERS render): the CORRECTED palette route --
 * SURFACE-DRIVEN, NOT the shared engine draw loop (the attempt-1 regression wrapped
 * ProcessObjectDrawLists in the shared RSDKv5 Object.cpp and killed the WHOLE VDP1
 * sprite path). p6_plr_sheet_slot = the SaturnSheet slot of the staged PLROBJ.SHT
 * (set once at stage time by p6_io_main.cpp; -1 == not staged == every blit keeps its
 * normal block). In the blit functions, when the surface being drawn IS the player
 * sheet, the colno is forced to block 7 (CRAM[1792], the merged Sonic+Tails palette)
 * for THAT blit only -- every other surface keeps P6_BLIT_PALBLOCK. Both players share
 * the ONE merged block so no per-character logic is needed. p6_w_plr_cut_landed counts
 * player-sheet blits that reached a VDP1 slot this frame (the gate's per-frame witness;
 * timing-noisy, NOT gate-failing -- the binding/anim/palette signals + the screenshot
 * are the proof). DOC: colno = CMDCOLR high byte (ST-013-R3 sec 6.4); SPCTL Type-3
 * full-11-bit DC + SPCAOS=0 -> CRAM addr = colno + char-pixel (ST-058-R2 sec 10.1). */
__attribute__((used)) int p6_plr_sheet_slot   = -1;
/* int (not int32): this is the jo-side C TU where the RSDK int32 typedef isn't in
 * scope; SH-2 int is 32-bit so the gate's peek_u32 reads it identically. */
__attribute__((used)) int p6_w_plr_cut_landed = 0;
#define P6_BLIT_PLAYER_PALBLOCK 7
#else
#define P6_BLIT_PALBLOCK 1
#endif

/* Draw a sheet rect with its TOP-LEFT at engine screen px (x,y) -- the
 * coordinate DrawSpriteFlipped receives (Drawing.cpp:2785: pos + pivot).
 * jo_sprite_draw3D positions the sprite CENTER in screen-centered coords;
 * the slot is a fixed P6_SPR_MAXW x P6_SPR_MAXH box with content in the
 * top-left corner, so centering on the box (offset 32) keeps the content at
 * engine [x, x+w) x [y, y+h). */
void p6_vdp1_blit(int sheet, int x, int y, int w, int h, int sx, int sy)
{
    int jid;

    if (sheet < 0 || sheet >= s_sheet_count) {
        ++p6_w_vdp1_handle_drops; /* W18: unbound-surface silent drop */
        p6_w_vdp1_lastdrop_h = sheet;
        return;
    }
#if defined(P6_FRONTEND_TITLE)
    /* EDGE-GLITCH FIX: cull a sprite whose ROUTED box would cross the 512 px VDP1
     * framebuffer line stride (the off-screen wrap). See p6_box_in_stride. */
    if (!p6_box_in_stride(x, 0, w, h))
        return;
#endif
    /* CP5b.7: p6_slot_for routes to the small/large pool by (w,h) and returns the
     * jo sprite id + sets s_last_box_w/h (the chosen box). */
    jid = p6_slot_for(sheet, sx, sy, w, h);
    if (jid < 0)
        return;

#if defined(P6_FRONTEND_TITLE)
    /* task #326 HEAD-VRAM forensic: for the head rect (496,636,110,120), read the
     * ACTUAL staged VDP1 VRAM the head command will reference (__jo_sprite_def[jid]
     * .adr, size) and count non-transparent (index != 0) bytes. headvram_nz == 0 ->
     * the head's staged sprite is BLANK (the invisible head despite a valid, in-front,
     * correctly-placed command). Byte reads on VDP1 VRAM are doc-legal (VDP1 manual). */
    if (h >= 100 && w >= 100 && sy >= 400) {
        const volatile unsigned char *vr = (const volatile unsigned char *)
            (JO_VDP1_VRAM + JO_MULT_BY_8(__jo_sprite_def[jid].adr));
        unsigned short cs = __jo_sprite_def[jid].size; /* (w/8)<<8 | h */
        int vw = ((cs >> 8) & 0x3F) * 8, vh = cs & 0xFF;
        int k, n = vw * vh, nz = 0;
        for (k = 0; k < n; ++k) if (vr[k] != 0) ++nz;
        p6_w_headvram_nz   = nz;
        p6_w_headvram_wh   = (vw << 16) | (vh & 0xFFFF);
        p6_w_headvram_adr  = __jo_sprite_def[jid].adr;
        p6_w_headvram_jid  = jid;
    }
#endif
    ++p6_w_vdp1_landed; /* W18: a blit that reached a valid VDP1 slot */
    /* STEP B: per-frame VDP1 workload (ROUTED box-as-drawn vs content-ideal). */
    ++p6_w_vdp1_cmds; p6_w_vdp1_boxpx += s_last_box_w * s_last_box_h;
    p6_w_vdp1_contentpx += w * h;
    if (w > p6_w_vdp1_maxw) p6_w_vdp1_maxw = w;
    if (h > p6_w_vdp1_maxh) p6_w_vdp1_maxh = h;
    {
        int palblk;
#if defined(P6_GHZCUT_BOOT)
        /* #2a SURFACE-DRIVEN player palette: the PLROBJ sheet routes to block 7. */
        if (sheet == p6_plr_sheet_slot && p6_plr_sheet_slot >= 0) {
            palblk = P6_BLIT_PLAYER_PALBLOCK;
            ++p6_w_plr_cut_landed;
        } else {
            palblk = P6_BLIT_PALBLOCK;
        }
#else
        palblk = P6_BLIT_PALBLOCK;
#endif
#if defined(P6_DIRECT_VDP1)
        /* #316 F1: direct command -- CMDSIZE is the restaged CONTENT, so vertex A
         * is simply the RSDK top-left minus the localcoord origin. */
        p6_dl_sprite(jid, x, y, 0, 0, palblk);
#else
        jo_sprite_set_palette(palblk);
        /* Task #241 + CP5b.7: the slot is a fixed s_last_box_w x s_last_box_h box with
         * content in the top-left corner; the box CENTER sits at content-top-left +
         * box/2, so placing the center there lands the content at engine top-left (x,y). */
        jo_sprite_draw3D(jid,
                         x + s_last_box_w / 2 - JO_TV_WIDTH_2,
                         y + s_last_box_h / 2 - JO_TV_HEIGHT_2, 450);
#if defined(P6_FRONTEND_TITLE)
        p6_sgl_emit_probe(); /* #313: did this emit reach SGL SpriteEntry? */
#endif
#endif
    }
}

/* W14c (Task #227): flipped draw -- the DrawSprite FX_FLIP arm. VDP1 HF/VF
 * (CMDCTRL Dir bits, ST-013-R3 sec 5.5.4) mirror the PIXELS inside the
 * part's bbox; jo exposes them as the h/v flip attribute toggles
 * (sprites.h:292-312). The caller passes the RSDK world TOP-LEFT already
 * flip-adjusted (Drawing.cpp:2796-2808: x - width - pivotX for FLIP_X).
 *
 * Task #241: the slot is now a FIXED 64x64 box (content top-left). VDP1 HF
 * mirrors the WHOLE box around its center, moving content from columns [0,w)
 * to [64-w,64); VF mirrors rows [0,h) to [64-h,64). To keep the (flipped)
 * content top-left at engine (x,y), the box origin compensates by (64-w) in X
 * when flipped (and (64-h) in Y) -- which reduces to the symmetric center
 * formula below (32 == P6_SPR_MAXW/2 == P6_SPR_MAXH/2). */
void p6_vdp1_blit_flipped(int sheet, int x, int y, int w, int h, int sx, int sy,
                          int flipX, int flipY)
{
    int jid;

    if (sheet < 0 || sheet >= s_sheet_count) {
        ++p6_w_vdp1_handle_drops; /* W18: unbound-surface silent drop */
        p6_w_vdp1_lastdrop_h = sheet;
        return;
    }
#if defined(P6_FRONTEND_TITLE)
    /* EDGE-GLITCH FIX (this is the REAL Saturn draw path -- p6_draw_flipped always
     * calls THIS, never p6_vdp1_blit). CULL a sprite whose ROUTED box would cross
     * the 512 px VDP1 framebuffer line stride: the verbatim decomp TitleBG_Update
     * scrolls + wraps the TitleBG parallax band for a wider PC screen, so on
     * Saturn's 320 px screen those wide sprites land off both edges (MEASURED via
     * the per-blit ring: x=-67 left, x=293 right) and their box crosses the stride
     * -> the crossing columns WRAP to the opposite edge as the visible fragment.
     * Culling the box-crossing sprite drops only the distant-mountain band's few
     * edge pixels (imperceptible) and -- unlike re-staging a clipped sub-rect --
     * touches neither the source rect nor the LRU cache (which a per-scroll-position
     * rect would thrash into stale-slot garbage). See p6_box_in_stride. The centred
     * FG sprites (EMBLEM/RIBBON/Sonic/logo, MEASURED box within [0,512)) are never
     * culled. */
    if (!p6_box_in_stride(x, flipX, w, h))
        return;
#endif
    /* CP5b.7: routed jo id + s_last_box_w/h (small or large pool). */
    jid = p6_slot_for(sheet, sx, sy, w, h);
    if (jid < 0)
        return;

#if defined(P6_FRONTEND_TITLE)
    /* task #326 HEAD-VRAM forensic (THE REAL PATH): count the head sprite's staged
     * VDP1 VRAM non-transparent bytes. nz==0 -> the head's staged content is BLANK. */
    if (h >= 100 && w >= 100 && sy >= 400) {
        const volatile unsigned char *vr = (const volatile unsigned char *)
            (JO_VDP1_VRAM + JO_MULT_BY_8(__jo_sprite_def[jid].adr));
        unsigned short cs = __jo_sprite_def[jid].size;
        int vw = ((cs >> 8) & 0x3F) * 8, vh = cs & 0xFF;
        int k, n = vw * vh, nz = 0;
        for (k = 0; k < n; ++k) if (vr[k] != 0) ++nz;
        p6_w_headvram_nz  = nz;
        p6_w_headvram_wh  = (vw << 16) | (vh & 0xFFFF);
        p6_w_headvram_adr = __jo_sprite_def[jid].adr;
        p6_w_headvram_jid = jid;
    }
#endif
    ++p6_w_vdp1_landed; /* W18: a blit that reached a valid VDP1 slot */
    ++p6_w_vdp1_cmds; p6_w_vdp1_boxpx += s_last_box_w * s_last_box_h;
    p6_w_vdp1_contentpx += w * h;
    if (w > p6_w_vdp1_maxw) p6_w_vdp1_maxw = w;
    if (h > p6_w_vdp1_maxh) p6_w_vdp1_maxh = h;
    {
        int palblk;
#if defined(P6_GHZCUT_BOOT)
        /* #2a SURFACE-DRIVEN player palette (this is the REAL DrawSprite path -- the
         * decomp Player_Draw FX_FLIP arm always lands here): the PLROBJ sheet -> block 7. */
        if (sheet == p6_plr_sheet_slot && p6_plr_sheet_slot >= 0) {
            palblk = P6_BLIT_PLAYER_PALBLOCK;
            ++p6_w_plr_cut_landed;
        } else {
            palblk = P6_BLIT_PALBLOCK;
        }
#else
        palblk = P6_BLIT_PALBLOCK;
#endif
#if defined(P6_DIRECT_VDP1)
        /* #316 F1: HF/VF mirror WITHIN the content bbox (CMDSIZE == content after
         * the restage), and the caller's (x,y) is already the flip-adjusted RSDK
         * top-left (Drawing.cpp:2796-2808) -- so vertex A is (x,y) either way.
         * No box-compensation math, no sticky jo attributes. */
        p6_dl_sprite(jid, x, y, flipX, flipY, palblk);
#else
        jo_sprite_set_palette(palblk);
        if (flipX)
            jo_sprite_enable_horizontal_flip();
        if (flipY)
            jo_sprite_enable_vertical_flip();
        jo_sprite_draw3D(jid,
                         (flipX ? x + w - s_last_box_w / 2 : x + s_last_box_w / 2) - JO_TV_WIDTH_2,
                         (flipY ? y + h - s_last_box_h / 2 : y + s_last_box_h / 2) - JO_TV_HEIGHT_2, 450);
        if (flipX)
            jo_sprite_disable_horizontal_flip();
        if (flipY)
            jo_sprite_disable_vertical_flip();
#if defined(P6_FRONTEND_TITLE)
        p6_sgl_emit_probe(); /* #313: did this emit reach SGL SpriteEntry? */
#endif
#endif
    }
}

#if defined(P6_DIRECT_VDP1)
/* Fix 1 (user-symptom-map-v2 "Sonic invisible on slopes/ramps"): the ROTATED
 * draw entry the DrawSprite FX_ROTATE arms call (p6_io_main.cpp default: arm,
 * resolved rotation != 0 -- ROTSTYLE_FULL slope draws, plus the 90-degree-step
 * STATICFRAMES/45DEG/90DEG/180DEG styles). Same slot/texture plumbing as
 * p6_vdp1_blit_flipped (handle check -> p6_slot_for LRU fetch/stage ->
 * witnesses); the ONLY difference is the emitted command: a DISTORTED SPRITE
 * (ST-013-R3 sec 7.6) via p6_dl_sprite_rot instead of the axis-aligned normal
 * sprite. (x,y) is the RSDK entity screen position (pos, NOT pos+pivot --
 * Drawing.cpp:2815-2823 passes pos and the pivot separately to
 * DrawSpriteRotozoom); sn/cs = RSDK::Sin512/Cos512(rotation) computed by the
 * C++ caller (see p6_dl_sprite_rot's citation block). flipX = direction &
 * FLIP_X (decomp masks FLIP_Y off for rotated draws, Drawing.cpp:2822).
 * s_last_box_w/h after p6_slot_for hold the CONTENT (mult-8 padded w, h) in
 * the P6_FRONTEND_TITLE bucket flavor -- the pw/ph the quad spans. No
 * p6_box_in_stride cull (see the emit's clipping note). DIRECT-list flavors
 * only: the non-direct SGL path has no distorted-part emitter, so plain-GHZ
 * (no P6_DIRECT_VDP1) keeps its byte-identical dispatch (flag-parity rule);
 * the shipping chain build always runs P6_DIRECT_VDP1 (build_shipping.sh:172). */
void p6_vdp1_blit_rot(int sheet, int x, int y, int w, int h, int sx, int sy,
                      int pivotX, int pivotY, int sn, int cs, int flipX)
{
    int jid;

    if (sheet < 0 || sheet >= s_sheet_count) {
        ++p6_w_vdp1_handle_drops; /* W18: unbound-surface silent drop */
        p6_w_vdp1_lastdrop_h = sheet;
        return;
    }
    jid = p6_slot_for(sheet, sx, sy, w, h);
    if (jid < 0)
        return;
    ++p6_w_vdp1_landed;
    ++p6_w_vdp1_cmds; p6_w_vdp1_boxpx += s_last_box_w * s_last_box_h;
    p6_w_vdp1_contentpx += w * h;
    if (w > p6_w_vdp1_maxw) p6_w_vdp1_maxw = w;
    if (h > p6_w_vdp1_maxh) p6_w_vdp1_maxh = h;
    {
        int palblk;
#if defined(P6_GHZCUT_BOOT)
        /* #2a SURFACE-DRIVEN player palette (rotated player draws are the
         * dominant caller): the PLROBJ sheet routes to block 7. */
        if (sheet == p6_plr_sheet_slot && p6_plr_sheet_slot >= 0) {
            palblk = P6_BLIT_PLAYER_PALBLOCK;
            ++p6_w_plr_cut_landed;
        } else {
            palblk = P6_BLIT_PALBLOCK;
        }
#else
        palblk = P6_BLIT_PALBLOCK;
#endif
        p6_dl_sprite_rot(jid, x, y, s_last_box_w, s_last_box_h,
                         pivotX, pivotY, sn, cs, flipX, palblk);
    }
}
#endif /* P6_DIRECT_VDP1 */
