#!/usr/bin/env python3
# P4 guard gate (Task #203) -- EntityBase.data[0x100] -> data[0x40] on Saturn.
#
# WHAT THIS LEVER DOES
#   EntityBase (Object.hpp) is Entity{88 B} + void *data[0x100]{1024 B} = 1112 B.
#   objectEntityList[ENTITY_COUNT] is EntityBase[] (Object.cpp:16), so at the stock
#   width every one of the 448 Saturn slots costs 1112 B -> 486.5 KB of .bss for the
#   entity table alone. The data[] block is a UNION OVERLAY: it only exists so that
#   sizeof(EntityBase) >= sizeof(the largest registered EntityXxx). The largest core
#   object is EntityDevOutput (char message[1012] -> 1112 B, sized to fill the slot
#   exactly). Shrinking data[] to 0x40 (256 B) makes EntityBase 344 B and reclaims
#   448 * (1112-344) = 344064 B ~= 336 KB.
#
# THE BUG CLASS THIS GATE CATCHES (RED-first)
#   If data[] shrinks BELOW sizeof(largest registered EntityXxx), the engine writes
#   that object's fields PAST its objectEntityList slot into the adjacent entity --
#   the exact Phase 1.4-1.15 .bss-overflow class (15 iterations). EntityDevOutput at
#   1112 B does NOT fit a 344 B slot. So the safe lever ALSO Saturn-shrinks
#   DevOutput's message[1012] -> message[240] (dev-only on-screen popup text that
#   never renders in the P5 proof) so EntityDevOutput is 340 <= 344, AND adds a
#   compile-time static_assert(sizeof(EntityXxx) <= sizeof(EntityBase)) to every
#   engine object TU (RED until the shrink lands), AND a Saturn-gated runtime refusal
#   in RegisterObject (Object.cpp:69) so any game object pulled later (e.g. P5 Ring)
#   that would overflow is rejected cleanly instead of corrupting .bss.
#
# RED on the current tree (macro undefined, data *0x100, message unshrunk, no asserts,
# no runtime guard). GREEN once every check below holds. PC build stays byte-identical
# (OBJECT_DATA_COUNT==0x100 off Saturn => EntityBase 1112; message[1012]; the static
# asserts are 1112<=1112; the runtime guard is #if'd out; strncpy sizeof==0x3F4).
#
# P6 RESTORATION: delete OBJECT_DATA_COUNT's Saturn branch + the DevOutput message
# guard + the Object.cpp Saturn refusal; data[] returns to 0x100 -- exactly like
# SCENEENTITY_COUNT and COLLISION_FLIPCOUNT.

import os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OBJ_HPP  = os.path.join(ROOT, "rsdkv5-src", "RSDKv5", "RSDK", "Scene", "Object.hpp")
OBJ_CPP  = os.path.join(ROOT, "rsdkv5-src", "RSDKv5", "RSDK", "Scene", "Object.cpp")
DEV_HPP  = os.path.join(ROOT, "rsdkv5-src", "RSDKv5", "RSDK", "Scene", "Objects", "DevOutput.hpp")
DEV_CPP  = os.path.join(ROOT, "rsdkv5-src", "RSDKv5", "RSDK", "Scene", "Objects", "DevOutput.cpp")
DEF_CPP  = os.path.join(ROOT, "rsdkv5-src", "RSDKv5", "RSDK", "Scene", "Objects", "DefaultObject.cpp")

# Fixed engine layout for the Saturn config (RETRO_REV02=1, RETRO_REV0U=0):
ENTITY_SIZE = 88   # sizeof(Entity), measured/derived this session
DEV_EXTRA   = 12   # EntityDevOutput adds int32 state + int32 timer + int32 ySize

def read(p):
    with open(p, "r", encoding="utf-8", errors="replace") as f:
        return f.read()

def norm(s):
    return re.sub(r"[ \t]+", " ", s)

checks = []  # (name, ok, detail)

ohpp = norm(read(OBJ_HPP))
ocpp = norm(read(OBJ_CPP))
dhpp = norm(read(DEV_HPP))
dcpp = norm(read(DEV_CPP))
fcpp = norm(read(DEF_CPP))

# E1: OBJECT_DATA_COUNT macro present, Saturn=0x40 / else=0x100
e1a = re.search(r"#define OBJECT_DATA_COUNT \(0x40\)", ohpp) is not None
e1b = re.search(r"#define OBJECT_DATA_COUNT \(0x100\)", ohpp) is not None
e1 = e1a and e1b
checks.append(("E1 OBJECT_DATA_COUNT macro (Saturn 0x40 / else 0x100)", e1,
               "found both #define forms" if e1 else f"saturn0x40={e1a} else0x100={e1b}"))

