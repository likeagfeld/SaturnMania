from PIL import Image
im = Image.open("tc2j2_cap_23.png").convert("RGB")
W,H = im.size
px = im.load()
# crop to active area (strip the window chrome + letterbox). The Mednafen
# active raster sits inside a blue border; find it by scanning for the
# content box. Use a simple inset.
def classify(r,g,b):
    if r<55 and g<55 and b<55: return 'K'      # black
    if r>200 and g>200 and b>200: return 'W'   # white
    if r>180 and g>150 and b<90: return 'Y'    # yellow
    if r<90 and g<110 and b>150: return 'B'    # blue strip/sky
    if r>180 and g<90 and b<90: return 'R'     # red strip
    if r<120 and g>150 and b>120 and b<200: return 'G' # teal/green strip
    if r>200 and g>110 and b<70: return 'O'    # orange strip
    if g>150 and r<150 and b<120: return 'g'   # foliage green
    return '.'
# For black region: per row, min/max x of black, restricted to interior
xlo, xhi = int(W*0.02), int(W*0.98)
def edges(tag):
    out=[]
    for y in range(0,H,8):
        xs=[x for x in range(xlo,xhi,2) if classify(*px[x,y])==tag]
        if len(xs)>=4:
            out.append((y,min(xs),max(xs),len(xs)))
    return out
for tag,name in [('K','black'),('W','white'),('B','blue'),('R','red'),('G','teal')]:
    e=edges(tag)
    if not e:
        print(f"{name}: none"); continue
    y0,y1=e[0][0],e[-1][0]
    # slope of left edge: dx/dy
    if len(e)>=2:
        lx0,lx1=e[0][1],e[-1][1]
        slope=(lx1-lx0)/max(1,(y1-y0))
    else: slope=0
    widths=[r[2]-r[1] for r in e]
    print(f"{name}: y[{y0}..{y1}] leftedge slope dx/dy={slope:+.2f} "
          f"meanwidth={sum(widths)//len(widths)}px maxwidth={max(widths)}px rows={len(e)}")
