#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Ali Baba and 40 Thieves (Sega, 1982) -- ROM converter for galagino.
# Hardware: Pac-Man derived (same tile/sprite/color scheme, same Namco WSG
# sound). Verified against MAME src/mame/misc/pacman.cpp, ROM_START(alibaba),
# via exact CRC32 match of every ROM file (see project memory).
#
# Source ROMs expected in ../roms/alibaba/ (extracted from romszip/alibaba.zip):
#   5e,5f,5h,5k      - gfx1 (tiles + sprites, 2KB each)
#   6e,6f,6h,6k,6l,6m - maincpu program ROM
#   ab7.bin          - gfx2 ("mystery" clock graphic)
#   82s123.e7        - master RGB palette PROM (32 bytes)
#   82s129.a4        - color lookup PROM (256 bytes)

import os

ROM_DIR = os.path.normpath(os.path.join("..", "roms", "alibaba"))
OUT_DIR = os.path.normpath(os.path.join("..", "..", "source", "src", "machines", "alibaba"))


def read_rom(name):
    path = os.path.join(ROM_DIR, name)
    with open(path, "rb") as f:
        return f.read()


def hex32(v):
    return "0x{:08x}".format(v & 0xffffffff)


def hex16(v):
    return "0x{:04x}".format(v & 0xffff)


def hex8(v):
    return "0x{:02x}".format(v & 0xff)


# ---------------------------------------------------------------------------
# Program ROM: CPU address space is non-contiguous
#   0x0000-0x3fff  6e,6f,6h,6k (4x 0x1000)
#   0x8000-0x8fff  6l           (1x 0x1000)
#   0xa000-0xa7ff  6m           (1x 0x0800), MAME: map(0xa000,0xa7ff).mirror(0x1800).rom()
#                  -> also readable/fetchable at 0xa800-0xafff, 0xb000-0xb7ff,
#                     0xb800-0xbfff (real HW: Z80 A15 wiring makes these alias
#                     the same 0x800-byte chip). MUST be replicated or any
#                     stray/legitimate fetch into those mirrors reads out of
#                     bounds of a short array (undefined behaviour / hangs).
# Build one FULL 0x10000-byte array (whole Z80 address space) with bytes
# placed at their real CPU offset (mirrors included); unused gaps are zero
# and should never be fetched in normal operation, but staying in-bounds
# for the full 64KB removes any risk of reading past the array.
# ---------------------------------------------------------------------------
def convert_rom():
    size = 0x10000
    rom = bytearray(size)

    def place(name, offset):
        data = read_rom(name)
        rom[offset:offset + len(data)] = data

    place("6e", 0x0000)
    place("6f", 0x1000)
    place("6h", 0x2000)
    place("6k", 0x3000)
    place("6l", 0x8000)

    bank6m = read_rom("6m")  # 0x0800 bytes, mirrored 4x across 0xa000-0xbfff
    for base in (0xa000, 0xa800, 0xb000, 0xb800):
        rom[base:base + len(bank6m)] = bank6m

    outfile = os.path.join(OUT_DIR, "alibaba_rom.h")
    with open(outfile, "w") as f:
        f.write("// Ali Baba and 40 Thieves - main CPU ROM, full 64KB Z80 address space.\n")
        f.write("// Populated: 0x0000-0x3fff, 0x8000-0x8fff, 0xa000-0xbfff (0x800-byte bank\n")
        f.write("// 'ab7=6m' mirrored 4x per MAME map(0xa000,0xa7ff).mirror(0x1800).rom()).\n")
        f.write("// Rest is zero and never legitimately fetched, but kept in-bounds.\n")
        f.write("const unsigned char alibaba_rom[] = {\n")
        for i in range(0, len(rom), 16):
            f.write("  " + ",".join(hex8(b) for b in rom[i:i + 16]) + ",\n")
        f.write("};\n")
    print("Written:", outfile, "(", size, "bytes )")


