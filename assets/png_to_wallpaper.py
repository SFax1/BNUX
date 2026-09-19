#!/usr/bin/env python3
"""
Конвертирует картинку (обои) в сырой C-массив пикселей формата 0x00RRGGBB,
который можно зашить прямо в ядро (как initrd_data.c).

Использование:
    python3 assets/png_to_wallpaper.py assets/lix_wallpaper.png 1024 768 > kernel/gui/wallpaper_data.c

Картинка будет растянута/обрезана под указанный размер (должен совпадать
с реальным разрешением framebuffer, который даёт Limine — см. boot-лог,
там печатается "framebuffer WxH").

ВНИМАНИЕ: результат может быть здоровенным .c файлом (WxH*4 байта) —
для 1024x768 это ~3MB исходника. Для kernel-обоев обычно достаточно
задать разрешение поменьше и растягивать в коде, либо сжимать (RLE) —
это пока не реализовано, просто сырой массив для начала.
"""
import sys
from PIL import Image

if len(sys.argv) != 4:
    print(__doc__)
    sys.exit(1)

path, w, h = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
img = Image.open(path).convert("RGB").resize((w, h))
pixels = list(img.getdata())

print("#include <BNUX/types.h>")
print(f"const uint32_t wallpaper_width = {w};")
print(f"const uint32_t wallpaper_height = {h};")
print(f"const uint32_t wallpaper_data[{w * h}] = {{")

line = []
for i, (r, g, b) in enumerate(pixels):
    line.append(f"0x00{r:02X}{g:02X}{b:02X}")
    if len(line) == 16:
        print(",".join(line) + ",")
        line = []
if line:
    print(",".join(line) + ",")

print("};")
