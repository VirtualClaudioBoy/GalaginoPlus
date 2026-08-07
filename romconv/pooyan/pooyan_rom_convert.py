#!/usr/bin/env python3
"""
Pooyan ROM converter per GALAGINO
Konami 1982 - set MAME "pooyan" - hardware gemello di Time Pilot ma gfx 4bpp

ROM set:
  Program: 1.4a + 2.5a + 3.6a + 4.7a (4x8K) = 32KB main CPU
  Sound:   xx.7a + xx.8a (2x4K) = 8KB sound CPU (timeplt_audio)
  Tiles:   8.10g (meta' 1) + 7.9g (meta' 2) = 256 tile 8x8 4bpp
  Sprites: 6.9a (meta' 1) + 5.8a (meta' 2) = 64 sprite 16x16 4bpp
  PROMs:   pooyan.pr1 (32B) = palette
           pooyan.pr3 (256B) = char lookup  (16 gruppi x 16 pen, |0x10)
           pooyan.pr2 (256B) = sprite lookup (16 gruppi x 16 pen)

Layout gfx MAME (charlayout/spritelayout): 4 piani
  { RGN_FRAC(1,2)+4, RGN_FRAC(1,2)+0, 4, 0 }  (primo = MSB), bit MSB-first:
  per xo=col%4: bit3=(b2>>(3-xo))&1, bit2=(b2>>(7-xo))&1,
                bit1=(b1>>(3-xo))&1, bit0=(b1>>(7-xo))&1
  con b1 = byte nella prima meta' ROM, b2 = stesso offset nella seconda.
"""

import os

ROM_SRC = os.path.normpath(os.path.join("..", "roms"))
OUT_DIR = os.path.normpath(os.path.join("..", "..", "source", "src", "machines", "pooyan"))

def load_file(name):
    path = os.path.join(ROM_SRC, name)
    with open(path, "rb") as f:
        return bytearray(f.read())

def hex8(v):  return "0x{:02X}".format(v & 0xFF)
def hex16(v): return "0x{:04X}".format(v & 0xFFFF)
def hex32(v): return "0x{:08X}".format(v & 0xFFFFFFFF)

# ---- Palette PROM (pr1, 32 colori) -> RGB565 byte-swapped ----
# MAME pooyan_state::palette: R = bit0-2 (1k/470/220), G = bit3-5 (1k/470/220),
# B = bit6-7 (470/220). Pesi normalizzati compute_resistor_weights:
# 1k/470/220 -> 33,71,151 ; 470/220 -> 81,174 (come rocnrope)
def convert_palette(prom):
    w3 = [33, 71, 151]      # 1k, 470, 220
    w2 = [81, 174]          # 470, 220
    rgb565 = []
    for i in range(32):
        v = prom[i]
        r = w3[0]*((v >> 0) & 1) + w3[1]*((v >> 1) & 1) + w3[2]*((v >> 2) & 1)
        g = w3[0]*((v >> 3) & 1) + w3[1]*((v >> 4) & 1) + w3[2]*((v >> 5) & 1)
        b = w2[0]*((v >> 6) & 1) + w2[1]*((v >> 7) & 1)
        r = min(r, 255); g = min(g, 255); b = min(b, 255)
        val = (((r >> 3) & 0x1F) << 11) | (((g >> 2) & 0x3F) << 5) | ((b >> 3) & 0x1F)
        rgb565.append(((val & 0xFF) << 8) | ((val >> 8) & 0xFF))  # byte-swap per SPI
    return rgb565

# ---- Colormap: 16 gruppi x 16 pen per char e sprite ----
def build_colormap(base_palette, prom_char, prom_sprite):
    # chars: entry = (lut & 0x0F) | 0x10  -> palette 16-31
    char_colors = [base_palette[(prom_char[i] & 0x0F) | 0x10] for i in range(256)]
    # sprites: entry = lut & 0x0F -> palette 0-15
    sprite_colors = [base_palette[prom_sprite[i] & 0x0F] for i in range(256)]
    # trasparenza sprite (MAME transpen_mask(gfx,color,0)):
    # pen trasparente se il valore LUT (colore indiretto) e' 0
    transmask = []
    for grp in range(16):
        mask = 0
        for pen in range(16):
            if (prom_sprite[grp * 16 + pen] & 0x0F) == 0:
                mask |= (1 << pen)
        transmask.append(mask)
    return char_colors, sprite_colors, transmask

