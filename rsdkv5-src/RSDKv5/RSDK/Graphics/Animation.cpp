#include "RSDK/Core/RetroEngine.hpp"

using namespace RSDK;

#if RETRO_REV0U
#include "Legacy/AnimationLegacy.cpp"
#endif

#if !defined(P6_SCENE_TEST) // P6.5b2: relocates to static backing in p6_io_main.cpp (pointer form, Animation.hpp:80-84)
SpriteAnimation RSDK::spriteAnimationList[SPRFILE_COUNT];
#endif

uint16 RSDK::LoadSpriteAnimation(const char *filePath, uint8 scope)
{
    if (!scope || scope > SCOPE_STAGE)
        return -1;

    char fullFilePath[0x100];
    sprintf_s(fullFilePath, sizeof(fullFilePath), "Data/Sprites/%s", filePath);

    RETRO_HASH_MD5(hash);
    GEN_HASH_MD5(filePath, hash);

    for (int32 i = 0; i < SPRFILE_COUNT; ++i) {
        if (HASH_MATCH_MD5(spriteAnimationList[i].hash, hash))
            return i;
    }

    uint16 id = -1;
    for (id = 0; id < SPRFILE_COUNT; ++id) {
        if (spriteAnimationList[id].scope == SCOPE_NONE)
            break;
    }

    if (id >= SPRFILE_COUNT)
        return -1;

    char nameBuffer[0x8][0x20];
    uint8 sheetIDs[0x18];
    sheetIDs[0] = 0;

#if defined(P6_SCENE_TEST)
    // Task #227 wedge bisect: phase stamp = (sprfile id << 8) | phase.
    // Phases: 1 pre-open, 2 opened, 3 frames-alloc'd, 4 sheets resolved,
    // 5 anims alloc'd, 6 parse done, 7 closed. After a wedge the savestate
    // peek names the exact file + step.
#define P6_ANIM_STAMP(ph)                                                                                                                            \
    do {                                                                                                                                             \
        extern int32 p6_w_anim_step;                                                                                                                 \
        p6_w_anim_step = ((int32)id << 8) | (ph);                                                                                                    \
    } while (0)
#else
#define P6_ANIM_STAMP(ph)
#endif

    FileInfo info;
    InitFileInfo(&info);
    P6_ANIM_STAMP(1);
    if (LoadFile(&info, fullFilePath, FMODE_RB)) {
        P6_ANIM_STAMP(2);
        uint32 sig = ReadInt32(&info, false);

        if (sig != RSDK_SIGNATURE_SPR) {
            CloseFile(&info);
            return -1;
        }

        SpriteAnimation *spr = &spriteAnimationList[id];
        spr->scope           = scope;
        memcpy(spr->hash, hash, 4 * sizeof(uint32));

        uint32 frameCount = ReadInt32(&info, false);
        AllocateStorage((void **)&spr->frames, frameCount * sizeof(SpriteFrame), DATASET_STG, false);
        P6_ANIM_STAMP(3);
#if RETRO_PLATFORM == RETRO_SATURN
        // Task #227 RUNAWAY-READ guard (same class as the LoadSpriteSheet
        // alloc-fail fix below at !RETRO_USE_ORIGINAL_CODE): on a FULL
        // DATASET_STG pool the frames/animations pointers stay NULL; the
        // parse then writes anim headers through NULL (ignored, ROM region)
        // and reads frameCount BACK from ROM as garbage -- MEASURED
        // (p6_c7.mcs): the frame loop ran 206 window refills to pack sector
        // 61210, 238 sectors PAST Sonic.bin's extent, and never returned.
        // Refuse-and-witness instead: the entry is released and the caller
        // gets -1 (the missing-file result), keeping the gap visible.
        if (!spr->frames) {
            extern int32 p6_saturn_anim_allocfail;
            extern int32 p6_w_anim_lastfail;
            extern int32 p6_w_stg_at_fail;
            ++p6_saturn_anim_allocfail;
            p6_w_anim_lastfail = ((int32)id << 16) | (int32)frameCount;
            p6_w_stg_at_fail   = (int32)dataStorage[DATASET_STG].usedStorage * 4;
            spr->scope = SCOPE_NONE;
            memset(spr->hash, 0, 4 * sizeof(uint32));
            CloseFile(&info);
            return -1;
        }
#endif

        uint8 sheetCount = ReadInt8(&info);
        for (int32 s = 0; s < sheetCount; ++s) {
            ReadString(&info, fullFilePath);
            sheetIDs[s] = LoadSpriteSheet(fullFilePath, scope);
        }
        P6_ANIM_STAMP(4);

        uint8 hitboxCount = ReadInt8(&info);
        for (int32 h = 0; h < hitboxCount; ++h) {
            ReadString(&info, nameBuffer[h]);
        }

        spr->animCount = ReadInt16(&info);
        AllocateStorage((void **)&spr->animations, spr->animCount * sizeof(SpriteAnimationEntry), DATASET_STG, false);
        P6_ANIM_STAMP(5);
#if RETRO_PLATFORM == RETRO_SATURN
        // (rationale at the frames guard above -- a NULL animations array
        // is THE measured runaway: animation->frameCount reads back ROM)
        if (!spr->animations) {
            extern int32 p6_saturn_anim_allocfail;
            extern int32 p6_w_anim_lastfail;
            extern int32 p6_w_stg_at_fail;
            ++p6_saturn_anim_allocfail;
            p6_w_anim_lastfail = ((int32)id << 16) | 0x8000 | (int32)spr->animCount;
            p6_w_stg_at_fail   = (int32)dataStorage[DATASET_STG].usedStorage * 4;
            spr->scope = SCOPE_NONE;
            memset(spr->hash, 0, 4 * sizeof(uint32));
            CloseFile(&info);
            return -1;
        }
#endif

        int32 frameID = 0;
        for (int32 a = 0; a < spr->animCount; ++a) {
            SpriteAnimationEntry *animation = &spr->animations[a];
            ReadString(&info, textBuffer);
            GEN_HASH_MD5_BUFFER(textBuffer, animation->hash);

            animation->frameCount      = ReadInt16(&info);
            animation->frameListOffset = frameID;
            animation->animationSpeed  = ReadInt16(&info);
            animation->loopIndex       = ReadInt8(&info);
            animation->rotationStyle   = ReadInt8(&info);

            for (int32 f = 0; f < animation->frameCount; ++f) {
                SpriteFrame *frame = &spr->frames[frameID++];

                frame->sheetID     = sheetIDs[ReadInt8(&info)];
                frame->duration    = ReadInt16(&info);
                frame->unicodeChar = ReadInt16(&info);
                frame->sprX        = ReadInt16(&info);
                frame->sprY        = ReadInt16(&info);
                frame->width       = ReadInt16(&info);
                frame->height      = ReadInt16(&info);
                frame->pivotX      = ReadInt16(&info);
                frame->pivotY      = ReadInt16(&info);

#if RETRO_PLATFORM == RETRO_SATURN
                // FRAMEHITBOX_COUNT Saturn retarget (Animation.hpp): clamp +
                // witness a file exceeding it -- extra hitboxes are read (the
                // stream must stay in sync) and dropped.
                frame->hitboxCount = hitboxCount > FRAMEHITBOX_COUNT ? FRAMEHITBOX_COUNT : hitboxCount;
                for (int32 h = 0; h < hitboxCount; ++h) {
                    int16 l = ReadInt16(&info);
                    int16 t = ReadInt16(&info);
                    int16 r = ReadInt16(&info);
                    int16 b = ReadInt16(&info);
                    if (h < FRAMEHITBOX_COUNT) {
                        frame->hitboxes[h].left   = l;
                        frame->hitboxes[h].top    = t;
                        frame->hitboxes[h].right  = r;
                        frame->hitboxes[h].bottom = b;
                    }
                    else {
                        extern int32 p6_saturn_hitbox_clamps;
                        ++p6_saturn_hitbox_clamps;
                    }
                }
#else
                frame->hitboxCount = hitboxCount;
                for (int32 h = 0; h < hitboxCount; ++h) {
                    frame->hitboxes[h].left   = ReadInt16(&info);
                    frame->hitboxes[h].top    = ReadInt16(&info);
                    frame->hitboxes[h].right  = ReadInt16(&info);
                    frame->hitboxes[h].bottom = ReadInt16(&info);
                }
#endif
            }
        }

        P6_ANIM_STAMP(6);
        CloseFile(&info);
        P6_ANIM_STAMP(7);

        return id;
    }

    return -1;
}

