#!/usr/bin/env python3
"""
Roc'n Rope ROM converter for GALAGINO
Konami 1983 - rocnrope ROM set

ROM set (MAME: rocnrope):
  Main CPU:  5 x 8KB  rr1..rr4 + rnr_h5.vid  (40KB, 0x6000-0xFFFF)
  Audio CPU: 2 x 4KB  rnr_7a.snd + rnr_8a.snd (8KB,  0x0000-0x1FFF)
  Tiles:     2 x 8KB  rnr_h12.vid + rnr_h11.vid (16KB, 4bpp)
  Sprites:   4 x 8KB  rnr_a11/a12/a9/a10.vid   (32KB, 4bpp)
  PROMs:     a17_prom.bin (32B  palette)
             b16_prom.bin (256B sprite LUT)
             rocnrope.pr3  (256B tile LUT)

CPU: Konami-1 (MC6809E with XOR-0x22 opcode encryption)
  - opcode byte stored in ROM must be XOR'd with 0x22 before execution
  - ROM patch (MAME init_rocnrope): main_rom[0x703D - 0x6000] = 0x98 ^ 0x22 = 0xBA (encrypted byte)
"""

import os
import sys
import zipfile
import hashlib

sys.dont_write_bytecode = True

ROM_SET = os.path.normpath(os.path.join("..", "..", "romszip", "rocnrope.zip"))
OUT_DIR = os.path.normpath(os.path.join("..", "..", "source", "src", "machines", "rocnrope"))

def hex8(v):  return "0x{:02x}".format(v & 0xFF)
def hex16(v): return "0x{:04x}".format(v & 0xFFFF)

def load_file(name, sha1):
    with zipfile.ZipFile(ROM_SET) as z:
        if name not in z.namelist():
            print(f"ERROR: '{name}' not found in {os.path.abspath(ROM_SET)}")
            return None
        with z.open(name) as f:
            data = bytearray(f.read())
            digest = hashlib.sha1(data).hexdigest()
            if sha1 and digest != sha1:
                print(f"WARNING: bad hash for {name}: expected {sha1}, got {digest}")
            return data

def write_rom(filename, name, data, comment):
    with open(filename, 'w') as f:
        f.write(f"// {comment} ({len(data)} bytes)\n")
        f.write(f"const unsigned char {name}[] = {{\n")
        for i in range(0, len(data), 16):
            line = ", ".join(hex8(data[j]) for j in range(i, min(i+16, len(data))))
            f.write("  " + line)
            if i + 16 < len(data): f.write(",")
            f.write("\n")
        f.write("};\n")
    print(f"Wrote: {os.path.abspath(filename)} ({len(data)} bytes)")

# ---- Color PROM -> base RGB565 palette (32 entries) ----
# Same resistor network as galaxian (from MAME rocnrope.cpp):
#   bit 0 -- 1k ohm  -- RED
#   bit 1 -- 470 ohm -- RED
#   bit 2 -- 220 ohm -- RED
#   bit 3 -- 1k ohm  -- GREEN
#   bit 4 -- 470 ohm -- GREEN
#   bit 5 -- 220 ohm -- GREEN
#   bit 6 -- 470 ohm -- BLUE
#   bit 7 -- 220 ohm -- BLUE
def build_base_palette(prom):
    result = []
    for i in range(32):
        b = prom[i]
        r = 0x1D * ((b >> 0) & 1) + 0x3E * ((b >> 1) & 1) + 0x85 * ((b >> 2) & 1)
        g = 0x1D * ((b >> 3) & 1) + 0x3E * ((b >> 4) & 1) + 0x85 * ((b >> 5) & 1)
        bl= 0x48 * ((b >> 6) & 1) + 0x99 * ((b >> 7) & 1)
        r5 = min(r, 255) >> 3
        g6 = min(g, 255) >> 2
        b5 = min(bl,255) >> 3
        val = (r5 << 11) | (g6 << 5) | b5
        # byte-swap for SPI display
        result.append(((val & 0xFF) << 8) | ((val >> 8) & 0xFF))
    return result

# ---- Build color maps from LUT PROMs ----
# sprite_cmap[pal][pixel] = RGB565 (16 palettes x 16 colors)
# tile_cmap[pal][pixel]   = RGB565 (16 palettes x 16 colors)
def build_colormaps(base_palette, sprite_lut, tile_lut):
    sprite_cmap = []
    tile_cmap = []
    for pal in range(16):
        sp_row = []
        tl_row = []
        for pixel in range(16):
            sp_idx = sprite_lut[pal * 16 + pixel] & 0x0F
            tl_idx = tile_lut[pal * 16 + pixel] & 0x0F
            sp_row.append(base_palette[sp_idx])
            tl_row.append(base_palette[tl_idx])
        sprite_cmap.append(sp_row)
        tile_cmap.append(tl_row)
    return sprite_cmap, tile_cmap

