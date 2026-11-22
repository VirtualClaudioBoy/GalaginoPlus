#!/usr/bin/env python3
# ============================================================
# Motos (Namco 1985) ROM converter
#
# Stand-alone, in the style of mappy_rom_convert.py: reads ../roms/,
# writes ../../source/src/machines/motos/*.h
#
# Motos is Mappy's video board (MAME mappy.cpp / mappy_v.cpp, gfx_mappy and
# mappy_palette) with twice the sprites and a bigger program:
# - tiles 4KB, charlayout as Pac-Man's, the ROM INVERTED
# - sprites 2 x 16KB interleaved as 16 bits, 16x16 4bpp (spritelayout_4bpp),
#   256 of them
# - palette 32B bbgggrrr; character lookup: pen = lookup + 0x10, sprite
#   lookup: pen = lookup; a sprite pixel is transparent where its lookup is
#   0xF, and so is a priority tile's
# - wave PROM 256B = 8 waveforms x 32 samples of 4 bits
#
# Nothing here checks what is in the chips, only their sizes: a game somebody
# wrote for this board comes as a set with Motos's chip names and other
# contents. What is in the program chips is noted (motos_info) so the machine
# keeps Motos's high scores only for Motos.
# ============================================================

import hashlib, os, sys

ROMS = "../roms/"
OUT = "../../source/src/machines/motos/"

# Each chip: its name and size, sha1 from MAME ROM_START(motos).
MAIN_1D = [("mo1_3.1d", 0x4000, "ade809a73ac24494b9f95f65b7592df5f86dce60")]
MAIN_1B = [("mo1_1.1b", 0x4000, "b050495fcc4a4d93551b29d4f05e49f64017c870")]
SUB_1K  = [("mo1_4.1k", 0x2000, "a8b195acfec542734751de29c9dafc2b165a5881")]
TILES   = [("mo1_5.3b", 0x1000, "4af9bf2ae9bb78d2d029ef38809181ffa3c0eb66")]
SPR_3M  = [("mo1_6.3m", 0x4000, "664679f9d3d74a3fccb086af910392b4fe40c9bc")]
SPR_3N  = [("mo1_7.3n", 0x4000, "2b49cdec516e23783f2a291633d81ab8bd0245fc")]
PALETTE = [("mo1-5.5b", 0x0020, "66b0619affcc5168b099108800a941d6e2416ab0")]
CHARLUT = [("mo1-6.4c", 0x0100, "24eb167266752b064689662e3ef0f62d0407ac26")]
SPRLUT  = [("mo1-7.5k", 0x0100, "543adb5348db81ea82a5c039451001ebd82735e3")]
WAVES   = [("mo1-3.3m", 0x0100, "e21a0618c0f8e35ce26666b6850ac9c0d95d7971")]

def load_chip(names):
    """(data, index of the release it matches or -1). The first name present wins."""
    for name, size, _ in names:
        path = os.path.join(ROMS, name)
        if not os.path.exists(path):
            continue
        with open(path, "rb") as f:
            data = f.read()
        if len(data) != size:
            raise ValueError(f"{name}: size {len(data)} != {size}")
        h = hashlib.sha1(data).hexdigest()
        for i, (_, _, sha1) in enumerate(names):
            if h == sha1:
                return data, i
        print(f"note: {name} is not the chip MAME knows by that name (a game of your own?)")
        return data, -1
    raise FileNotFoundError("none of " + ", ".join(n for n, _, _ in names) + " in " + ROMS)

# ------------------------------------------------------------
# generic MAME-style gfx decoder (planes[0] = most significant bit)
# ------------------------------------------------------------
def mame_decode(data, width, height, planes, xoffs, yoffs, bits_per_tile, count):
    tiles = []
    for t in range(count):
        base = t * bits_per_tile
        tile = []
        for y in range(height):
            row = []
            for x in range(width):
                v = 0
                for p in planes:
                    off = base + yoffs[y] + xoffs[x] + p
                    bit = (data[off >> 3] >> (7 - (off & 7))) & 1
                    v = (v << 1) | bit
                row.append(v)
            tile.append(row)
        tiles.append(tile)
    return tiles

# galagino's rotation (portrait): out[y][x] = mame[N-1-x][y]
def rot_galagino(tile):
    n = len(tile)
    return [[tile[n - 1 - x][y] for x in range(n)] for y in range(n)]

def flip_tile(tile, fx, fy):
    out = tile
    if fy: out = list(reversed(out))
    if fx: out = [list(reversed(r)) for r in out]
    return out

