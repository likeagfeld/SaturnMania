import os, sys
sys.path.insert(0, "tools/_portspike")
import qa_p6_scene as Q
mp = Q.read_text(Q.MAP_DEFAULT); mod = Q.load_harness()
sec = mod.parse_savestate(Q._as_path(sys.argv[1]))
ma = Q.map_symbol(mp, "_p6_w_magic")
_, perm = Q.calibrate(mod._peek_bytes(sec, ma, 4) if ma else None)
for n in ["_p6_w_aiz_cutscene_state","_p6_w_cont_frames","_p6_w_aiz_seq_classid",
          "_p6_w_aiz_ruby_active","_p6_w_aiz_ruby_timer","_p6_w_aiz_ruby_flashfin"]:
    sym = Q.map_symbol(mp, n)
    v = Q.peek_u32(mod, sec, sym, perm, signed=True) if sym else None
    print("  %-28s = %s" % (n, v))
b = mod._peek_bytes(sec, 0x06094614, 16)
if b:
    sw = bytes(bytes(b)[i^1] for i in range(len(b)))
    print("  currentSceneFolder           = %r" % sw.split(b'\x00')[0].decode('latin1','replace'))
