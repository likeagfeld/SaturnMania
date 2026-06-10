#include "RSDK/Core/RetroEngine.hpp"

#if RETRO_REV02
using namespace RSDK;

// P4 Task #203 -- compile-time slot-fit guard (RED-first). objectEntityList is
// EntityBase[]; an EntityDevOutput larger than its slot overflows into the adjacent
// entity (the Phase 1.4-1.15 .bss-corruption class). On Saturn data[0x40] makes
// sizeof(EntityBase)==344, so this is RED until message[] is Saturn-shrunk below.
// Off Saturn it is the stock 1112<=1112 and always passes. Correct on every platform.
static_assert(sizeof(EntityDevOutput) <= sizeof(EntityBase),
              "EntityDevOutput overflows its objectEntityList slot (OBJECT_DATA_COUNT too small)");

ObjectDevOutput *RSDK::DevOutput;

void RSDK::DevOutput_Update()
{
    RSDK_THIS(DevOutput);

    switch (self->state) {
        default: break;

        case DEVOUTPUT_DELAY:
            if (self->timer <= 0)
                self->state = DEVOUTPUT_ENTERPOPUP;
            else
                self->timer--;
            break;

        case DEVOUTPUT_ENTERPOPUP:
            if (self->position.y >= 0)
                self->state = DEVOUTPUT_SHOWPOPUP;
            else
                self->position.y += 2;
            break;

        case DEVOUTPUT_SHOWPOPUP:
            if (self->timer >= 120)
                self->state = 3;
            else
                self->timer++;
            break;

        case DEVOUTPUT_EXITPOPUP:
            self->position.y -= 2;
            if (-self->position.y > self->ySize)
                ResetEntity(self, TYPE_DEFAULTOBJECT, NULL);
            break;
    }
}

void RSDK::DevOutput_LateUpdate() {}

void RSDK::DevOutput_StaticUpdate() {}

void RSDK::DevOutput_Draw()
{
    RSDK_THIS(DevOutput);

    DrawRectangle(0, 0, currentScreen->size.x, self->position.y + self->ySize, 0x000080, 0xFF, INK_NONE, true);
    DrawDevString(self->message, 8, self->position.y + 8, 0, 0xF0F0F0);
}

void RSDK::DevOutput_Create(void *data)
{
    RSDK_THIS(DevOutput);
    strncpy(self->message, (char *)data, sizeof(self->message)); // sizeof tracks the Saturn-shrunk message[] (Task #203). Byte-identical on PC (message is 1012 there, same as the stock literal).

    self->active      = ACTIVE_ALWAYS;
    self->visible     = true;
    self->isPermanent = true;
    self->drawGroup   = 15;
    self->timer       = 180 * GetEntityCount(DevOutput->classID, false);
    self->ySize       = DevOutput_GetStringYSize(self->message);
    self->position.y  = -self->ySize;
}

void RSDK::DevOutput_StageLoad() {}

#if RETRO_REV0U
void RSDK::DevOutput_StaticLoad(ObjectDevOutput *staticVars) { memset(staticVars, 0, sizeof(*staticVars)); }
#endif

void RSDK::DevOutput_EditorLoad() {}

void RSDK::DevOutput_EditorDraw() {}

void RSDK::DevOutput_Serialize() {}

int32 RSDK::DevOutput_GetStringYSize(char *string)
{
    if (!*string)
        return 24;

    int32 lineCount = 0;
    while (*string) {
        if (*string == '\n')
            lineCount++;

        ++string;
    }

    if (lineCount >= 1)
        return 8 * lineCount + 16;
    else
        return 24;
}
#endif