# ---- 4bpp tile decode (charlayout from MAME rocnrope.cpp) ----
# half0 = rnr_h12.vid (8KB): planes 0+1 (low nibble=pl0, high nibble=pl1)
# half1 = rnr_h11.vid (8KB): planes 2+3 (low nibble=pl2, high nibble=pl3)
# 512 tiles, 16 bytes per tile per half
# planeoffset = { 0x2000*8+4, 0x2000*8+0, 4, 0 }  (MSB to LSB order)
# xoffset = { 0,1,2,3, 64,65,66,67 }
# yoffset = { 0,8,16,24,32,40,48,56 }
# charsize = 16 bytes
def decode_tile(half0, half1, tile):
    pixels = []
    for py in range(8):
        row = []
        for px in range(8):
            byte_off = py + (8 if px >= 4 else 0)
            bit = px % 4
            b0 = half0[tile * 16 + byte_off]
            b1 = half1[tile * 16 + byte_off]
            # MAME reads gfx bits MSB-first within each byte: xoffset n -> byte
            # bit 7-n. So planeoffset 0 (xoff 0..3) lives in the HIGH nibble
            # (bits 7..4) and planeoffset 4 in the LOW nibble (bits 3..0).
            p0 = (b0 >> (7 - bit)) & 1  # LSB,  planeoffset[3]=0 (high nibble)
            p1 = (b0 >> (3 - bit)) & 1  #       planeoffset[2]=4 (low nibble)
            p2 = (b1 >> (7 - bit)) & 1  #       planeoffset[1]=0x2000*8+0
            p3 = (b1 >> (3 - bit)) & 1  # MSB,  planeoffset[0]=0x2000*8+4
            row.append(p0 | (p1 << 1) | (p2 << 2) | (p3 << 3))
        pixels.append(row)
    return pixels  # pixels[y][x], landscape orientation

def convert_tiles(half0, half1):
    """
    Convert 512 tiles, rotate 90 CW for portrait (same as TimePlt).
    portrait[py][px] = landscape[7-px][py]
    Output: list of 512 tiles, each tile = 8 rows of 4 bytes (8 nibble-packed pixels).
    pixel = (tile[py][px>>1] >> ((px&1)*4)) & 0xF
    """
    num_tiles = len(half0) // 16
    result = []
    for t in range(num_tiles):
        landscape = decode_tile(half0, half1, t)
        # Rotate 90 CW: portrait[py][px] = landscape[7-px][py]
        tile_rows = []
        for py in range(8):
            packed = [0, 0, 0, 0]  # 4 bytes for 8 nibble-packed pixels
            for px in range(8):
                pixel = landscape[7 - px][py]
                # pack pixel into nibble: low nibble for even px, high for odd
                packed[px >> 1] |= (pixel << ((px & 1) * 4))
            tile_rows.append(packed)
        result.append(tile_rows)
    return result

def write_tilemap(filename, tiles, tile_cmap):
    with open(filename, 'w') as f:
        f.write(f"// Roc'n Rope tilemap: {len(tiles)} tiles, 8x8, 4bpp, portrait orientation\n")
        f.write(f"// pixel = (rocnrope_tilemap[tile][row][col>>1] >> ((col&1)*4)) & 0xF\n")
        f.write(f"const unsigned char rocnrope_tilemap[{len(tiles)}][8][4] = {{\n")
        for t, tile in enumerate(tiles):
            f.write("  { ")
            rows_str = []
            for row in tile:
                rows_str.append("{ " + ", ".join(hex8(b) for b in row) + " }")
            f.write(", ".join(rows_str))
            f.write(" }")
            if t < len(tiles) - 1: f.write(",")
            f.write("\n")
        f.write("};\n\n")

        f.write("// Tile color map: 16 palettes x 16 colors, RGB565 byte-swapped\n")
        f.write("const unsigned short rocnrope_tile_cmap[16][16] = {\n")
        for pal in range(16):
            colors = tile_cmap[pal]
            f.write("  { " + ", ".join(hex16(c) for c in colors) + " }")
            if pal < 15: f.write(",")
            f.write(f"  // palette {pal}\n")
        f.write("};\n")
    print(f"Wrote: {os.path.abspath(filename)} ({len(tiles)} tiles)")

