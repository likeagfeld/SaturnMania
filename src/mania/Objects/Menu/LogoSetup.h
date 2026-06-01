#ifndef MANIA_MENU_LOGOSETUP_H
#define MANIA_MENU_LOGOSETUP_H

/* Phase 3.1 Path B — Sega-only abbreviated pre-title logo splash.
 *
 * Decomp authority: tools/_decomp_raw/SonicMania_Objects_Menu_LogoSetup.c
 * (Christian Whitehead/Simon Thomley/Hunter Bridges; decomp by
 *  Rubberduckycooly & RMGRich).
 *
 * Saturn-side adaptation rationale (CLAUDE.md §4.5.1 boot-budget):
 *   * The decomp's State_ShowLogos + State_FadeToNext + State_NextLogos
 *     state machine cycles through ALL FOUR logos (Sega, CW, HC, PWG)
 *     across roughly 16 s of pre-title runtime on PC.  Saturn's
 *     synchronous boot path adds the Mednafen-emulated CD-DA seek
 *     latency on top, so faithfully porting the full 4-logo sequence
 *     blows the 5 s budget (~3.2 s for boot ROM + ~16 s logos +
 *     ~2 s title fade = boot-to-title ~21 s on Saturn).
 *   * Path B (user-approved 2026-05-28 per Task #123): ship ONLY the
 *     Sega frame for ~2 s before the existing Title scene takes over.
 *   * Audio: per LogoSetup.c:101-103 the decomp calls
 *     RSDK.PlaySfx(LogoSetup->sfxSega) when entering State_FadeToNext
 *     iff ScreenInfo->position.y == 0 (top-screen logo).  Saturn has
 *     Sega.wav at extracted/Data/SoundFX/Stage/Sega.wav but the
 *     RSDK_PlaySfx path is not wired into the boot sequence (audio
 *     module init defers SFX bank loads to post-title); for Phase 3.1
 *     the audio cue is stubbed (skipped, not broken) per the strict
 *     constraint "If Sega.wav SFX is absent, skip audio — don't ship
 *     broken audio path".
 *
 * The Saturn-side state machine deliberately does NOT register a
 * full RSDK class.  LogoSetup never appears in the Title Scene1.bin
 * (it only exists in Logos/Scene1.bin which the Saturn build skips —
 * the entire logo splash on Saturn runs before any scene loads).
 * Instead, it's a tiny standalone state machine driven from the title
 * tick path in Game.c, gated on s_logo_state != LOGO_STATE_DONE.
 *
 * Asset contract:
 *   cd/LOGOS.ATL  built by tools/build_logos_atlas.py (Sega frame only,
 *                 ~5.6 KB, 4-bpp Color Bank 16 atlas v4).  Loaded via
 *                 the same load_*_atlas pattern as TSONIC/ELECTRA/
 *                 TITLE3D in TitleAssets.c.
 *
 * Sub-state timing (60 Hz tick):
 *   LOGO_STATE_INIT       1 tick   asset-load + state-pointer arm
 *   LOGO_STATE_SHOW_SEGA  60 ticks Sega logo fully visible
 *   LOGO_STATE_FADE_OUT   60 ticks black overlay ramp 0..255
 *   LOGO_STATE_DONE       latched  Title scene takes over
 *   TOTAL                 ~121 ticks = ~2.02 s on a 60 Hz NTSC field
 */

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    LOGO_STATE_INIT      = 0,
    LOGO_STATE_SHOW_SEGA = 1,
    LOGO_STATE_FADE_OUT  = 2,
    LOGO_STATE_DONE      = 3
} logo_state_t;

/* Per-frame tick.  Advances s_logo_state, increments s_logo_timer.
 * Must be called UNCONDITIONALLY each tick from mania_tick BEFORE
 * title_state_tick + title_direct_draw (so when LogoSetup is active
 * the title rendering stays gated). */
void LogoSetup_Update(void);

/* Per-frame draw.  Issues the VDP1 sprite draw for the Sega logo
 * during SHOW_SEGA + FADE_OUT (during fade, the logo still draws
 * but at half-transparency — Saturn's only single-pass alpha primitive
 * — to approximate the decomp's RSDK.FillScreen darkening ramp). */
void LogoSetup_Draw(void);

/* Predicate: LogoSetup has reached LOGO_STATE_DONE.  When true, the
 * title state machine + title_direct_draw resume their normal pre-Phase
 * 3.1 pipeline.  When false, title rendering is gated off so the
 * Sega frame is not over-painted. */
int  LogoSetup_IsDone(void);

/* Predicate: LogoSetup has been initialised at least once.  Used by
 * the title pipeline to distinguish "before LogoSetup started" (paint
 * normal black fade-in) vs "during/after LogoSetup" (delegate the
 * frame to LogoSetup_Draw or the post-Logo Title pipeline). */
int  LogoSetup_IsActive(void);

/* Internal accessor: current sub-state (for the RED-gate to query). */
logo_state_t LogoSetup_State(void);

#endif /* MANIA_MENU_LOGOSETUP_H */
