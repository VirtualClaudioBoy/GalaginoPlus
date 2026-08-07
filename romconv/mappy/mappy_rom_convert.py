#!/usr/bin/env python3
# ============================================================
# Mappy (Namco 1983) ROM converter per galagino29-main
#
# Autonomo (stile gyruss_rom_convert.py): legge ../roms/, scrive
# ../../source/src/machines/mappy/*.h
#
# Decoder gfx generico basato sui gfx_layout MAME + trasformazione
# ROT90 galagino: out[y][x] = mame[N-1-x][y] (stessa rotazione che
# tileconv.parse_chr / spriteconv.parse_sprite applicano a pacman/
# galaga — AUTOTEST incluso contro le ROM di galaga per provarlo).
#
# Riferimenti MAME: mappy.cpp / mappy_v.cpp (forniti dall'utente):
# - tiles mp1_5.3b 4KB ROMREGION_INVERT, charlayout pacman-style
# - sprites mp1_6.3m + mp1_7.3n interallacciate 16bit, 16x16 4bpp
# - palette mp1-5.5b 32B bbgggrrr; lookup char mp1-6.4c (pen+0x10),
#   lookup sprite mp1-7.5k; trasparenza = nibble lookup 0xF
# - wave WSG mp1-3.3m 256B = 8 forme x 32 campioni 4 bit
# ============================================================

import hashlib, os, sys

ROMS = "../roms/"
OUT = "../../source/src/machines/mappy/"

# file: (nome, dimensione, sha1 da MAME ROM_START(mappy))
REQUIRED = [
    ("mpx_3.1d", 0x2000, "b9722941438e93325e84691ada4e95620bec73b2"),
    ("mp1_2.1c", 0x2000, "e5198703cdf47b2cd7fc9f2a5fde7bf4ab2275db"),
    ("mpx_1.1b", 0x2000, "1dbc4f42d4c16a08240a221bec27dcc3a8dd7461"),
    ("mp1_4.1k", 0x2000, "f36b57f7f1e79f00b3f07afe1960bca5f5325ee2"),
    ("mp1_5.3b", 0x1000, "76610149c65f955484fef1c033ddc3fed3f4e568"),
    ("mp1_6.3m", 0x2000, "3cc216793c6a5f73c437ad2524563deb3b5e2890"),
    ("mp1_7.3n", 0x2000, "8dfbf03953d5219d9eb5fc654ec3392442ba1dc4"),
    ("mp1-5.5b", 0x0020, "2e356706c07f43eeb67783fb122bdc7fed1b3589"),
    ("mp1-6.4c", 0x0100, "f578e14f15783acb2073644db4a2f0d196cc0957"),
    ("mp1-7.5k", 0x0100, "2e387e5d8b8cab005f67f821b4db65d0ae8bd362"),
    ("mp1-3.3m", 0x0100, "847cbaf7c88616576c410177e066ae1d792ac0ba"),
]

def load_rom(name, size, sha1):
    path = os.path.join(ROMS, name)
    with open(path, "rb") as f:
        data = f.read()
    if len(data) != size:
        raise ValueError(f"{name}: dimensione {len(data)} != {size}")
    h = hashlib.sha1(data).hexdigest()
    if h != sha1:
        print(f"ATTENZIONE: {name} sha1 {h} != atteso {sha1} (set diverso?)")
    return data

# ------------------------------------------------------------
# decoder gfx generico stile MAME (planes[0] = bit PIU' significativo)
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

# rotazione galagino (portrait): out[y][x] = mame[N-1-x][y]
def rot_galagino(tile):
    n = len(tile)
    return [[tile[n - 1 - x][y] for x in range(n)] for y in range(n)]

def flip_tile(tile, fx, fy):
    out = tile
    if fy: out = list(reversed(out))
    if fx: out = [list(reversed(r)) for r in out]
    return out

# ------------------------------------------------------------
# layout MAME
# ------------------------------------------------------------
# charlayout pacman/galaga/mappy: 8x8x2bpp, 16 byte/char
CHAR_PLANES = [0, 4]
CHAR_XOFFS = [64, 65, 66, 67, 0, 1, 2, 3]
CHAR_YOFFS = [y * 8 for y in range(8)]

