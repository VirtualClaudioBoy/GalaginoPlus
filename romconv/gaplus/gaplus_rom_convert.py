#!/usr/bin/env python3
# ============================================================
# Gaplus / Galaga 3 (Namco 1984) ROM converter per galagino29-main
#
# Autonomo (stile mappy_rom_convert.py): legge ../roms/, scrive
# ../../source/src/machines/gaplus/*.h
#
# ATTENZIONE romset IBRIDO (vedi memoria project_gaplus.md): il codice
# main+sub e' del set "galaga3" (Version 2/3 PCB), ma gfx1 (char) e i due
# prom colore sprite sono della variante "gaplus" (gp2-*, non gp3-*) --
# il file .zip fornito e' cosi', si usa il contenuto REALE per nome.
#
# Riferimenti MAME: gaplus.cpp / gaplus_v.cpp / gaplus_m.cpp (forniti
# dall'utente). Hardware gemello di galaga (STESSO tileaddr.h, stesso
# split videoram tile/attr 0x400+0x400, stesse 2 categorie di priorita')
# e di mappy/todruaga (stesso namcoio 56XX/58XX, stessa WSG 15XX, stessa
# trasformazione ROT90 per gli sprite: gal_x = 208-(sy+16*y), gal_y =
# sx+16*x -- qui pero' sizex/sizey sono INDIPENDENTI, non sempre quadrati,
# e c'e' il flag "duplicate" in piu' che gaplus ha e mappy no).
# ============================================================

import hashlib, os, sys, wave

ROMS = "../roms/"
OUT = "../../source/src/machines/gaplus/"

