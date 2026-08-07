#!/usr/bin/env python3
# ============================================================
# Xevious (Namco 1982) ROM converter per galagino29-main
#
# Autonomo (stile mappy/gaplus_rom_convert.py): legge ../roms/, scrive
# ../../source/src/machines/xevious/*.h
#
# Riferimenti MAME CORRENTI forniti dall'utente e letti per intero:
# E:\Download\galaga.cpp (driver condiviso Galaga/Xevious/Bosconian/...,
# contiene ROM_START(xevious), gfx_layout, init_xevious(), machine_config),
# E:\Download\xevious.h (classe xevious_state), E:\Download\xevious.cpp
# (video: palette, tile_info, xevious_bb_r/bs_w planet-map, draw_sprites).
# Nessun dato preso da xevious_m.cpp (riguarda solo il bootleg battles_state,
# xevious_state reale usa i chip Namco 06XX/50XX/51XX/54XX come device MAME
# veri, non lo shortcut ad-hoc del vecchio fork mame4all).
# ============================================================

import hashlib, os, sys, wave

ROMS = "../roms/"
OUT = "../../source/src/machines/xevious/"

# file: (nome, dimensione, sha1) -- sha1 calcolato sul contenuto REALE di
# romszip/xevious.zip e incrociato con ROM_START(xevious) in galaga.cpp
REQUIRED = [
    ("xvi_1.3p",    0x1000, "4882b25b0938a903f3a367455ba788a30759b5b0"),  # cpu1 @0x0000
    ("xvi_2.3m",    0x1000, "8adc60a5fcbca74092518dbc570ffff0f04c5b17"),  # cpu1 @0x1000
    ("xvi_3.2m",    0x1000, "c6a154858716e1f073b476824b183de20e06d093"),  # cpu1 @0x2000
    ("xvi_4.2l",    0x1000, "4b846de204d08651253d3a141677c8a31626af07"),  # cpu1 @0x3000
    ("xvi_5.3f",    0x1000, "15f1c005b9d806a384ab1f2240b9c580bfe83893"),  # cpu2 @0x0000
    ("xvi_6.3j",    0x1000, "6b79efee1a9642edb9f752101737132401248aed"),  # cpu2 @0x1000
    ("xvi_7.2c",    0x1000, "f8d1f8e019d8198308443c2e7e815d0d04b23d14"),  # cpu3 @0x0000
    ("xvi_12.3b",   0x1000, "9c3b61dfca2f84673a78f7f66e363777a8f47a59"),  # gfx1 fg char 1bpp
    ("xvi_13.3c",   0x1000, "32bc09be5ff8b52ee3a26e0ac3ebc2d4107badb7"),  # gfx2 bg tile plane0 (B0)
    ("xvi_14.3d",   0x1000, "fb9ffe5fc43e0213231267e98d605d43c15f61e8"),  # gfx2 bg tile plane1 (B1)
    ("xvi_15.4m",   0x2000, "19ddbd9805f77f38c9a9a1bb30dba6c720b8609f"),  # gfx3 sprite set1 planes0/1
    ("xvi_17.4p",   0x2000, "acff2bf5cde85a16cdc98a52cdea11f77fadf25a"),  # gfx3 sprite set2 planes0/1
    ("xvi_16.4n",   0x1000, "3bf380ef76c03822a042ecc73b5edd4543c268ce"),  # gfx3 sprite set3 planes0/1
    ("xvi_18.4r",   0x2000, "b5f830dd2cf25cf154308d2e640f0ecdcda5d8cd"),  # gfx3 sprite set1/2 plane2 (nibble packed)
    ("xvi_9.2a",    0x1000, "3106d1aacff06cf78371bd19967141072b32b7d7"),  # gfx4 planet-map 2A
    ("xvi_10.2b",   0x2000, "49064b25667ffcd81137cd5e800df4b78b182a46"), # gfx4 planet-map 2B
    ("xvi_11.2c",   0x1000, "3f7eac12863697a98e1122111801606759e44b2a"), # gfx4 planet-map 2C
    ("xvi_8bpr.6a", 0x0100, "0dc1e63a47a4cb0ba75f6f1e0c15e408bb0ee2a1"), # palette rosso
    ("xvi_9bpr.6d", 0x0100, "63015e3c0874afc6b1ca032f1ffb8f90562c77c8"), # palette verde
    ("xvi10bpr.6e", 0x0100, "c94d5a5dd4d8a08d6d39c051a4a722581b903f45"), # palette blu
    ("xvi_7bpr.4h", 0x0200, "ec6626828c79350417d08b98e9631ad35edd4a41"), # bg tiles lookup low
    ("xvi_6bpr.4f", 0x0200, "a4bdf58c190ca16fc7b976c97f41087a61fdb8b8"), # bg tiles lookup high
    ("xvi_4bpr.3l", 0x0200, "87ddf0b9d723aabb422d6d416aa9ec6bc246bf34"), # sprite lookup low
    ("xvi_5bpr.3m", 0x0200, "776168a73d3b9f0ce05610acc8a623deae0a572b"), # sprite lookup high
    ("xvi_2bpr.7n", 0x0100, "816a0fafa0b084ac11ae1af70a5186539376fc2a"), # namco WSG waveform
    ("xvi_1bpr.5n", 0x0100, "0c4d0bee858b97632411c440bea6948a74759746"), # namco timing (non usato)
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
# stesso decoder di mappy/gaplus_rom_convert.py)
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
# layout MAME (bit offset assoluti nel buffer GIA' ricostruito come da
# ROM_START+init_xevious(), vedi main())
# ------------------------------------------------------------
# gfx1: char_layout standard 8x8x1bpp (gfx_8x8x1, layout MAME generico:
# RGN_FRAC(1,1), 1 piano offset 0, xoffset ascendente MSB-first, yoffset
# 8 bit/riga) -- xvi_12.3b, 512 char da 8 byte = 4096 byte esatti
CHAR_PLANES = [0]
CHAR_XOFFS = [0,1,2,3,4,5,6,7]
CHAR_YOFFS = [y*8 for y in range(8)]
CHAR_BITS_PER_TILE = 8*8
CHAR_COUNT = 512

# gfx2: bgcharlayout (galaga.cpp:1450) 8x8x2bpp, RGN_FRAC(1,2), planes={0,half}
# xvi_13.3c (B0, plane0) + xvi_14.3d (B1, plane1) concatenati = 0x2000 byte
BG_XOFFS = [0,1,2,3,4,5,6,7]
BG_YOFFS = [y*8 for y in range(8)]
BG_BITS_PER_TILE = 8*8
BG_COUNT = 512   # 0x1000 (meta' regione) / 8 byte

# gfx3: spritelayout_xevious (galaga.cpp) 16x16x3bpp, RGN_FRAC(1,2),
# planes={half+4, 0, 4} -- STESSO schema/offset di gaplus (stessa famiglia
# hw), regione gfx3 totale 0xa000 byte dopo init_xevious() (vedi main())
SPR_XOFFS = [0,1,2,3, 8*8,8*8+1,8*8+2,8*8+3, 16*8,16*8+1,16*8+2,16*8+3, 24*8,24*8+1,24*8+2,24*8+3]
SPR_YOFFS = [y*8 for y in range(8)] + [32*8 + y*8 for y in range(8)]
SPR_BITS_PER_TILE = 64*8
SPR_COUNT = 320   # 0xa000/2/64 = 0x5000/64 = 320 sprite (set1:128 set2:128 set3:64)

# ------------------------------------------------------------
# scritture header
# ------------------------------------------------------------
def write_fg_tiles(tiles):
    with open(OUT + "xevious_fgtilemap.h", "w") as f:
        print("// Xevious foreground text tiles (xvi_12.3b) — 512 char 8x8 1bpp", file=f)
        print("// pen0=trasparente (mappato via colormap), indice diretto da fg_videoram", file=f)
        print("const unsigned char xevious_fgtilemap[][8] = {", file=f)
        rows = []
        for t in tiles:
            vals = []
            for y in range(8):
                v = 0
                for x in range(8):
                    v |= t[y][x] << x
                vals.append(hex(v))
            rows.append(" { " + ",".join(vals) + " }")
        print(",\n".join(rows), file=f)
        print("};", file=f)

def write_bg_tiles(tiles):
    with open(OUT + "xevious_bgtilemap.h", "w") as f:
        print("// Xevious background tiles (xvi_13.3c+xvi_14.3d) — 512 tile 8x8 2bpp", file=f)
        print("// nessuna trasparenza: layer bg e' OPACO (disegnato per primo)", file=f)
        print("const unsigned short xevious_bgtilemap[][8] = {", file=f)
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
    # varianti [0]=(fx0,fy0) [1]=(fx0,fy1) [2]=(fx1,fy0) [3]=(fx1,fy1), stesso
    # schema di galaga/mappy/gaplus_spritemap.h
    with open(OUT + "xevious_spritemap.h", "w") as f:
        print("// Xevious sprites (xvi_15.4m+xvi_17.4p+xvi_16.4n+xvi_18.4r, dopo", file=f)
        print("// init_xevious() unpack) — 320 sprite 16x16 3bpp (valori pixel 0-7),", file=f)
        print("// impacchettati a nibble (4 bit) come mappy/gaplus_spritemap.h.", file=f)
        print("const unsigned long xevious_sprites[][320][32] = {", file=f)
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
    rgb = ((r*31//255) << 11) + ((g*63//255) << 5) + (b*31//255)
    return ((rgb & 0xff00) >> 8) + ((rgb & 0xff) << 8)

def decode_palette(red_prom, green_prom, blue_prom):
    # xevious_palette() (xevious.cpp): resistenze pesate 0x0e/0x1f/0x43/0x8f
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
    return v if v != 0 else 0x2000  # nero vero -> quasi nero (0 e' trasparenza)

def write_colormaps(pal, bg_lut_lo, bg_lut_hi, spr_lut_lo, spr_lut_hi):
    # bg: gruppi da 4 pen (2bpp), sprite: gruppi da 8 pen (3bpp). Formule
    # esatte da xevious_palette() (xevious.cpp:70-99):
    #  bg:    pen indiretto = (lut_lo&0xf) | ((lut_hi&0xf)<<4)   (0-127, diretto in pal[])
    #  sprite:c = (lut_lo&0xf)|((lut_hi&0xf)<<4); pen = (c&0x80)?(c&0x7f):0x80 (trasp.)
    #  fg:    pen diretto, i dispari -> pal[i>>1], i pari -> trasparente (0x80)
    with open(OUT + "xevious_cmap_bg.h", "w") as f:
        print("// Xevious colormap sfondo, da xvi_7bpr.4h (lut low)/xvi_6bpr.4f (lut high)", file=f)
        print("// 128 gruppi x 4 pen (2bpp), layer OPACO -> nessuna trasparenza", file=f)
        print("const unsigned short xevious_colormap_bg[][4] = {", file=f)
        rows = []
        for g in range(128):
            vals = []
            for p in range(4):
                idx = g*4 + p
                lut = (bg_lut_lo[idx] & 0x0f) | ((bg_lut_hi[idx] & 0x0f) << 4)
                vals.append(hex(nudge(pal[lut & 0x7f])))
            rows.append("{" + ",".join(vals) + "}")
        print(",\n".join(rows), file=f)
        print("};", file=f)

    with open(OUT + "xevious_cmap_sprites.h", "w") as f:
        print("// Xevious colormap sprite, da xvi_4bpr.3l (lut low)/xvi_5bpr.3m (lut high)", file=f)
        print("// 64 gruppi x 8 pen (3bpp). c&0x80==0 -> pen trasparente (valore 0).", file=f)
        print("const unsigned short xevious_colormap_sprites[][8] = {", file=f)
        rows = []
        for g in range(64):
            vals = []
            for p in range(8):
                idx = g*8 + p
                c = (spr_lut_lo[idx] & 0x0f) | ((spr_lut_hi[idx] & 0x0f) << 4)
                vals.append(hex(0) if (c & 0x80) == 0 else hex(nudge(pal[c & 0x7f])))
            rows.append("{" + ",".join(vals) + "}")
        print(",\n".join(rows), file=f)
        print("};", file=f)

    # colormap fg: dipende dal color code a 6 bit del tile (attr, non un
    # indice di gruppo fisso come bg/sprite), formula esatta in
    # get_fg_tile_info()/xevious_palette() (xevious.cpp): color code g =
    # ((attr&3)<<4)|((attr&0x3c)>>2), pen1 -> pal[g] diretto, pen0 -> trasp.
    with open(OUT + "xevious_cmap_fg.h", "w") as f:
        print("// Xevious colormap testo (xevious_palette, 'foreground characters map", file=f)
        print("// directly to a palette color', NESSUNA lut PROM). Per ogni color code", file=f)
        print("// a 6 bit (g = ((attr&3)<<4)|((attr&0x3c)>>2)) e pen 0/1: pen1 -> pal[g],", file=f)
        print("// pen0 -> trasparente (0).", file=f)
        print("const unsigned short xevious_colormap_fg[][2] = {", file=f)
        rows = []
        for g in range(64):
            rows.append("{" + f"0x0000,{hex(nudge(pal[g & 0x7f]))}" + "}")
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
    # stesso formato di galaga_wavetable.h: 8 forme x 32 campioni, solo
    # xvi_2bpr.7n e' usato (xvi_1bpr.5n = "timing - not used" per commento
    # ROM_LOAD in galaga.cpp)
    with open(OUT + "xevious_wavetable.h", "w") as f:
        print("// Xevious WSG waveforms (xvi_2bpr.7n): 8 forme x 32 campioni", file=f)
        print("const signed char xevious_wavetable[][32] = {", file=f)
        rows = []
        for w in range(8):
            vals = [str((prom[w*32+i] & 0x0f) - 8) for i in range(32)]
            rows.append(" { " + ",".join(vals) + " }")
        print(",\n".join(rows), file=f)
        print("};", file=f)

def write_sample_boom():
    # Campioni esplosione (forniti dall'utente, romconv/xevious/explo*.wav:
    # gli stessi WAV che MAME usa per il bootleg Battles, privo del 54xx —
    # battles_sample_names in galaga.cpp: explo1 = bersaglio a terra,
    # explo2 = esplosione Solvalou). Mappa sui suoni del 54xx reale
    # (namco54.cpp): "suono tipo B" (pacchetto 40..20, dal loop oggetti a
    # terra 0x1a00) -> explo1; "suono tipo A" (pacchetti 30..10) -> explo2.
    # Il chip reale piloterebbe un filtro discreto i cui parametri non sono
    # noti. Ricampionati a 24000 Hz 8 bit CON SEGNO (stesso trucco di
    # galaga_sample_boom.h/gaplus_sample_bang.h: bit pattern in unsigned
    # char, letto a runtime con un cast a signed char*).
    try:
        import numpy as np
    except ImportError:
        print("numpy non disponibile: xevious_sample_boom*.h NON generati")
        return
    for wav, name in (("explo1", "xevious_sample_boom"),
                      ("explo2", "xevious_sample_boom2")):
        path = os.path.join(ROMS, "..", "xevious", wav + ".wav")
        if not os.path.exists(path):
            print(f"{wav}.wav non trovato, {name}.h NON generato")
            continue
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

        with open(OUT + name + ".h", "w") as f:
            print(f"// Campione esplosione (fornito dall'utente, romconv/xevious/{wav}.wav),", file=f)
            print(f"// ricampionato a 24000 Hz 8bit con segno (bit pattern in unsigned char,", file=f)
            print(f"// leggere con cast a 'const signed char*' come galaga_sample_boom.h).", file=f)
            print(f"// {len(samples8)} campioni ({len(samples8)/target_fr:.3f}s a 24000 Hz)", file=f)
            print(f"const unsigned char {name}[] = {{", file=f)
            vals = [f"0x{(int(v) & 0xFF):02X}" for v in samples8]
            for i in range(0, len(vals), 16):
                print("  " + ",".join(vals[i:i+16]) + ",", file=f)
            print("};", file=f)
        print(f"{name}.h: {len(samples8)} campioni @24kHz ({len(samples8)/target_fr:.3f}s)")

def write_planetmap(rom2a, rom2b, rom2c):
    # Le 3 ROM del "planet map" (schematic 9B) sono lette a runtime via
    # xevious_bb_r()/xevious_bs_w() (0xf000-0xffff) -- dati flash grezzi,
    # NESSUN decode, copiati cosi' come sono (rom2a=xvi_9.2a, rom2b=xvi_10.2b,
    # rom2c=xvi_11.2c, indirizzati come UN SOLO blob concatenato 0x4000 byte
    # come nella region "gfx4" reale: rom2a@0, rom2b@0x1000, rom2c@0x3000).
    data = rom2a + rom2b + rom2c
    write_rom("xevious_planetmap.h", "xevious_planetmap", data,
              "Xevious planet-map lookup ROM (xvi_9.2a+xvi_10.2b+xvi_11.2c), 0x4000 byte: "
              "rom2a@0x0000 rom2b@0x1000 rom2c@0x3000, letta via xevious_bb_r()")

def write_dips():
    with open(OUT + "xevious_dipswitches.h", "w") as f:
        print("""#ifndef _xevious_dipswitches_h_
#define _xevious_dipswitches_h_

// DSWA/DSWB lette via bosco_dsw_r a 0x6800-0x6807 (galaga.cpp, formula
// ESATTA verificata sul sorgente reale: bit0=(DSWB>>offset)&1,
// bit1=(DSWA>>offset)&1, offset=indirizzo&7 -- NESSUN bit-reversal/
// inversione, a differenza dello shortcut di galaga.cpp che ne ha
// entrambi (bug trovato con harness Python: con quella formula DSWA=DSWB
// =0xFF produceva un valore che falliva un controllo di boot in CPU1,
// bloccando l'handshake con CPU2 per sempre -- vedi romconv/xevious/
// harness_cpu1.py/harness_cpu12.py). Default 0xFF = valori di fabbrica
// MAME (INPUT_PORTS_START(xevious): Coin_A 1C1C, Lives 3, Upright,
// Bonus_Life factory default, Difficulty Normal, Freeze Off).
#define XEVIOUS_DSWA  0xFF
#define XEVIOUS_DSWB  0xFF

#endif""", file=f)

# ------------------------------------------------------------
# preview PNG (validazione offline orientamento/decode)
# ------------------------------------------------------------
def preview(fg_tiles, bg_tiles, sprites, pal, bg_lut_lo, bg_lut_hi, spr_lut_lo, spr_lut_hi, outpng):
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

    W = 32*9
    fg_rows = (512 + 31) // 32
    bg_rows = (512 + 31) // 32
    spr_rows = (320 + 15) // 16
    H = fg_rows*9 + bg_rows*9 + spr_rows*18 + 24
    img = Image.new("RGB", (W, H), (32, 32, 32))
    px = img.load()

    for t in range(512):
        gx, gy = (t % 32) * 9, (t // 32) * 9
        for y in range(8):
            for x in range(8):
                v = fg_tiles[t][y][x]
                col = (0,0,0) if v == 0 else unswap(pal[1])
                px[gx + x, gy + y] = col

    # bg: colore runtime-dipendente (da colorram, non dal tile) -- si usa
    # un gruppo fisso (0) solo per rendere visibile la grafica nella preview
    base = fg_rows*9 + 8
    for t in range(512):
        gx, gy = (t % 32) * 9, base + (t // 32) * 9
        for y in range(8):
            for x in range(8):
                v = bg_tiles[t][y][x]
                lut = (bg_lut_lo[v] & 0x0f) | ((bg_lut_hi[v] & 0x0f) << 4)
                px[gx + x, gy + y] = unswap(pal[lut & 0x7f])

    # gruppo 0 e' vuoto/riservato nella PROM reale (tutti pen trasparenti,
    # verificato sui byte grezzi) -- si usa il gruppo 1 solo per la preview
    base2 = base + bg_rows*9 + 8
    for s in range(min(320, len(sprites))):
        gx, gy = (s % 16) * 18, base2 + (s // 16) * 18
        for y in range(16):
            for x in range(16):
                v = sprites[s][y][x]
                idx = 1*8 + v
                lut = (spr_lut_lo[idx] & 0x0f) | ((spr_lut_hi[idx] & 0x0f) << 4)
                px[gx + x, gy + y] = (0,0,0) if (lut & 0x80) == 0 else unswap(pal[lut & 0x7f])

    img = img.resize((img.width*3, img.height*3), Image.NEAREST)
    img.save(outpng)
    print("preview:", outpng)

# ------------------------------------------------------------
def main():
    roms = {}
    for name, size, sha1 in REQUIRED:
        roms[name] = load_rom(name, size, sha1)

    os.makedirs(OUT, exist_ok=True)

    # --- gfx1 (fg char): nessun unpack, uso diretto ---
    fg_tiles = [rot_galagino(t) for t in
                mame_decode(roms["xvi_12.3b"], 8, 8, CHAR_PLANES, CHAR_XOFFS, CHAR_YOFFS,
                            CHAR_BITS_PER_TILE, CHAR_COUNT)]
    write_fg_tiles(fg_tiles)

    # --- gfx2 (bg tile): concatenazione diretta B0+B1, nessun unpack ---
    bg_src = roms["xvi_13.3c"] + roms["xvi_14.3d"]
    bg_tiles = [rot_galagino(t) for t in
                mame_decode(bg_src, 8, 8, [0, 0x1000*8], BG_XOFFS, BG_YOFFS,
                            BG_BITS_PER_TILE, BG_COUNT)]
    write_bg_tiles(bg_tiles)

    # --- gfx3 (sprite): replica ROM_START(xevious) + init_xevious() -----
    # region "gfx3" 0xa000 byte:
    #  0x0000-0x1fff xvi_15.4m  (set1 planes0/1, nibble packed)
    #  0x2000-0x3fff xvi_17.4p  (set2 planes0/1)
    #  0x4000-0x4fff xvi_16.4n  (set3 planes0/1)
    #  0x5000-0x6fff xvi_18.4r  (set1+set2 plane2, nibble packed: alta=plane2)
    #  0x7000-0x8fff init_xevious(): rom[0x7000+i] = rom[0x5000+i]>>4
    #  0x9000-0x9fff zero (set3 non ha piano2 fisico -> letto come 0)
    spr_full = bytearray(0xa000)
    spr_full[0x0000:0x2000] = roms["xvi_15.4m"]
    spr_full[0x2000:0x4000] = roms["xvi_17.4p"]
    spr_full[0x4000:0x5000] = roms["xvi_16.4n"]
    spr_full[0x5000:0x7000] = roms["xvi_18.4r"]
    for i in range(0x2000):
        spr_full[0x7000 + i] = (roms["xvi_18.4r"][i] >> 4) & 0xff
    # 0x9000-0x9fff resta a zero (bytearray gia' inizializzato a 0)

    half_bits = (0xa000 * 8) // 2  # RGN_FRAC(1,2)
    SPR_PLANES = [half_bits + 4, 0, 4]
    sprites = [rot_galagino(s) for s in
               mame_decode(bytes(spr_full), 16, 16, SPR_PLANES, SPR_XOFFS, SPR_YOFFS,
                           SPR_BITS_PER_TILE, SPR_COUNT)]
    write_sprites(sprites)

    pal = decode_palette(roms["xvi_8bpr.6a"], roms["xvi_9bpr.6d"], roms["xvi10bpr.6e"])
    write_colormaps(pal, roms["xvi_7bpr.4h"], roms["xvi_6bpr.4f"],
                     roms["xvi_4bpr.3l"], roms["xvi_5bpr.3m"])
    write_wavetable(roms["xvi_2bpr.7n"])
    write_planetmap(roms["xvi_9.2a"], roms["xvi_10.2b"], roms["xvi_11.2c"])
    write_sample_boom()

    cpu1_rom = roms["xvi_1.3p"] + roms["xvi_2.3m"] + roms["xvi_3.2m"] + roms["xvi_4.2l"]
    write_rom("xevious_rom_cpu1.h", "xevious_rom_cpu1", cpu1_rom,
              "Xevious CPU1 (main) Z80 ROM 0x0000-0x3FFF (xvi_1.3p+xvi_2.3m+xvi_3.2m+xvi_4.2l)")
    cpu2_rom = roms["xvi_5.3f"] + roms["xvi_6.3j"]
    write_rom("xevious_rom_cpu2.h", "xevious_rom_cpu2", cpu2_rom,
              "Xevious CPU2 (motion) Z80 ROM 0x0000-0x1FFF (xvi_5.3f+xvi_6.3j)")
    write_rom("xevious_rom_cpu3.h", "xevious_rom_cpu3", roms["xvi_7.2c"],
              "Xevious CPU3 (sound) Z80 ROM 0x0000-0x0FFF (xvi_7.2c)")
    write_dips()

    preview(fg_tiles, bg_tiles, sprites, pal,
            roms["xvi_7bpr.4h"], roms["xvi_6bpr.4f"],
            roms["xvi_4bpr.3l"], roms["xvi_5bpr.3m"],
            "xevious_preview.png")
    print("Conversione Xevious completata.")

if __name__ == "__main__":
    main()
