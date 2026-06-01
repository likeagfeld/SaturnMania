from PIL import Image
im = Image.open("tc2j2_cap_23.png").convert("RGB")
W,H = im.size
px = im.load()
# Active raster: strip the window chrome. The blue window border + title bar.
# From visual: content starts ~x=12,y=28 and ends ~x=900,y=740.
x0,y0,x1,y1 = 14,30,900,740
def is_black(r,g,b): return r<40 and g<40 and b<40
def is_yellow(r,g,b): return r>180 and g>150 and b<100
# For each row in active area, find black run extents, but only count black that
# is INSIDE the card (i.e. has yellow or strip color somewhere on the row),
# to exclude letterbox. Report leftmost/rightmost black per row.
rows=[]
for y in range(y0,y1,4):
    blk=[x for x in range(x0,x1,2) if is_black(*px[x,y])]
    yel=[x for x in range(x0,x1,2) if is_yellow(*px[x,y])]
    if blk:
        rows.append((y,min(blk),max(blk),len(blk)*2, len(yel)*2))
# characterize the main black slab: per row leftmost black x to see diagonal slope
print(f"image {W}x{H} active [{x0},{y0}]-[{x1},{y1}]")
print("y, blackLeft, blackRight, blackWidthPx, yellowWidthPx")
for r in rows[::6]:
    print(r)
# slope of black-left edge across rows
if len(rows)>=2:
    yA,lA=rows[0][0],rows[0][1]
    yB,lB=rows[-1][0],rows[-1][1]
    print(f"black-left edge: ({lA},{yA})->({lB},{yB}) slope dx/dy={(lB-lA)/max(1,(yB-yA)):+.2f}")
    yspan=rows[-1][0]-rows[0][0]
    print(f"black vertical span={yspan}px of active {y1-y0}px = {yspan/(y1-y0)*100:.0f}%")
maxw=max(r[3] for r in rows)
print(f"max black row width={maxw}px (active width {x1-x0}px)")
