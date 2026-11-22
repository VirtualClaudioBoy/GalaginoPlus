#!/usr/bin/env python3
# ============================================================
# Super Pac-Man (Namco 1982) ROM converter
#
# Stand-alone, in the style of mappy_rom_convert.py: reads ../roms/,
# writes ../../source/src/machines/superpac/*.h
#
# The board is the first of the family Mappy belongs to, so the decoding
# is Mappy's with this board's differences (MAME mappy.cpp / mappy_v.cpp):
# - tiles 4KB, charlayout as Pac-Man's, NOT inverted
# - sprites 8KB, 16x16 2bpp (spritelayout_2bpp), 128 of them
# - palette 32B bbgggrrr; character lookup: pen = (lookup ^ 15) + 0x10,
#   sprite lookup: pen = lookup; a sprite pixel is transparent where its
#   lookup is 0xF, a priority tile's where its lookup is 0
# - wave PROM 256B = 8 waveforms x 32 samples of 4 bits
#
# The chips are taken by the names of either release (Namco's sp1-*, or
# Midway's spc-*/spv-*), Namco's first. A game somebody wrote for this board
# comes as a set with one release's names and other contents: nothing here
# checks what is in the chips, only their sizes. What is in them is noted
# (superpac_info) so the machine keeps Super Pac-Man's high scores only for
# Super Pac-Man.
# ============================================================

import hashlib, os, sys

ROMS = "../roms/"
OUT = "../../source/src/machines/superpac/"

# Each chip: the names it goes by (Namco's, then Midway's) and its size.
# sha1 from MAME ROM_START(superpac) and ROM_START(superpacm).
MAIN_1C = [("sp1-2.1c", 0x2000, "dd87f71b4db090a32a6b791079eedd17580cc741"),
           ("spc-2.1c", 0x2000, "ae0ee9f3df0991a80698fe745a7a853a4bb60710")]
MAIN_1B = [("sp1-1.1b", 0x2000, "f6bf90281986b9b7a3ef1dbbeddb722182e84d7c"),
           ("spc-1.1b", 0x2000, "ca73c8bcb03c2f5c05968c707a5d3f7f9956b886")]
SUB_1K  = [("spc-3.1k", 0x1000, "ce7d14963d5ddaefdeaf433a6f82c43cd1611d9b")]
TILES   = [("sp1-6.3c", 0x1000, "10579edabc26a0910253fab7d41b4c19ecdaaa09"),
           ("spv-1.3c", 0x1000, "11222adb55e6bce508896ccb1f6dbab0c1d44e5b")]
SPRITES = [("spv-2.3f", 0x2000, "9171922df07e31fd1dc415766f7d2cc50a9d10dc")]
PALETTE = [("superpac.4c", 0x0020, "d97f53ef4c5ef26659a22ed0de4ce7ef3758c924")]
CHARLUT = [("superpac.4e", 0x0100, "df46a90170e9761d45c90fbd04ef2aa1e8c9944b")]
SPRLUT  = [("superpac.3l", 0x0100, "a486573437c54bfb503424574ad82655491e85e1")]
WAVES   = [("superpac.3m", 0x0100, "072f427453efb1dda8147da61804fff06e1bc4d5")]

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

# spritelayout_2bpp: 16x16x2bpp, 64 bytes a sprite
SPR_PLANES = [0, 4]
SPR_XOFFS = [0,1,2,3, 64,65,66,67, 128,129,130,131, 192,193,194,195]
SPR_YOFFS = [y*8 for y in range(8)] + [256 + y*8 for y in range(8)]

# ------------------------------------------------------------
# headers
# ------------------------------------------------------------
def write_tiles(tiles):
    with open(OUT + "superpac_tilemap.h", "w") as f:
        print("// Super Pac-Man tiles: 256 tiles 8x8 2bpp, pixels LSB-first (blit: (pix>>2c)&3)", file=f)
        print("const unsigned short superpac_tilemap[][8] = {", file=f)
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
    with open(OUT + "superpac_spritemap.h", "w") as f:
        print("// Super Pac-Man sprites: 128 sprites 16x16 2bpp", file=f)
        print("// [flip variant][code][row]: 16 pixels of 2 bits, LSB-first", file=f)
        print("const unsigned long superpac_sprites[][128][16] = {", file=f)
        for (fx, fy) in [(0,0),(0,1),(1,0),(1,1)]:
            print(" {", file=f)
            rows = []
            for s in sprites:
                t = flip_tile(s, fx, fy)
                vals = []
                for y in range(16):
                    v = 0
                    for x in range(16):
                        v |= t[y][x] << (2*x)
                    vals.append(hex(v))
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
    with open(OUT + "superpac_cmap.h", "w") as f:
        print("// Super Pac-Man colours, from the character and sprite lookup PROMs", file=f)
        print("//  - tiles: the colour (first pass, the tilemap is opaque)", file=f)
        print("//  - tiles_prio: 0 where the lookup is 0 (tiles drawn again over the sprites, attribute bit 6)", file=f)
        print("//  - sprites: 0 where the lookup is 0xF (not drawn)", file=f)
        print("//  - sprites_super: the colour where the lookup is 0 or 1, else 0 (those two pens stay over priority tiles)", file=f)
        chars = [[(char_lut[g*4+p] & 0x0f) for p in range(4)] for g in range(64)]
        sprs = [[(spr_lut[g*4+p] & 0x0f) for p in range(4)] for g in range(64)]
        table(f, "superpac_colormap_tiles", 4, [[nudge(pal[16 + (l ^ 15)]) for l in r] for r in chars])
        table(f, "superpac_colormap_tiles_prio", 4, [[0 if l == 0 else nudge(pal[16 + (l ^ 15)]) for l in r] for r in chars])
        table(f, "superpac_colormap_sprites", 4, [[0 if l == 0x0f else nudge(pal[l]) for l in r] for r in sprs])
        table(f, "superpac_colormap_sprites_super", 4, [[nudge(pal[l]) if l < 2 else 0 for l in r] for r in sprs])

