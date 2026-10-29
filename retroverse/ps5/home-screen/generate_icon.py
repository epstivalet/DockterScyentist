#!/usr/bin/env python3
"""Create a 512x512 original RetroVerse PS5 home-screen icon. Pillow required."""
from PIL import Image, ImageDraw, ImageFont
from pathlib import Path
import math
import sys

target = Path(sys.argv[1]) if len(sys.argv)>1 else Path("icon0.png")
target.parent.mkdir(parents=True,exist_ok=True)
S=512
im=Image.new("RGB",(S,S))
pixels=im.load()
for y in range(S):
    for x in range(S):
        glow=max(0.0,1-math.hypot((x-256)/440,(y-200)/410))
        pixels[x,y]=(int(8+18*glow),int(12+10*glow),int(29+35*glow))
d=ImageDraw.Draw(im)
# Glowing retro monitor bezel and deep-screen inset.
for i in range(15,0,-1):
    k=20-i
    d.rounded_rectangle((78-k,111-k,434+k,349+k),radius=36+i,
                        outline=(int(60+i*6),int(50+i*3),int(115+i*7)),width=2)
d.rounded_rectangle((81,114,431,352),radius=33,fill=(21,24,47),outline=(190,167,255),width=10)
d.rounded_rectangle((103,136,409,328),radius=18,fill=(5,12,26),outline=(95,120,179),width=4)
for y in range(146,320,8): d.line((112,y,400,y),fill=(12,20,42),width=2)
# Graphic waveform + cartridge aesthetic.
d.polygon([(150,258),(197,182),(238,257),(281,171),(329,254),(365,192)],fill=None)
d.line([(150,258),(197,182),(238,257),(281,171),(329,254),(365,192)],fill=(114,234,219),width=13,joint="curve")
d.rounded_rectangle((171,370,340,387),radius=5,fill=(185,165,245))
d.ellipse((366,356,389,378),fill=(107,233,211))
d.ellipse((398,356,421,378),fill=(240,122,176))
font_candidates=[
 "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
 "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf"
]
font=next((ImageFont.truetype(p,46) for p in font_candidates if Path(p).exists()),ImageFont.load_default())
label="RETROVERSE"
box=d.textbbox((0,0),label,font=font)
w=box[2]-box[0]
d.text(((S-w)//2,420),label,font=font,fill=(246,245,255))
im.save(target,format="PNG")
print("Created",target,"512x512",target.stat().st_size,"bytes")
