#!/usr/bin/env python3
# ============================================================
# Burger Time (Data East 1982) ROM converter per galagino29-main
#
# Autonomo (stile xevious_rom_convert.py): legge ../roms/, scrive
# ../../source/src/machines/btime/*.h
#
# Riferimento MAME MODERNO fornito dall'utente e letto per intero:
# E:\Download\btime.cpp (driver Data East btime.cpp, classi btime_state/
# scregg_state/mmonkey_state), E:\Download\decocpu7.h/.cpp,
# E:\Download\deco222.h/.cpp (decrittazione CPU). NON usato mame4all
# (esplicitamente vietato dall'utente in questa sessione).
#
# Set convertito: "btime" (Data East set 1) — CRC/SHA1 di romszip/btime.zip
# verificati byte-per-byte contro ROM_START(btime) in btime.cpp.
# ============================================================

import hashlib, os, sys

ROMS = "../roms/"
OUT = "../../source/src/machines/btime/"

# (nome, dimensione, sha1) -- sha1 calcolato su romszip/btime.zip e
# incrociato con ROM_START(btime) in btime.cpp (righe 2546-2571)
REQUIRED = [
    ("aa04.9b",  0x1000, "ed3f3712423979dcb351941fa85dce6a0a7bb16b"),  # maincpu @0xc000 (CIFRATA CPU-7)
    ("aa06.13b", 0x1000, "8c77397e934907bc47a739f263196a0f2f81ba3d"),  # maincpu @0xd000
    ("aa05.10b", 0x1000, "d0da4e360039f6a8d8142a4e8e05c1f90c0af68a"),  # maincpu @0xe000
    ("aa07.15b", 0x1000, "4a32bc92f8ff5fbe112f56e62d2c03da8851a7b9"),  # maincpu @0xf000
    ("ab14.12h", 0x1000, "27940026d0c6212d1138d2fd88880df697218627"),  # audiocpu @0xe000 (NON cifrata)
    ("aa12.7k",  0x1000, "24204d591aa2c264a852ee9ba8c4be63efd97728"),  # gfx1 third0 (low)  @0x0000
    ("ab13.9k",  0x1000, "e64b6381a9298eaf74e79fa5f1ea8e9596c58a49"),  # gfx1 third0 (low)  @0x1000
    ("ab10.10k", 0x1000, "3d2ecfd54a5a9d68b53cf4b4ee1f2daa6aef2123"),  # gfx1 third1 (mid)  @0x2000
    ("ab11.12k", 0x1000, "0a55b091cd4e7f317c35defe13d5051b26042eee"),  # gfx1 third1 (mid)  @0x3000
    ("aa8.13k",  0x1000, "d9b1ee2d1f2fd66705d497c80252861b49aa9254"),  # gfx1 third2 (high) @0x4000
    ("ab9.15k",  0x1000, "b72633de6268ce16742bba4dcba835df860d6c2f"),  # gfx1 third2 (high) @0x5000
    ("ab00.1b",  0x0800, "6a0a8e6b7860859f22daa33634e34fbf91387659"),  # gfx2 third0 (low)  @0x0000
    ("ab01.3b",  0x0800, "4abdcbd4f3362c3e4463a1274731289f1a72d2e6"),  # gfx2 third1 (mid)  @0x0800
    ("ab02.4b",  0x0800, "4a03bf011dc1fb2902f42587b1174b880cf06df1"),  # gfx2 third2 (high) @0x1000
    ("ab03.6b",  0x0800, "737af6e264183a1f151f277a07cf250d6abb3fd8"),  # bg_map (lookup grezzo, non gfx)
]

def load_rom(name, size, sha1):
    path = os.path.join(ROMS, name)
    with open(path, "rb") as f:
        data = f.read()
    if len(data) != size:
        raise ValueError(f"{name}: dimensione {len(data)} != {size}")
    h = hashlib.sha1(data).hexdigest()
    if h != sha1:
        raise ValueError(f"{name}: sha1 {h} != atteso {sha1} (set diverso?)")
    return data

# ------------------------------------------------------------
# decoder gfx generico stile MAME (planes/xoffs/yoffs come OFFSET BIT
# assoluti), stesso decoder di xevious_rom_convert.py/gaplus_rom_convert.py
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

# ------------------------------------------------------------
# Layout MAME letti PER INTERO da btime.cpp (righe 2087-2130):
#
# gfx_8x8x3_planar (char set #1, gfx1, MACRO STANDARD MAME): 8x8 3bpp,
# RGN_FRAC(1,3), planeoffset {RGN_FRAC(2,3),RGN_FRAC(1,3),RGN_FRAC(0,3)}
# IDENTICO a tile16layout (stessa regione gfx1, stesso schema di piani —
# char e sprite leggono LO STESSO ROM fisico con granularita' diversa,
# 8x8 vs 16x16). xoffset/yoffset per la variante 8x8 NON split sono lo
# standard MAME ascendente (STEP8(0,1)/STEP8(0,8)), a differenza della
# variante 16x16 che usa lo split "meta' rovesciata" (STEP8(16*8,1) poi
# STEP8(0,1)) visibile in tile16layout stesso.
CHAR_XOFFS = [0,1,2,3,4,5,6,7]
CHAR_YOFFS = [y*8 for y in range(8)]
CHAR_BITS_PER_TILE = 8*8

