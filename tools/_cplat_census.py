# One-off (task: CollapsingPlatform port): census every CollapsingPlatform
# placement across ALL scenes -- size vector -> tile count sx*sy ceiling,
# per the whole-game-scalability binding rule.
import sys, os, hashlib
sys.path.insert(0, os.path.dirname(__file__))
import importlib.util
spec = importlib.util.spec_from_file_location("pte", os.path.join(os.path.dirname(__file__), "parse_title_entities.py"))
pte = importlib.util.module_from_spec(spec); spec.loader.exec_module(pte)

# extend the reverse-hash table with the names we need
NAMES = ["CollapsingPlatform", "size", "respawn", "targetLayer", "type",
         "delay", "eventOnly", "mightyOnly"]
target_hash = hashlib.md5(b"CollapsingPlatform").digest()
attr_rev = {hashlib.md5(n.encode()).digest(): n for n in NAMES}

root = r"D:\sonicmaniasaturn\extracted\Data\Stages"
worst = (0, None)
rows = []
for zone in sorted(os.listdir(root)):
    zdir = os.path.join(root, zone)
    if not os.path.isdir(zdir): continue
    for fn in sorted(os.listdir(zdir)):
        if not (fn.startswith("Scene") and fn.endswith(".bin")): continue
        path = os.path.join(zdir, fn)
        try:
            with open(path, "rb") as f: d = f.read()
            r = pte.R(d); pte.skip_layers(r)
            obj_count = r.u8()
            for _ in range(obj_count):
                nhash = bytes(r.take(16))
                var_count = r.u8()
                attribs = []
                for _ in range(max(0, var_count - 1)):
                    ahash = bytes(r.take(16)); atype = r.u8()
                    attribs.append((attr_rev.get(ahash, ahash.hex()[:8]), atype))
                entity_count = r.u16()
                for _ in range(entity_count):
                    slot = r.u16(); x = r.i32(); y = r.i32()
                    vals = {}
                    for aname, atype in attribs:
                        lab, v = pte.read_attr(r, atype)
                        vals[aname] = v
                    if nhash == target_hash:
                        sx_f, sy_f = vals.get("size", (0, 0))
                        sx, sy = sx_f >> 20, sy_f >> 20
                        rows.append((zone, fn, slot, x >> 16, y >> 16, sx, sy, sx * sy, vals.get("type"), vals.get("respawn"), vals.get("targetLayer"), vals.get("delay")))
                        if sx * sy > worst[0]: worst = (sx * sy, (zone, fn, slot, sx, sy))
        except Exception as e:
            print(f"PARSE-FAIL {zone}/{fn}: {e}")
for row in rows:
    print("%-5s %-11s slot=%4d pos=(%6d,%6d) tiles=%2dx%-2d =%3d type=%s respawn=%s layer=%s delay=%s" % row)
print(f"\nTOTAL placements: {len(rows)}   WHOLE-GAME worst tile count: {worst[0]} at {worst[1]}")
