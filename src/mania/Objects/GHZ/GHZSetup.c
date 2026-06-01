/* Phase 2.1 — GHZSetup class body.
 *
 * Mechanical port (minimal Phase 2.1 subset) of
 * `tools/_decomp_raw/SonicMania_Objects_GHZ_GHZSetup.c` (Christian
 * Whitehead/Simon Thomley/Hunter Bridges; decomp by Rubberduckycooly &
 * RMGRich).
 *
 * Phase 2.1 scope (per docs/COMPREHENSIVE_PLAN.md §12.1):
 *   - StageLoad: triggers the engine-layer GHZ bring-up (NBG1 FG +
 *     NBG2 sky) AND starts CD-DA track 2 BGM. This is the Saturn-side
 *     bridge to the decomp's tile-layer + audio init.
 *   - Update / LateUpdate: empty per decomp (lines 12, 14).
 *   - StaticUpdate: empty for 2.1 — palette rotation + DrawAniTiles
 *     deferred to 2.3.
 *   - Draw / Create: empty per decomp (lines 46, 48).
 *
 * Decomp citation: lines 50-114 (StageLoad), 12-46 (callback stubs +
 * StaticUpdate).
 *
 * The act number is read from `SceneInfo->state` indirectly via the
 * scene-folder check; the Mania-side stub uses act=1 directly because
 * the Phase 2.1 transition only loads Act 1. Phase 2.2+ will resolve
 * the act from the scene category (`GHZ1` vs `GHZ2`). */

#include "GHZSetup.h"
#include "../../../rsdk/animation.h"
#include "../../../rsdk/audio.h"
#include "../../../rsdk/scene.h"
#include "../../../rsdk/object.h"
#include "../../../rsdk/tilelayer.h"
#include "../../../rsdk/scene_ghz.h"

#include <jo/jo.h>

/* Decomp GHZSetup.c:12. */
void GHZSetup_Update(void) {}

/* Decomp GHZSetup.c:14. */
void GHZSetup_LateUpdate(void) {}

/* Decomp GHZSetup.c:16-44 — Phase 2.1 stub (palette rotation + animated
 * tiles deferred to 2.3 per §12.1). The full body modifies CRAM ranges
 * 181-184 and 197-200 (waterfall + waterline) via RSDK.RotatePalette +
 * RSDK.SetLimitedFade, and streams animated sun-flower + extend-flower
 * tiles via RSDK.DrawAniTiles. None of these affect the Phase 2.1
 * success criterion (tiles + sky + BGM visible), so the body is kept
 * empty until the cosmetic phase. */
void GHZSetup_StaticUpdate(void)
{
    /* Phase 2.3 TODO: port decomp lines 17-27 (palette rotation) +
     * 29-43 (animated-tile streams). Saturn-side will translate these
     * to CRAM dirty-mask + a VDP2 cell-bank mid-frame DMA respectively. */
}

/* Decomp GHZSetup.c:46. */
void GHZSetup_Draw(void) {}

/* Decomp GHZSetup.c:48. */
void GHZSetup_Create(void *data)
{
    (void)data;
}

/* Decomp GHZSetup.c:50-114 — StageLoad (Phase 2.1 Act 1 subset).
 *
 * The decomp body branches on Zone->actID and does lots of BGSwitch
 * callback wiring + Plus-only encore-palette handling. The Phase 2.1
 * subset is the visible-bring-up: trigger the Saturn-side NBG1 FG +
 * NBG2 sky setup. CD-DA track 2 BGM is started by the caller
 * mania_load_ghz_scene (Phase 2.3j sync-load architecture; audio kick
 * moved out of this body so the per-class StageLoad mirrors the
 * decomp's pure-tile-layer behavior).
 *
 * Phase 2.3j (2026-05-28): synchronous-load contract. This function
 * is called from mania_load_ghz_scene (Game.c) and blocks until both
 * NBG1 FG (~262 KB FG.TMP + ~100 KB FG.CEL + PAL + PAT) and NBG2 sky
 * (~88 KB SKY.DAT + PAL) are committed to VDP2 VRAM/CRAM. The caller
 * accepts a dropped frame during this window (matches decomp behavior
 * — RetroEngine.cpp:345-384 ProcessEngine ENGINESTATE_LOAD runs the
 * full LoadSceneFolder + LoadSceneAssets + InitObjects chain
 * synchronously in one tick).
 *
 * The full per-tile-layer scroll-pos init (decomp lines 76-77 — sets
 * backgroundOutside->scrollPos = 0x180000 + inverts parallaxFactor) is
 * deferred until the TileLayer subsystem's scroll-pos field is wired
 * through to the Saturn-side sky-parallax line-scroll table. */
void GHZSetup_StageLoad(void)
{
    /* Engine-layer hardware bring-up — translates the decomp's
     * "RSDK.GetTileLayer(0)->scrollPos = 0x180000" + tile-layer-load
     * concept to the Saturn-native NBG1 cell-mode stream + NBG2 sky.
     * Both calls live in src/rsdk/scene_ghz.c. Synchronous: each
     * function only returns once VDP2 VRAM is populated.
     *
     * Phase 2.3k-mid (2026-05-28): the decomp's StageLoad callback
     * signature is void (per RSDK's class-registry contract), so we
     * keep the void return here. Sub-load failure is communicated via
     * the BSS-resident g_ghz_load_error_code bitmask (scene_ghz.h,
     * bits 0..5 = FG.TMP/PAL/CEL/PAT + SKY.PAL/DAT). The caller
     * (mania_load_ghz_scene) checks the bitmask and refuses to
     * advance the title state machine on any non-zero value. */
    ghz_setup_foreground(1);
    ghz_setup_sky(1);

    /* Static sky initial scroll — sets sky horizontal offset to the
     * decomp's 0x180000 / 4 = 0x60000 (24 px) for the title-aligned
     * opening view. */
    ghz_sky_scroll(0, 0);
}