# galaga spritelayout (per autotest): 16x16x2bpp, 64 byte/sprite
GSPR_PLANES = [0, 4]
GSPR_XOFFS = [0,1,2,3, 64,65,66,67, 128,129,130,131, 192,193,194,195]
GSPR_YOFFS = [y*8 for y in range(8)] + [256 + y*8 for y in range(8)]

# mappy spritelayout_4bpp: 16x16x4bpp, 128 byte/sprite (stream interallacciato)
MSPR_PLANES = [0, 4, 8, 12]
MSPR_XOFFS = [0,1,2,3, 128,129,130,131, 256,257,258,259, 384,385,386,387]
MSPR_YOFFS = [y*16 for y in range(8)] + [512 + y*16 for y in range(8)]

# ------------------------------------------------------------
# AUTOTEST: il decoder generico + rot_galagino deve riprodurre
# ESATTAMENTE parse_chr/parse_sprite (copiati da tileconv/spriteconv)
# sulle ROM di galaga gia' validate su HW
# ------------------------------------------------------------
def parse_chr_ref(data):
    char = []
    for y in range(8):
        row = []
        for x in range(8):
            byte = data[15 - x - 2*(y&4)]
            c0 = 1 if byte & (0x08 >> (y&3)) else 0
            c1 = 2 if byte & (0x80 >> (y&3)) else 0
            row.append(c0+c1)
        char.append(row)
    return char

def parse_sprite_ref(data):
    sprite = []
    for y in range(16):
        row = []
        for x in range(16):
            idx = ((y&8)<<1) + (((x&8)^8)<<2) + (7-(x&7)) + 2*(y&4)
            c0 = 1 if data[idx] & (0x08 >> (y&3)) else 0
            c1 = 2 if data[idx] & (0x80 >> (y&3)) else 0
            row.append(c0+c1)
        sprite.append(row)
    return sprite

def selftest():
    with open(os.path.join(ROMS, "gg1_9.4l"), "rb") as f:
        cdata = f.read()
    dec = mame_decode(cdata, 8, 8, CHAR_PLANES, CHAR_XOFFS, CHAR_YOFFS, 128, 256)
    for t in range(256):
        ref = parse_chr_ref(cdata[16*t:16*(t+1)])
        got = rot_galagino(dec[t])
        if got != ref:
            raise AssertionError(f"AUTOTEST char {t} FALLITO")
    with open(os.path.join(ROMS, "gg1_11.4d"), "rb") as f:
        sdata = f.read()
    dec = mame_decode(sdata, 16, 16, GSPR_PLANES, GSPR_XOFFS, GSPR_YOFFS, 512, 64)
    for t in range(64):
        ref = parse_sprite_ref(sdata[64*t:64*(t+1)])
        got = rot_galagino(dec[t])
        if got != ref:
            raise AssertionError(f"AUTOTEST sprite {t} FALLITO")
    print("Autotest decoder vs tileconv/spriteconv (ROM galaga): OK")

# ------------------------------------------------------------
# scritture header
# ------------------------------------------------------------
def write_tiles(tiles):
    with open(OUT + "mappy_tilemap.h", "w") as f:
        print("// Mappy tiles (mp1_5.3b, ROMREGION_INVERT) — 256 tile 8x8 2bpp", file=f)
        print("// pixel LSB-first come galaga_tilemap (blit: (pix>>2c)&3)", file=f)
        print("const unsigned short mappy_tilemap[][8] = {", file=f)
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
    # varianti come spriteconv galaga: [0]=(fx0,fy0) [1]=(fx0,fy1) [2]=(fx1,fy0) [3]=(fx1,fy1)
    with open(OUT + "mappy_spritemap.h", "w") as f:
        print("// Mappy sprites (mp1_6.3m+mp1_7.3n interallacciate) — 128 sprite 16x16 4bpp", file=f)
        print("// [variante flip][codice][riga*2+meta']: nibble LSB-first,", file=f)
        print("// [2r]=pixel 0-7, [2r+1]=pixel 8-15 (stile 1942 4bpp)", file=f)
        print("const unsigned long mappy_sprites[][128][32] = {", file=f)
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
    # bbgggrrr -> RGB565 byte-swapped, identico a cmapconv.py (galaga/pacman)
    b = 31*((c>>6) & 0x3)//3
    g = 63*((c>>3) & 0x7)//7
    r = 31*((c>>0) & 0x7)//7
    rgb = (r << 11) + (g << 5) + b
    return ((rgb & 0xff00) >> 8) + ((rgb & 0xff) << 8)

