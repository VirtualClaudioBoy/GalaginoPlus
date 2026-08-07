#!/usr/bin/env python3
# ============================================================
# Circus Charlie (Konami 1984) ROM converter per galagino29-main
#
# Autonomo (stile mappy/todruaga): legge ../roms/, scrive
# ../../source/src/machines/circusc/*.h
#
# Hardware (MAME konami/circusc.cpp, fornito dall'utente):
# - tiles 8x8 4bpp "packed msb" (gfx_8x8x4_packed_msb): 32 byte/tile,
#   2 pixel/byte, nibble ALTO = pixel di sinistra; 512 tile da 16KB
# - sprite 16x16 4bpp packed msb: 128 byte/sprite, 384 sprite da 48KB
# - palette PROM 32B bbgggrrr (identica a mappy) + 2 lookup 256B:
#   char pen = lut+0x10, sprite pen = lut; trasparenza sprite =
#   transpen_mask(color, 0): pen p trasparente se lut[c*16+p]==lut[c*16+0]
#
# Formati output:
# - tiles PRE-RUOTATI ROT90 galagino (out[y][x] = mame[7-x][y], come
#   mappy/todruaga) e impacchettati [8][4] nibble LSB-first (nibble basso
#   = colonna pari) -> blit stile timeplt con ptr[c]
# - sprite in ORIENTAMENTO LANDSCAPE nativo [16][8] nibble LSB-first,
#   senza varianti flip (48KB una copia sola): la rotazione la fa il blit
#   trasposto (screen_x = spr_x + 15 - r) e i flip si fanno a runtime,
#   come rocnrope
# ============================================================

import hashlib, os

ROMS = "../roms/"
OUT = "../../source/src/machines/circusc/"

# il circusc.zip dell'utente usa la VECCHIA convenzione nomi: mappa
# (nome nel zip, dimensione, sha1 atteso dal set MAME "circusc" set 1)
REQUIRED = [
    # main CPU (KONAMI-1), 0x6000-0xFFFF
    ("s05",          0x2000, "0e5bd350fa5fee42569eb0c4accf7512d645b792"),  # 380_s05.3h @6000
    ("q04",          0x2000, "458c398911453d558003f49c298b0d593c941c11"),  # 380_q04.4h @8000
    ("q03",          0x2000, "03211f0cc90b6e356989c5e2a41b70f4ff2ead83"),  # 380_q03.5h @A000
    ("q02",          0x2000, "a1f65e73c4e5abff1b0970bad32a128173245561"),  # 380_q02.6h @C000
    ("q01",          0x2000, "2f40e1a109d129bb127a8b98e27817988cd08c8b"),  # 380_q01.7h @E000
    # audio CPU (Z80), 0x0000-0x3FFF
    ("cd05_l14.bin", 0x2000, "67103d61994fd3a1e2de7cf9487e4f763234b18e"),  # 380_l14.5c
    ("cd07_l15.bin", 0x2000, "14f305717edcc2471e763b262960a0b96eef3530"),  # 380_l15.7c
    # tiles
    ("a04_j12.bin",  0x2000, "73b9e3d46dfe9e39b390c634df153648a0906876"),  # 380_j12.4a
    ("a05_k13.bin",  0x2000, "4d0b0a773c385b7f1dcf024760d0437f47e78fbe"),  # 380_j13.5a
    # sprites
    ("e11_j06.bin",  0x2000, "70a50dcc86dfbdaa9c2af613105aae7f90747804"),
    ("e12_j07.bin",  0x2000, "2ad7cbcbdbb434dc43e9c94cd00df9e57ac097f5"),
    ("e13_j08.bin",  0x2000, "b22ad7cfda392894208eb4b39505f38bfe4c4342"),
    ("e14_j09.bin",  0x2000, "1a649ec667d377ffab26b4694be790b3a2742f30"),
    ("e15_j10.bin",  0x2000, "4c02b75a62993cce60d2cb87b81c7738abbc9a0d"),
    ("e16_j11.bin",  0x2000, "d315588e6cc2f4263be621d2d8603c8215a90046"),
    # PROM
    ("a02_j18.bin",  0x0020, "599acd25f36445221c553510a5de23ddba5ecc15"),  # palette
    ("b07_j17.bin",  0x0100, "0d61d468f6d3e1570fd18d236ec8cab92db4ed5c"),  # char lut
    ("c10_j16.bin",  0x0100, "86df21c8e0b1ed51a0a4bd33dbb33f6efdea7d39"),  # sprite lut
]