def write_rom(name, sym, data, comment):
    with open(OUT + name, "w") as f:
        print(f"// {comment}", file=f)
        print(f"const unsigned char {sym}[] = {{", file=f)
        for i in range(0, len(data), 16):
            print("  " + ",".join(f"0x{b:02X}" for b in data[i:i+16]) + ",", file=f)
        print("};", file=f)

def write_wavetable(prom):
    # 256 bytes, low 4 bits = 8 waveforms x 32 samples, centred (-8..7)
    with open(OUT + "superpac_wavetable.h", "w") as f:
        print("// Super Pac-Man WSG 15XX waveforms: 8 waveforms x 32 samples", file=f)
        print("const signed char superpac_wavetable[][32] = {", file=f)
        rows = []
        for w in range(8):
            vals = [str((prom[w*32+i] & 0x0f) - 8) for i in range(32)]
            rows.append(" { " + ",".join(vals) + " }")
        print(",\n".join(rows), file=f)
        print("};", file=f)

def write_info(variant):
    with open(OUT + "superpac_info.h", "w") as f:
        print("// What the program chips are: 1 = Super Pac-Man (Namco), 2 = Super Pac-Man (Midway), 0 = something else", file=f)
        print("const unsigned char superpac_info[] = { %d, 0, 0, 0 };" % variant, file=f)

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
    img = Image.new("RGB", (16*18, 16*9 + 8*18 + 8), (32, 32, 32))
    px = img.load()
    for t in range(256):
        gx, gy = (t % 16) * 9, (t // 16) * 9
        for y in range(8):
            for x in range(8):
                lut = char_lut[1*4 + tiles[t][y][x]] & 0x0f
                px[gx + x, gy + y] = pal[16 + (lut ^ 15)]
    base = 16*9 + 8
    for s in range(128):
        gx, gy = (s % 16) * 18, base + (s // 16) * 18
        for y in range(16):
            for x in range(16):
                lut = spr_lut[1*4 + sprites[s][y][x]] & 0x0f
                px[gx + x, gy + y] = (0,0,0) if lut == 0x0f else pal[lut]
    img = img.resize((img.width*3, img.height*3), Image.NEAREST)
    img.save(outpng)
    print("preview:", outpng)

# ------------------------------------------------------------
def main():
    c1, v1 = load_chip(MAIN_1C)
    b1, v2 = load_chip(MAIN_1B)
    sub, _ = load_chip(SUB_1K)
    tdata, _ = load_chip(TILES)
    sdata, _ = load_chip(SPRITES)
    pal, _ = load_chip(PALETTE)
    clut, _ = load_chip(CHARLUT)
    slut, _ = load_chip(SPRLUT)
    waves, _ = load_chip(WAVES)

    os.makedirs(OUT, exist_ok=True)

    tiles = [rot_galagino(t) for t in
             mame_decode(tdata, 8, 8, CHAR_PLANES, CHAR_XOFFS, CHAR_YOFFS, 128, 256)]
    write_tiles(tiles)

    sprites = [rot_galagino(s) for s in
               mame_decode(sdata, 16, 16, SPR_PLANES, SPR_XOFFS, SPR_YOFFS, 512, 128)]
    write_sprites(sprites)

    write_colormaps(pal, clut, slut)
    write_wavetable(waves)

    # The board has a third program socket (1D, 0xA000) that Super Pac-Man leaves empty.
    main_rom = bytes([0xFF]) * 0x2000 + c1 + b1
    write_rom("superpac_rom_main.h", "superpac_rom_main", main_rom,
              "Super Pac-Man main M6809 ROM 0xA000-0xFFFF (1D empty, 1C, 1B)")
    write_rom("superpac_rom_sub.h", "superpac_rom_sub", bytes([0xFF]) * 0x1000 + sub,
              "Super Pac-Man sound M6809 ROM 0xE000-0xFFFF (1K at 0xF000)")
    write_info(v1 + 1 if v1 == v2 and v1 >= 0 else 0)

    preview(tiles, sprites, pal, clut, slut, "superpac_preview.png")
    print("Super Pac-Man conversion done.")

if __name__ == "__main__":
    main()