# ---- 4bpp sprite decode (spritelayout from MAME rocnrope.cpp) ----
# sprites_lo = rnr_a11 + rnr_a12 (16KB): planes 0+1
# sprites_hi = rnr_a9  + rnr_a10 (16KB): planes 2+3
# 256 sprites, 64 bytes per sprite per half
# planeoffset = { 256*64*8+4, 256*64*8+0, 4, 0 }
# xoffset = { 0,1,2,3, 64,65,66,67, 128,129,130,131, 192,193,194,195 }
# yoffset = { 0,8,...,56, 256,264,...,312 }
# charsize = 64 bytes
Y_BITS = [y * 8 for y in range(8)] + [(32 + y) * 8 for y in range(8)]
X_BITS = ([x for x in range(4)] + [64 + x for x in range(4)] +
          [128 + x for x in range(4)] + [192 + x for x in range(4)])

def decode_sprite(sprites_lo, sprites_hi, sprite):
    pixels = []
    for py in range(16):
        row = []
        for px in range(16):
            total_bit = sprite * 64 * 8 + Y_BITS[py] + X_BITS[px]
            byte_idx = total_bit // 8
            bit = total_bit % 8  # always 0..3 since x_bits contribute 0..3
            lo = sprites_lo[byte_idx]
            hi = sprites_hi[byte_idx]
            # MAME reads gfx bits MSB-first within each byte (see decode_tile)
            p0 = (lo >> (7 - bit)) & 1  # LSB,  planeoffset[3]=0 (high nibble)
            p1 = (lo >> (3 - bit)) & 1  #       planeoffset[2]=4 (low nibble)
            p2 = (hi >> (7 - bit)) & 1  #       planeoffset[1]=256*64*8+0
            p3 = (hi >> (3 - bit)) & 1  # MSB,  planeoffset[0]=256*64*8+4
            row.append(p0 | (p1 << 1) | (p2 << 2) | (p3 << 3))
        pixels.append(row)
    return pixels  # pixels[row][col], landscape orientation

def convert_sprites(sprites_lo, sprites_hi):
    """
    Convert 256 sprites in landscape orientation (no rotation).
    Transposed rendering at runtime handles the ROT270 display rotation.
    Output: list of 256 sprites, each = 16 rows of 8 bytes (16 nibble-packed pixels).
    pixel = (sprite[row][col>>1] >> ((col&1)*4)) & 0xF
    """
    num_sprites = len(sprites_lo) // 64
    result = []
    for s in range(num_sprites):
        landscape = decode_sprite(sprites_lo, sprites_hi, s)
        # Pack in landscape orientation (no rotation)
        spr_rows = []
        for row in range(16):
            packed = [0] * 8  # 8 bytes for 16 nibble-packed pixels
            for col in range(16):
                pixel = landscape[row][col]
                packed[col >> 1] |= (pixel << ((col & 1) * 4))
            spr_rows.append(packed)
        result.append(spr_rows)
    return result

def write_spritemap(filename, sprites, sprite_cmap):
    with open(filename, 'w') as f:
        f.write(f"// Roc'n Rope spritemap: {len(sprites)} sprites, 16x16, 4bpp, landscape orientation\n")
        f.write(f"// pixel = (rocnrope_spritemap[sprite][row][col>>1] >> ((col&1)*4)) & 0xF\n")
        f.write(f"// pixel==0 is transparent\n")
        f.write(f"const unsigned char rocnrope_spritemap[{len(sprites)}][16][8] = {{\n")
        for s, spr in enumerate(sprites):
            f.write("  { ")
            rows_str = []
            for row in spr:
                rows_str.append("{ " + ", ".join(hex8(b) for b in row) + " }")
            f.write(", ".join(rows_str))
            f.write(" }")
            if s < len(sprites) - 1: f.write(",")
            f.write("\n")
        f.write("};\n\n")

        f.write("// Sprite color map: 16 palettes x 16 colors, RGB565 byte-swapped\n")
        f.write("const unsigned short rocnrope_sprite_cmap[16][16] = {\n")
        for pal in range(16):
            colors = sprite_cmap[pal]
            f.write("  { " + ", ".join(hex16(c) for c in colors) + " }")
            if pal < 15: f.write(",")
            f.write(f"  // palette {pal}\n")
        f.write("};\n")
    print(f"Wrote: {os.path.abspath(filename)} ({len(sprites)} sprites)")