uint16 RSDK::CreateSpriteAnimation(const char *filename, uint32 frameCount, uint32 animCount, uint8 scope)
{
    if (!scope || scope > SCOPE_STAGE)
        return -1;

    char fullFilePath[0x100];
    sprintf_s(fullFilePath, sizeof(fullFilePath), "Data/Sprites/%s", filename);

    RETRO_HASH_MD5(hash);
    GEN_HASH_MD5(filename, hash);

    for (int32 i = 0; i < SPRFILE_COUNT; ++i) {
        if (HASH_MATCH_MD5(spriteAnimationList[i].hash, hash)) {
            return i;
        }
    }

    uint16 id = -1;
    for (id = 0; id < SPRFILE_COUNT; ++id) {
        if (spriteAnimationList[id].scope == SCOPE_NONE)
            break;
    }

    if (id >= SPRFILE_COUNT)
        return -1;

    SpriteAnimation *spr = &spriteAnimationList[id];
    spr->scope           = scope;
    memcpy(spr->hash, hash, 4 * sizeof(uint32));

    AllocateStorage((void **)&spr->frames, sizeof(SpriteFrame) * MIN(frameCount, SPRITEFRAME_COUNT), DATASET_STG, true);
    AllocateStorage((void **)&spr->animations, sizeof(SpriteAnimationEntry) * MIN(animCount, SPRITEANIM_COUNT), DATASET_STG, true);

    return id;
}

