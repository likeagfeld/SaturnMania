#include "RSDK/Core/RetroEngine.hpp"
#include <math.h>

using namespace RSDK;

int32 RSDK::sin1024LookupTable[0x400];
int32 RSDK::cos1024LookupTable[0x400];
int32 RSDK::tan1024LookupTable[0x400];
int32 RSDK::asin1024LookupTable[0x400];
int32 RSDK::acos1024LookupTable[0x400];

int32 RSDK::sin512LookupTable[0x200];
int32 RSDK::cos512LookupTable[0x200];
int32 RSDK::tan512LookupTable[0x200];
int32 RSDK::asin512LookupTable[0x200];
int32 RSDK::acos512LookupTable[0x200];

int32 RSDK::sin256LookupTable[0x100];
int32 RSDK::cos256LookupTable[0x100];
int32 RSDK::tan256LookupTable[0x100];
int32 RSDK::asin256LookupTable[0x100];
int32 RSDK::acos256LookupTable[0x100];

uint8 RSDK::arcTan256LookupTable[0x100 * 0x100];

#if RETRO_PLATFORM == RETRO_SATURN
// P6.1-fast (Task #212): the float loops in CalculateTrigAngles below end in a
// 65,536-iteration DOUBLE-precision atan2 fill -- a multi-minute one-time
// soft-float boot stall on the FPU-less SH-2 (no FPU -> every atan2 is thousands
// of newlib/libgcc _unpack_df/_pack_df cycles). These 16 const tables are THIS
// build's own on-target newlib output, captured from the savestate after the real
// fill ran (tools/_portspike/_p6/gen_trig_tables.py), so they are bit-identical to
// what the loops compute. Only the runtime FILL is replaced, never the values.
#include "TrigTables_Saturn.inc"
#endif

uint32 RSDK::randSeed = 0;

void RSDK::ClearTrigLookupTables()
{
    memset(sin256LookupTable, 0, sizeof(sin256LookupTable));
    memset(cos256LookupTable, 0, sizeof(cos256LookupTable));
    memset(tan256LookupTable, 0, sizeof(tan256LookupTable));
    memset(asin256LookupTable, 0, sizeof(asin256LookupTable));
    memset(acos256LookupTable, 0, sizeof(acos256LookupTable));
    memset(sin512LookupTable, 0, sizeof(sin512LookupTable));
    memset(cos512LookupTable, 0, sizeof(cos512LookupTable));
    memset(tan512LookupTable, 0, sizeof(tan512LookupTable));
    memset(asin512LookupTable, 0, sizeof(asin512LookupTable));
    memset(acos512LookupTable, 0, sizeof(acos512LookupTable));
    memset(sin1024LookupTable, 0, sizeof(sin1024LookupTable));
    memset(cos1024LookupTable, 0, sizeof(cos1024LookupTable));
    memset(tan1024LookupTable, 0, sizeof(tan1024LookupTable));
    memset(asin1024LookupTable, 0, sizeof(asin1024LookupTable));
    memset(acos1024LookupTable, 0, sizeof(acos1024LookupTable));
    memset(arcTan256LookupTable, 0, sizeof(arcTan256LookupTable));
    randSeed = 0;
}

