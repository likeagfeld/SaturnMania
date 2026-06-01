#ifndef MANIA_OBJECTS_GLOBAL_PLAYER_H
#define MANIA_OBJECTS_GLOBAL_PLAYER_H

/* Phase 2.2 — Saturn-side Player.
 *
 * Mechanical port of `tools/_decomp_raw/SonicMania_Objects_Global_Player.c`
 * + `Player.h` (Christian Whitehead/Simon Thomley/Hunter Bridges; decomp
 * by Rubberduckycooly & RMGRich).
 *
 * Scope per docs/COMPREHENSIVE_PLAN.md §12.2 ("Phase 2.2 minimum"):
 *   - Sonic-only (ID_SONIC).
 *   - Two states: Player_State_Ground + Player_State_Air.
 *   - Mania-faithful Q16.16 physics from decomp sonicPhysicsTable[0..7].
 *   - Ground-projection along the floor angle (SIN8 table).
 *   - 17 px wall / cliff threshold using the per-column GHZ1SURF.BIN
 *     surface table (Saturn-port deviation: no 6-sensor sweep yet; the
 *     full RSDK sensor model lives in src/rsdk/collision.c and is
 *     scheduled for Phase 2.3 when badnik wall collisions need it).
 *   - Variable jump height + jump-along-normal.
 *   - Autorun + auto-jump AI ported verbatim from archived
 *     main.c:1740-1786.
 *
 * Phase 2.2 deferrals (each cited inline at the relevant point):
 *   - Player_State_Roll (decomp L3932-3958) — 2.2b.
 *   - Spindash / Peelout / Drop Dash — 2.2b.
 *   - Hurt / Death / Drown / OuttaHere / Transform — 2.3.
 *   - Super state / water / shields / invincibility — 2.5.
 *   - Per-character branches (Tails, Knuckles, Mighty, Ray) — 2.4.
 *   - Full RSDK.ProcessObjectMovement 6-sensor sweep — 2.3.
 *
 * Saturn-port deviations from the decomp:
 *   - The decomp `EntityPlayer` struct has ~250 fields. Phase 2.2's
 *     `player_t` carries ONLY the fields the active state machine
 *     consumes. Decomp-spelling C struct is preserved for future
 *     forward-compat (e.g. when Player_CheckBadnikBreak() lands in 2.3
 *     it will dereference `entity->groundVel` etc.).
 *   - Collision is per-column (GHZ1SURF.BIN) not per-sensor; see
 *     `docs/COMPREHENSIVE_PLAN.md` §12.2 "Collision strategy".
 *   - Gravity uses decomp Mania value 0x5800 (=0.34375) NOT archived
 *     Sonic 1/2 value 0x38000 (=0.21875). Per CLAUDE.md §1.
 *
 * Audit: every Saturn function below cites the source decomp file:line
 * range it mechanically translates. */

#include <jo/jo.h>
#include <stdint.h>
#include <stdbool.h>

/* Player character IDs — decomp Player.h ID_SONIC etc. via GameObjects.h.
 * Phase 2.2 only handles ID_SONIC; the others are reserved bits so future
 * per-character branches compile without changes. */
typedef enum {
    PLAYER_ID_SONIC    = 1,
    PLAYER_ID_TAILS    = 2,
    PLAYER_ID_KNUCKLES = 4,
    PLAYER_ID_MIGHTY   = 8,
    PLAYER_ID_RAY      = 16
} player_character_t;

/* Q16.16 fixed-point pixels (SGL slIntToFixed / JO_MULT_BY_65536 compatible).
 * Mirrors the archived src/_archived/physics.h convention. Decomp Mania uses
 * raw int32 = Q16.16 throughout. */
#define PLAYER_FIXED(x) ((int32_t)((double)(x) * 65536.0))

/* sms_world_t — per-column surface lookup for GHZ.
 *
 * Saturn-port deviation: the decomp uses RSDK.GetTileLayer + Process-
 * ObjectMovement which probes TileConfig.bin masks via 6 sensors. We use
 * the precomputed `cd/GHZ%dSURF.BIN` per-column table (4 B/col big-endian
 * `u16 surfY + u8 angle + u8 flag`, built by tools/build_collision.py
 * with MIN_SOLID_RUN=4 decoration filter per
 * memory/rsdk-decoration-tile-skip.md).
 *
 * Phase 2.3 will switch to the full sensor model in src/rsdk/collision.c
 * when badnik wall collisions need it. */
