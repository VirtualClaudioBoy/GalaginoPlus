#!/usr/bin/env python3
"""
Turtles ROM converter for GALAGINO

ROM set (turtles) MAME:
  Main CPU:  5x 0x1000  turt_vid.2c/2e/2f/2h/2j
  Audio CPU: 2x 0x1000  turt_snd.5c/5d
  gfx:       2x 0x0800  turt_vid.5h/5f
  prom:      1x 0x0020  turtles.clr
"""

import os
import sys
import zipfile
import hashlib

sys.dont_write_bytecode = True

ROM_SET = os.path.normpath(os.path.join("..", "..", "romszip", "turtles.zip"))
OUT_DIR = os.path.normpath(os.path.join("..", "..", "source", "src", "machines", "turtles"))

def hex8(v):  return "0x{:02x}".format(v & 0xFF)
def hex16(v): return "0x{:04x}".format(v & 0xFFFF)
def hex32(v): return "0x{:08x}".format(v & 0xFFFFFFFF)

def load_file(names, sha1):
    for name in names:
        with zipfile.ZipFile(ROM_SET) as z:
            if name in z.namelist():
                with z.open(name, 'r') as f:
                    rom = bytearray(f.read())
                    digest = hashlib.sha1(rom).hexdigest()
                    if sha1 != digest:
                        print(f"bad hash for {name}: expected {sha1}, got {digest}")
                        return None
                    return rom
    print(f"ERROR: None of {names} found in {os.path.abspath(ROM_SET)}")
    return None

def write_rom(filename, name, data):
    with open(filename, 'w') as f:
        f.write("// Turtles program ROM ({} bytes)\n".format(len(data)))
        f.write("const unsigned char {}[] = {{\n".format(name))
        for i in range(0, len(data), 16):
            line = ", ".join(hex8(data[j]) for j in range(i, min(i+16, len(data))))
            f.write("  " + line)
            if i + 16 < len(data): f.write(",")
            f.write("\n")
        f.write("};\n")
    print("Wrote: {} ({} bytes)".format(os.path.abspath(filename), len(data)))

def write_tilemap(filename, tiles):
    with open(filename, 'w') as f:
        f.write("// Turtles tilemap: {} tiles, 8x8, 2bpp\n".format(len(tiles)))
        f.write("const unsigned short turtles_tilemap[][8] = {\n")
        for t, rows in enumerate(tiles):
            f.write("  { " + ", ".join(hex16(r) for r in rows) + " }")
            if t < len(tiles) - 1: f.write(",")
            f.write("\n")
        f.write("};\n")
    print("Wrote: {} ({} tiles)".format(os.path.abspath(filename), len(tiles)))

def write_spritemap(filename, all_orientations):
    num_sprites = len(all_orientations[0])
    with open(filename, 'w') as f:
        f.write("// Turtles spritemap: {} sprites, 16x16, 2bpp, 4 orientations\n".format(num_sprites))
        f.write("const unsigned long turtles_spritemap[][%d][16] = {\n" % num_sprites)
        for o, sprites in enumerate(all_orientations):
            f.write("  { // orientation %d\n" % o)
            for s, rows in enumerate(sprites):
                f.write("    { " + ", ".join(hex32(r) for r in rows) + " }")
                if s < len(sprites) - 1: f.write(",")
                f.write("\n")
            f.write("  }")
            if o < 3: f.write(",")
            f.write("\n")
        f.write("};\n")
    print("Wrote: {} ({} sprites x 4 orientations)".format(os.path.abspath(filename), num_sprites))

def write_colormap(filename, rgb565):
    with open(filename, 'w') as f:
        f.write("// Turtles colormap: 8 palettes x 4 colors, RGB565\n")
        f.write("const unsigned short turtles_colormap[][4] = {\n")
        for pal in range(8):
            colors = rgb565[pal*4 : pal*4+4]
            f.write("  { " + ", ".join(hex16(c) for c in colors) + " }")
            if pal < 7: f.write(",")
            f.write("  // palette {}\n".format(pal))
        f.write("};\n")
    print("Wrote: {} (8 palettes)".format(os.path.abspath(filename)))

def parse_chr_2(data0, data1):
    char = []
    for y in range(8):
        row = []
        for x in range(8):
            c0 = 1 if data0[7 - x] & (0x80 >> y) else 0
            c1 = 2 if data1[7 - x] & (0x80 >> y) else 0
            row.append(c0 + c1)
        char.append(row)
    return char

def dump_chr(data):
    vals = []
    for y in range(8):
        val = 0
        for x in range(8):
            val = (val >> 2) + (data[y][x] << (16 - 2))
        vals.append(val)
    return vals

def convert_tiles(plane0, plane1):
    num_tiles = len(plane0) // 8
    tiles = []
    for t in range(num_tiles):
        d0 = plane0[t * 8 : t * 8 + 8]
        d1 = plane1[t * 8 : t * 8 + 8]
        tiles.append(dump_chr(parse_chr_2(d0, d1)))
    return tiles

