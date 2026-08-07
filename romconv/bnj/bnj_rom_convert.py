#!/usr/bin/env python3
# ============================================================
# Bump'n'Jump (Data East 1982, set "bnjm" / Bally Midway license) ROM
# converter per galagino29-main
#
# Autonomo (stile btime_rom_convert.py): legge ../roms/, scrive
# ../../source/src/machines/bnj/*.h
#
# Riferimento MAME MODERNO E:\Download\btime.cpp (stesso driver di Burger
# Time, classe btime_state, machine_config bnj(), ROM_START(bnjm)), letto
# per intero per le sezioni bnj-specifiche (bnj_map, INPUT_PORTS_START(bnj),
# bnj(), gfx_bnj/bnj_tile16layout, VIDEO_START_MEMBER(bnj),
# screen_update_bnj, init_bnj). NON usato mame4all.
#
# Set convertito: "bnjm" (Bump'n'Jump, Bally Midway license) -- 9 file di
# romszip/bnj.zip (senza i 2 PLD) verificati CRC32 esatti (unzip -v) contro
# ROM_START(bnjm) in btime.cpp righe 2915-2932. SHA1 sotto calcolato sui
# file unpacked (../roms/), usato come verifica di integrita' propria.
# ============================================================

import hashlib, os

ROMS = "../roms/"
OUT = "../../source/src/machines/bnj/"

