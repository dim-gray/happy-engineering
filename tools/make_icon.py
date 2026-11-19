#!/usr/bin/env python3
"""Generates assets/icon.png and assets/icon.ico: a plain ":)" on a vanilla background."""
import sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter

FONT = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
S = 1024                       # master size, downscaled afterwards
VANILLA = (243, 229, 171)      # #F3E5AB
VANILLA_LIGHT = (252, 244, 208)
INK = (58, 38, 24)             # dark chocolate

# vanilla background: soft radial highlight on a flat colour
bg = Image.new("RGB", (S, S), VANILLA)
glow = Image.new("L", (S, S), 0)
ImageDraw.Draw(glow).ellipse((S * 0.08, S * 0.02, S * 0.92, S * 0.78), fill=255)
glow = glow.filter(ImageFilter.GaussianBlur(S * 0.12))
bg.paste(Image.new("RGB", (S, S), VANILLA_LIGHT), (0, 0), glow.point(lambda v: int(v * 0.65)))

# rounded square mask
mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle((0, 0, S - 1, S - 1), radius=int(S * 0.22), fill=255)
img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
img.paste(bg, (0, 0), mask)

# the smiley: just the two characters ":)"
font = ImageFont.truetype(FONT, int(S * 0.62))
d = ImageDraw.Draw(img)
text = ":)"
box = d.textbbox((0, 0), text, font=font)
w, h = box[2] - box[0], box[3] - box[1]
x = (S - w) / 2 - box[0]
y = (S - h) / 2 - box[1] - S * 0.01
d.text((x, y), text, font=font, fill=INK)

img.resize((512, 512), Image.LANCZOS).save("assets/icon.png")
img.save("assets/icon.ico", format="ICO",
         sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print("ok")
