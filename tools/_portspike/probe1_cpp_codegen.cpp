// Probe 1 (engine true-port feasibility spike, Task #194).
// Question: does jo-engine's bundled sh-none-elf-gcc-8.2.0 compile C++ to SH-2
// freestanding (-m2 -nostdlib -fno-exceptions -fno-rtti)?
// It exercises EXACTLY the C++ language features the RSDKv5 core relies on when
// RETRO_USE_MOD_LOADER=0: namespaces, classes + member functions + ctors,
// references, function-pointer struct members (the no-STL object model from
// Object.hpp:146 #else branch), fixed arrays, 64-bit ints, sizeof, extern "C".
// No standard-library headers at all -> isolates pure C++ codegen capability.
namespace RSDK {

typedef signed char       int8;
typedef unsigned char     uint8;
typedef signed int        int32;
typedef unsigned int      uint32;
typedef signed long long  int64;

struct Entity {
    int32 x, y;
    uint8 active;
};

// Mirrors RSDK::Object with RETRO_USE_MOD_LOADER == 0 (raw C function pointers,
// NOT std::function -- see Object.hpp:146).
struct Object {
    void (*update)(void);
    void (*draw)(void);
    void (*create)(void *data);
    uint32 entityClassSize;
};

static Object objectList[256];
static Entity entityList[1056];
static int32  objectCount = 0;

class GameEngine {
    int32 frame;
public:
    GameEngine() : frame(0) {}
    int32 step(Entity &e)
    {
        e.x += 1;
        return ++frame;
    }
};

static GameEngine s_engine;

static void obj_update(void) { entityList[0].active = 1; }

extern "C" int32 probe_register(void)
{
    objectList[objectCount].update          = obj_update;
    objectList[objectCount].draw            = obj_update;
    objectList[objectCount].entityClassSize = (uint32)sizeof(Entity);
    objectCount++;

    int64 wide = (int64)objectCount * 0x100000000LL;
    return s_engine.step(entityList[0]) + (int32)(wide >> 32);
}

} // namespace RSDK
