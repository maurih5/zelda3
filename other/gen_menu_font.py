# Genera src/menu_font.h: una fuente bitmap (proporcional, 1 bit) para el menu de guardados.
# Uso: python other/gen_menu_font.py [fuente.ttf] [tamano] [preview.png]
import sys
from PIL import Image, ImageDraw, ImageFont

font_path = sys.argv[1] if len(sys.argv) > 1 else r"C:\Windows\Fonts\tahomabd.ttf"
size = int(sys.argv[2]) if len(sys.argv) > 2 else 11
preview = sys.argv[3] if len(sys.argv) > 3 else None

EXTRA = "áéíóúÁÉÍÓÚñÑ¿¡üÜ·"
chars = [chr(c) for c in range(32, 127)] + list(EXTRA)

font = ImageFont.truetype(font_path, size)
ascent, descent = font.getmetrics()
H = ascent + descent

glyphs = []
for ch in chars:
    w = max(1, int(round(font.getlength(ch))))
    img = Image.new("1", (w + 4, H + 4), 0)
    d = ImageDraw.Draw(img)
    d.fontmode = "1"
    d.text((0, 0), ch, font=font, fill=1)
    rows = []
    for y in range(H):
        bits = 0
        for x in range(w):
            if img.getpixel((x, y)):
                bits |= 1 << x
        rows.append(bits)
    glyphs.append((ch, w, rows))

maxw = max(g[1] for g in glyphs)
assert maxw <= 16, maxw

out = []
out.append("// Generado por other/gen_menu_font.py a partir de %s %dpx. No editar a mano." % (font_path.split('\\')[-1], size))
out.append("#pragma once")
out.append("#define MENU_FONT_HEIGHT %d" % H)
out.append("#define MENU_FONT_NUM_EXTRA %d" % len(EXTRA))
out.append("static const uint16 kMenuFontExtraCp[%d] = {%s};" % (len(EXTRA), ",".join("0x%x" % ord(c) for c in EXTRA)))
out.append("static const uint8 kMenuFontWidth[%d] = {%s};" % (len(glyphs), ",".join(str(g[1]) for g in glyphs)))
out.append("static const uint16 kMenuFontRows[%d][%d] = {" % (len(glyphs), H))
for ch, w, rows in glyphs:
    out.append("  {%s}, // %s" % (",".join("0x%x" % r for r in rows), repr(ch)))
out.append("};")
open("src/menu_font.h", "w", encoding="utf-8").write("\n".join(out) + "\n")
print("height", H, "glyphs", len(glyphs), "maxw", maxw)

if preview:
    text = ["Cargar partida  ·  Autoguardado", "hace 5 min  Palacio del Este", "¿Cargar? Sí / No  ¡Ñandú! ÁÉÍÓÚ áéíóú ü", "0123456789 23/11 14:32  A: Cargar  B: Volver"]
    idx = {g[0]: g for g in glyphs}
    W = 430
    img = Image.new("RGB", (W, (H + 3) * len(text) + 4), (20, 20, 40))
    for li, line in enumerate(text):
        x = 2
        for ch in line:
            g = idx.get(ch, idx["?"])
            for y, r in enumerate(g[2]):
                for xx in range(g[1]):
                    if r >> xx & 1:
                        img.putpixel((x + xx, 2 + li * (H + 3) + y), (255, 255, 255))
            x += g[1]
    img = img.resize((W * 3, img.height * 3), Image.NEAREST)
    img.save(preview)
