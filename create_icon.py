import math
from PIL import Image, ImageDraw, ImageFilter

# Create high-res 1024x1024 image for super-sampling down to 256x256
S = 1024
img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
draw = ImageDraw.Draw(img)

# Background squircle / rounded rect
bg_radius = 210
margin = 24
draw.rounded_rectangle(
    [margin, margin, S - margin, S - margin],
    radius=bg_radius,
    fill=(14, 16, 24, 255)
)

# Subtle background radial gradient / glow in center
center_x, center_y = S // 2, S // 2
glow_layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
glow_draw = ImageDraw.Draw(glow_layer)
glow_draw.ellipse([center_x - 360, center_y - 320, center_x + 360, center_y + 400], fill=(25, 30, 48, 160))
glow_draw.ellipse([center_x - 220, center_y - 180, center_x + 220, center_y + 260], fill=(42, 38, 70, 120))
glow_layer = glow_layer.filter(ImageFilter.GaussianBlur(50))
img = Image.alpha_composite(img, glow_layer)
draw = ImageDraw.Draw(img)

# TV Stand Legs at bottom
leg_color = (60, 68, 85, 255)
# Left leg
draw.polygon([(360, 780), (320, 890), (355, 890), (395, 780)], fill=leg_color)
# Right leg
draw.polygon([(629, 780), (669, 890), (704, 890), (664, 780)], fill=leg_color)
# TV Stand foot caps
draw.rounded_rectangle([305, 882, 365, 898], radius=8, fill=(40, 45, 58, 255))
draw.rounded_rectangle([659, 882, 719, 898], radius=8, fill=(40, 45, 58, 255))

# TV Antennae on top
ant_color = (130, 140, 165, 255)
draw.line([(center_x - 60, 260), (center_x - 240, 105)], fill=ant_color, width=16)
draw.line([(center_x + 60, 260), (center_x + 240, 105)], fill=ant_color, width=16)
# Antenna tips (glowing balls)
draw.ellipse([center_x - 262, 83, center_x - 218, 127], fill=(0, 220, 255, 255))
draw.ellipse([center_x + 218, 83, center_x + 262, 127], fill=(255, 60, 90, 255))
# Antenna base
draw.rounded_rectangle([center_x - 90, 240, center_x + 90, 270], radius=12, fill=(45, 52, 68, 255))

# Main TV Dimensions
tv_x0, tv_y0 = 130, 250
tv_x1, tv_y1 = 894, 790
tv_w = tv_x1 - tv_x0
tv_h = tv_y1 - tv_y0

# Joy-Con Ear Widths
jc_w = 115

# Left Joy-Con Ear (Neon Blue: #00C3E3)
blue_color = (0, 195, 227, 255)
blue_dark = (0, 150, 180, 255)
# Draw rounded left ear
draw.rounded_rectangle([tv_x0, tv_y0, tv_x0 + jc_w + 30, tv_y1], radius=65, fill=blue_color)
# Minus (-) button
draw.rounded_rectangle([tv_x0 + 40, tv_y0 + 55, tv_x0 + 75, tv_y0 + 67], radius=6, fill=(15, 25, 45, 220))
# Left Stick
ls_cx, ls_cy = tv_x0 + 58, tv_y0 + 175
draw.ellipse([ls_cx - 36, ls_cy - 36, ls_cx + 36, ls_cy + 36], fill=(20, 30, 48, 240))
draw.ellipse([ls_cx - 24, ls_cy - 24, ls_cx + 24, ls_cy + 24], fill=(35, 50, 75, 255))
# D-Pad directional buttons (4 circular buttons)
d_cx, d_cy = tv_x0 + 58, tv_y0 + 360
btn_rad = 14
draw.ellipse([d_cx - btn_rad, d_cy - 45 - btn_rad, d_cx + btn_rad, d_cy - 45 + btn_rad], fill=(20, 30, 48, 240))
draw.ellipse([d_cx - btn_rad, d_cy + 45 - btn_rad, d_cx + btn_rad, d_cy + 45 + btn_rad], fill=(20, 30, 48, 240))
draw.ellipse([d_cx - 45 - btn_rad, d_cy - btn_rad, d_cx - 45 + btn_rad, d_cy + btn_rad], fill=(20, 30, 48, 240))
draw.ellipse([d_cx + 45 - btn_rad, d_cy - btn_rad, d_cx + 45 + btn_rad, d_cy + btn_rad], fill=(20, 30, 48, 240))
# Capture button
draw.rounded_rectangle([tv_x0 + 44, tv_y1 - 85, tv_x0 + 72, tv_y1 - 57], radius=6, fill=(20, 30, 48, 240))

# Right Joy-Con Ear (Neon Red / Coral: #FF3C28)
red_color = (255, 60, 40, 255)
red_dark = (205, 40, 30, 255)
# Draw rounded right ear
draw.rounded_rectangle([tv_x1 - jc_w - 30, tv_y0, tv_x1, tv_y1], radius=65, fill=red_color)
# Plus (+) button
px, py = tv_x1 - 58, tv_y0 + 61
draw.rectangle([px - 14, py - 4, px + 14, py + 4], fill=(45, 15, 20, 220))
draw.rectangle([px - 4, py - 14, px + 4, py + 14], fill=(45, 15, 20, 220))
# Right ABXY Buttons
b_cx, b_cy = tv_x1 - 58, tv_y0 + 175
draw.ellipse([b_cx - btn_rad, b_cy - 45 - btn_rad, b_cx + btn_rad, b_cy - 45 + btn_rad], fill=(45, 15, 20, 240))
draw.ellipse([b_cx - btn_rad, b_cy + 45 - btn_rad, b_cx + btn_rad, b_cy + 45 + btn_rad], fill=(45, 15, 20, 240))
draw.ellipse([b_cx - 45 - btn_rad, b_cy - btn_rad, b_cx - 45 + btn_rad, b_cy + btn_rad], fill=(45, 15, 20, 240))
draw.ellipse([b_cx + 45 - btn_rad, b_cy - btn_rad, b_cx + 45 + btn_rad, b_cy + btn_rad], fill=(45, 15, 20, 240))
# Right Stick
rs_cx, rs_cy = tv_x1 - 58, tv_y0 + 360
draw.ellipse([rs_cx - 36, rs_cy - 36, rs_cx + 36, rs_cy + 36], fill=(45, 15, 20, 240))
draw.ellipse([rs_cx - 24, rs_cy - 24, rs_cx + 24, rs_cy + 24], fill=(75, 30, 35, 255))
# Home button
draw.ellipse([tv_x1 - 72, tv_y1 - 85, tv_x1 - 44, tv_y1 - 57], fill=(45, 15, 20, 240))
draw.ellipse([tv_x1 - 65, tv_y1 - 78, tv_x1 - 51, tv_y1 - 64], fill=(120, 60, 65, 255))