def parse_sprite_galaxian(data0, data1):
    sprite = []
    for y in range(16):
        row = []
        for x in range(16):
            ym = (y & 7) | ((x & 8) ^ 8)
            xm = (x & 7) | (y & 8)
            byte_idx = (xm ^ 7) + ((ym & 8) << 1)
            bit_mask = 0x80 >> (ym & 7)
            c0 = 1 if data0[byte_idx] & bit_mask else 0
            c1 = 2 if data1[byte_idx] & bit_mask else 0
            row.append(c0 + c1)
        sprite.append(row)
    return sprite

def dump_sprite(data, flip_x, flip_y):
    vals = []
    y_range = range(16) if not flip_y else reversed(range(16))
    for y in y_range:
        val = 0
        for x in range(16):
            if not flip_x:
                val = (val >> 2) + (data[y][x] << (32 - 2))
            else:
                val = (val << 2) + data[y][x]
        vals.append(val)
    return vals

def convert_sprites(plane0, plane1):
    num_sprites = len(plane0) // 32
    sprites = []
    for s in range(num_sprites):
        d0 = plane0[32 * s : 32 * (s + 1)]
        d1 = plane1[32 * s : 32 * (s + 1)]
        sprites.append(parse_sprite_galaxian(d0, d1))
    all_orientations = []
    for flip_x, flip_y in [(False, False), (False, True), (True, False), (True, True)]:
        orientation = [dump_sprite(s, flip_x, flip_y) for s in sprites]
        all_orientations.append(orientation)
    return all_orientations

def convert_colors(prom):
    rgb565 = []
    for i in range(32):
        bits = prom[i]
        r = 0x21 * ((bits >> 0) & 1) + 0x47 * ((bits >> 1) & 1) + 0x97 * ((bits >> 2) & 1)
        g = 0x21 * ((bits >> 3) & 1) + 0x47 * ((bits >> 4) & 1) + 0x97 * ((bits >> 5) & 1)
        b = 0x4F * ((bits >> 6) & 1) + 0xA8 * ((bits >> 7) & 1)
        r5 = (r >> 3) & 0x1F
        g6 = (g >> 2) & 0x3F
        b5 = (b >> 3) & 0x1F
        val = (r5 << 11) | (g6 << 5) | b5
        rgb565.append(((val & 0xFF) << 8) | ((val >> 8) & 0xFF))
    return rgb565

def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    print(f"Load ROM from: {os.path.abspath(ROM_SET)}")
    print(f"Target files:  {os.path.abspath(OUT_DIR)}")

    rom_2c = load_file(["turt_vid.2c"], "3ca89800fda7a7e61f54d71d5302908be2706def")
    rom_2e = load_file(["turt_vid.2e"], "af74602bf2454eb8f3b9bb5c425e2476feeecd69")
    rom_2f = load_file(["turt_vid.2f"], "2af9383e5a289c2d7fbe6cf5e5b1519c352afbab")
    rom_2h = load_file(["turt_vid.2h"], "3dcdf5dc601c875fc9d8b9a46e3ef588e7478e0d")
    rom_2j = load_file(["turt_vid.2j"], "bb1e91b2e6d4b5a861bf37907ef6b198328d8d83")

    rom_5c = load_file(["turt_snd.5c"], "5621f336e9be8acf986a34bbb8855ed5d45c28ef")
    rom_5d = load_file(["turt_snd.5d"], "8a49c55feba094b07380615cf0b6f0878c25a260")

    rom_5h = load_file(["turt_vid.5h"], "bc3f52cf6c6e19dfd2dacd1e8c9128f437e995fc")
    rom_5f = load_file(["turt_vid.5f"], "dee51d77be262a2944488e381541c10a2b6e5d83")

    prom_clr = load_file(["turtles.clr"], "09fd795170d7d30f101d579f57553da5ff3800ab")

    if not all(v is not None for v in [rom_2c, rom_2e, rom_2f, rom_2h, rom_2j,
                                        rom_5c, rom_5d, rom_5h, rom_5f, prom_clr]):
        print("ERROR: Not all files loaded")
        return

    main_cpu = rom_2c + rom_2e + rom_2f + rom_2h + rom_2j  # 5 x 4KB = 20KB
    write_rom(os.path.join(OUT_DIR, "turtles_main_rom.h"), "turtles_main_rom", main_cpu)

    audio_cpu = rom_5c + rom_5d  # 2 x 4KB = 8KB
    write_rom(os.path.join(OUT_DIR, "turtles_audio_rom.h"), "turtles_audio_rom", audio_cpu)

    tiles = convert_tiles(rom_5h, rom_5f)
    write_tilemap(os.path.join(OUT_DIR, "turtles_tilemap.h"), tiles)

    sprites = convert_sprites(rom_5h, rom_5f)
    write_spritemap(os.path.join(OUT_DIR, "turtles_spritemap.h"), sprites)

    rgb565 = convert_colors(prom_clr)
    write_colormap(os.path.join(OUT_DIR, "turtles_cmap.h"), rgb565)

    print("\n--- Complete ---")
    print(f"All files generated in: {os.path.abspath(OUT_DIR)}")

if __name__ == "__main__":
    main()