typedef struct {
    const unsigned char *raw;        /* 4 B/col big-endian; from jo_malloc  */
    int                  width_px;   /* level width in pixels (16384 GHZ1)  */
    int                  height_px;  /* level height (used to clamp falls)  */

    /* Phase 2.4g.3 — two-plane tile-collision bridge.
     *
     * The decomp probes TileConfig with a per-player `collisionPlane` (0=A,
     * 1=B) that PlaneSwitch writes as the player crosses a switch marker
     * (PlaneSwitch.c:94,103). Saturn mirrors that with a SECOND per-column
     * surface table `raw_alt` (the B path) and a mutable `active_path`
     * selector. Player_Tick sets `active_path = p->collisionPlane & 1` at
     * the top of every step; Player_SurfaceY/Angle/Flag then read
     * `active_path ? raw_alt : raw`.
     *
     * GHZ1SURF.BIN currently carries only the primary plane, so Game.c
     * seeds `raw_alt = raw` (the B path is degenerate / identical to A for
     * now). The PlaneSwitch write still TOGGLES which table pointer the
     * probe reads — when a real divergent second plane is extracted, only
     * the `raw_alt` seed in Game.c changes; no probe-path code changes. */
    const unsigned char *raw_alt;    /* plane B table (== raw until B asset) */
    int                  active_path;/* 0 = raw (plane A), 1 = raw_alt (B)   */
} sms_world_t;

#define SMS_NO_FLOOR 0xFFFF

/* player_t — minimal runtime struct.
 *
 * This is NOT the full decomp EntityPlayer — see header comment for the
 * scope rationale. Fields named to mirror their decomp counterparts so
 * future-phase ports of objects that touch the player don't need
 * field-name translation:
 *   gsp           ↔ EntityPlayer::groundVel
 *   xsp, ysp      ↔ EntityPlayer::velocity.x/y
 *   xpos, ypos    ↔ EntityPlayer::position.x/y
 *   angle         ↔ EntityPlayer::angle (Q0.8, 0=flat)
 *   onGround      ↔ EntityPlayer::onGround
 *   facing_left   ↔ EntityPlayer::direction & FLIP_X
 *   topSpeed/...  ↔ matching EntityPlayer physics-state fields */
typedef struct {
    /* Kinematics (Q16.16 world pixels) */
    int32_t xpos, ypos;
    int32_t xsp, ysp;          /* velocity (decomp velocity.x/y)            */
    int32_t gsp;               /* ground speed (decomp groundVel)           */
    int     angle;             /* Q0.8 ground angle (decomp angle byte)     */

    /* State flags */
    bool    onGround;          /* decomp EntityPlayer::onGround             */
    bool    jumping;           /* in air due to jump (enables jumpCap)      */
    bool    facing_left;       /* decomp direction & FLIP_X                 */
    bool    applyJumpCap;      /* decomp EntityPlayer::applyJumpCap         */

    /* Per-character physics-state (from sonicPhysicsTable per
     * UpdatePhysicsState, decomp Player.c:2747-2813). Phase 2.2 sets
     * these once at init from the default table-entry-0 chunk. */
    int32_t topSpeed;
    int32_t acceleration;
    int32_t deceleration;
    int32_t airAcceleration;
    int32_t skidSpeed;
    int32_t rollingFriction;
    int32_t jumpStrength;
    int32_t jumpCap;
    int32_t gravityStrength;

    /* Identity */
    player_character_t characterID;

    /* Input edge tracking — used by the auto-takeover logic in
     * mania_tick (archived main.c:1740-1753). */
    bool    prev_jump;

    /* Decomp EntityPlayer collision-state parity (Player.h upstream).
     * Phase 2.4g: solid-object Update bodies (InvisibleBlock, future
     * Platform/Bridge) raise these bits via Player_CheckCollisionBox
     * dispatch. collisionFlagH bit0=left, bit1=right; collisionFlagV
     * bit0=top(ceiling), bit1=bottom(floor). collisionPlane selects the
     * A/B tile-collision path (consumed once two-plane lands at 2.4g.3;
     * PlaneSwitch writes it per decomp PlaneSwitch.c). */
    int32_t collisionFlagH;
    int32_t collisionFlagV;
    uint8_t collisionPlane;
} player_t;

/* === Public API ============================================================ */

/* Initialise a player at a world position with a character's tuning. Sets
 * all physics-state fields to the canonical Mania default (no shoes, no
 * super, on land) from sonicPhysicsTable[0..7]. */
void Player_Init(player_t *p, player_character_t who,
                 int32_t xpos_fixed, int32_t ypos_fixed);

/* Read the topmost solid surface Y at world column x_px (clamped to level).
 * Returns SMS_NO_FLOOR for pit columns. Decomp equivalent: rsdk_find_floor
 * sensor probe; Phase 2.2 uses the precomputed per-column table. */
int  Player_SurfaceY(const sms_world_t *w, int x_px);