def write_colormaps(pal_prom, char_lut, spr_lut):
    pal = [rgb565_swapped(c) for c in pal_prom]  # 32 colori
    # 0x0000 e' riservato alla trasparenza nei blit: i neri veri diventano quasi-neri
    def nudge(v):
        return v if v != 0 else 0x2000  # r=1 (formato swapped) ~ invisibile
    with open(OUT + "mappy_cmap.h", "w") as f:
        print("// Colormap Mappy da mp1-6.4c (char, pen+0x10) e mp1-7.5k (sprite)", file=f)
        print("// Trasparenza hardware = nibble lookup 0xF:", file=f)
        print("//  - tiles opachi: colore reale (prima passata, tilemap OPACO)", file=f)
        print("//  - tiles_prio: 0x0000 dove lookup==0xF (ridisegno tile prioritari", file=f)
        print("//    sopra gli sprite: attr bit6, si salta il colore 'trasparente')", file=f)
        print("//  - sprites: 0x0000 dove lookup==0xF (pixel non disegnato)", file=f)
        print("const unsigned short mappy_colormap_tiles[][4] = {", file=f)
        rows = []
        for g in range(64):
            vals = [hex(nudge(pal[16 + (char_lut[g*4+p] & 0x0f)])) for p in range(4)]
            rows.append("{" + ",".join(vals) + "}")
        print(",\n".join(rows), file=f)
        print("};", file=f)
        print("const unsigned short mappy_colormap_tiles_prio[][4] = {", file=f)
        rows = []
        for g in range(64):
            vals = []
            for p in range(4):
                lut = char_lut[g*4+p] & 0x0f
                vals.append(hex(0) if lut == 0x0f else hex(nudge(pal[16 + lut])))
            rows.append("{" + ",".join(vals) + "}")
        print(",\n".join(rows), file=f)
        print("};", file=f)
        print("const unsigned short mappy_colormap_sprites[][16] = {", file=f)
        rows = []
        for g in range(16):
            vals = []
            for p in range(16):
                lut = spr_lut[g*16+p] & 0x0f
                vals.append(hex(0) if lut == 0x0f else hex(nudge(pal[lut])))
            rows.append("{" + ",".join(vals) + "}")
        print(",\n".join(rows), file=f)
        print("};", file=f)

def write_rom(name, sym, data, comment):
    with open(OUT + name, "w") as f:
        print(f"// {comment}", file=f)
        print(f"const unsigned char {sym}[] = {{", file=f)
        for i in range(0, len(data), 16):
            print("  " + ",".join(f"0x{b:02X}" for b in data[i:i+16]) + ",", file=f)
        print("};", file=f)

def write_wavetable(prom):
    # 256 byte, 4 bit bassi = 8 forme d'onda x 32 campioni, centrate (-8..7)
    with open(OUT + "mappy_wavetable.h", "w") as f:
        print("// Mappy WSG 15XX waveforms (mp1-3.3m): 8 forme x 32 campioni", file=f)
        print("const signed char mappy_wavetable[][32] = {", file=f)
        rows = []
        for w in range(8):
            vals = [str((prom[w*32+i] & 0x0f) - 8) for i in range(32)]
            rows.append(" { " + ",".join(vals) + " }")
        print(",\n".join(rows), file=f)
        print("};", file=f)