def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    print(f"Load ROM from: {os.path.abspath(ROM_SET)}")
    print(f"Target files:  {os.path.abspath(OUT_DIR)}")

    # Main CPU ROMs (5 x 8KB = 40KB, addresses 0x6000-0xFFFF)
    rr1 = load_file("rr1.1h",       "c9509cfb9f9043cd6c226cc84dbc2e2b744488f6")
    rr2 = load_file("rr2.2h",       "70bb4b838cdafedf3d94425fad84f77815898d83")
    rr3 = load_file("rr3.3h",       "c08ab3caaa646f4752f890d8339bce6b723864bb")
    rr4 = load_file("rr4.4h",       "93762d1890f40abc98372a2aa9fe0f63252b6389")
    rr5 = load_file("rnr_h5.vid",   "930ccf8dcf4971d0a15f406d9114be5ecfaa1727")

    # Audio CPU ROMs (2 x 4KB = 8KB)
    snd7 = load_file("rnr_7a.snd",  "b701019b4e7b06b268be660ce7958b5367318c27")
    snd8 = load_file("rnr_8a.snd",  "34ac035c0c2ed6bcafde1491d976bb9e9d2a2a7d")

    # Tile ROMs (2 x 8KB = 16KB)
    tile_h12 = load_file("rnr_h12.vid", "0ea19ae4d7c2da14f23c81abb8e2c931785b2715")
    tile_h11 = load_file("rnr_h11.vid", "182c7c9b9849ebb57b3ff7c0b629f2f8e2efa9ba")

    # Sprite ROMs (4 x 8KB = 32KB)
    spr_a11 = load_file("rnr_a11.vid", "27c090cb1c3767c997daeedbe1ba24786f9e78f1")
    spr_a12 = load_file("rnr_a12.vid", "4c3cd850b347217af3dd5c9bb84bcff7b30689bd")
    spr_a9  = load_file("rnr_a9.vid",  "42d2b05360e58b1b2b3ad06c98eb46d9da2b1c21")
    spr_a10 = load_file("rnr_a10.vid", "476d67821519feddc9f9c8537b46e6eede790035")

    # PROMs
    prom_a17 = load_file("a17_prom.bin", "1c2198b286c75aa9e78d000432795b1ce86ad6b9")
    prom_b16 = load_file("b16_prom.bin", "7a5b4aed5f87180850657b8852bb3f3138d58b5b")
    prom_pr3 = load_file("rocnrope.pr3", "923d6ccf015fd7458494416cc05426cc922a9238")

    files = [rr1, rr2, rr3, rr4, rr5, snd7, snd8,
             tile_h12, tile_h11, spr_a11, spr_a12, spr_a9, spr_a10,
             prom_a17, prom_b16, prom_pr3]
    if any(f is None for f in files):
        print("ERROR: Not all ROM files loaded")
        return

    # Assemble 40KB main ROM (CPU addresses 0x6000-0xFFFF)
    main_rom = bytearray(rr1 + rr2 + rr3 + rr4 + rr5)
    # KONAMI1 ROM patch (MAME init_rocnrope):
    #   memregion("maincpu")->base()[0x703d] = 0x98 ^ 0x22;   // = 0xBA
    # MAME stores the ENCRYPTED byte 0xBA. The opcode fetch then decrypts it
    # with the address-dependent mask: at CPU 0x703D the mask is 0x28
    # (addr & 0x0A == 0x08), giving opcode 0xBA ^ 0x28 = 0x92 = SBCA direct
    # (2 bytes), which keeps the object-update loop at 0x702F-0x704D aligned.
    # Storing 0x98 here is WRONG: it decodes to 0xB0 = SUBA extended (3 bytes),
    # misaligning the whole loop and eventually crashing into data at 0xE5xx.
    main_rom[0x103D] = 0xBA
    print(f"Applied KONAMI1 ROM patch at offset 0x103D (CPU addr 0x703D, encrypted 0xBA, decoded = 0x92)")
    write_rom(os.path.join(OUT_DIR, "rocnrope_main_rom.h"),
              "rocnrope_main_rom", main_rom,
              "Roc'n Rope main CPU ROM (40KB, KONAMI1, addresses 0x6000-0xFFFF)")

    # Assemble 8KB audio ROM
    audio_rom = bytearray(snd7 + snd8)
    write_rom(os.path.join(OUT_DIR, "rocnrope_audio_rom.h"),
              "rocnrope_audio_rom", audio_rom,
              "Roc'n Rope audio CPU ROM (8KB, Z80)")

    # Build color lookup tables
    base_palette = build_base_palette(prom_a17)
    sprite_cmap, tile_cmap = build_colormaps(base_palette, prom_b16, prom_pr3)

    # Convert tiles (4bpp, ROT90 for portrait)
    tiles = convert_tiles(tile_h12, tile_h11)
    write_tilemap(os.path.join(OUT_DIR, "rocnrope_tilemap.h"), tiles, tile_cmap)

    # Convert sprites (4bpp, landscape orientation)
    sprites_lo = bytearray(spr_a11 + spr_a12)
    sprites_hi = bytearray(spr_a9  + spr_a10)
    sprites = convert_sprites(sprites_lo, sprites_hi)
    write_spritemap(os.path.join(OUT_DIR, "rocnrope_spritemap.h"), sprites, sprite_cmap)

    print("\n--- Complete ---")
    print(f"All files generated in: {os.path.abspath(OUT_DIR)}")

if __name__ == "__main__":
    main()