/* Return the Mania Q0.8 floor angle (unsigned 0..255) at world column. */
int  Player_SurfaceAngle(const sms_world_t *w, int x_px);

/* Return the TileConfig flag byte at world column (spikes, breakable...).
 * Phase 2.2 doesn't consume this; reserved for Phase 2.3 hazards. */
int  Player_SurfaceFlag(const sms_world_t *w, int x_px);

/* Phase 2.4h — accessor for the GHZ surface world (g_ghz_world is static
 * in Game.c). RSDK-engine badnik _Update callbacks (Chopper/Crabmeat/
 * Batbrain) run from rsdk_object_tick with no world parameter, but their
 * decomp wall/floor probes need the surface table. Returns NULL before
 * mania_load_ghz_scene has run. */
const sms_world_t *mania_ghz_world(void);

/* Advance one 60 Hz step.
 *
 * Mirrors decomp Player_Update body (Player.c:14-152) — the state-machine
 * dispatch + post-state ProcessObjectMovement. Saturn-side this collapses
 * to: read input → run state (ground or air) → integrate position →
 * surface-snap.
 *
 * Inputs:
 *   left/right/down/up - controller axes (after autorun merging)
 *   jump_held          - any of A/B/C currently held
 *
 * Per archived main.c:1786 the caller drives this once per game tick
 * with input states already merged. */
void Player_Tick(player_t *p, const sms_world_t *w,
                 bool left, bool right, bool down, bool up,
                 bool jump_held);

/* SIN8 table accessor (Q1.7 signed, 256-entry full circle). Exposed so
 * the auto-jump AI in mania_tick can also project look-ahead surfaces.
 * Generated from `sin(i * 2*pi / 256) * 127`. */
int8_t Player_Sin8(unsigned char a);

/* === Phase 2.4b — Decomp-exact collision side enum + box hitbox =====
 *
 * Per rsdkv5-src/RSDKv5/RSDK/Scene/Collision.hpp:13-22:
 *   enum CollisionSides { C_NONE, C_TOP, C_LEFT, C_RIGHT, C_BOTTOM };
 *
 * Per rsdkv5-src/RSDKv5/RSDK/Scene/Collision.hpp Hitbox struct:
 *   struct Hitbox { int16 left, top, right, bottom; };
 *
 * Saturn port uses int16 to match the decomp memory layout exactly. */
typedef enum {
    C_NONE   = 0,
    C_TOP    = 1,
    C_LEFT   = 2,
    C_RIGHT  = 3,
    C_BOTTOM = 4
} player_collision_side_t;

typedef struct {
    int16_t left;
    int16_t top;
    int16_t right;
    int16_t bottom;
} hitbox_t;

/* Decomp Player.c:12 — `Hitbox Player_FallbackHitbox = { -10, -20, 10, 20 };`
 * Decomp's Player_GetHitbox returns this when the active animation's
 * `RSDK.GetHitbox(&animator, 0)` returns NULL. Phase 2.2's player_t
 * doesn't carry an Animator; we use the fallback for every collision
 * site as the conservative-correct baseline. The dimensions match the
 * standing/rolling Sonic hitbox (20 px wide, 40 px tall, origin at
 * pelvis). */
extern hitbox_t Player_FallbackHitbox;

/* Phase 2.4b — Player_CheckCollisionBox.
 *
 * Decomp authority:
 *   - tools/_decomp_raw/SonicMania_Objects_Global_Player.c:2267-2341
 *   - rsdkv5-src/RSDKv5/RSDK/Scene/Collision.cpp:276-487
 *     (CheckObjectCollisionBox with setValues=true)
 *
 * Args:
 *   p          - player to test against (mutated on contact: position
 *                snaps to the box surface AND xsp/ysp zero on that
 *                axis when penetrating)
 *   box_x_px   - entity (box) world X in INTEGER pixels (decomp uses
 *                Q16.16 entity->position; Saturn entity tables store
 *                integer pixels per Entities.c, so the caller passes
 *                pixels directly)
 *   box_y_px   - entity world Y in integer pixels
 *   box        - entity's hitbox (decomp's `entityHitbox` arg — left/
 *                top/right/bottom in integer-pixel offsets relative
 *                to the entity origin)
 *
 * Returns: C_NONE, C_TOP, C_LEFT, C_RIGHT, C_BOTTOM — the side of the
 * box the player contacted. On C_LEFT/RIGHT/TOP/BOTTOM the player_t's
 * position is snapped to the surface AND the matching velocity
 * component is zeroed (per decomp Collision.cpp:386-460). */
int Player_CheckCollisionBox(player_t *p, int box_x_px, int box_y_px,
                             const hitbox_t *box);

#endif /* MANIA_OBJECTS_GLOBAL_PLAYER_H */
