#include "RSDK/Core/RetroEngine.hpp"

using namespace RSDK;

// P4 Task #203 -- compile-time slot-fit guard. Every engine object TU carries this:
// objectEntityList is EntityBase[], so a registered EntityXxx must never exceed its
// slot or it corrupts the adjacent entity (the Phase 1.4-1.15 .bss-overflow class).
// EntityDefaultObject is empty (88 B) so this is always slack -- it documents the
// contract every object (incl. the P5 Ring) must satisfy on Saturn (data[0x40]).
static_assert(sizeof(EntityDefaultObject) <= sizeof(EntityBase),
              "EntityDefaultObject overflows its objectEntityList slot (OBJECT_DATA_COUNT too small)");

ObjectDefaultObject *RSDK::DefaultObject;

void RSDK::DefaultObject_Update()
{
    if (controller[CONT_ANY].keyUp.down) {
        if (screens[CONT_ANY].position.y > 0)
            screens[CONT_ANY].position.y -= 4;
    }
    else if (controller[CONT_ANY].keyDown.down) {
        screens[CONT_ANY].position.y += 4;
    }

    if (controller[CONT_ANY].keyLeft.down) {
        if (screens[CONT_ANY].position.x > 0)
            screens[CONT_ANY].position.x -= 4;
    }
    else if (controller[CONT_ANY].keyRight.down) {
        screens[CONT_ANY].position.x += 4;
    }
}

void RSDK::DefaultObject_LateUpdate() {}

void RSDK::DefaultObject_StaticUpdate() {}

void RSDK::DefaultObject_Draw() {}

void RSDK::DefaultObject_Create(void *data)
{
    RSDK_THIS(DefaultObject);

    self->active          = ACTIVE_ALWAYS;
    DefaultObject->active = ACTIVE_ALWAYS;
}

void RSDK::DefaultObject_StageLoad() {}

#if RETRO_REV0U
void RSDK::DefaultObject_StaticLoad(ObjectDefaultObject *staticVars) { memset(staticVars, 0, sizeof(*staticVars)); }
#endif

void RSDK::DefaultObject_EditorLoad() {}

void RSDK::DefaultObject_EditorDraw() {}

void RSDK::DefaultObject_Serialize() {}
