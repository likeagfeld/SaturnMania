from PIL import Image
im = Image.open("tc2j2_38.png").convert("RGB")
W,H = im.size
px = im.load()
def is_black(r,g,b): return r<45 and g<45 and b<45
# Interior raster only (strip the ~8px window border + title bar).
ax0,ax1 = 12, 900
ay0,ay1 = 32, 730
rows=[]
for y in range(ay0,ay1,3):
    # longest contiguous black run on this row
    best=(0,0,0); cur=None
    for x in range(ax0,ax1):
        if is_black(*px[x,y]):
            if cur is None: cur=x
        else:
            if cur is not None:
                if x-cur>best[0]: best=(x-cur,cur,x-1)
                cur=None
    if cur is not None and (ax1-cur)>best[0]: best=(ax1-cur,cur,ax1-1)
    if best[0]>=8: rows.append((y,best[1],best[2],best[0]))
if rows:
    yt,yb=rows[0][0],rows[-1][0]
    lt,lb=rows[0][1],rows[-1][1]
    rt,rb=rows[0][2],rows[-1][2]
    print(f"BLACK SLAB (longest run): yspan [{yt}..{yb}] = {yb-yt}px (raster ~{ay1-ay0})")
    print(f"  top  x[{lt}..{rt}] w={rt-lt}")
    print(f"  bot  x[{lb}..{rb}] w={rb-lb}")
    print(f"  left-edge  dx/dy={(lb-lt)/max(1,yb-yt):+.3f}")
    print(f"  right-edge dx/dy={(rb-rt)/max(1,yb-yt):+.3f}")
    ws=[r[3] for r in rows]
    print(f"  run-width min={min(ws)} max={max(ws)} mean={sum(ws)//len(ws)}")
for r in rows[::max(1,len(rows)//14)]:
    print("   ",r)