def load_rom(name, size, sha1):
    with open(os.path.join(ROMS, name), "rb") as f:
        data = f.read()
    if len(data) != size:
        raise ValueError(f"{name}: dimensione {len(data)} != {size}")
    h = hashlib.sha1(data).hexdigest()
    if h != sha1:
        print(f"ATTENZIONE: {name} sha1 {h} != atteso {sha1} (set diverso?)")
    return data

# ------------------------------------------------------------
# decode 4bpp "packed msb": 2 pixel/byte, nibble alto = pixel sinistro
# ------------------------------------------------------------
def decode_packed(data, base, w, h):
    tile = []
    for y in range(h):
        row = []
        for x in range(w):
            b = data[base + y * (w // 2) + (x >> 1)]
            row.append((b >> 4) & 0xF if (x & 1) == 0 else b & 0xF)
        tile.append(row)
    return tile

# rotazione galagino (portrait, ROT90 + 180 display): out[y][x] = mame[N-1-x][y]
def rot_galagino(tile):
    n = len(tile)
    return [[tile[n - 1 - x][y] for x in range(n)] for y in range(n)]

# ------------------------------------------------------------
# scritture header
# ------------------------------------------------------------
def write_tiles(tiles):
    # pre-ruotati; [8][4] nibble LSB-first (nibble basso = colonna pari)
    with open(OUT + "circusc_tilemap.h", "w") as f:
        print("// Circus Charlie tiles (a04_j12+a05_k13) — 512 tile 8x8 4bpp", file=f)
        print("// PRE-RUOTATI ROT90 galagino; nibble LSB-first:", file=f)
        print("//   px = (tile[r][c>>1] >> ((c&1)*4)) & 0xF", file=f)
        print("const unsigned char circusc_tilemap[][8][4] = {", file=f)
        rows = []
        for t in tiles:
            lines = []
            for y in range(8):
                vals = []
                for xb in range(4):
                    v = t[y][2 * xb] | (t[y][2 * xb + 1] << 4)
                    vals.append(f"0x{v:02X}")
                lines.append("{" + ",".join(vals) + "}")
            rows.append(" {" + ",".join(lines) + "}")
        print(",\n".join(rows), file=f)
        print("};", file=f)

def write_sprites(sprites):
    # orientamento LANDSCAPE nativo (rotazione nel blit trasposto), una
    # sola copia (flip a runtime): [16][8] nibble LSB-first
    with open(OUT + "circusc_spritemap.h", "w") as f:
        print("// Circus Charlie sprites (e11_j06..e16_j11) — 384 sprite 16x16 4bpp", file=f)
        print("// orientamento LANDSCAPE nativo, flip a runtime (stile rocnrope):", file=f)
        print("//   px = (spr[riga][col>>1] >> ((col&1)*4)) & 0xF", file=f)
        print("const unsigned char circusc_spritemap[][16][8] = {", file=f)
        rows = []
        for s in sprites:
            lines = []
            for y in range(16):
                vals = []
                for xb in range(8):
                    v = s[y][2 * xb] | (s[y][2 * xb + 1] << 4)
                    vals.append(f"0x{v:02X}")
                lines.append("{" + ",".join(vals) + "}")
            rows.append(" {" + ",".join(lines) + "}")
        print(",\n".join(rows), file=f)
        print("};", file=f)

def rgb565_swapped(c):
    b = 31*((c>>6) & 0x3)//3
    g = 63*((c>>3) & 0x7)//7
    r = 31*((c>>0) & 0x7)//7
    rgb = (r << 11) + (g << 5) + b
    return ((rgb & 0xff00) >> 8) + ((rgb & 0xff) << 8)

def write_colormaps(pal_prom, char_lut, spr_lut):
    pal = [rgb565_swapped(c) for c in pal_prom]
    def nudge(v):
        return v if v != 0 else 0x2000  # 0x0000 riservato alla trasparenza
    with open(OUT + "circusc_cmap.h", "w") as f:
        print("// Colormap Circus Charlie da b07_j17 (char, pen+0x10) e c10_j16", file=f)
        print("// (sprite). Tiles sempre OPACHI (MAME li disegna opachi in", file=f)
        print("// entrambe le passate). Sprite: trasparenza = transpen_mask(c,0):", file=f)
        print("// pen p trasparente se lut[c*16+p]==lut[c*16+0] -> 0x0000", file=f)
        print("const unsigned short circusc_colormap_tiles[][16] = {", file=f)
        rows = []
        for g in range(16):
            vals = [hex(nudge(pal[16 + (char_lut[g*16+p] & 0x0f)])) for p in range(16)]
            rows.append("{" + ",".join(vals) + "}")
        print(",\n".join(rows), file=f)
        print("};", file=f)
        print("const unsigned short circusc_colormap_sprites[][16] = {", file=f)
        rows = []
        for g in range(16):
            vals = []
            t0 = spr_lut[g*16] & 0x0f
            for p in range(16):
                lut = spr_lut[g*16+p] & 0x0f
                vals.append(hex(0) if lut == t0 else hex(nudge(pal[lut])))
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

def write_dips():
    with open(OUT + "circusc_dipswitches.h", "w") as f:
        print("""#ifndef _circusc_dipswitches_h_
#define _circusc_dipswitches_h_

// DSW1 @0x1400 (attivo basso): bit0-3 Coin A, bit4-7 Coin B; 0xFF = 1C/1C
#define CIRCUSC_DSW1  0xFF

// DSW2 @0x1800: bit0-1 vite (0x03=3, 0x02=4, 0x01=5, 0x00=7),
// bit2 cabinet (0=UPRIGHT!), bit3 bonus (0x08=20k/90k/70k+),
// bit4 inutilizzato (default 0), bit5-6 difficolta' (0x40=Normal),
// bit7 demo sounds (0=ON!). Default MAME = 0x4B
#define CIRCUSC_DSW2  0x4B
// maschera per demo sounds off (bit7=1 = off)
#define CIRCUSC_DSW2_DEMO_SOUND_OFF  0x80

#endif""", file=f)

# ------------------------------------------------------------
def preview(tiles_rot, sprites, pal_prom, char_lut, spr_lut, outpng):
    try:
        from PIL import Image
    except ImportError:
        print("PIL non disponibile, niente preview")
        return
    def pal_rgb(c):
        return (255*((c>>0)&7)//7, 255*((c>>3)&7)//7, 255*((c>>6)&3)//3)
    pal = [pal_rgb(c) for c in pal_prom]
    img = Image.new("RGB", (16*18, 32*9 + 8 + 24*18), (32, 32, 32))
    px = img.load()
    # 512 tile (pre-ruotati: a video appariranno come sul cabinet verticale)
    for t in range(512):
        gx, gy = (t % 16) * 9, (t // 16) * 9
        for y in range(8):
            for x in range(8):
                lut = char_lut[0*16 + tiles_rot[t][y][x]] & 0x0f
                px[gx + x, gy + y] = pal[16 + lut]
    # 384 sprite (landscape nativo, gruppo colore 1)
    base = 32*9 + 8
    for s in range(384):
        gx, gy = (s % 16) * 18, base + (s // 16) * 18
        t0 = spr_lut[1*16] & 0x0f
        for y in range(16):
            for x in range(16):
                lut = spr_lut[1*16 + sprites[s][y][x]] & 0x0f
                px[gx + x, gy + y] = (0,0,0) if lut == t0 else pal[lut]
    img = img.resize((img.width*2, img.height*2), Image.NEAREST)
    img.save(outpng)
    print("preview:", outpng)

# ------------------------------------------------------------
def main():
    roms = {}
    for name, size, sha1 in REQUIRED:
        roms[name] = load_rom(name, size, sha1)

    os.makedirs(OUT, exist_ok=True)

    # tiles: 16KB -> 512 tile pre-ruotati
    tdata = roms["a04_j12.bin"] + roms["a05_k13.bin"]
    tiles = [rot_galagino(decode_packed(tdata, 32*t, 8, 8)) for t in range(512)]
    write_tiles(tiles)

    # sprites: 48KB -> 384 sprite in landscape nativo
    sdata = (roms["e11_j06.bin"] + roms["e12_j07.bin"] + roms["e13_j08.bin"] +
             roms["e14_j09.bin"] + roms["e15_j10.bin"] + roms["e16_j11.bin"])
    sprites = [decode_packed(sdata, 128*s, 16, 16) for s in range(384)]
    write_sprites(sprites)

    write_colormaps(roms["a02_j18.bin"], roms["b07_j17.bin"], roms["c10_j16.bin"])

    main_rom = (roms["s05"] + roms["q04"] + roms["q03"] + roms["q02"] + roms["q01"])
    write_rom("circusc_main_rom.h", "circusc_main_rom", main_rom,
              "Circus Charlie main KONAMI-1 ROM 0x6000-0xFFFF (s05+q04+q03+q02+q01)")
    write_rom("circusc_audio_rom.h", "circusc_audio_rom",
              roms["cd05_l14.bin"] + roms["cd07_l15.bin"],
              "Circus Charlie sound Z80 ROM 0x0000-0x3FFF (l14+l15)")
    write_dips()

    preview(tiles, sprites, roms["a02_j18.bin"], roms["b07_j17.bin"],
            roms["c10_j16.bin"], "circusc_preview.png")
    print("Conversione Circus Charlie completata.")

if __name__ == "__main__":
    main()