# charlayout: 8x8x2bpp, 16 bytes a character
CHAR_PLANES = [0, 4]
CHAR_XOFFS = [64, 65, 66, 67, 0, 1, 2, 3]
CHAR_YOFFS = [y * 8 for y in range(8)]

# spritelayout_4bpp: 16x16x4bpp, 128 bytes a sprite (the two ROMs interleaved)
SPR_PLANES = [0, 4, 8, 12]
SPR_XOFFS = [0,1,2,3, 128,129,130,131, 256,257,258,259, 384,385,386,387]
SPR_YOFFS = [y*16 for y in range(8)] + [512 + y*16 for y in range(8)]

# ------------------------------------------------------------
# headers
# ------------------------------------------------------------
def write_tiles(tiles):
    with open(OUT + "motos_tilemap.h", "w") as f:
        print("// Motos tiles: 256 tiles 8x8 2bpp, pixels LSB-first (blit: (pix>>2c)&3)", file=f)
        print("const unsigned short motos_tilemap[][8] = {", file=f)
        rows = []
        for t in tiles:
            vals = []
            for y in range(8):
                v = 0
                for x in range(8):
                    v |= t[y][x] << (2*x)
                vals.append(hex(v))
            rows.append(" { " + ",".join(vals) + " }")
        print(",\n".join(rows), file=f)
        print("};", file=f)

def write_sprites(sprites):
    # variants as galaga's spriteconv: [0]=(fx0,fy0) [1]=(fx0,fy1) [2]=(fx1,fy0) [3]=(fx1,fy1)
    with open(OUT + "motos_spritemap.h", "w") as f:
        print("// Motos sprites: 256 sprites 16x16 4bpp", file=f)
        print("// [flip variant][code][row*2+half]: nibbles LSB-first, [2r] = pixels 0-7, [2r+1] = pixels 8-15", file=f)
        print("const unsigned long motos_sprites[][256][32] = {", file=f)
        for (fx, fy) in [(0,0),(0,1),(1,0),(1,1)]:
            print(" {", file=f)
            rows = []
            for s in sprites:
                t = flip_tile(s, fx, fy)
                vals = []
                for y in range(16):
                    v = 0
                    for x in range(16):
                        v |= t[y][x] << (4*x)
                    vals.append(hex(v & 0xffffffff))
                    vals.append(hex(v >> 32))
                rows.append("  { " + ",".join(vals) + " }")
            print(",\n".join(rows), file=f)
            print(" }," if not (fx and fy) else " }", file=f)
        print("};", file=f)

def rgb565_swapped(c):
    # bbgggrrr -> RGB565 byte-swapped, as cmapconv.py does for galaga/pacman
    b = 31*((c>>6) & 0x3)//3
    g = 63*((c>>3) & 0x7)//7
    r = 31*((c>>0) & 0x7)//7
    rgb = (r << 11) + (g << 5) + b
    return ((rgb & 0xff00) >> 8) + ((rgb & 0xff) << 8)

def write_colormaps(pal_prom, char_lut, spr_lut):
    pal = [rgb565_swapped(c) for c in pal_prom]  # 32 colours
    # 0x0000 means "not drawn" in the blits: true blacks become nearly black
    def nudge(v):
        return v if v != 0 else 0x2000
    def table(f, name, width, rows):
        print(f"const unsigned short {name}[][{width}] = {{", file=f)
        print(",\n".join("{" + ",".join(hex(v) for v in r) + "}" for r in rows), file=f)
        print("};", file=f)
    with open(OUT + "motos_cmap.h", "w") as f:
        print("// Motos colours, from the character and sprite lookup PROMs", file=f)
        print("//  - tiles: the colour (first pass, the tilemap is opaque)", file=f)
        print("//  - tiles_prio: 0 where the lookup is 0xF (tiles drawn again over the sprites, attribute bit 6)", file=f)
        print("//  - sprites: 0 where the lookup is 0xF (not drawn)", file=f)
        chars = [[(char_lut[g*4+p] & 0x0f) for p in range(4)] for g in range(64)]
        sprs = [[(spr_lut[g*16+p] & 0x0f) for p in range(16)] for g in range(16)]
        table(f, "motos_colormap_tiles", 4, [[nudge(pal[16 + l]) for l in r] for r in chars])
        table(f, "motos_colormap_tiles_prio", 4, [[0 if l == 0x0f else nudge(pal[16 + l]) for l in r] for r in chars])
        table(f, "motos_colormap_sprites", 16, [[0 if l == 0x0f else nudge(pal[l]) for l in r] for r in sprs])