# file: (nome, dimensione, sha1 -- verificato contro il contenuto REALE
# dello zip romszip/gaplus.zip, non contro un singolo ROM_START "puro")
REQUIRED = [
    ("gp3-4c.8d",  0x2000, "e39f77af16016d28170e4ac1c2a784b0a7ec5454"),  # main @0xA000
    ("gp3-3c.8c",  0x2000, "2b6bb2a5d77a837810180391ef6c0ce745bfed64"),  # main @0xC000
    ("gp3-2d.8b",  0x2000, "b176b46bd6f2501d3a74ed11186be8411fd1105b"),  # main @0xE000
    ("gp3-8b.11d", 0x2000, "bbed2056dc28dc2828e29987c16d89fb16e7059e"),  # sub  @0xA000
    ("gp2-7.11c",  0x2000, "b86020f819fefb134cb57e203f7c90b1b29581c8"),  # sub  @0xC000
    ("gp3-6b.11b", 0x2000, "a19f2942dafc899d686a42240fc2f7a7a7d3b1f5"),  # sub  @0xE000
    ("gp2-1.4b",   0x2000, "4e0a31d84cb7aca497485dbe0240009d58275765"),  # sub2 (sound) @0xE000
    ("gp2-5.8s",   0x2000, "a0107fa4659597ac42c875ab1c0deb845534268b"),  # gfx1 char (variante gaplus!)
    ("gp2-11.11p", 0x2000, "16873e0ac5f975768d596d7d32af7571f4817f2b"),  # gfx2 sprite
    ("gp2-10.11n", 0x2000, "fc346e98737c9fc20810e32d4c150ae4b4051979"),
    ("gp2-12.11r", 0x2000, "368e4541a5151e906a189712bc05192c2ceec8ae"),
    ("gp2-9.11m",  0x2000, "99c1e67c3b216aa1b63f199e21c73cdedde80e1b"),
    ("gp2-1.1p",   0x0100, "dcd6dfbfbd5281ba0c7b7c189d6fde23617ed3e3"),  # palette rosso
    ("gp2-1.1n",   0x0100, "c76f9d9b066e268621d41a703c5280261234709a"),  # palette verde
    ("gp2-2.2n",   0x0100, "64d7b333f529d3ba66aeefd380fd1cbf9ddf460d"),  # palette blu
    ("gp2-6s.bin",  0x0100, "781ffe9088476798409cb922350eff881590cf35"), # char color lut
    ("gp2-6n.bin",  0x0200, "a93a5bc448dc127e1389d10a9cb06acadfe940cf"), # sprite lut upper nibble
    ("gp2-6p.bin",  0x0200, "955dcef363870ee8e91edc73b9ea3ce489738aad"), # sprite lut lower nibble
    ("gp2-4.3f",    0x0100, "e6a23cd5ce3d3e76de3b70c8ab5a3c45b1147af4"), # namco WSG waveform
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
# decoder gfx generico stile MAME (planes date come OFFSET BIT assoluti,
# stesso identico decoder di mappy_rom_convert.py)
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
# layout MAME (bit offset assoluti dentro il buffer GIA' ricostruito
# come da driver_init, vedi main())
# ------------------------------------------------------------
# charlayout: 8x8x2bpp, planeoffset={4,6}, 32 byte/char (256 bit)
CHAR_PLANES = [4, 6]
CHAR_XOFFS = [16*8, 16*8+1, 24*8, 24*8+1, 0, 1, 8*8, 8*8+1]
CHAR_YOFFS = [y*8 for y in range(8)]
CHAR_BITS_PER_TILE = 32*8
CHAR_COUNT = 512   # 0x4000 byte totali (dopo driver_init) / 32 byte

# spritelayout: 16x16x3bpp RGN_FRAC(1,2), planeoffset={RGN_FRAC(1,2),0,4}
# region gfx2 totale 0xC000 byte dopo driver_init -> RGN_FRAC(1,2) = meta'
# in BIT = 0xC000*8/2 = 0x30000
SPR_XOFFS = [0,1,2,3, 8*8,8*8+1,8*8+2,8*8+3, 16*8,16*8+1,16*8+2,16*8+3, 24*8,24*8+1,24*8+2,24*8+3]
SPR_YOFFS = [y*8 for y in range(8)] + [32*8 + y*8 for y in range(8)]
SPR_BITS_PER_TILE = 64*8
SPR_COUNT = 384    # 0x6000 (meta' regione) / 64 byte

# ------------------------------------------------------------
# scritture header
# ------------------------------------------------------------
def write_tiles(tiles):
    with open(OUT + "gaplus_tilemap.h", "w") as f:
        print("// Gaplus tiles (gp2-5.8s + driver_init unpack nibble) — 512 tile 8x8 2bpp", file=f)
        print("// stesso schema di galaga_tilemap.h: pixel LSB-first, indice diretto", file=f)
        print("// (nessuno scan/scroll: la tilemap di gaplus e' STATICA, come galaga)", file=f)
        print("const unsigned short gaplus_tilemap[][8] = {", file=f)
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
    # varianti come mappy/galaga: [0]=(fx0,fy0) [1]=(fx0,fy1) [2]=(fx1,fy0) [3]=(fx1,fy1)
    with open(OUT + "gaplus_spritemap.h", "w") as f:
        print("// Gaplus sprites (gp2-11+gp2-10+gp2-12+gp2-9 + driver_init unpack) —", file=f)
        print("// 384 sprite 16x16 3bpp (valori pixel 0-7), impacchettati a nibble", file=f)
        print("// (4 bit) come mappy_spritemap.h per semplicita' di accesso.", file=f)
        print("const unsigned long gaplus_sprites[][384][32] = {", file=f)
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

def rgb565_swapped(r, g, b):
    # r,g,b gia' 0..255 -> RGB565 byte-swapped, identico a cmapconv.py
    rgb = ((r*31//255) << 11) + ((g*63//255) << 5) + (b*31//255)
    return ((rgb & 0xff00) >> 8) + ((rgb & 0xff) << 8)

def decode_palette(red_prom, green_prom, blue_prom):
    # gaplus_palette(): resistenze pesate 0x0e/0x1f/0x43/0x8f sui 4 bit
    def comp(byte):
        v = 0
        if byte & 1: v += 0x0e
        if byte & 2: v += 0x1f
        if byte & 4: v += 0x43
        if byte & 8: v += 0x8f
        return v
    pal = []
    for i in range(256):
        r = comp(red_prom[i])
        g = comp(green_prom[i])
        b = comp(blue_prom[i])
        pal.append(rgb565_swapped(r, g, b))
    return pal

def nudge(v):
    return v if v != 0 else 0x2000  # nero vero -> quasi nero (0 e' il marcatore trasparenza)

def write_colormaps(pal, char_lut, spr_lut_lo, spr_lut_hi):
    with open(OUT + "gaplus_cmap.h", "w") as f:
        print("// Colormap Gaplus da gp2-6s.bin (char lut, 64 gruppi x4 pen,", file=f)
        print("// pen = 0xF0+lut, SOLO 16 colori fisici usati per tutto il testo)", file=f)
        print("// e gp2-6n.bin/gp2-6p.bin (sprite lut upper/lower nibble, 64 gruppi", file=f)
        print("// x8 pen, colore DIRETTO lut_lo|lut_hi<<4 = indice 0-255 nella", file=f)
        print("// stessa tavolozza RGB della palette -- NON indiretto come mappy).", file=f)
        print("// Trasparenza hardware: MAME chiama configure_groups(gfx(0),0xff)", file=f)
        print("// -- QUALSIASI pixel char il cui lut (nibble basso) vale 0x0F", file=f)
        print("// risolve a pen indiretto 0xF0+0x0F=0xFF ed e' TRASPARENTE in", file=f)
        print("// ENTRAMBE le passate (non solo quella prioritaria): le stelle", file=f)
        print("// dello starfield, disegnate PRIMA della tilemap, devono restare", file=f)
        print("// visibili nei buchi. Sprite -> pen 0xFF trasparente (separato).", file=f)
        print("const unsigned short gaplus_colormap_tiles[][4] = {", file=f)
        rows = []
        for g in range(64):
            vals = []
            for p in range(4):
                lut = char_lut[g*4+p] & 0x0f
                vals.append(hex(0) if lut == 0x0f else hex(nudge(pal[0xf0 + lut])))
            rows.append("{" + ",".join(vals) + "}")
        print(",\n".join(rows), file=f)
        print("};", file=f)

        print("const unsigned short gaplus_colormap_tiles_prio[][4] = {", file=f)
        rows = []
        for g in range(64):
            vals = []
            for p in range(4):
                lut = char_lut[g*4+p] & 0x0f
                vals.append(hex(0) if lut == 0x0f else hex(nudge(pal[0xf0 + lut])))
            rows.append("{" + ",".join(vals) + "}")
        print(",\n".join(rows), file=f)
        print("};", file=f)

        print("const unsigned short gaplus_colormap_sprites[][8] = {", file=f)
        rows = []
        for g in range(64):
            vals = []
            for p in range(8):
                idx = g*8 + p
                lut = (spr_lut_lo[idx] & 0x0f) | ((spr_lut_hi[idx] & 0x0f) << 4)
                vals.append(hex(0) if lut == 0xff else hex(nudge(pal[lut])))
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
    with open(OUT + "gaplus_wavetable.h", "w") as f:
        print("// Gaplus WSG 15XX waveforms (gp2-4.3f): 8 forme x 32 campioni", file=f)
        print("const signed char gaplus_wavetable[][32] = {", file=f)
        rows = []
        for w in range(8):
            vals = [str((prom[w*32+i] & 0x0f) - 8) for i in range(32)]
            rows.append(" { " + ",".join(vals) + " }")
        print(",\n".join(rows), file=f)
        print("};", file=f)

MAX_STARS = 250
STARFIELD_CLIPPING_X = 16
SCREEN_W, SCREEN_H = 288, 224

def gen_starfield():
    # replica ESATTA di gaplus_v.cpp starfield_init() (LFSR 32bit, stesso
    # ordine di generazione): 3 set ciclici, colore = color_base+(1..7)
    stars = []
    generator = 0
    sett = 0
    for y in range(SCREEN_H):
        for x in range(SCREEN_W - STARFIELD_CLIPPING_X*2 - 1, -1, -1):
            generator = (generator << 1) & 0xFFFFFFFF
            bit1 = (~generator >> 17) & 1
            bit2 = (generator >> 5) & 1
            if bit1 ^ bit2:
                generator |= 1
            if ((~generator) >> 16) & 1 and (generator & 0xff) == 0xff:
                color = ((~(generator >> 8)) & 0xFFFFFFFF) % 7 + 1
                color_base = {0: 0x250, 1: 0x230, 2: 0x210}[sett]
                if color and len(stars) < MAX_STARS:
                    stars.append((x + STARFIELD_CLIPPING_X, y, color_base + color, sett))
                    sett = (sett + 1) % 3
    return stars

def write_starfield(stars, pal, spr_lut_lo, spr_lut_hi):
    with open(OUT + "gaplus_starseed.h", "w") as f:
        print("// Gaplus starfield (CUS26): posizioni/colori precalcolati con lo", file=f)
        print("// STESSO LFSR di gaplus_v.cpp starfield_init() (deterministico,", file=f)
        print("// dipende solo dalla risoluzione 288x224). Colore gia' risolto", file=f)
        print("// via sprite lut (i pen 0x210-0x257 usati da MAME ricadono nello", file=f)
        print("// spazio colore sprite: locale = pen-256, RGB = spr_lut_lo/hi).", file=f)
        print("// x,y sono le coordinate MAME (x=asse largo 288, y=asse alto 224)", file=f)
        print("// PRIMA della rotazione: la macchina calcola a runtime", file=f)
        print("// gal_x = 223 - y, gal_y = x (stesso schema sprite/tile gia'", file=f)
        print("// validato per mappy/todruaga/galaga su questa famiglia hw).", file=f)
        print("struct gaplus_star_S { unsigned short x, y; unsigned short col; unsigned char set; };", file=f)
        print(f"#define GAPLUS_NUM_STARS {len(stars)}", file=f)
        print("const struct gaplus_star_S gaplus_starseed[] = {", file=f)
        rows = []
        for (x, y, penidx, sett) in stars:
            local = penidx - 256
            lut = (spr_lut_lo[local] & 0x0f) | ((spr_lut_hi[local] & 0x0f) << 4)
            col = 0 if lut == 0xff else nudge(pal[lut])
            rows.append(f" {{ {x}, {y}, {hex(col)}, {sett} }}")
        print(",\n".join(rows), file=f)
        print("};", file=f)

def write_sample_bang():
    # campione esplosione "bang" (fornito dall'utente, non presente nel
    # romset: il chip customio_3 lo pilota via samples device MAME, un
    # asset esterno non derivabile dalle ROM). Ricampionato a 24000 Hz
    # (stesso sample rate nativo di galagino, namco_15xx_render_buffer)
    # e convertito a 8 bit CON SEGNO memorizzato in un array unsigned char
    # (stesso trucco di galaga_sample_boom.h: bit pattern identico, letto
    # a runtime con un cast a signed char*).
    try:
        import numpy as np
    except ImportError:
        print("numpy non disponibile: gaplus_sample_bang.h NON generato")
        return
    path = os.path.join(ROMS, "..", "gaplus", "bang.wav")
    if not os.path.exists(path):
        print("bang.wav non trovato, gaplus_sample_bang.h NON generato")
        return
    w = wave.open(path, "rb")
    nch, sw, fr, nframes = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    frames = w.readframes(nframes)
    dtype = {1: np.int8, 2: "<i2", 4: "<i4"}[sw]
    data = np.frombuffer(frames, dtype=dtype).astype(np.float64)
    if nch > 1:
        data = data.reshape(-1, nch).mean(axis=1)

    target_fr = 24000
    n_out = int(len(data) * target_fr / fr)
    x_old = np.linspace(0, 1, len(data), endpoint=False)
    x_new = np.linspace(0, 1, n_out, endpoint=False)
    resampled = np.interp(x_new, x_old, data)

    peak = np.abs(resampled).max()
    scale = 127.0 / peak if peak > 0 else 1.0
    samples8 = np.clip(np.round(resampled * scale), -128, 127).astype(np.int8)

    with open(OUT + "gaplus_sample_bang.h", "w") as f:
        print(f"// Campione esplosione \"bang\" (fornito dall'utente, romconv/gaplus/bang.wav),", file=f)
        print(f"// ricampionato a 24000 Hz 8bit con segno (bit pattern in unsigned char,", file=f)
        print(f"// leggere con cast a 'const signed char*' come galaga_sample_boom.h).", file=f)
        print(f"// {len(samples8)} campioni ({len(samples8)/target_fr:.3f}s a 24000 Hz)", file=f)
        print("const unsigned char gaplus_sample_bang[] = {", file=f)
        vals = [f"0x{(int(v) & 0xFF):02X}" for v in samples8]
        for i in range(0, len(vals), 16):
            print("  " + ",".join(vals[i:i+16]) + ",", file=f)
        print("};", file=f)
    print(f"gaplus_sample_bang.h: {len(samples8)} campioni @24kHz ({len(samples8)/target_fr:.3f}s)")

def write_dips():
    with open(OUT + "gaplus_dipswitches.h", "w") as f:
        print("""#ifndef _gaplus_dipswitches_h_
#define _gaplus_dipswitches_h_

// DSWA (58XX in0/in3), attivo basso, default MAME 0xFF:
// bit0-1 Coin B (0x03=1C1C), bit3 demo sounds (0x08=on, DSWA_LOW)
// bit0-1 Coin A (0x03=1C1C), bit2-3 vite (0x0C=3, DSWA_HIGH)
#define GAPLUS_DSWA_LOW   0xFF
#define GAPLUS_DSWA_HIGH  0xFF

// DSWB (58XX in1/in2), attivo basso, default MAME 0xFF:
// bit3 round advance (0x08=off), bit0-2 bonus life (0x07, DSWB_LOW)
// bit3 unknown (0x08=off), bit0-2 difficolta' (0x07=standard, DSWB_HIGH)
#define GAPLUS_DSWB_LOW   0xFF
#define GAPLUS_DSWB_HIGH  0xFF

// IN2 (customio_3): bit2 cabinet (1=upright), bit3 service (attivo basso)
#define GAPLUS_CABINET_UPRIGHT  0x04

#endif""", file=f)

# ------------------------------------------------------------
# preview PNG (validazione offline orientamento/decode)
# ------------------------------------------------------------
def preview(tiles, sprites, pal, char_lut, spr_lut_lo, spr_lut_hi, outpng):
    try:
        from PIL import Image
    except ImportError:
        print("PIL non disponibile, niente preview")
        return
    def unswap(c):
        rgb = ((c & 0xff) << 8) | (c >> 8)
        r = (rgb >> 11) & 0x1f
        g = (rgb >> 5) & 0x3f
        b = rgb & 0x1f
        return (r*255//31, g*255//63, b*255//31)
    img = Image.new("RGB", (16*18, 32*9 + 8*18 + 8), (32, 32, 32))
    px = img.load()
    for t in range(min(512, len(tiles))):
        gx, gy = (t % 16) * 9, (t // 16) * 9
        for y in range(8):
            for x in range(8):
                lut = char_lut[1*4 + tiles[t][y][x]] & 0x0f
                px[gx + x, gy + y] = unswap(pal[0xf0 + lut])
    base = 32*9 + 8
    for s in range(min(128, len(sprites))):
        gx, gy = (s % 16) * 18, base + (s // 16) * 18
        for y in range(16):
            for x in range(16):
                v = sprites[s][y][x]
                lut = (spr_lut_lo[v] & 0x0f) | ((spr_lut_hi[v] & 0x0f) << 4)
                px[gx + x, gy + y] = (0,0,0) if lut == 0xff else unswap(pal[lut])
    img = img.resize((img.width*3, img.height*3), Image.NEAREST)
    img.save(outpng)
    print("preview:", outpng)

# ------------------------------------------------------------
def main():
    roms = {}
    for name, size, sha1 in REQUIRED:
        roms[name] = load_rom(name, size, sha1)

    os.makedirs(OUT, exist_ok=True)

    # --- gfx1 (char): replica driver_init: rom[i+0x2000] = rom[i]>>4 ---
    char_src = roms["gp2-5.8s"]
    char_full = bytearray(0x4000)
    char_full[0:0x2000] = char_src
    for i in range(0x2000):
        char_full[0x2000 + i] = char_src[i] >> 4
    tiles = [rot_galagino(t) for t in
             mame_decode(bytes(char_full), 8, 8, CHAR_PLANES, CHAR_XOFFS, CHAR_YOFFS,
                         CHAR_BITS_PER_TILE, CHAR_COUNT)]
    write_tiles(tiles)

    # --- gfx2 (sprite): replica driver_init: 4 rom da 8KB @0/0x2000/0x4000/
    # 0x6000, poi rom[0x8000+i] = rom[0x6000+i]<<4 per i<0x2000, poi
    # 0xA000-0xBFFF a zero (ROM_FILL, "optional non usata") ---
    spr_full = bytearray(0xC000)
    spr_full[0x0000:0x2000] = roms["gp2-11.11p"]
    spr_full[0x2000:0x4000] = roms["gp2-10.11n"]
    spr_full[0x4000:0x6000] = roms["gp2-12.11r"]
    spr_full[0x6000:0x8000] = roms["gp2-9.11m"]
    for i in range(0x2000):
        spr_full[0x8000 + i] = (roms["gp2-9.11m"][i] << 4) & 0xff
    # 0xa000-0xbfff resta a zero (bytearray gia' inizializzato a 0)

    half_bits = (0xC000 * 8) // 2  # RGN_FRAC(1,2)
    SPR_PLANES = [half_bits, 0, 4]
    sprites = [rot_galagino(s) for s in
               mame_decode(bytes(spr_full), 16, 16, SPR_PLANES, SPR_XOFFS, SPR_YOFFS,
                           SPR_BITS_PER_TILE, SPR_COUNT)]
    write_sprites(sprites)

    pal = decode_palette(roms["gp2-1.1p"], roms["gp2-1.1n"], roms["gp2-2.2n"])
    write_colormaps(pal, roms["gp2-6s.bin"], roms["gp2-6p.bin"], roms["gp2-6n.bin"])
    write_wavetable(roms["gp2-4.3f"])

    main_rom = roms["gp3-4c.8d"] + roms["gp3-3c.8c"] + roms["gp3-2d.8b"]
    write_rom("gaplus_rom_main.h", "gaplus_rom_main", main_rom,
              "Gaplus main M6809 ROM 0xA000-0xFFFF (gp3-4c.8d+gp3-3c.8c+gp3-2d.8b)")
    sub_rom = roms["gp3-8b.11d"] + roms["gp2-7.11c"] + roms["gp3-6b.11b"]
    write_rom("gaplus_rom_sub.h", "gaplus_rom_sub", sub_rom,
              "Gaplus sub M6809 ROM 0xA000-0xFFFF (gp3-8b.11d+gp2-7.11c+gp3-6b.11b)")
    write_rom("gaplus_rom_sub2.h", "gaplus_rom_sub2", roms["gp2-1.4b"],
              "Gaplus sound (sub2) M6809 ROM 0xE000-0xFFFF (gp2-1.4b)")
    write_dips()

    stars = gen_starfield()
    write_starfield(stars, pal, roms["gp2-6p.bin"], roms["gp2-6n.bin"])
    print(f"Starfield: {len(stars)} stelle generate")

    write_sample_bang()

    preview(tiles, sprites, pal, roms["gp2-6s.bin"], roms["gp2-6p.bin"], roms["gp2-6n.bin"],
            "gaplus_preview.png")
    print("Conversione Gaplus completata.")

if __name__ == "__main__":
    main()