def write_dips():
    with open(OUT + "mappy_dipswitches.h", "w") as f:
        print("""#ifndef _mappy_dipswitches_h_
#define _mappy_dipswitches_h_

// DSW1 (58XX #1 porte 22-29), attivo basso — default MAME 0xFF:
// bit0-2 difficolta' (7=Rank A ... 0=Rank H), bit3-4 Coin B (0x18=1C1C),
// bit5 demo sounds (0x20=on), bit6 rack test (0x40=off), bit7 freeze (0x80=off)
#define MAPPY_DSW1  0xFF

// DSW2 (multiplexato via LS157), default 0xFF:
// bit0-2 Coin A (0x07=1C1C), bit3-5 bonus (0x38=20k&70k con 3 vite),
// bit6-7 vite (0xC0=3)
#define MAPPY_DSW2  0xFF

// DSW0 (4 bit, 58XX #1 porte 30-33), attivo basso: bit0 = service mode off
#define MAPPY_DSW0  0x0F

#endif""", file=f)

# ------------------------------------------------------------
# preview PNG (validazione offline orientamento/decode)
# ------------------------------------------------------------
def preview(tiles, sprites, pal_prom, char_lut, spr_lut, outpng):
    try:
        from PIL import Image
    except ImportError:
        print("PIL non disponibile, niente preview")
        return
    def pal_rgb(c):
        return (255*((c>>0)&7)//7, 255*((c>>3)&7)//7, 255*((c>>6)&3)//3)
    pal = [pal_rgb(c) for c in pal_prom]
    # tiles: griglia 16x16 (144px), sprite: griglia 16x8 (288px)
    img = Image.new("RGB", (16*18, 16*9 + 8*18 + 8), (32, 32, 32))
    px = img.load()
    for t in range(256):
        gx, gy = (t % 16) * 9, (t // 16) * 9
        for y in range(8):
            for x in range(8):
                lut = char_lut[1*4 + tiles[t][y][x]] & 0x0f
                px[gx + x, gy + y] = pal[16 + lut]
    # sprites: griglia 16x8, gruppo colore 0
    base = 16*9 + 8
    for s in range(128):
        gx, gy = (s % 16) * 18, base + (s // 16) * 18
        for y in range(16):
            for x in range(16):
                lut = spr_lut[0*16 + sprites[s][y][x]] & 0x0f
                px[gx + x, gy + y] = (0,0,0) if lut == 0x0f else pal[lut]
    img = img.resize((img.width*3, img.height*3), Image.NEAREST)
    img.save(outpng)
    print("preview:", outpng)

# ------------------------------------------------------------
def main():
    roms = {}
    for name, size, sha1 in REQUIRED:
        roms[name] = load_rom(name, size, sha1)

    selftest()

    os.makedirs(OUT, exist_ok=True)

    # tiles (invertite: ROMREGION_INVERT)
    tdata = bytes(b ^ 0xFF for b in roms["mp1_5.3b"])
    tiles = [rot_galagino(t) for t in
             mame_decode(tdata, 8, 8, CHAR_PLANES, CHAR_XOFFS, CHAR_YOFFS, 128, 256)]
    write_tiles(tiles)

    # sprites: stream interallacciato 16 bit (mp1_6 = byte pari, mp1_7 = dispari)
    r6, r7 = roms["mp1_6.3m"], roms["mp1_7.3n"]
    sdata = bytearray(0x4000)
    sdata[0::2] = r6
    sdata[1::2] = r7
    sprites = [rot_galagino(s) for s in
               mame_decode(bytes(sdata), 16, 16, MSPR_PLANES, MSPR_XOFFS, MSPR_YOFFS, 1024, 128)]
    write_sprites(sprites)

    write_colormaps(roms["mp1-5.5b"], roms["mp1-6.4c"], roms["mp1-7.5k"])
    write_wavetable(roms["mp1-3.3m"])

    main_rom = roms["mpx_3.1d"] + roms["mp1_2.1c"] + roms["mpx_1.1b"]
    write_rom("mappy_rom_main.h", "mappy_rom_main", main_rom,
              "Mappy main M6809 ROM 0xA000-0xFFFF (mpx_3.1d+mp1_2.1c+mpx_1.1b)")
    write_rom("mappy_rom_sub.h", "mappy_rom_sub", roms["mp1_4.1k"],
              "Mappy sound M6809 ROM 0xE000-0xFFFF (mp1_4.1k)")
    write_dips()

    preview(tiles, sprites, roms["mp1-5.5b"], roms["mp1-6.4c"], roms["mp1-7.5k"],
            "mappy_preview.png")
    print("Conversione Mappy completata.")

if __name__ == "__main__":
    main()
