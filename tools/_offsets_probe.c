/* offline struct-offset probe for qa_signpost_run.py -- compiled with the SAME
 * census Game.h tree the live overlay uses; offsets read from the .s output.
 * Diagnostic tool only; never linked into any build. */
#include <stddef.h>
#include "Game.h"
#define O(tag, s, f) const unsigned long OFF_##tag = offsetof(s, f);
O(motobug_anim,   EntityMotobug,    animator)
O(crabmeat_anim,  EntityCrabmeat,   animator)
O(newtron_anim,   EntityNewtron,    animator)
O(chopper_anim,   EntityChopper,    animator)
O(batbrain_anim,  EntityBatbrain,   animator)
O(buzz_anim,      EntityBuzzBomber, animator)
O(bridge_anim,    EntityBridge,     animator)
O(signpost_state, EntitySignPost,   state)
O(signpost_type,  EntitySignPost,   type)
O(signpost_spin,  EntitySignPost,   spinCount)
O(player_state,   EntityPlayer,     state)
O(player_anim,    EntityPlayer,     animator)
O(ddw_state,      EntityDDWrecker,  state)
O(ddw_stateball,  EntityDDWrecker,  stateBall)
O(ddw_type,       EntityDDWrecker,  type)
O(ddw_health,     EntityDDWrecker,  health)
O(ddw_animator,   EntityDDWrecker,  animator)
O(o_ddw_anif,     ObjectDDWrecker,  aniFrames)
O(o_motobug_anif,  ObjectMotobug,    aniFrames)
O(o_crabmeat_anif, ObjectCrabmeat,   aniFrames)
O(o_newtron_anif,  ObjectNewtron,    aniFrames)
O(o_chopper_anif,  ObjectChopper,    aniFrames)
O(o_batbrain_anif, ObjectBatbrain,   aniFrames)
O(o_buzz_anif,     ObjectBuzzBomber, aniFrames)
O(o_bridge_anif,   ObjectBridge,     aniFrames)
const unsigned long SZ_ENTITY = sizeof(Entity);