# TV Center Frame (Body)
frame_x0 = tv_x0 + jc_w - 6
frame_x1 = tv_x1 - jc_w + 6
draw.rounded_rectangle([frame_x0, tv_y0, frame_x1, tv_y1], radius=24, fill=(28, 32, 44, 255))

# Inner screen bezel (dark border)
screen_margin_x = 24
screen_margin_y = 28
scr_x0 = frame_x0 + screen_margin_x
scr_y0 = tv_y0 + screen_margin_y
scr_x1 = frame_x1 - screen_margin_x
scr_y1 = tv_y1 - screen_margin_y
draw.rounded_rectangle([scr_x0 - 4, scr_y0 - 4, scr_x1 + 4, scr_y1 + 4], radius=22, fill=(16, 18, 25, 255))

# Screen Display (Deep glowing indigo screen)
draw.rounded_rectangle([scr_x0, scr_y0, scr_x1, scr_y1], radius=18, fill=(10, 12, 18, 255))

# Screen Content: Glowing JoyStream Play / Stream Icon
play_cx = (scr_x0 + scr_x1) // 2
play_cy = (scr_y0 + scr_y1) // 2 - 15

# Glow behind Play Icon
play_glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
play_gdraw = ImageDraw.Draw(play_glow)
play_gdraw.ellipse([play_cx - 160, play_cy - 160, play_cx + 160, play_cy + 160], fill=(0, 212, 255, 90))
play_gdraw.ellipse([play_cx - 110, play_cy - 110, play_cx + 110, play_cy + 110], fill=(168, 85, 247, 120))
play_glow = play_glow.filter(ImageFilter.GaussianBlur(35))
img = Image.alpha_composite(img, play_glow)
draw = ImageDraw.Draw(img)

# Play Triangle (Rounded triangle pointing right)
tri_size = 90
p1 = (play_cx - 50, play_cy - 85)
p2 = (play_cx - 50, play_cy + 85)
p3 = (play_cx + 75, play_cy)

# Vibrant Cyan & Violet Play Arrow
draw.polygon([p1, p2, p3], fill=(0, 225, 255, 255))
# Triangle inner gradient highlight
p1_in = (play_cx - 42, play_cy - 70)
p2_in = (play_cx - 42, play_cy + 70)
p3_in = (play_cx + 62, play_cy)
draw.polygon([p1_in, p2_in, p3_in], fill=(168, 85, 247, 220))
p1_core = (play_cx - 32, play_cy - 48)
p2_core = (play_cx - 32, play_cy + 48)
p3_core = (play_cx + 42, play_cy)
draw.polygon([p1_core, p2_core, p3_core], fill=(255, 255, 255, 230))

# Streaming signal arcs / WiFi waves to the right of the TV
arc_layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
arc_draw = ImageDraw.Draw(arc_layer)
arc_draw.arc([play_cx + 20, play_cy - 130, play_cx + 170, play_cy + 130], start=-60, end=60, fill=(0, 220, 255, 180), width=12)
arc_draw.arc([play_cx + 50, play_cy - 160, play_cx + 210, play_cy + 160], start=-55, end=55, fill=(168, 85, 247, 160), width=10)
img = Image.alpha_composite(img, arc_layer)
draw = ImageDraw.Draw(img)

# Bottom TV chin LED light
draw.ellipse([center_x - 6, tv_y1 - 18, center_x + 6, tv_y1 - 6], fill=(0, 230, 255, 255))

# Glass screen reflection (diagonal gloss arc)
gloss = Image.new("RGBA", (S, S), (0, 0, 0, 0))
gdraw = ImageDraw.Draw(gloss)
gdraw.polygon([(scr_x0 + 20, scr_y0), (scr_x0 + 170, scr_y0), (scr_x0, scr_y0 + 260), (scr_x0, scr_y0 + 90)], fill=(255, 255, 255, 25))
img = Image.alpha_composite(img, gloss)

# "JOYSTREAM" text or clean badge at the top
# Downsample to 256x256 for Switch NRO
icon_256 = img.resize((256, 256), Image.Resampling.LANCZOS)

# Convert to RGB JPEG
icon_rgb = Image.new("RGB", (256, 256), (14, 16, 24))
icon_rgb.paste(icon_256, (0, 0), icon_256)

icon_rgb.save("/root/JoyStream/icon.jpg", "JPEG", quality=95)
icon_rgb.save("/root/JoyStream/assets/icon.jpg", "JPEG", quality=95)
icon_256.save("/root/JoyStream/icon.png", "PNG")
icon_256.save("/root/JoyStream/assets/icon.png", "PNG")
print("Icons successfully created: 256x256 icon.jpg and icon.png!")
