#!/usr/bin/env python3
# ============================================================
# The Tower of Druaga (Namco 1984) ROM converter per galagino29-main
#
# Derivato da mappy_rom_convert.py: STESSO hardware (Super Pacman class,
# driver MAME mappy.cpp, config todruaga = digdug2 + gfx_todruaga).
# Differenze rispetto a mappy:
#  - ROM main 32KB @0x8000 (td2_3.1d @0x8000 + td2_1.1b @0xC000)
#  - lookup sprite td1-7.5k da 0x400 byte = 64 gruppi colore x 16 pen
#    (mappy: 0x100 = 16 gruppi); palette entries 64*4 + 64*16
#  - DIP diversi (todruaga: DSW2 tutto inutilizzato)
# Tutto il resto identico: tiles 4KB ROMREGION_INVERT charlayout pacman,
# sprite 16x16 4bpp interallacciati 16 bit (128 sprite), palette 32B
# bbgggrrr, wave WSG 15xx 8 forme x 32 campioni, trasparenza lookup 0xF.
# ============================================================

import hashlib, os, sys

ROMS = "../roms/"
OUT = "../../source/src/machines/todruaga/"

# file: (nome, dimensione, sha1 da MAME ROM_START(todruaga) "New Ver.")
REQUIRED = [
    ("td2_3.1d", 0x4000, "9abbaaaf0a53aff38df8287f62d091b13146cf13"),
    ("td2_1.1b", 0x4000, "ab8eadd45638ff1ab2dacbd5ab2c6870b9f79086"),
    ("td1_4.1k", 0x2000, "3d8621fdd74fafa61f342886faa37f0aab50c5a7"),
    ("td1_5.3b", 0x1000, "7d7cee4101ef615fb92c3702f89a9823a6231195"),
    ("td1_6.3m", 0x2000, "74e0af4c7d6e334bcd211a33eb18dddc8a182aa7"),
    ("td1_7.3n", 0x2000, "74cdcafc26475bda085bf62ed17e6474ed782453"),
    ("td1-5.5b", 0x0020, "a648c53f2e95634bb5b27d79be3fd908021d056e"),
    ("td1-6.4c", 0x0100, "1340e4f657f4f2c4ef651a441c3b51632e757d0b"),
    ("td1-7.5k", 0x0400, "dfd7d6b2740761c3bcab4c7999d2699d920843e7"),
    ("td1-3.3m", 0x0100, "16db55525034bacb71e7dc8bd2a7c3c4464d4808"),
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
# layout MAME (identici a mappy)
# ------------------------------------------------------------
# charlayout pacman/galaga/mappy: 8x8x2bpp, 16 byte/char
CHAR_PLANES = [0, 4]
CHAR_XOFFS = [64, 65, 66, 67, 0, 1, 2, 3]
CHAR_YOFFS = [y * 8 for y in range(8)]

# galaga spritelayout (per autotest): 16x16x2bpp, 64 byte/sprite
GSPR_PLANES = [0, 4]
GSPR_XOFFS = [0,1,2,3, 64,65,66,67, 128,129,130,131, 192,193,194,195]
GSPR_YOFFS = [y*8 for y in range(8)] + [256 + y*8 for y in range(8)]

# spritelayout_4bpp: 16x16x4bpp, 128 byte/sprite (stream interallacciato)
MSPR_PLANES = [0, 4, 8, 12]
MSPR_XOFFS = [0,1,2,3, 128,129,130,131, 256,257,258,259, 384,385,386,387]
MSPR_YOFFS = [y*16 for y in range(8)] + [512 + y*16 for y in range(8)]

# ------------------------------------------------------------
# AUTOTEST contro le ROM galaga (come mappy_rom_convert.py); se le ROM
# galaga non sono in ../roms (unpack di galaga.zip mai eseguito) l'autotest
# viene SALTATO con un avviso — il decoder e' comunque identico a quello
# gia' validato per mappy su HW.
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
    gchr = os.path.join(ROMS, "gg1_9.4l")
    gspr = os.path.join(ROMS, "gg1_11.4d")
    if not (os.path.exists(gchr) and os.path.exists(gspr)):
        print("AVVISO: ROM galaga assenti, autotest decoder SALTATO")
        return
    with open(gchr, "rb") as f:
        cdata = f.read()
    dec = mame_decode(cdata, 8, 8, CHAR_PLANES, CHAR_XOFFS, CHAR_YOFFS, 128, 256)
    for t in range(256):
        ref = parse_chr_ref(cdata[16*t:16*(t+1)])
        got = rot_galagino(dec[t])
        if got != ref:
            raise AssertionError(f"AUTOTEST char {t} FALLITO")
    with open(gspr, "rb") as f:
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
    with open(OUT + "todruaga_tilemap.h", "w") as f:
        print("// Tower of Druaga tiles (td1_5.3b, ROMREGION_INVERT) — 256 tile 8x8 2bpp", file=f)
        print("// pixel LSB-first come galaga_tilemap (blit: (pix>>2c)&3)", file=f)
        print("const unsigned short todruaga_tilemap[][8] = {", file=f)
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
    with open(OUT + "todruaga_spritemap.h", "w") as f:
        print("// Tower of Druaga sprites (td1_6.3m+td1_7.3n interallacciate) — 128 sprite 16x16 4bpp", file=f)
        print("// [variante flip][codice][riga*2+meta']: nibble LSB-first,", file=f)
        print("// [2r]=pixel 0-7, [2r+1]=pixel 8-15 (stile 1942 4bpp)", file=f)
        print("const unsigned long todruaga_sprites[][128][32] = {", file=f)
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
    with open(OUT + "todruaga_cmap.h", "w") as f:
        print("// Colormap Tower of Druaga da td1-6.4c (char, pen+0x10) e td1-7.5k", file=f)
        print("// (sprite, 0x400 byte = 64 gruppi x 16 — todruaga ha 64 gruppi", file=f)
        print("// sprite contro i 16 di mappy). Trasparenza hardware = lookup 0xF:", file=f)
        print("//  - tiles opachi: colore reale (prima passata, tilemap OPACO)", file=f)
        print("//  - tiles_prio: 0x0000 dove lookup==0xF (ridisegno tile prioritari", file=f)
        print("//    sopra gli sprite: attr bit6, si salta il colore 'trasparente')", file=f)
        print("//  - sprites: 0x0000 dove lookup==0xF (pixel non disegnato)", file=f)
        print("const unsigned short todruaga_colormap_tiles[][4] = {", file=f)
        rows = []
        for g in range(64):
            vals = [hex(nudge(pal[16 + (char_lut[g*4+p] & 0x0f)])) for p in range(4)]
            rows.append("{" + ",".join(vals) + "}")
        print(",\n".join(rows), file=f)
        print("};", file=f)
        print("const unsigned short todruaga_colormap_tiles_prio[][4] = {", file=f)
        rows = []
        for g in range(64):
            vals = []
            for p in range(4):
                lut = char_lut[g*4+p] & 0x0f
                vals.append(hex(0) if lut == 0x0f else hex(nudge(pal[16 + lut])))
            rows.append("{" + ",".join(vals) + "}")
        print(",\n".join(rows), file=f)
        print("};", file=f)
        print("const unsigned short todruaga_colormap_sprites[][16] = {", file=f)
        rows = []
        for g in range(64):
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
    with open(OUT + "todruaga_wavetable.h", "w") as f:
        print("// Tower of Druaga WSG 15XX waveforms (td1-3.3m): 8 forme x 32 campioni", file=f)
        print("const signed char todruaga_wavetable[][32] = {", file=f)
        rows = []
        for w in range(8):
            vals = [str((prom[w*32+i] & 0x0f) - 8) for i in range(32)]
            rows.append(" { " + ",".join(vals) + " }")
        print(",\n".join(rows), file=f)
        print("};", file=f)

def write_dips():
    with open(OUT + "todruaga_dipswitches.h", "w") as f:
        print("""#ifndef _todruaga_dipswitches_h_
#define _todruaga_dipswitches_h_

// DSW1 (56XX #1 porte 22-29), attivo basso — default MAME 0xFF:
// bit0-1 vite (0x03=3, 0x02=2, 0x01=1, 0x00=5), bit2-3 Coin A (0x0C=1C1C),
// bit4 freeze (0x10=off), bit5 service mode (0x20=off),
// bit6-7 Coin B (0xC0=1C1C)
#define TODRUAGA_DSW1  0xFF

// DSW2 (multiplexato via LS157): tutti gli 8 bit inutilizzati in todruaga
#define TODRUAGA_DSW2  0xFF

// DSW0 (4 bit, 56XX #1 porte 30-33), attivo basso:
// bit0-1 inutilizzati, bit2 cabinet (1=upright), bit3 service (1=off)
#define TODRUAGA_DSW0  0x0F

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
    # tiles: griglia 16x16, sprite: griglia 16x8
    img = Image.new("RGB", (16*18, 16*9 + 8*18 + 8), (32, 32, 32))
    px = img.load()
    for t in range(256):
        gx, gy = (t % 16) * 9, (t // 16) * 9
        for y in range(8):
            for x in range(8):
                lut = char_lut[1*4 + tiles[t][y][x]] & 0x0f
                px[gx + x, gy + y] = pal[16 + lut]
    # sprites: griglia 16x8, gruppo colore 1 (lo 0 in todruaga puo' essere vuoto)
    base = 16*9 + 8
    for s in range(128):
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
    roms = {}
    for name, size, sha1 in REQUIRED:
        roms[name] = load_rom(name, size, sha1)

    selftest()

    os.makedirs(OUT, exist_ok=True)

    # tiles (invertite: ROMREGION_INVERT)
    tdata = bytes(b ^ 0xFF for b in roms["td1_5.3b"])
    tiles = [rot_galagino(t) for t in
             mame_decode(tdata, 8, 8, CHAR_PLANES, CHAR_XOFFS, CHAR_YOFFS, 128, 256)]
    write_tiles(tiles)

    # sprites: stream interallacciato 16 bit (td1_6 = byte pari, td1_7 = dispari)
    r6, r7 = roms["td1_6.3m"], roms["td1_7.3n"]
    sdata = bytearray(0x4000)
    sdata[0::2] = r6
    sdata[1::2] = r7
    sprites = [rot_galagino(s) for s in
               mame_decode(bytes(sdata), 16, 16, MSPR_PLANES, MSPR_XOFFS, MSPR_YOFFS, 1024, 128)]
    write_sprites(sprites)

    write_colormaps(roms["td1-5.5b"], roms["td1-6.4c"], roms["td1-7.5k"])
    write_wavetable(roms["td1-3.3m"])

    main_rom = roms["td2_3.1d"] + roms["td2_1.1b"]
    write_rom("todruaga_rom_main.h", "todruaga_rom_main", main_rom,
              "Tower of Druaga main M6809 ROM 0x8000-0xFFFF (td2_3.1d+td2_1.1b)")
    write_rom("todruaga_rom_sub.h", "todruaga_rom_sub", roms["td1_4.1k"],
              "Tower of Druaga sound M6809 ROM 0xE000-0xFFFF (td1_4.1k)")
    write_dips()

    preview(tiles, sprites, roms["td1-5.5b"], roms["td1-6.4c"], roms["td1-7.5k"],
            "todruaga_preview.png")
    print("Conversione Tower of Druaga completata.")

if __name__ == "__main__":
    main()