# E2: EntityBase uses data[OBJECT_DATA_COUNT], no bare data[0x100]
e2a = re.search(r"void \*data\[OBJECT_DATA_COUNT\]", ohpp) is not None
e2b = re.search(r"void \*data\[0x100\]", ohpp) is None
e2 = e2a and e2b
checks.append(("E2 EntityBase data[] uses OBJECT_DATA_COUNT (no bare 0x100)", e2,
               "migrated" if e2 else f"uses_macro={e2a} no_bare_0x100={e2b}"))

# E3: DevOutput.hpp Saturn-gates message[] to a shrunk size (240) with the stock 1012 off-Saturn
e3a = re.search(r"#if RETRO_PLATFORM == RETRO_SATURN", dhpp) is not None
m_sat = re.search(r"char message\[(\d+)\];\s*#else\s*char message\[(\d+)\];", dhpp)
msg_sat = int(m_sat.group(1)) if m_sat else None
msg_pc  = int(m_sat.group(2)) if m_sat else None
e3 = e3a and (m_sat is not None) and (msg_pc == 1012) and (msg_sat is not None and msg_sat < 1012)
checks.append(("E3 DevOutput.hpp message[] Saturn-gated + shrunk", e3,
               (f"saturn message[{msg_sat}] / else message[{msg_pc}]") if m_sat else
               f"guard={e3a} gated-message-pair=NOTFOUND"))

# E4: DevOutput.cpp strncpy clamps to sizeof(self->message) (tracks the shrunk buffer;
#     byte-identical on PC where sizeof==0x3F4). The bare 0x3F4 literal must be gone.
e4a = re.search(r"strncpy\(self->message, \(char \*\)data, sizeof\(self->message\)\)", dcpp) is not None
e4b = re.search(r"0x3F4", dcpp) is None
e4 = e4a and e4b
checks.append(("E4 DevOutput.cpp strncpy clamps to sizeof(message)", e4,
               "sizeof-clamped" if e4 else f"uses_sizeof={e4a} no_0x3F4_literal={e4b}"))

# E5: compile-time slot-fit static_assert in DevOutput.cpp AND DefaultObject.cpp
e5a = re.search(r"static_assert\(\s*sizeof\(EntityDevOutput\) <= sizeof\(EntityBase\)", dcpp) is not None
e5b = re.search(r"static_assert\(\s*sizeof\(EntityDefaultObject\) <= sizeof\(EntityBase\)", fcpp) is not None
e5 = e5a and e5b
checks.append(("E5 static_assert(sizeof(EntityXxx)<=sizeof(EntityBase)) in object TUs", e5,
               "both present" if e5 else f"devoutput={e5a} defaultobject={e5b}"))

# E6: Object.cpp has a Saturn-gated runtime refusal of oversize registrations.
#     Look for a `#if RETRO_PLATFORM == RETRO_SATURN` region inside RegisterObject_STD
#     that tests entityClassSize > sizeof(EntityBase) and returns.
e6 = re.search(
    r"#if RETRO_PLATFORM == RETRO_SATURN.{0,800}?if \(entityClassSize > sizeof\(EntityBase\)\)\s*return;.{0,400}?#endif",
    ocpp, re.DOTALL) is not None
checks.append(("E6 Object.cpp Saturn registration refuses oversize EntityXxx", e6,
               "guard present" if e6 else "no Saturn `entityClassSize > sizeof(EntityBase) -> return` guard"))

# E7: arithmetic cross-check -- the shrunk EntityDevOutput must fit the shrunk EntityBase,
#     and a representative game object (EntityRing ~= 172 B) must fit too.
m_dc = re.search(r"#define OBJECT_DATA_COUNT \(0x40\)", ohpp)
data_count_sat = 0x40 if m_dc else None
if data_count_sat is not None and msg_sat is not None:
    base_sat = ENTITY_SIZE + data_count_sat * 4         # 88 + 64*4 = 344
    dev_sat  = ENTITY_SIZE + DEV_EXTRA + msg_sat         # 88 + 12 + 240 = 340
    ring_sz  = 172                                        # computed from Ring.h this session
    e7 = (dev_sat <= base_sat) and (ring_sz <= base_sat)
    detail = f"EntityBase={base_sat} EntityDevOutput={dev_sat} EntityRing~{ring_sz} (all must be <= EntityBase)"
else:
    e7 = False
    detail = "could not parse OBJECT_DATA_COUNT(0x40) and/or saturn message[] size"
checks.append(("E7 arithmetic: shrunk DevOutput + Ring fit the shrunk slot", e7, detail))

print("=" * 72)
print("P4 GUARD GATE: EntityBase.data[0x100] -> data[0x40] (Saturn)")
print("=" * 72)
allok = True
for name, ok, detail in checks:
    allok = allok and ok
    print(f"  [{'GREEN' if ok else ' RED '}] {name}")
    print(f"          {detail}")
print("-" * 72)
if allok:
    print("RESULT: GREEN -- data[] shrink applied; slot-fit is compiler+runtime enforced.")
    sys.exit(0)
else:
    print("RESULT: RED -- data[] shrink not (fully) applied; slot shrink would be unsafe.")
    sys.exit(1)