def write_rom(name, sym, data, comment):
    with open(OUT + name, "w") as f:
        print(f"// {comment}", file=f)
        print(f"const unsigned char {sym}[] = {{", file=f)
        for i in range(0, len(data), 16):
            print("  " + ",".join(f"0x{b:02X}" for b in data[i:i+16]) + ",", file=f)
        print("};", file=f)

def write_wavetable(prom):
    # 256 bytes, low 4 bits = 8 waveforms x 32 samples, centred (-8..7)
    with open(OUT + "motos_wavetable.h", "w") as f:
        print("// Motos WSG 15XX waveforms: 8 waveforms x 32 samples", file=f)
        print("const signed char motos_wavetable[][32] = {", file=f)
        rows = []
        for w in range(8):
            vals = [str((prom[w*32+i] & 0x0f) - 8) for i in range(32)]
            rows.append(" { " + ",".join(vals) + " }")
        print(",\n".join(rows), file=f)
        print("};", file=f)

def write_info(variant):
    with open(OUT + "motos_info.h", "w") as f:
        print("// What the program chips are: 1 = Motos, 0 = something else", file=f)
        print("const unsigned char motos_info[] = { %d, 0, 0, 0 };" % variant, file=f)

# ------------------------------------------------------------
# preview PNG (to check orientation and decoding by eye)
# ------------------------------------------------------------
def preview(tiles, sprites, pal_prom, char_lut, spr_lut, outpng):
    try:
        from PIL import Image
    except ImportError:
        return
    def pal_rgb(c):
        return (255*((c>>0)&7)//7, 255*((c>>3)&7)//7, 255*((c>>6)&3)//3)
    pal = [pal_rgb(c) for c in pal_prom]
    img = Image.new("RGB", (16*18, 16*9 + 16*18 + 8), (32, 32, 32))
    px = img.load()
    for t in range(256):
        gx, gy = (t % 16) * 9, (t // 16) * 9
        for y in range(8):
            for x in range(8):
                lut = char_lut[1*4 + tiles[t][y][x]] & 0x0f
                px[gx + x, gy + y] = pal[16 + lut]
    base = 16*9 + 8
    for s in range(256):
        gx, gy = (s % 16) * 18, base + (s // 16) * 18
        for y in range(16):
            for x in range(16):
                lut = spr_lut[1*16 + sprites[s][y][x]] & 0x0f
                px[gx + x, gy + y] = (0,0,0) if lut == 0x0f else pal[lut]
    img = img.resize((img.width*3, img.height*3), Image.NEAREST)
    img.save(outpng)
    print("preview:", outpng)

# ------------------------------------------------------------
def main():
    d1, v1 = load_chip(MAIN_1D)
    b1, v2 = load_chip(MAIN_1B)
    sub, _ = load_chip(SUB_1K)
    tdata, _ = load_chip(TILES)
    s6, _ = load_chip(SPR_3M)
    s7, _ = load_chip(SPR_3N)
    pal, _ = load_chip(PALETTE)
    clut, _ = load_chip(CHARLUT)
    slut, _ = load_chip(SPRLUT)
    waves, _ = load_chip(WAVES)

    os.makedirs(OUT, exist_ok=True)

    tdata = bytes(b ^ 0xFF for b in tdata)      # ROMREGION_INVERT
    tiles = [rot_galagino(t) for t in
             mame_decode(tdata, 8, 8, CHAR_PLANES, CHAR_XOFFS, CHAR_YOFFS, 128, 256)]
    write_tiles(tiles)

    sdata = bytearray(0x8000)                   # 3M = even bytes, 3N = odd
    sdata[0::2] = s6
    sdata[1::2] = s7
    sprites = [rot_galagino(s) for s in
               mame_decode(bytes(sdata), 16, 16, SPR_PLANES, SPR_XOFFS, SPR_YOFFS, 1024, 256)]
    write_sprites(sprites)

    write_colormaps(pal, clut, slut)
    write_wavetable(waves)

    write_rom("motos_rom_main.h", "motos_rom_main", d1 + b1,
              "Motos main M6809 ROM 0x8000-0xFFFF (1D, 1B)")
    write_rom("motos_rom_sub.h", "motos_rom_sub", sub,
              "Motos sound M6809 ROM 0xE000-0xFFFF (1K)")
    write_info(1 if v1 == 0 and v2 == 0 else 0)

    preview(tiles, sprites, pal, clut, slut, "motos_preview.png")
    print("Motos conversion done.")

if __name__ == "__main__":
    main()