# ---- Decodifica pixel 4bpp da due meta' ROM ----
def pixel4(half1, half2, byte_off, xo):
    b1 = half1[byte_off]
    b2 = half2[byte_off]
    bit3 = (b2 >> (3 - xo)) & 1
    bit2 = (b2 >> (7 - xo)) & 1
    bit1 = (b1 >> (3 - xo)) & 1
    bit0 = (b1 >> (7 - xo)) & 1
    return (bit3 << 3) | (bit2 << 2) | (bit1 << 1) | bit0

# ---- Tiles 8x8 4bpp: 256 tile, 16 byte/tile per meta' ----
# xoffset {0-3, 64-67} -> col>=4 = +8 byte; yoffset row*8 bit -> +row byte
# Rotazione 90 CW per portrait (come timeplt): portrait[py][px] = landscape[7-px][py]
def convert_tiles(half1, half2):
    tiles = []
    for t in range(256):
        base = t * 16
        landscape = [[pixel4(half1, half2, base + row + (8 if col >= 4 else 0), col % 4)
                      for col in range(8)] for row in range(8)]
        packed = []
        for py in range(8):
            row_val = 0
            for px in range(8):
                row_val |= landscape[7 - px][py] << (px * 4)
            packed.append(row_val)
        tiles.append(packed)
    return tiles