void RSDK::CalculateTrigAngles()
{
    srand((uint32)time(NULL));
    randSeed = rand();

#if RETRO_PLATFORM == RETRO_SATURN
    // P6.1-fast (Task #212): the float loops below culminate in a 65,536-iteration
    // double-precision atan2 fill -- a multi-minute one-time soft-float stall on
    // the FPU-less SH-2. Replace the deterministic FILL with a memcpy of the baked
    // on-target tables (TrigTables_Saturn.inc). randSeed above is preserved; only
    // the table contents are swapped in, bit-identical to the loops' own output.
    memcpy(sin1024LookupTable, saturn_sin1024LookupTable, sizeof(sin1024LookupTable));
    memcpy(cos1024LookupTable, saturn_cos1024LookupTable, sizeof(cos1024LookupTable));
    memcpy(tan1024LookupTable, saturn_tan1024LookupTable, sizeof(tan1024LookupTable));
    memcpy(asin1024LookupTable, saturn_asin1024LookupTable, sizeof(asin1024LookupTable));
    memcpy(acos1024LookupTable, saturn_acos1024LookupTable, sizeof(acos1024LookupTable));
    memcpy(sin512LookupTable, saturn_sin512LookupTable, sizeof(sin512LookupTable));
    memcpy(cos512LookupTable, saturn_cos512LookupTable, sizeof(cos512LookupTable));
    memcpy(tan512LookupTable, saturn_tan512LookupTable, sizeof(tan512LookupTable));
    memcpy(asin512LookupTable, saturn_asin512LookupTable, sizeof(asin512LookupTable));
    memcpy(acos512LookupTable, saturn_acos512LookupTable, sizeof(acos512LookupTable));
    memcpy(sin256LookupTable, saturn_sin256LookupTable, sizeof(sin256LookupTable));
    memcpy(cos256LookupTable, saturn_cos256LookupTable, sizeof(cos256LookupTable));
    memcpy(tan256LookupTable, saturn_tan256LookupTable, sizeof(tan256LookupTable));
    memcpy(asin256LookupTable, saturn_asin256LookupTable, sizeof(asin256LookupTable));
    memcpy(acos256LookupTable, saturn_acos256LookupTable, sizeof(acos256LookupTable));
    memcpy(arcTan256LookupTable, saturn_arcTan256LookupTable, sizeof(arcTan256LookupTable));
#else
    for (int32 i = 0; i < 0x400; ++i) {
        sin1024LookupTable[i]  = (int32)(sinf((i / 512.f) * RSDK_PI) * 1024.f);
        cos1024LookupTable[i]  = (int32)(cosf((i / 512.f) * RSDK_PI) * 1024.f);
        tan1024LookupTable[i]  = (int32)(tanf((i / 512.f) * RSDK_PI) * 1024.f);
        asin1024LookupTable[i] = (int32)((asinf(i / 1023.f) * 512.f) / RSDK_PI);
        acos1024LookupTable[i] = (int32)((acosf(i / 1023.f) * 512.f) / RSDK_PI);
    }

    cos1024LookupTable[0x000] = 0x400;
    cos1024LookupTable[0x100] = 0;
    cos1024LookupTable[0x200] = -0x400;
    cos1024LookupTable[0x300] = 0;

    sin1024LookupTable[0x000] = 0;
    sin1024LookupTable[0x100] = 0x400;
    sin1024LookupTable[0x200] = 0;
    sin1024LookupTable[0x300] = -0x400;

    for (int32 i = 0; i < 0x200; ++i) {
        sin512LookupTable[i]  = (int32)(sinf((i / 256.f) * RSDK_PI) * 512.f);
        cos512LookupTable[i]  = (int32)(cosf((i / 256.f) * RSDK_PI) * 512.f);
        tan512LookupTable[i]  = (int32)(tanf((i / 256.f) * RSDK_PI) * 512.f);
        asin512LookupTable[i] = (int32)((asinf(i / 511.f) * 256.f) / RSDK_PI);
        acos512LookupTable[i] = (int32)((acosf(i / 511.f) * 256.f) / RSDK_PI);
    }

    cos512LookupTable[0x00]  = 0x200;
    cos512LookupTable[0x80]  = 0;
    cos512LookupTable[0x100] = -0x200;
    cos512LookupTable[0x180] = 0;

    sin512LookupTable[0x00]  = 0;
    sin512LookupTable[0x80]  = 0x200;
    sin512LookupTable[0x100] = 0;
    sin512LookupTable[0x180] = -0x200;

    for (int32 i = 0; i < 0x100; i++) {
        sin256LookupTable[i]  = (int32)((sin512LookupTable[i * 2] >> 1));
        cos256LookupTable[i]  = (int32)((cos512LookupTable[i * 2] >> 1));
        tan256LookupTable[i]  = (int32)((tan512LookupTable[i * 2] >> 1));
        asin256LookupTable[i] = (int32)((asinf(i / 255.f) * 128.f) / RSDK_PI);
        acos256LookupTable[i] = (int32)((acosf(i / 255.f) * 128.f) / RSDK_PI);
    }

    for (int32 y = 0; y < 0x100; ++y) {
        uint8 *arcTan = (uint8 *)&arcTan256LookupTable[y];

        for (int32 x = 0; x < 0x100; ++x) {
            // 40.743664 = 0x100 / (2 * M_PI) (roughly)
            *arcTan = (int32)(float)((float)atan2((float)y, x) * 40.743664f);
            arcTan += 0x100;
        }
    }
#endif
}

uint8 RSDK::ArcTanLookup(int32 X, int32 Y)
{
    int32 x = abs(X);
    int32 y = abs(Y);

    if (x <= y) {
        while (y > 0xFF) {
            x >>= 4;
            y >>= 4;
        }
    }
    else {
        while (x > 0xFF) {
            x >>= 4;
            y >>= 4;
        }
    }
    if (X <= 0) {
        if (Y <= 0)
            return arcTan256LookupTable[(x << 8) + y] + 0x80;
        else
            return 0x80 - arcTan256LookupTable[(x << 8) + y];
    }
    else if (Y <= 0)
        return -arcTan256LookupTable[(x << 8) + y];
    else
        return arcTan256LookupTable[(x << 8) + y];
}