# ---------------------------------------------------------------------------
# Generic MAME-style planar gfx decoder: given the raw combined region bytes
# and a gfx_layout description (as literal python lists matching the MAME
# C structs verbatim), decode `total` characters into a list of 2D pixel
# arrays (pen 0-3). This mirrors MAME's generic tile decoder exactly, so we
# do not need to special-case each layout by hand.
# ---------------------------------------------------------------------------
def decode_layout(data, width, height, total, planes, planeoffset, xoffset, yoffset, charincrement):
    nbits = len(data) * 8
    chars = []
    for code in range(total):
        base_bit = code * charincrement
        img = [[0] * width for _ in range(height)]
        for y in range(height):
            for x in range(width):
                pen = 0
                for p in range(planes):
                    bitofs = base_bit + planeoffset[p] + yoffset[y] + xoffset[x]
                    if bitofs >= nbits:
                        bit = 0
                    else:
                        byte = data[bitofs // 8]
                        bit = (byte >> (7 - (bitofs % 8))) & 1
                    pen |= bit << p
                img[y][x] = pen
        chars.append(img)
    return chars


def rgn_frac(num, den, region_len_bits):
    return (region_len_bits * num) // den


# The MAME xoffset/yoffset arrays decode each character in its NATIVE ROM
# orientation, which is landscape (MAME applies a separate whole-screen 90 deg
# rotation pass at display time because the physical monitor is mounted
# rotated -- see GAME(...,ROT90,...) for alibaba/pacman). This project renders
# straight into a portrait framebuffer with no such separate rotation pass, so
# every character decoded from ROM must be pre-rotated here instead. Verified
# empirically against a systematic set of candidates (plain, transposed, both
# bit orders, both rotation directions): 90 deg CLOCKWISE is the one that
# reproduces a correct, non-mirrored, correctly-sequenced font (digits 0-9,
# punctuation, then A-Z, all upright and in order).
def rotate_cw(img):
    n = len(img)
    return [[img[n - 1 - x][y] for x in range(n)] for y in range(n)]


# same rotation, generalised to non-square images: an H x W image becomes a
# W x H image (used for the 24x16 "clock" graphic -> 16x24 after rotation).
def rotate_cw_rect(img):
    h = len(img)
    w = len(img[0])
    return [[img[h - 1 - i][j] for i in range(h)] for j in range(w)]


# ---------------------------------------------------------------------------
# Tiles (8x8, 2bpp) + sprites (16x16, 2bpp) from "gfx1" = 5e+5h+5f+5k
# (0x800 bytes each, concatenated in THIS order to match MAME's ROM_LOAD).
# tilelayout:   total=RGN_FRAC(1,2), planes=2, planeoffset={0,4},
#               xoffset={8*8+0..3, 0..3}, yoffset={0,8,...,56}, charincrement=16*8
# spritelayout: total=RGN_FRAC(1,2), planes=2, planeoffset={0,4},
#               xoffset={8*8,8*8+1,8*8+2,8*8+3,16*8+0..3,24*8+0..3,0..3},
#               yoffset={0,8,...,56, 32*8,33*8,...,39*8}, charincrement=64*8
# (verified against E:\Download\pacman.cpp lines 3598-3621)
# ---------------------------------------------------------------------------
def convert_tiles_sprites():
    gfx1 = read_rom("5e") + read_rom("5h") + read_rom("5f") + read_rom("5k")
    region_bits = len(gfx1) * 8  # 0x2000 bytes = 0x10000 bits

    # --- tiles: decoded from offset 0x0000 of gfx1, 256 chars ---
    # NOTE: planeoffset swapped from the literal MAME {0,4} to {4,0} -- with
    # the literal order, 2-tone graphics (e.g. money bag, player sprite) came
    # out with foreground/background colours swapped (confirmed on real HW:
    # "should be white bag/blue $, shows blue bag/white $", same inversion on
    # the player sprite). A plane swap flips pen1<->pen2 while leaving pen0
    # (transparent) and pen3 unchanged, which exactly matches "two colours
    # swapped, single-colour graphics like most of the font unaffected".
    tile_total = rgn_frac(1, 2, region_bits) // (16 * 8)
    tile_planeoffset = [4, 0]
    tile_xoffset = [8 * 8 + 0, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3, 0, 1, 2, 3]
    tile_yoffset = [i * 8 for i in range(8)]
    tiles = decode_layout(gfx1, 8, 8, tile_total, 2, tile_planeoffset,
                           tile_xoffset, tile_yoffset, 16 * 8)
    tiles = [rotate_cw(t) for t in tiles]

    outfile = os.path.join(OUT_DIR, "alibaba_tilemap.h")
    with open(outfile, "w") as f:
        f.write("// Ali Baba tiles: 8x8, 2bpp, packed as one uint16 per row (2 bits/pixel)\n")
        f.write("const unsigned short alibaba_tilemap[][8] = {\n")
        for img in tiles:
            row_words = []
            for y in range(8):
                word = 0
                for x in range(8):
                    word |= img[y][x] << (2 * x)
                row_words.append(hex16(word))
            f.write(" { " + ",".join(row_words) + " },\n")
        f.write("};\n")
    print("Written:", outfile, "(", len(tiles), "tiles )")

    # --- sprites: decoded from offset 0x1000 of gfx1, 64 sprites, 16x16 ---
    # NOTE: RGN_FRAC is relative to the FULL "gfx1" region (region_bits, same
    # as used for tiles above), NOT to the byte-sliced sub-region below -- the
    # GFXDECODE_ENTRY offset (0x1000) only shifts where character 0 starts.
    sprite_region = gfx1[0x1000:]
    sprite_total = rgn_frac(1, 2, region_bits) // (64 * 8)
    sprite_planeoffset = [4, 0]  # swapped, see note on tile_planeoffset above
    sprite_xoffset = [8 * 8, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3,
                       16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3,
                       24 * 8 + 0, 24 * 8 + 1, 24 * 8 + 2, 24 * 8 + 3,
                       0, 1, 2, 3]
    sprite_yoffset = [i * 8 for i in range(8)] + [32 * 8 + i * 8 for i in range(8)]
    sprites = decode_layout(sprite_region, 16, 16, sprite_total, 2, sprite_planeoffset,
                             sprite_xoffset, sprite_yoffset, 64 * 8)

    # precompute the 4 flip orientations (flags bit0=flipx, bit1=flipy), matching
    # how blit_sprite in pacman.cpp consumes pacman_sprites[flags][code][row].
    # IMPORTANT: flip MUST be applied in the ORIGINAL (pre-rotation) hardware
    # orientation, THEN rotated -- not the other way round. Rotating first and
    # flipping the already-rotated image swaps which screen axis each flip bit
    # affects (confirmed on real HW: moving right drew the sprite upside down,
    # i.e. flipY was being triggered by what hardware intended as flipX).
    def flipped(img, flipx, flipy):
        h = len(img)
        w = len(img[0])
        out = [[0] * w for _ in range(h)]
        for y in range(h):
            sy = h - 1 - y if flipy else y
            for x in range(w):
                sx = w - 1 - x if flipx else x
                out[y][x] = img[sy][sx]
        return out

    outfile = os.path.join(OUT_DIR, "alibaba_spritemap.h")
    with open(outfile, "w") as f:
        f.write("// Ali Baba sprites: 16x16, 2bpp, packed as one uint32 per row, 4 flip orientations\n")
        f.write("const unsigned long alibaba_sprites[][64][16] = {\n")
        for flags in range(4):
            flipx = flags & 1
            flipy = (flags >> 1) & 1
            f.write(" {\n")
            for img in sprites:
                fimg = rotate_cw(flipped(img, flipx, flipy))
                row_words = []
                for y in range(16):
                    word = 0
                    for x in range(16):
                        word |= fimg[y][x] << (2 * x)
                    row_words.append(hex32(word))
                f.write("  { " + ",".join(row_words) + " },\n")
            f.write(" },\n")
        f.write("};\n")
    print("Written:", outfile, "(", len(sprites), "sprites x 4 flips )")


# ---------------------------------------------------------------------------
# "Mystery" clock graphic (gfx2 = ab7.bin, reloaded to fill 0x1000 bytes).
# alibaba_clocklayout: 24x16, 2bpp, total=RGN_FRAC(1,2), planeoffset =
#   {RGN_FRAC(1,2), RGN_FRAC(0,2)} i.e. plane0 from the SECOND half of the
#   (already reloaded) region, plane1 from the FIRST half -- since ROM_RELOAD
#   makes both halves byte-identical, both planes always carry the same bit,
#   so every pixel pen is 0 or 3 (2-tone graphic).
# xoffset spans 24 columns split 8/8/8 across three byte-groups (16*8,8*8,0),
# yoffset covers 16 rows in two groups of 8 (256+, 0+), charincrement=32*16.
# (verified against E:\Download\pacman.cpp lines 3624-3640)
# ---------------------------------------------------------------------------
def convert_clock():
    half = read_rom("ab7.bin")  # 0x800, MAME reloads this to fill 0x800-0xfff too
    gfx2 = half + half
    region_bits = len(gfx2) * 8

    total = rgn_frac(1, 2, region_bits) // (32 * 16)
    planeoffset = [rgn_frac(1, 2, region_bits), rgn_frac(0, 2, region_bits)]
    xoffset = [16 * 8 + 7, 16 * 8 + 6, 16 * 8 + 5, 16 * 8 + 4, 16 * 8 + 3, 16 * 8 + 2, 16 * 8 + 1, 16 * 8 + 0,
               8 * 8 + 7, 8 * 8 + 6, 8 * 8 + 5, 8 * 8 + 4, 8 * 8 + 3, 8 * 8 + 2, 8 * 8 + 1, 8 * 8 + 0,
               0 * 8 + 7, 0 * 8 + 6, 0 * 8 + 5, 0 * 8 + 4, 0 * 8 + 3, 0 * 8 + 2, 0 * 8 + 1, 0 * 8 + 0]
    yoffset = [256 + 0 * 8, 256 + 1 * 8, 256 + 2 * 8, 256 + 3 * 8, 256 + 4 * 8, 256 + 5 * 8, 256 + 6 * 8, 256 + 7 * 8,
               0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8]

    clocks = decode_layout(gfx2, 24, 16, total, 2, planeoffset, xoffset, yoffset, 32 * 16)
    # same physical-hardware rotation as tiles/sprites (see rotate_cw comment);
    # a 24-wide x 16-tall source becomes 16-wide x 24-tall after rotation.
    clocks = [rotate_cw_rect(c) for c in clocks]

    outfile = os.path.join(OUT_DIR, "alibaba_clockmap.h")
    with open(outfile, "w") as f:
        f.write("// Ali Baba 'mystery' clock graphic: 16x24 (rotated from source 24x16,\n")
        f.write("// same hardware quirk as tiles/sprites), 2bpp (2-tone: plane0==plane1\n")
        f.write("// always, since the source ROM is byte-identical in both halves).\n")
        f.write("// Packed as one uint32 per row (16 pixels x 2 bits fits in 32 bits).\n")
        f.write("const unsigned long alibaba_clockmap[][24] = {\n")
        for img in clocks:
            row_words = []
            for y in range(24):
                word = 0
                for x in range(16):
                    word |= img[y][x] << (2 * x)
                row_words.append(hex32(word))
            f.write(" { " + ",".join(row_words) + " },\n")
        f.write("};\n")
    print("Written:", outfile, "(", len(clocks), "clock tiles )")


# ---------------------------------------------------------------------------
# Palette: standard Pac-Man-family weighted-resistor RGB decode.
#   82s123.e7 (32 bytes) = master RGB palette, 3 bits R / 3 bits G / 2 bits B
#   82s129.a4 (256 bytes) = color lookup: index = colorcode*4 + pen, low
#     nibble selects one of the 32 master colors.
# Same algorithm as MAME's pacman_state::palette() (standard for this
# hardware family, weights 0x21/0x47/0x97).
# ---------------------------------------------------------------------------
def convert_colors():
    master = read_rom("82s123.e7")
    lookup = read_rom("82s129.a4")

    def chan3(byte, b0, b1, b2):
        v0 = (byte >> b0) & 1
        v1 = (byte >> b1) & 1
        v2 = (byte >> b2) & 1
        return min(255, v0 * 0x21 + v1 * 0x47 + v2 * 0x97)

    palette = []
    for byte in master:
        r = chan3(byte, 0, 1, 2)
        g = chan3(byte, 3, 4, 5)
        b = chan3(byte, 0, 6, 7)  # bit0 unused for blue (2 bits only), matches pacman convention
        b = min(255, ((byte >> 6) & 1) * 0x47 + ((byte >> 7) & 1) * 0x97)
        r5 = (r >> 3) & 0x1f
        g6 = (g >> 2) & 0x3f
        b5 = (b >> 3) & 0x1f
        rgb565 = (r5 << 11) | (g6 << 5) | b5
        # byte-swap for ESP32 SPI, same convention as other converters in this project
        swapped = ((rgb565 & 0xff) << 8) | ((rgb565 >> 8) & 0xff)
        palette.append(swapped)

    outfile = os.path.join(OUT_DIR, "alibaba_cmap.h")
    with open(outfile, "w") as f:
        f.write("// Ali Baba colormap: 64 color codes x 4 pens, RGB565 (byte-swapped)\n")
        f.write("const unsigned short alibaba_colormap[][4] = {\n")
        for code in range(64):
            pens = []
            for pen in range(4):
                idx = lookup[code * 4 + pen] & 0x1f
                pens.append(hex16(palette[idx]))
            f.write("{" + ",".join(pens) + "},\n")
        f.write("};\n")
    print("Written:", outfile, "(64 color codes)")


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    convert_rom()
    convert_tiles_sprites()
    convert_clock()
    convert_colors()


if __name__ == "__main__":
    main()