# tile16layout (sprite gfx1 E sfondo gfx2): 16x16 3bpp, RGN_FRAC(1,3),
# stesso planeoffset del char, xoffset split (meta' destra prima)
TILE16_XOFFS = [16*8+x for x in range(8)] + [x for x in range(8)]
TILE16_YOFFS = [y*8 for y in range(16)]
TILE16_BITS_PER_TILE = 32*8

def planes3(region_bits):
    return [2*(region_bits//3), 1*(region_bits//3), 0*(region_bits//3)]

# ------------------------------------------------------------
def write_rom(name, sym, data, comment):
    with open(OUT + name, "w") as f:
        print(f"// {comment}", file=f)
        print(f"const unsigned char {sym}[] = {{", file=f)
        for i in range(0, len(data), 16):
            print("  " + ",".join(f"0x{b:02X}" for b in data[i:i+16]) + ",", file=f)
        print("};", file=f)

def write_char_tiles(tiles):
    with open(OUT + "btime_chartiles.h", "w") as f:
        print("// Burger Time char set #1 (aa12.7k+ab13.9k+ab10.10k+ab11.12k+aa8.13k+ab9.15k)", file=f)
        print("// 1024 tile 8x8 3bpp (valori pixel 0-7). pen0 = trasparente quando il", file=f)
        print("// tilemap speciale e' attivo (m_bnj_scroll[0]&0x10), opaco altrimenti.", file=f)
        print("// Colore SEMPRE fisso a palette RAM[0..7] (color group 0, vedi btime.cpp).", file=f)
        print("const unsigned char btime_chartiles[][8][8] = {", file=f)
        rows = []
        for t in tiles:
            trows = []
            for y in range(8):
                trows.append("{" + ",".join(str(v) for v in t[y]) + "}")
            rows.append(" {" + ",".join(trows) + "}")
        print(",\n".join(rows), file=f)
        print("};", file=f)

def write_sprite_tiles(tiles):
    with open(OUT + "btime_spritetiles.h", "w") as f:
        print("// Burger Time sprites (STESSA ROM del char set #1, gfx1, granularita'", file=f)
        print("// 16x16 invece di 8x8 -- tile16layout). 256 sprite 16x16 3bpp.", file=f)
        print("// pen0 = trasparente (transpen ultimo parametro 0 in btime.cpp).", file=f)
        print("// Colore SEMPRE fisso a palette RAM[0..7] (color group 0).", file=f)
        print("const unsigned char btime_spritetiles[][16][16] = {", file=f)
        rows = []
        for t in tiles:
            trows = []
            for y in range(16):
                trows.append("{" + ",".join(str(v) for v in t[y]) + "}")
            rows.append(" {" + ",".join(trows) + "}")
        print(",\n".join(rows), file=f)
        print("};", file=f)

def write_bg_tiles(tiles):
    with open(OUT + "btime_bgtiles.h", "w") as f:
        print("// Burger Time sfondo (ab00.1b+ab01.3b+ab02.4b, gfx2, tile16layout).", file=f)
        print("// 64 tile 16x16 3bpp (valori pixel 0-7). Layer OPAQUE (mai trasparente,", file=f)
        print("// vedi gfxdecode->gfx(2)->opaque in draw_background). Colore SEMPRE", file=f)
        print("// fisso a palette RAM[8..15] (color group base 8, vedi GFXDECODE_ENTRY).", file=f)
        print("const unsigned char btime_bgtiles[][16][16] = {", file=f)
        rows = []
        for t in tiles:
            trows = []
            for y in range(16):
                trows.append("{" + ",".join(str(v) for v in t[y]) + "}")
            rows.append(" {" + ",".join(trows) + "}")
        print(",\n".join(rows), file=f)
        print("};", file=f)

def write_bg_map(data):
    write_rom("btime_bgmap.h", "btime_bgmap", data,
               "Burger Time bg_map lookup ROM (ab03.6b), 0x800 byte grezzi: "
               "4 banchi x 0x200 selezionabili da m_btime_tilemap[i&3], ogni "
               "banco indicizzato 0..0xff, valore = indice diretto in btime_bgtiles "
               "(vedi draw_background() in btime.cpp)")

def preview(char_tiles, sprite_tiles, bg_tiles, outpng):
    try:
        from PIL import Image
    except ImportError:
        print("PIL non disponibile, niente preview")
        return
    # palette di comodo SOLO per la preview (in gioco e' RAM dinamica):
    # scala di grigi 8 livelli, cosi' si vede la forma dei tile.
    def gray(v):
        g = v * 255 // 7
        return (g, g, g)

    W = 32*9
    char_rows = (len(char_tiles) + 31) // 32
    spr_rows = (len(sprite_tiles) + 15) // 16
    bg_rows = (len(bg_tiles) + 15) // 16
    H = char_rows*9 + spr_rows*18 + bg_rows*18 + 24
    img = Image.new("RGB", (W, H), (32, 32, 96))
    px = img.load()

    for t, tile in enumerate(char_tiles):
        gx, gy = (t % 32) * 9, (t // 32) * 9
        for y in range(8):
            for x in range(8):
                px[gx + x, gy + y] = gray(tile[y][x])

    base = char_rows*9 + 8
    for s, tile in enumerate(sprite_tiles):
        gx, gy = (s % 16) * 18, base + (s // 16) * 18
        for y in range(16):
            for x in range(16):
                px[gx + x, gy + y] = gray(tile[y][x])

    base2 = base + spr_rows*18 + 8
    for b, tile in enumerate(bg_tiles):
        gx, gy = (b % 16) * 18, base2 + (b // 16) * 18
        for y in range(16):
            for x in range(16):
                px[gx + x, gy + y] = gray(tile[y][x])

    img = img.resize((img.width*3, img.height*3), Image.NEAREST)
    img.save(outpng)
    print("preview:", outpng)

# ------------------------------------------------------------
def main():
    roms = {}
    for name, size, sha1 in REQUIRED:
        roms[name] = load_rom(name, size, sha1)

    os.makedirs(OUT, exist_ok=True)

    # --- gfx1 (char set #1 + sprites): 6 file concatenati = 0x6000 byte ---
    gfx1 = (roms["aa12.7k"] + roms["ab13.9k"] +
            roms["ab10.10k"] + roms["ab11.12k"] +
            roms["aa8.13k"] + roms["ab9.15k"])
    assert len(gfx1) == 0x6000
    gfx1_bits = len(gfx1) * 8
    planes_gfx1 = planes3(gfx1_bits)

    char_count = gfx1_bits // 3 // CHAR_BITS_PER_TILE
    char_tiles = [rot_galagino(t) for t in
                  mame_decode(gfx1, 8, 8, planes_gfx1, CHAR_XOFFS, CHAR_YOFFS,
                              CHAR_BITS_PER_TILE, char_count)]
    write_char_tiles(char_tiles)
    print(f"char tiles: {char_count}")

    sprite_count = gfx1_bits // 3 // TILE16_BITS_PER_TILE
    sprite_tiles = [rot_galagino(t) for t in
                    mame_decode(gfx1, 16, 16, planes_gfx1, TILE16_XOFFS, TILE16_YOFFS,
                                TILE16_BITS_PER_TILE, sprite_count)]
    write_sprite_tiles(sprite_tiles)
    print(f"sprite tiles: {sprite_count}")

    # --- gfx2 (sfondo): 3 file concatenati = 0x1800 byte ---
    gfx2 = roms["ab00.1b"] + roms["ab01.3b"] + roms["ab02.4b"]
    assert len(gfx2) == 0x1800
    gfx2_bits = len(gfx2) * 8
    planes_gfx2 = planes3(gfx2_bits)
    bg_count = gfx2_bits // 3 // TILE16_BITS_PER_TILE
    bg_tiles = [rot_galagino(t) for t in
                mame_decode(gfx2, 16, 16, planes_gfx2, TILE16_XOFFS, TILE16_YOFFS,
                            TILE16_BITS_PER_TILE, bg_count)]
    write_bg_tiles(bg_tiles)
    print(f"bg tiles: {bg_count}")

    write_bg_map(roms["ab03.6b"])

    # --- ROM CPU (rimangono CIFRATE in flash, decrittate a runtime dal core
    # m6502 patchato con l'hook fetch — vedi decocpu7.cpp) ---
    # maincpu: solo 0xc000-0xffff popolato in questo set (16KB, 4 file);
    # 0xb000-0xbfff resta a 0 (non presente nel set "btime" Data East set 1).
    maincpu = bytearray(0x1000) + roms["aa04.9b"] + roms["aa06.13b"] + roms["aa05.10b"] + roms["aa07.15b"]
    assert len(maincpu) == 0x5000  # 0xb000-0xffff
    write_rom("btime_rom_main.h", "btime_rom_main", bytes(maincpu),
              "Burger Time main CPU ROM 0xb000-0xffff (0xb000-0xbfff vuoto in questo "
              "set), CIFRATA DECO CPU-7 -- decrittazione a runtime (vedi btime.cpp "
              "fetch_cpu7), NON pre-decifrata qui perche' dipende da stato dinamico "
              "(had_written) non derivabile in fase di conversione")

    write_rom("btime_rom_audio.h", "btime_rom_audio", roms["ab14.12h"],
              "Burger Time audio CPU ROM 0xe000-0xefff (mirror 0x1000-0x1fff), NON cifrata")

    preview(char_tiles, sprite_tiles, bg_tiles, "btime_preview.png")
    print("Conversione Burger Time completata.")

if __name__ == "__main__":
    main()
