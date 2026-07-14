#!/usr/bin/env python3
"""
Build ALL reminder icons from the user's photos (reinderesp/imagess/) at TWO
native sizes so they're crisp and never clipped:
    img_<name>.c    -> BIG   (alert hero)
    img_<name>_h.c  -> HOME  (up-next preview)
Breakfast has no photo folder, so it uses the fried-egg emoji.
Output -> ReminderESP32/src/images/  (+ images.h)
"""
import os
from PIL import Image, ImageDraw, ImageFont

HERE  = os.path.dirname(os.path.abspath(__file__))
IMG   = os.path.normpath(os.path.join(HERE, "..", "..", "imagess"))
C_DIR = os.path.normpath(os.path.join(HERE, "..", "ReminderESP32", "src", "images"))
FONT  = "C:/Windows/Fonts/seguiemj.ttf"

BIG, HOME = 196, 104          # alert size (bigger), home size
BLACK = (0, 0, 0)

# name, kind, source(rel path or emoji), key_checkerboard
ICONS = [
    ("almonds_eat",  "photo", "fresh-almonds-nuts-with-green-leaves-transparent-background/0e33f8e0-dd20-4561-93d1-f03b8e3b4c3c.psd", True),
    ("almonds_soak", "photo", "fresh-almonds-nuts-with-green-leaves-transparent-background/0e33f8e0-dd20-4561-93d1-f03b8e3b4c3c.psd", True),
    ("milk",         "photo", "milk protine/9828648.png",   False),
    ("breakfast",    "emoji", "\U0001F373",                 False),
    ("salad",        "photo", "fruits/10030320.png",        False),
    ("lunch",        "photo", "lunch/11449835.png",         False),
    ("chaat",        "photo", "sprouts/11155517.png",       False),
    ("coconut",      "photo", "coconut water/11063401.png", False),
    ("dinner",       "photo", "dinner/11449831.png",        False),
    ("doorbell",     "emoji", "\U0001F514",                 False),  # 🔔 bell (MQTT doorbell)
]


def key_checkerboard(im):
    im = im.convert("RGBA")
    s = im.convert("RGB").convert("HSV").getchannel("S")
    px, sp = im.load(), s.load()
    for y in range(im.height):
        for x in range(im.width):
            if sp[x, y] < 40:
                r, g, b, a = px[x, y]; px[x, y] = (r, g, b, 0)
    return im


def clean_alpha(im, thr=24):
    """Zero out near-transparent pixels so getbbox() tightly wraps the subject."""
    a = im.getchannel("A").point(lambda v: 0 if v < thr else v)
    im.putalpha(a)
    return im


def photo_subject(rel, key):
    im = Image.open(os.path.join(IMG, rel))
    im = key_checkerboard(im) if key else im.convert("RGBA")
    im = clean_alpha(im)
    bbox = im.getbbox()
    return im.crop(bbox) if bbox else im


def render_photo(subj, size):
    fill = int(size * 0.96)
    s = subj.copy(); s.thumbnail((fill, fill), Image.LANCZOS)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 255))
    canvas.alpha_composite(s, ((size - s.width) // 2, (size - s.height) // 2))
    return canvas.convert("RGB")


def render_emoji(emoji, size):
    img = Image.new("RGB", (size, size), BLACK)
    d = ImageDraw.Draw(img)
    px = int(size * 0.86)
    while px > 30:
        font = ImageFont.truetype(FONT, px)
        b = d.textbbox((0, 0), emoji, font=font, embedded_color=True)
        if (b[2] - b[0]) <= size - 8 and (b[3] - b[1]) <= size - 8:
            break
        px -= 4
    b = d.textbbox((0, 0), emoji, font=font, embedded_color=True)
    w, h = b[2] - b[0], b[3] - b[1]
    d.text(((size - w) // 2 - b[0], (size - h) // 2 - b[1]), emoji, font=font, embedded_color=True)
    return img


def to_rgb565_le(img):
    px, out = img.load(), bytearray()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b = px[x, y]
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            out.append(v & 0xFF); out.append((v >> 8) & 0xFF)
    return out


TMPL = '''#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif
#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif
#ifndef LV_ATTRIBUTE_MEM_ALIGN
    #define LV_ATTRIBUTE_MEM_ALIGN
#endif

const LV_ATTRIBUTE_MEM_ALIGN uint8_t {n}_map[] = {{
{data}
}};
const lv_img_dsc_t {n} = {{
    .header.cf = LV_IMG_CF_TRUE_COLOR, .header.always_zero = 0, .header.reserved = 0,
    .header.w = {s}, .header.h = {s}, .data_size = {s} * {s} * 2, .data = {n}_map,
}};
'''


def write_c(name, img, size):
    data = to_rgb565_le(img)
    lines = ["    " + "".join(f"0x{b:02x}," for b in data[i:i+16]) for i in range(0, len(data), 16)]
    with open(os.path.join(C_DIR, f"{name}.c"), "w", encoding="ascii") as f:
        f.write(TMPL.format(n=name, data="\n".join(lines), s=size))


def main():
    os.makedirs(C_DIR, exist_ok=True)
    decls, total = [], 0
    prev = Image.new("RGB", ((BIG + 6) * len(ICONS) + 6, BIG + 6), BLACK)
    for i, (name, kind, src, key) in enumerate(ICONS):
        subj = photo_subject(src, key) if kind == "photo" else None
        big  = render_photo(subj, BIG)  if kind == "photo" else render_emoji(src, BIG)
        home = render_photo(subj, HOME) if kind == "photo" else render_emoji(src, HOME)
        write_c(f"img_{name}",   big,  BIG)
        write_c(f"img_{name}_h", home, HOME)
        decls += [f"img_{name}", f"img_{name}_h"]
        total += BIG * BIG * 2 + HOME * HOME * 2
        prev.paste(big, (6 + i * (BIG + 6), 3))
        print(f"  img_{name}(.c/_h.c)  <- {src.split('/')[0] if kind=='photo' else 'emoji'}")

    with open(os.path.join(C_DIR, "images.h"), "w", encoding="ascii") as f:
        f.write("#ifndef REMINDER_IMAGES_H\n#define REMINDER_IMAGES_H\n\n")
        f.write('#ifdef __has_include\n  #if __has_include("lvgl.h")\n')
        f.write("    #ifndef LV_LVGL_H_INCLUDE_SIMPLE\n      #define LV_LVGL_H_INCLUDE_SIMPLE\n    #endif\n  #endif\n#endif\n")
        f.write('#if defined(LV_LVGL_H_INCLUDE_SIMPLE)\n  #include "lvgl.h"\n#else\n  #include "lvgl/lvgl.h"\n#endif\n\n')
        for d in decls:
            f.write(f"LV_IMG_DECLARE({d});\n")
        f.write("\n#endif\n")

    prev.save(os.path.join(HERE, "png", "_all_sheet.png"))
    print(f"\ntotal icon flash: {total/1024:.0f} KB   preview -> png/_all_sheet.png")


if __name__ == "__main__":
    main()