void RSDK::ProcessAnimation(Animator *animator)
{
    if (!animator || !animator->frames)
        return;

    animator->timer += animator->speed;

    if (animator->frames == (SpriteFrame *)1) { // model anim
        while (animator->timer > animator->frameDuration) {
            ++animator->frameID;

            animator->timer -= animator->frameDuration;
            if (animator->frameID >= animator->frameCount)
                animator->frameID = animator->loopIndex;
        }
    }
    else { // sprite anim
        while (animator->timer > animator->frameDuration) {
            ++animator->frameID;

            animator->timer -= animator->frameDuration;
            if (animator->frameID >= animator->frameCount)
                animator->frameID = animator->loopIndex;

            animator->frameDuration = animator->frames[animator->frameID].duration;
        }
    }
}

int32 RSDK::GetStringWidth(uint16 aniFrames, uint16 animID, String *string, int32 startIndex, int32 length, int32 spacing)
{
    if (aniFrames >= SPRFILE_COUNT || !string || !string->chars)
        return 0;

    SpriteAnimation *spr = &spriteAnimationList[aniFrames];
    if (animID < spr->animCount) {
        SpriteAnimationEntry *anim = &spr->animations[animID];

        startIndex = CLAMP(startIndex, 0, string->length - 1);

        if (length <= 0 || length > string->length)
            length = string->length;

        int32 w = 0;
        for (int32 c = startIndex; c < length; ++c) {
            int32 charFrame = string->chars[c];
            if (charFrame < anim->frameCount) {
                w += spr->frames[anim->frameListOffset + charFrame].width;
                if (c + 1 >= length)
                    return w;

                w += spacing;
            }
        }

        return w;
    }

    return 0;
}

void RSDK::SetSpriteString(uint16 aniFrames, uint16 animID, String *string)
{
    if (aniFrames >= SPRFILE_COUNT || !string)
        return;

    SpriteAnimation *spr = &spriteAnimationList[aniFrames];
    if (animID < spr->animCount) {
        SpriteAnimationEntry *anim = &spr->animations[animID];

        for (int32 c = 0; c < string->length; ++c) {
            int32 unicodeChar = string->chars[c];
            string->chars[c]  = -1;
            for (int32 f = 0; f < anim->frameCount; ++f) {
                if (spr->frames[anim->frameListOffset + f].unicodeChar == unicodeChar) {
                    string->chars[c] = f;
                    break;
                }
            }
        }
    }
}
