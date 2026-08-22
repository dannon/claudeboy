import math as m, zlib, struct, json

import os
BW,BH,S = int(os.environ.get('BW',30)), int(os.environ.get('BH',26)), 2   # bead grid, pixels per bead
def blank(): return [[' ']*BW for _ in range(BH)]
def px(g,x,y,ch='#'):
    if 0<=x<BW and 0<=y<BH: g[y][x]=ch
def ell(g,cx,cy,rx,ry,ch,fill=True):
    for y in range(cy-ry,cy+ry+1):
        for x in range(cx-rx,cx+rx+1):
            if ((x-cx)/rx)**2+((y-cy)/ry)**2<=1.0:
                if fill: px(g,x,y,ch)
def rect(g,x0,y0,x1,y1,ch):
    for y in range(y0,y1+1):
        for x in range(x0,x1+1): px(g,x,y,ch)
def poly(g,pts,ch):
    ys=[p[1] for p in pts]
    for y in range(min(ys),max(ys)+1):
        xs=[]
        for i in range(len(pts)):
            (x0,y0),(x1,y1)=pts[i],pts[(i+1)%len(pts)]
            if (y0<=y<y1) or (y1<=y<y0): xs.append(x0+(x1-x0)*(y-y0)/(y1-y0))
        xs.sort()
        for i in range(0,len(xs)-1,2):
            for x in range(int(round(xs[i])),int(round(xs[i+1]))+1): px(g,x,y,ch)
def edge(g,ch='#',over=('+','.',':')):
    """Trace a one-bead border round every filled region -- the black outline
    a perler piece gets for free from its background."""
    out=[r[:] for r in g]
    for y in range(BH):
        for x in range(BW):
            if g[y][x] in over:
                for dx,dy in ((1,0),(-1,0),(0,1),(0,-1)):
                    nx,ny=x+dx,y+dy
                    if 0<=nx<BW and 0<=ny<BH and g[ny][nx]==' ': out[ny][nx]=ch
                    elif not (0<=nx<BW and 0<=ny<BH): out[y][x]=ch
    for y in range(BH): g[y]=out[y]

def upscale(g):
    rows=[]
    for r in g:
        line=''.join(c*S for c in r)
        for _ in range(S): rows.append(line)
    return rows

def render(art,path,scale=6):
    h=len(art); w=max(len(r) for r in art); art=[r.ljust(w) for r in art]
    lut={'#':255,'+':200,'.':110,':':45,' ':0}
    rows=[]
    for y in range(h*scale):
        row=bytearray()
        for x in range(w*scale):
            v=lut.get(art[y//scale][x//scale],0)
            row+=bytes((int(v*0.15),int(v*0.85),int(v*0.25)))
        rows.append(bytes(row))
    raw=b''.join(b'\x00'+r for r in rows)
    def ck(t,d): return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    open(path,'wb').write(b'\x89PNG\r\n\x1a\n'+ck(b'IHDR',struct.pack('>IIBBBBB',w*scale,h*scale,8,2,0,0,0))+ck(b'IDAT',zlib.compress(raw))+ck(b'IEND',b''))
