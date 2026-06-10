#ifndef OBJ_DEVOUTPUT_H
#define OBJ_DEVOUTPUT_H

#if RETRO_REV02
namespace RSDK
{

enum DevOutputStates {
    DEVOUTPUT_DELAY,
    DEVOUTPUT_ENTERPOPUP,
    DEVOUTPUT_SHOWPOPUP,
    DEVOUTPUT_EXITPOPUP,
};

// Object Class
struct ObjectDevOutput : Object {
    // Nothin'
};

// Entity Class
struct EntityDevOutput : Entity {
    int32 state;
    int32 timer;
    int32 ySize;
#if RETRO_PLATFORM == RETRO_SATURN
    // P4 Task #203: message[1012] makes EntityDevOutput 1112 B, which OVERFLOWS the
    // Saturn EntityBase slot (data[0x40] => 344 B). This holds dev-only on-screen
    // popup text (DevOutput_Draw -> DrawDevString) that NEVER renders in the P5 proof.
    // Shrink to fit: 88(Entity) + 12 + 240 == 340 <= 344. P6 RESTORATION: drop the
    // guard; message returns to [1012] when OBJECT_DATA_COUNT is 0x100.
    char message[240];
#else
    char message[1012];
#endif
};

// Object Entity
extern ObjectDevOutput *DevOutput;

// Standard Entity Events
void DevOutput_Update();
void DevOutput_LateUpdate();
void DevOutput_StaticUpdate();
void DevOutput_Draw();
void DevOutput_Create(void *data);
void DevOutput_StageLoad();
#if RETRO_REV0U
void DevOutput_StaticLoad(ObjectDevOutput *staticVars);
#endif
void DevOutput_EditorLoad();
void DevOutput_EditorDraw();
void DevOutput_Serialize();

// Extra Entity Functions
int32 DevOutput_GetStringYSize(char *string);

} // namespace RSDK
#endif

#endif //! OBJ_DEVOUTPUT_H