REQUIRED = [
    ("bnj12b.bin", 0x2000, "56284076d938c33c1492a07281b936681eb09808"),  # maincpu @0xa000 (CIFRATA C10707)
    ("bnj12c.bin", 0x2000, "4a964389cc8035b9264d4cb133eb6d3826e74b95"),  # maincpu @0xc000
    ("bnj12d.bin", 0x2000, "08a4ddea4037f9e14d0d9f4262a1746b0a3a140c"),  # maincpu @0xe000
    ("bnj6c.bin",  0x1000, "1279d564e65fd3ccac25b1f9fbb40d910de2b544"),  # audiocpu @0xe000 (NON cifrata)
    ("bnj4e.bin",  0x2000, "cacf71fa6c0f7121d077381a0ff6222f534295ab"),  # gfx1 third0 (low)
    ("bnj4f.bin",  0x2000, "5e52554f594f569527af4768d244cc40a7b4460a"),  # gfx1 third1 (mid)
    ("bnj4h.bin",  0x2000, "e98f0eb476b8f033f5cc70a6e503afc4e651fd45"),  # gfx1 third2 (high)
    ("bnj10e.bin", 0x1000, "b356512d2ebd4e2005e76496b434e5ecebadb251"),  # gfx2 half0
    ("bnj10f.bin", 0x1000, "49d5f9c0b695f474197fbb761bacc065b6b5808a"),  # gfx2 half1
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
# assoluti), STESSO decoder di btime_rom_convert.py
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

def rot_galagino(tile):
    n = len(tile)
    return [[tile[n - 1 - x][y] for x in range(n)] for y in range(n)]

# ------------------------------------------------------------
# Layout MAME (btime.cpp righe 2087-2130, IDENTICI a btime per char/sprite:
# gfx1 di bnj e' la STESSA disposizione fisica di btime, solo contenuto
# artistico diverso -- vedi gfx_bnj righe 2149-2152: usa gfx_8x8x3_planar
# per i char e tile16layout per gli sprite, esattamente come gfx_btime).
CHAR_XOFFS = [0,1,2,3,4,5,6,7]
CHAR_YOFFS = [y*8 for y in range(8)]
CHAR_BITS_PER_TILE = 8*8

TILE16_XOFFS = [16*8+x for x in range(8)] + [x for x in range(8)]
TILE16_YOFFS = [y*8 for y in range(16)]
TILE16_BITS_PER_TILE = 32*8

def planes3(region_bits):
    return [2*(region_bits//3), 1*(region_bits//3), 0*(region_bits//3)]

# bnj_tile16layout (btime.cpp righe 2121-2130), NUOVO in questo progetto,
# usato SOLO per lo sfondo scrollabile di bnj (regione gfx2, 0x2000 byte =
# bnj10e.bin+bnj10f.bin, ciascuno 0x1000):
#
#   bnj_tile16layout = { 16,16, RGN_FRAC(1,2), 3,
#     { RGN_FRAC(1,2)+4, RGN_FRAC(0,2)+0, RGN_FRAC(0,2)+4 },
#     { STEP4(3*16*8,1), STEP4(2*16*8,1), STEP4(1*16*8,1), STEP4(0*16*8,1) },
#     { STEP16(0,8) }, 64*8 }
#
# planeoffset: plane0 = meta'-alta regione (bnj10f) NIBBLE ALTO (+4);
# plane1 = meta'-bassa regione (bnj10e) NIBBLE BASSO (+0); plane2 =
# meta'-bassa regione (bnj10e) NIBBLE ALTO (+4). Il nibble basso di
# bnj10f (meta'-alta) NON e' usato da questo layout (3bpp, non 4bpp).
# xoffset: 4 gruppi da 4 colonne, in ordine INVERTITO (col 12-15, poi 8-11,
# poi 4-7, poi 0-3) -- ogni gruppo di 4 pixel adiacenti condivide lo stesso
# nibble ma bit diversi (STEP4 con incremento 1 = 4 bit consecutivi nello
# stesso nibble). charincrement 64*8=512 bit = 64 byte/tile.
def bnj_bg_layout_decode(gfx2):
    total_bits = len(gfx2) * 8
    half_bits = total_bits // 2
    planes = [half_bits + 4, 0 + 0, 0 + 4]
    xoffs = ([3*16*8 + i for i in range(4)] +
             [2*16*8 + i for i in range(4)] +
             [1*16*8 + i for i in range(4)] +
             [0*16*8 + i for i in range(4)])
    yoffs = [y*8 for y in range(16)]
    bits_per_tile = 64*8
    count = half_bits // bits_per_tile
    return mame_decode(gfx2, 16, 16, planes, xoffs, yoffs, bits_per_tile, count), count

# ------------------------------------------------------------
def write_rom(name, sym, data, comment):
    with open(OUT + name, "w") as f:
        print(f"// {comment}", file=f)
        print(f"const unsigned char {sym}[] = {{", file=f)
        for i in range(0, len(data), 16):
            print("  " + ",".join(f"0x{b:02X}" for b in data[i:i+16]) + ",", file=f)
        print("};", file=f)

def write_char_tiles(tiles):
    with open(OUT + "bnj_chartiles.h", "w") as f:
        print("// Bump'n'Jump char set #1 (bnj4e.bin+bnj4f.bin+bnj4h.bin, gfx1).", file=f)
        print("// 1024 tile 8x8 3bpp (valori pixel 0-7). Colore SEMPRE fisso a", file=f)
        print("// palette RAM[0..7] (color group 0, vedi gfx_bnj in btime.cpp).", file=f)
        print("const unsigned char bnj_chartiles[][8][8] = {", file=f)
        rows = []
        for t in tiles:
            trows = ["{" + ",".join(str(v) for v in t[y]) + "}" for y in range(8)]
            rows.append(" {" + ",".join(trows) + "}")
        print(",\n".join(rows), file=f)
        print("};", file=f)

def write_sprite_tiles(tiles):
    with open(OUT + "bnj_spritetiles.h", "w") as f:
        print("// Bump'n'Jump sprites (STESSA ROM del char set #1, gfx1, granularita'", file=f)
        print("// 16x16 -- tile16layout). 256 sprite 16x16 3bpp.", file=f)
        print("// pen0 = trasparente (transpen ultimo parametro 0 in draw_sprites).", file=f)
        print("const unsigned char bnj_spritetiles[][16][16] = {", file=f)
        rows = []
        for t in tiles:
            trows = ["{" + ",".join(str(v) for v in t[y]) + "}" for y in range(16)]
            rows.append(" {" + ",".join(trows) + "}")
        print(",\n".join(rows), file=f)
        print("};", file=f)

def write_bg_tiles(tiles):
    with open(OUT + "bnj_bgtiles.h", "w") as f:
        print("// Bump'n'Jump sfondo scrollabile (bnj10e.bin+bnj10f.bin, gfx2,", file=f)
        print("// bnj_tile16layout NUOVO -- nibble packing, vedi commenti nel", file=f)
        print("// converter). 16x16 3bpp, layer OPAQUE. Colore SEMPRE fisso a", file=f)
        print("// palette RAM[8..15] (color group base 8, come btime).", file=f)
        print("const unsigned char bnj_bgtiles[][16][16] = {", file=f)
        rows = []
        for t in tiles:
            trows = ["{" + ",".join(str(v) for v in t[y]) + "}" for y in range(16)]
            rows.append(" {" + ",".join(trows) + "}")
        print(",\n".join(rows), file=f)
        print("};", file=f)

def preview(char_tiles, sprite_tiles, bg_tiles, outpng):
    try:
        from PIL import Image
    except ImportError:
        print("PIL non disponibile, niente preview")
        return

    # Preview con colori DISTINTI per valore pixel (NON scala di grigi
    # lineare) -- lezione da btime (bring-up #13): una scala lineare
    # nasconde contenuto a bassa intensita'. Palette di comodo SOLO per la
    # preview (in gioco e' RAM dinamica).
    PALETTE = [(20,20,20),(200,60,60),(60,200,60),(60,60,200),
               (200,200,60),(200,60,200),(60,200,200),(230,230,230)]
    def col(v):
        return PALETTE[v & 7]

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
                px[gx + x, gy + y] = col(tile[y][x])

    base = char_rows*9 + 8
    for s, tile in enumerate(sprite_tiles):
        gx, gy = (s % 16) * 18, base + (s // 16) * 18
        for y in range(16):
            for x in range(16):
                px[gx + x, gy + y] = col(tile[y][x])

    base2 = base + spr_rows*18 + 8
    for b, tile in enumerate(bg_tiles):
        gx, gy = (b % 16) * 18, base2 + (b // 16) * 18
        for y in range(16):
            for x in range(16):
                px[gx + x, gy + y] = col(tile[y][x])

    img = img.resize((img.width*3, img.height*3), Image.NEAREST)
    img.save(outpng)
    print("preview:", outpng)

# ------------------------------------------------------------
def main():
    roms = {}
    for name, size, sha1 in REQUIRED:
        roms[name] = load_rom(name, size, sha1)

    os.makedirs(OUT, exist_ok=True)

    # --- gfx1 (char set #1 + sprites): 3 file concatenati = 0x6000 byte,
    # STESSA dimensione/layout di btime, solo contenuto diverso ---
    gfx1 = roms["bnj4e.bin"] + roms["bnj4f.bin"] + roms["bnj4h.bin"]
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

    # --- gfx2 (sfondo scrollabile): 2 file concatenati = 0x2000 byte,
    # bnj_tile16layout NUOVO (nibble packing) ---
    gfx2 = roms["bnj10e.bin"] + roms["bnj10f.bin"]
    assert len(gfx2) == 0x2000
    bg_tiles_raw, bg_count = bnj_bg_layout_decode(gfx2)
    bg_tiles = [rot_galagino(t) for t in bg_tiles_raw]
    write_bg_tiles(bg_tiles)
    print(f"bg tiles: {bg_count}")

    # --- ROM CPU (rimangono CIFRATE in flash: DECO C10707 e' STATICA,
    # decifrata a runtime dal core m6502 patchato via hook fetch, stesso
    # meccanismo di btime/CPU-7 ma bitswap fisso senza stato) ---
    maincpu = roms["bnj12b.bin"] + roms["bnj12c.bin"] + roms["bnj12d.bin"]
    assert len(maincpu) == 0x6000  # 0xa000-0xffff
    write_rom("bnj_rom_main.h", "bnj_rom_main", maincpu,
              "Bump'n'Jump main CPU ROM 0xa000-0xffff (24KB), CIFRATA DECO "
              "C10707 (bitswap statico bit5<->bit6 su OGNI fetch opcode, "
              "NESSUNO stato -- diverso dalla CPU-7 dinamica di Burger Time) "
              "-- decrittazione a runtime, vedi bnj.cpp fetch_c10707")

    write_rom("bnj_rom_audio.h", "bnj_rom_audio", roms["bnj6c.bin"],
               "Bump'n'Jump audio CPU ROM 0xe000-0xefff (mirror 0x1000-0x1fff), "
               "NON cifrata, STESSA identica infrastruttura audio di Burger Time "
               "(2x AY-3-8910, stesso audio_map, stesso meccanismo NMI+IRQ)")

    preview(char_tiles, sprite_tiles, bg_tiles, "bnj_preview.png")
    print("Conversione Bump'n'Jump (ROM/gfx) completata.")

if __name__ == "__main__":
    main()
