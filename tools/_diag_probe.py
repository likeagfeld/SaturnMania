import importlib.util, time, sys
from pathlib import Path
H=Path('tools')
def load(m,f):
    s=importlib.util.spec_from_file_location(m,H/f);x=importlib.util.module_from_spec(s);s.loader.exec_module(x);return x
qt=load('qa_trace','qa_trace.py')
mt=Path('game.map').read_text(errors='replace')
rd=qt.Reader(True,None,'127.0.0.1',55355)
def A(n): return rd.sym(mt,n)
def U(n):
    a=A(n); return rd.r32(a) if a else None
def s32(v): return v-0x100000000 if v and v>=0x80000000 else v
POOL=0x243000; ST=556
csf=A('RSDK::currentSceneFolder')
def folder():
    try: return rd.mem.read_saturn(csf,16).split(b'\0')[0].decode(errors='replace')
    except: return '?'
def slot0():
    b=POOL; return (rd.r32(b+52) or 0)&0xFFFF, s32(rd.r32(b+0) or 0)>>16, s32(rd.r32(b+4) or 0)>>16
# phase 1: reach GHZ alive
t0=time.time()
while time.time()-t0<200:
    if folder()=='GHZ' and slot0()[0]==8: break
    time.sleep(0.4)
print('GHZ alive @%.1f trans=%s ring_cid=%s'%(time.time()-t0,U('p6_w_transitions'),U('p6_w_ring_cid')),flush=True)
# phase 2: watch death + after, sample trans+ring_cid+slot0
died=False; t1=time.time()
while time.time()-t1<70:
    c,x,y=slot0(); tr=U('p6_w_transitions'); rc=U('p6_w_ring_cid')
    if c==0 and not died:
        died=True; print('[DEATH] slot0->0 @%.1f pos=(%d,%d) trans=%s ring_cid=%s'%(time.time()-t1,x,y,tr,rc),flush=True)
    if died:
        print('  t%.1f slot0 cls=%d pos=(%d,%d) trans=%s ring_cid=%s'%(time.time()-t1,c,x,y,tr,rc),flush=True)
        if c==8:
            print('*** RESPAWNED (slot0 classID 8) *** GREEN',flush=True); break
        time.sleep(1.5)
    else:
        time.sleep(0.3)
print('final: trans=%s ring_cid=%s slot0=%s'%(U('p6_w_transitions'),U('p6_w_ring_cid'),slot0()),flush=True)
