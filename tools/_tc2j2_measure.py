import sys, glob, os
from PIL import Image

frames = sorted(glob.glob("tc2j2_cap_*.png"))
# find the card frame: max yellow dominance
def stats(path):
    im = Image.open(path).convert("RGB")
    W,H = im.size
    px = im.load()
    yellow=black=tot=0
    for y in range(0,H,3):
        for x in range(0,W,3):
            r,g,b = px[x,y]
            tot+=1
            if r>170 and g>140 and b<90: yellow+=1
            if r<60 and g<60 and b<60: black+=1
    return W,H,yellow/tot,black/tot

best=None
for f in frames:
    W,H,yf,bf = stats(f)
    if best is None or yf>best[3]:
        best=(f,W,H,yf,bf)
    print(f"{os.path.basename(f)} yellow={yf:.3f} black={bf:.3f}")
print("CARD FRAME:", best[0], "yellow=%.3f black=%.3f"%(best[3],best[4]))

# Analyze the card frame black geometry
f=best[0]; W=best[1]; H=best[2]
im=Image.open(f).convert("RGB"); px=im.load()
# per-row black extent (min/max x of black pixels) to detect diagonal band
rows=[]
for y in range(H):
    xs=[x for x in range(0,W,2) if px[x,y][0]<60 and px[x,y][1]<60 and px[x,y][2]<60]
    if len(xs)>=3:
        rows.append((y,min(xs),max(xs),len(xs)))
if rows:
    y0=rows[0][0]; y1=rows[-1][0]
    print(f"black rows span y[{y0}..{y1}] of {H} ({(y1-y0)/H*100:.1f}% of height)")
    # diagonal detection: does min-x of black shift with y?
    top=[r for r in rows if r[0]<(y0+y1)//2]
    bot=[r for r in rows if r[0]>=(y0+y1)//2]
    if top and bot:
        tcx=sum((r[1]+r[2])/2 for r in top)/len(top)
        bcx=sum((r[1]+r[2])/2 for r in bot)/len(bot)
        print(f"black centroid-x top-half={tcx:.0f} bot-half={bcx:.0f} shift={abs(tcx-bcx):.0f}px")
    # total black mass
    bm=sum(r[3] for r in rows)
    print(f"black sampled-mass={bm} max-row-width={max(r[2]-r[1] for r in rows)}px")
else:
    print("no significant black rows")