# ---- Sprites 16x16 4bpp: 64 sprite, 64 byte/sprite per meta' ----
# xoffset {0-3, 64-67, 128-131, 192-195} -> gruppo col*8 byte
# yoffset {0..56, 256..312} -> righe 8-15 a +32 byte (24+row)
# Landscape senza rotazione, 4 orientamenti [norm, flipY, flipX, flipXY],
# riga impacchettata come 8 byte nibble (col pari = nibble basso).
def convert_sprites(half1, half2):
    all_pixels = []
    for s in range(64):
        base = s * 64
        landscape = []
        for row in range(16):
            row_byte = row if row < 8 else 24 + row
            landscape.append([pixel4(half1, half2, base + row_byte + (col // 4) * 8, col % 4)
                              for col in range(16)])
        all_pixels.append(landscape)

    all_orientations = []
    for flip_x, flip_y in [(False, False), (False, True), (True, False), (True, True)]:
        orientation = []
        for pix in all_pixels:
            rows = []
            y_range = range(15, -1, -1) if flip_y else range(16)
            for y in y_range:
                row_bytes = [0] * 8
                for x in range(16):
                    xx = (15 - x) if flip_x else x
                    row_bytes[xx >> 1] |= pix[y][x] << ((xx & 1) * 4)
                rows.append(row_bytes)
            orientation.append(rows)
        all_orientations.append(orientation)
    return all_orientations

# ---- Scrittura header ----
def write_bytes(filename, comment, name, data):
    with open(filename, 'w') as f:
        f.write("// {} ({} bytes)\n".format(comment, len(data)))
        f.write("const unsigned char {}[] = {{\n".format(name))
        for i in range(0, len(data), 16):
            line = ", ".join(hex8(b) for b in data[i:i+16])
            f.write("  " + line)
            if i + 16 < len(data):
                f.write(",")
            f.write("\n")
        f.write("};\n")
    print("Written: {} ({} bytes)".format(filename, len(data)))

def write_tilemap(filename, tiles, char_colors):
    with open(filename, 'w') as f:
        f.write("// Pooyan tilemap: {} tile, 8x8, 4bpp, ruotati 90CW (portrait)\n".format(len(tiles)))
        f.write("// pixel = (riga >> (px*4)) & 0xF\n")
        f.write("const unsigned long pooyan_tilemap[][8] = {\n")
        for t, rows in enumerate(tiles):
            f.write("  { " + ", ".join(hex32(r) for r in rows) + " }")
            f.write("," if t < len(tiles) - 1 else "")
            f.write("\n")
        f.write("};\n\n")
        f.write("// Pooyan char colormap: 16 gruppi x 16 pen, RGB565 byte-swapped\n")
        f.write("const unsigned short pooyan_char_colormap[][16] = {\n")
        for grp in range(16):
            colors = char_colors[grp*16 : grp*16+16]
            f.write("  { " + ", ".join(hex16(c) for c in colors) + " }")
            f.write("," if grp < 15 else "")
            f.write("  // gruppo {}\n".format(grp))
        f.write("};\n")
    print("Written: {} ({} tiles, 16 char groups)".format(filename, len(tiles)))

def write_spritemap(filename, all_orientations, sprite_colors, transmask):
    num = len(all_orientations[0])
    with open(filename, 'w') as f:
        f.write("// Pooyan spritemap: {} sprite, 16x16, 4bpp, 4 orientamenti, landscape\n".format(num))
        f.write("// pixel col c = (riga[c>>1] >> ((c&1)*4)) & 0xF\n")
        f.write("const unsigned char pooyan_spritemap[4][%d][16][8] = {\n" % num)
        for o, sprites in enumerate(all_orientations):
            f.write("  { // orientamento %d\n" % o)
            for s, rows in enumerate(sprites):
                f.write("    {")
                for r, rb in enumerate(rows):
                    f.write("{" + ",".join(hex8(b) for b in rb) + "}")
                    if r < 15: f.write(",")
                f.write("}")
                if s < num - 1: f.write(",")
                f.write("\n")
            f.write("  }")
            if o < 3: f.write(",")
            f.write("\n")
        f.write("};\n\n")
        f.write("// Pooyan sprite colormap: 16 gruppi x 16 pen, RGB565 byte-swapped\n")
        f.write("const unsigned short pooyan_sprite_colormap[][16] = {\n")
        for grp in range(16):
            colors = sprite_colors[grp*16 : grp*16+16]
            f.write("  { " + ", ".join(hex16(c) for c in colors) + " }")
            f.write("," if grp < 15 else "")
            f.write("  // gruppo {}\n".format(grp))
        f.write("};\n\n")
        f.write("// Maschera trasparenza per gruppo (bit N = pen N trasparente, MAME transpen_mask)\n")
        f.write("const unsigned short pooyan_sprite_transmask[16] = {\n  ")
        f.write(", ".join(hex16(m) for m in transmask))
        f.write("\n};\n")
    print("Written: {} ({} sprites x 4, 16 groups + transmask)".format(filename, num))

def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    program = load_file("1.4a") + load_file("2.5a") + load_file("3.6a") + load_file("4.7a")
    snd     = load_file("xx.7a") + load_file("xx.8a")
    tiles1  = load_file("8.10g");  tiles2  = load_file("7.9g")
    spr1    = load_file("6.9a");   spr2    = load_file("5.8a")
    pr1     = load_file("pooyan.pr1")
    pr2     = load_file("pooyan.pr2")   # sprite lookup
    pr3     = load_file("pooyan.pr3")   # char lookup

    assert len(program) == 0x8000 and len(snd) == 0x2000
    assert len(tiles1) == 0x1000 and len(tiles2) == 0x1000
    assert len(spr1) == 0x1000 and len(spr2) == 0x1000
    assert len(pr1) == 32 and len(pr2) == 256 and len(pr3) == 256

    write_bytes(os.path.join(OUT_DIR, "pooyan_rom.h"), "Pooyan program ROM", "pooyan_rom", program)
    write_bytes(os.path.join(OUT_DIR, "pooyan_snd_rom.h"), "Pooyan sound ROM", "pooyan_snd_rom", snd)

    base_palette = convert_palette(pr1)
    char_colors, sprite_colors, transmask = build_colormap(base_palette, pr3, pr2)

    write_tilemap(os.path.join(OUT_DIR, "pooyan_tilemap.h"), convert_tiles(tiles1, tiles2), char_colors)
    write_spritemap(os.path.join(OUT_DIR, "pooyan_spritemap.h"), convert_sprites(spr1, spr2),
                    sprite_colors, transmask)

    # NB: pooyan_dipswitches.h e' mantenuto a mano, non viene sovrascritto
    print("\n--- OPERAZIONE COMPLETATA ---")

if __name__ == "__main__":
    main()
