#!/usr/bin/env python3
import math, struct
from pathlib import Path

W, H = 147, 133
TW, TH = 256, 256
# RGBA in normal palette order. PS2 alpha uses 0x80 as opaque.
P = [
    (8, 8, 18, 0x80), (13, 12, 30, 0x80), (20, 18, 42, 0x80),
    (31, 28, 58, 0x80), (42, 39, 70, 0x80), (55, 50, 80, 0x80),
    (32, 78, 112, 0x80), (27, 116, 160, 0x80), (36, 169, 213, 0x80),
    (83, 218, 244, 0x80), (157, 244, 255, 0x80), (204, 251, 255, 0x80),
    (128, 117, 65, 0x80), (174, 157, 76, 0x80), (213, 191, 88, 0x80),
    (242, 217, 104, 0x80), (255, 235, 130, 0x80), (84, 173, 94, 0x80),
    (127, 218, 119, 0x80), (200, 229, 204, 0x80), (221, 225, 235, 0x80),
    (248, 248, 255, 0x80), (89, 66, 122, 0x80), (132, 80, 178, 0x80),
    (184, 102, 222, 0x80), (238, 157, 255, 0x80), (180, 92, 52, 0x80),
    (228, 133, 54, 0x80), (255, 176, 64, 0x80), (80, 37, 48, 0x80),
    (141, 57, 66, 0x80), (214, 92, 91, 0x80),
]
P += [(0,0,0,0x80)] * (256-len(P))
px = [[1 for _ in range(W)] for _ in range(H)]

def rect(x0,y0,x1,y1,c):
    for y in range(max(0,y0), min(H,y1)):
        for x in range(max(0,x0), min(W,x1)):
            px[y][x]=c

def line(x0,y0,x1,y1,c,t=1):
    dx=abs(x1-x0); sx=1 if x0<x1 else -1
    dy=-abs(y1-y0); sy=1 if y0<y1 else -1; err=dx+dy
    while True:
        rect(x0-t//2,y0-t//2,x0+(t+1)//2,y0+(t+1)//2,c)
        if x0==x1 and y0==y1: break
        e2=2*err
        if e2>=dy: err+=dy; x0+=sx
        if e2<=dx: err+=dx; y0+=sy

def circle(cx,cy,r,c,fill=False,t=1):
    r2=r*r; rin=max(0,r-t); rin2=rin*rin
    for y in range(max(0,cy-r-1),min(H,cy+r+2)):
        for x in range(max(0,cx-r-1),min(W,cx+r+2)):
            d=(x-cx)*(x-cx)+(y-cy)*(y-cy)
            if d<=r2 and (fill or d>=rin2): px[y][x]=c

def panel(x0,y0,x1,y1):
    rect(x0,y0,x1,y1,2); rect(x0+1,y0+1,x1-1,y1-1,3)
    line(x0,y0,x1-1,y0,15); line(x0,y0,x0,y1-1,13)
    line(x0,y1-1,x1-1,y1-1,5); line(x1-1,y0,x1-1,y1-1,5)
    rect(x0+3,y0+3,x1-3,y1-3,1)

# dark scanline panel
rect(0,0,W,H,1)
for y in range(1,H,3):
    rect(0,y,W,y+1,0)
panel(2,2,W-2,H-2)
# subtle inner cyan frame
line(9,9,W-10,9,7); line(9,H-10,W-10,H-10,6)
line(9,9,9,H-10,6); line(W-10,9,W-10,H-10,6)

# central holographic globe + base
cx,cy=74,59
circle(cx,cy,25,7,True)
circle(cx,cy,23,8,True)
circle(cx,cy,19,6,True)
# continents-ish shapes
rect(60,45,69,51,9); rect(68,50,76,56,9); rect(77,43,84,49,9)
rect(78,60,89,67,9); rect(67,66,75,75,9); rect(58,58,65,66,9)
# latitude/longitude and orbit
circle(cx,cy,23,10,False,1)
line(51,59,97,59,10); line(74,36,74,82,10)
# diagonal orbit using ellipse-ish points
pts=[]
for deg in range(0,361,5):
    a=math.radians(deg); x=int(cx+34*math.cos(a)); y=int(cy+12*math.sin(a)-0.28*(x-cx)); pts.append((x,y))
for a,b in zip(pts,pts[1:]): line(a[0],a[1],b[0],b[1],10)
# base
rect(52,88,97,100,3); rect(56,84,93,91,4); rect(62,81,87,86,6)
line(54,89,95,89,13); line(58,84,91,84,9)
rect(58,95,65,99,28); rect(84,95,91,99,28)

# module icon boxes around globe
boxes=[(17,18,42,38),(104,18,130,38),(9,49,35,70),(113,49,139,70),(17,79,43,101),(104,79,130,101)]
for b in boxes:
    panel(*b)
    line(b[0]+3,b[1]+3,b[2]-4,b[1]+3,8)
# top-left bolt
line(22,24,35,29,15,3); circle(35,29,3,14,True); circle(27,32,2,14,False,1)
# top-right map pin/planet
circle(117,28,7,8,False,2); circle(117,28,2,10,True); line(117,34,117,36,10)
# mid-left health plus
rect(17,56,27,62,18); rect(19,54,25,65,18); line(29,57,33,57,10); line(29,61,33,61,10)
# mid-right save floppy
rect(120,55,132,66,20); rect(122,56,129,60,7); rect(123,63,129,66,9)
# bottom-left cart
line(22,85,25,94,24); line(25,94,36,94,24); line(28,84,37,84,24); line(26,86,35,91,24); circle(28,97,2,25,True); circle(35,97,2,25,True)
# bottom-right gear
circle(117,90,6,20,False,2); circle(117,90,2,20,True)
for dx,dy in [(0,-8),(0,8),(-8,0),(8,0),(-6,-6),(6,-6),(-6,6),(6,6)]: rect(117+dx-1,90+dy-1,117+dx+2,90+dy+2,20)

# header markers and accent dots
circle(15,14,2,15,True); circle(W-16,14,2,15,True)
line(24,112,123,112,7); line(36,116,111,116,6)

# Encode ClutImage, top-left 147x133 used by RAC1's options box; rest transparent/dark.
indices=bytearray(TW*TH)
for y in range(H):
    indices[y*TW:y*TW+W]=bytes(px[y])
# palette must be stored in CSM1 order, alpha 0x80 = opaque.
pal=bytearray(0x400)
def csm1(i):
    return i ^ 0x18 if ((i>>3)&1)!=((i>>4)&1) else i
for i,(r,g,b,a) in enumerate(P):
    j=csm1(i); pal[j*4:j*4+4]=bytes((r,g,b,a))
header=bytearray(0x20)
struct.pack_into('<ii',header,8,TW,TH)
out=header+pal+indices
assert len(out)==0x10420
Path('assets/qol-preview.clut').write_bytes(out)
# A PPM of the visible region for quick local inspection.
with open('assets/qol-preview.ppm','wb') as f:
    f.write(f'P6\n{W} {H}\n255\n'.encode())
    for row in px:
        for i in row:
            f.write(bytes(P[i][:3]))
print('wrote', len(out), 'bytes')
