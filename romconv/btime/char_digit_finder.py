#!/usr/bin/env python3
# Trova i tile carattere che rappresentano le cifre 0-9 (per il display del
# punteggio) dentro btime_chartiles.h, per verificare se il loro codice
# ricade nel range 512-863 usato dall'hack "ingrediente" (btime_is_ingredient_char).
# Renderizza con la STESSA correzione 180 gradi usata a runtime in blit_tile
# (tile[7-r][7-c]) cosi' la vista corrisponde a quanto appare realmente su schermo.
import re
from PIL import Image, ImageDraw

SRC = "../../source/src/machines/btime/btime_chartiles.h"

with open(SRC, "r") as f:
    text = f.read()

# Ogni tile e' {{...8 valori...},{...} x8}
tile_re = re.compile(r"\{((?:\{[^}]*\},?){8})\}")
row_re = re.compile(r"\{([^}]*)\}")

tiles = []
for m in tile_re.finditer(text):
    rows_txt = m.group(1)
    rows = []
    for rm in row_re.finditer(rows_txt):
        vals = [int(v) for v in rm.group(1).split(",") if v.strip() != ""]
        assert len(vals) == 8
        rows.append(vals)
    assert len(rows) == 8
    tiles.append(rows)

print(f"tile totali trovati: {len(tiles)}")
assert len(tiles) == 1024

def corrected(tile):
    # runtime: tile[7-r][7-c]
    return [[tile[7-r][7-c] for c in range(8)] for r in range(8)]

CELL = 24
COLS = 32
ROWS = (len(tiles) + COLS - 1) // COLS
img = Image.new("RGB", (COLS*CELL, ROWS*CELL), (0,0,0))
draw = ImageDraw.Draw(img)

for idx, t in enumerate(tiles):
    ct = corrected(t)
    col = idx % COLS
    row = idx // COLS
    ox, oy = col*CELL, row*CELL
    for r in range(8):
        for c in range(8):
            v = ct[r][c]
            if v:
                shade = 40 + v*28
                draw.rectangle([ox+c*3, oy+r*3, ox+c*3+2, oy+r*3+2], fill=(shade,shade,255))
    if idx % 50 == 0:
        draw.text((ox, oy), str(idx), fill=(255,0,0))

img = img.resize((img.width*2, img.height*2), Image.NEAREST)
img.save("char_all_indexed.png")
print("salvato char_all_indexed.png")

# Zoom sui primi 40 tile (dove ci si aspetta 0-9 + eventuali lettere UI)
CELL2 = 48
COLS2 = 10
n = 40
ROWS2 = (n + COLS2 - 1) // COLS2
img2 = Image.new("RGB", (COLS2*CELL2, ROWS2*(CELL2+16)), (30,30,30))
draw2 = ImageDraw.Draw(img2)
for idx in range(n):
    t = tiles[idx]
    ct = corrected(t)
    col = idx % COLS2
    row = idx // COLS2
    ox, oy = col*CELL2, row*(CELL2+16)
    for r in range(8):
        for c in range(8):
            v = ct[r][c]
            if v:
                shade = 40 + v*28
                draw2.rectangle([ox+c*6, oy+r*6, ox+c*6+5, oy+r*6+5], fill=(shade,shade,255))
    draw2.text((ox, oy+CELL2), str(idx), fill=(255,255,0))
img2.save("char_first40_zoom.png")
print("salvato char_first40_zoom.png")
