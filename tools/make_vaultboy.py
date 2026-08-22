#!/usr/bin/env python3
"""Generate the Vault Boy sprites in src/core/vaultboy.cpp.

Authored on a coarse bead grid and upscaled 2:1, so no feature is ever
thinner than two pixels. That is the whole point: scanlines take every
other row and bloom fills small gaps, so a one-pixel line and a one-pixel
hole are both gambling on surviving the CRT pipeline.

Run:  BW=42 BH=35 uv run --with pillow python tools/make_vaultboy.py
Then paste the emitted arrays into src/core/vaultboy.cpp.

ALWAYS judge the result on the post-processed frame -- render out/ambient.png
and zoom that -- never on the raw sprite at 6x on black. Three iterations of
this sprite looked best in isolation and worst on the device.
"""
from bead import *

# 42 x 35 beads. The extra room over the 30x26 grid goes almost entirely into
# the hair, which is what was never distinctive enough to read as Vault Boy.
HCX,HCY,HRX,HRY = 20,20,17,15

def skull(g): ell(g,HCX,HCY,HRX,HRY,':')

def hair_swept(g):
    # Only the top third of the head, not a dome over half of it -- a big
    # symmetrical mass reads as a helmet however it is shaped. What sells it
    # as hair is asymmetry (heavy on the left, tapering right, overhanging the
    # skull) and a parting line cut through the middle of the mass.
    poly(g,[(0,16),(0,8),(3,4),(8,2),(15,2),(22,4),(28,3),
            (33,2),(38,4),(39,8),(35,9),(30,7),
            (25,9),(19,7),(12,9),(5,12)],'.')
    ell(g,4,9,5,5,'.'); ell(g,13,5,7,4,'.'); ell(g,23,5,6,4,'.')
    rect(g,0,13,3,20,'.')                                   # sideburn
    # the parting, swept back from a point above the left eye
    for i,bx in enumerate(range(6,34)):
        by = 6 + (i*i)//90
        px(g,bx,by,' '); px(g,bx,by+1,' ')

def hair_spiked(g):
    poly(g,[(0,15),(1,9),(6,6),(13,5),(21,5),(29,6),(36,8),(41,13),
            (35,10),(26,8),(18,8),(10,10),(4,13)],'.')
    for bx in (1,6,11,16,21,26,31,36,40):
        for dy in range(6): px(g,bx,5-dy,'.'); px(g,bx+1,5-dy,'.')

def eyes_wink(g):
    rect(g,9,17,13,21,' ')                                  # open eye
    rect(g,25,19,32,20,' '); rect(g,26,17,31,18,' ')        # the wink, closed and curving
def eyes_open(g):
    rect(g,9,17,13,21,' '); rect(g,26,17,30,21,' ')
def eyes_x(g):
    for cx in (11,28):
        for i in range(-4,5):
            for d in (0,1):
                px(g,cx+i+d,19+i,' '); px(g,cx+i+d,19-i,' ')

def grin(g):
    poly(g,[(5,23),(34,23),(30,31),(9,31)],' ')
    rect(g,6,23,33,24,'.')                                  # one band of teeth
def flat_mouth(g):
    rect(g,10,26,29,27,' ')
def gape(g):
    ell(g,20,26,8,5,' ')
    poly(g,[(16,27),(25,27),(25,31),(17,31)],'.')           # tongue

poses={}
g=blank(); skull(g); hair_swept(g);  eyes_wink(g); grin(g);       edge(g); poses['fine']=upscale(g)
g=blank(); skull(g); hair_swept(g);  eyes_open(g); flat_mouth(g); edge(g); poses['steady']=upscale(g)
g=blank(); skull(g); hair_spiked(g); eyes_x(g);    gape(g);       edge(g); poses['fried']=upscale(g)
strip=[''.join(poses[k][y].ljust(BW*S)+'    ' for k in ('fine','steady','fried')) for y in range(BH*S)]
render(strip,"/tmp/vb/head42.png",scale=4)
import json; json.dump(poses, open('/tmp/vb/head42.json','w'))
print(BW*S,"x",BH*S)
