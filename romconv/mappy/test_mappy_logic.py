#!/usr/bin/env python3
# Verifiche offline della logica mappy.cpp (senza HW):
# 1. namcoio 58XX mode 5 (bootup check LFSR) contro i valori documentati
#    in MAME namcoio.cpp: mappy scrive 9-15 = 3 6 5 f a c e e si aspetta
#    ram[1..7] = 8 4 6 e d 9 d (il gioco non parte se sbaglia)
# 2. mappy_tilemap_scan: la mia render_tiles deve toccare gli stessi idx
#    del mapper MAME per ogni (colonna hw, riga hw) visibile
# 3. copertura orizzontale dello scroll: per ogni scroll 0..255 i 29 tile
#    devono coprire tutti i 224 pixel senza buchi

def NEXT(n):
    return ((n ^ 0x90) if (n & 1) else n) >> 1

def customio_mode5(ram):
    # replica esatta di mappy.cpp customio_run case 5
    out = [0] * 8
    n = ((ram[9] & 0x0F) * 16 + (ram[10] & 0x0F)) & 0x7F
    seed = 0x22
    for _ in range(n):
        seed = NEXT(seed)
    for i in range(1, 8):
        n = 0
        rng = seed
        if rng & 1: n ^= ~(ram[11] & 0x0F)
        rng = NEXT(rng)
        seed = rng
        if rng & 1: n ^= ~(ram[10] & 0x0F)
        rng = NEXT(rng)
        if rng & 1: n ^= ~(ram[9] & 0x0F)
        rng = NEXT(rng)
        if rng & 1: n ^= ~(ram[15] & 0x0F)
        rng = NEXT(rng)
        if rng & 1: n ^= ~(ram[14] & 0x0F)
        rng = NEXT(rng)
        if rng & 1: n ^= ~(ram[13] & 0x0F)
        rng = NEXT(rng)
        if rng & 1: n ^= ~(ram[12] & 0x0F)
        out[i] = (~n) & 0x0F
    out[0] = 0x0F if (ram[9] & 0x0F) == 0x0F else 0x00
    return out

ram = [0] * 16
ram[9:16] = [3, 6, 5, 0xF, 0xA, 0xC, 0xE]
got = customio_mode5(ram)
expected = [0x0, 0x8, 0x4, 0x6, 0xE, 0xD, 0x9, 0xD]
assert got == expected, f"mode5 mappy: {[hex(x) for x in got]} != {[hex(x) for x in expected]}"
print("1. namcoio 58XX mode 5 (bootup check mappy): OK", [hex(x) for x in got])

# grobda: 9-15 = 2 3 4 5 6 7 8, expects ram[2]=f e ram[6]=c
ram[9:16] = [2, 3, 4, 5, 6, 7, 8]
got = customio_mode5(ram)
assert got[2] == 0xF and got[6] == 0xC, f"mode5 grobda: {[hex(x) for x in got]}"
print("   controprova grobda (ram[2]=f, ram[6]=c): OK")

# ---------------------------------------------------------------
def mame_scan(col, row):
    # TILEMAP_MAPPER mappy_tilemap_scan (col 0..35, row 0..59)
    col -= 2
    if col & 0x20:
        if row & 0x20:
            return 0x7FF
        return ((row + 2) & 0x0F) + (row & 0x10) + ((col & 3) << 5) + 0x780
    return col + (row << 5)

# la mia render_tiles: strip galagino r = colonna hw ct = r
def my_indices(row, scroll):
    colp = row - 2
    out = {}
    if row < 2 or row > 33:
        for rt in range(28):
            idx = ((rt + 2) & 0x0F) + (rt & 0x10) + ((colp & 3) << 5) + 0x780
            out[rt] = idx
    else:
        rt0 = scroll >> 3
        for k in range(29):
            rt = (rt0 + k) % 60
            out[rt] = (rt << 5) + colp
    return out

# 2a. colonne fisse: idx identici al mapper MAME per righe visibili 0..27
for row in (0, 1, 34, 35):
    mine = my_indices(row, 0)
    for rt in range(28):
        assert mine[rt] == mame_scan(row, rt), (row, rt, hex(mine[rt]), hex(mame_scan(row, rt)))
print("2. colonne fisse: idx = mappy_tilemap_scan MAME per tutte le righe: OK")

# 2b. colonne scorrevoli: per ogni scroll, ogni riga hw toccata deve avere
#     l'idx del mapper MAME
for row in range(2, 34):
    for scroll in range(0, 256, 17):
        mine = my_indices(row, scroll)
        for rt, idx in mine.items():
            assert idx == mame_scan(row, rt), (row, scroll, rt)
print("2b. colonne scorrevoli: idx = mapper MAME per ogni scroll: OK")

# 3. copertura pixel: x_block = 216 - 8k + fine, tile largo 8 -> ogni
#    pixel 0..223 coperto esattamente una volta
for scroll in range(256):
    fine = scroll & 7
    cover = [0] * 224
    for k in range(29):
        x = 216 - 8 * k + fine
        for c in range(8):
            if 0 <= x + c < 224:
                cover[x + c] += 1
    assert all(c == 1 for c in cover), f"scroll {scroll}: buchi/sovrapposizioni"
print("3. copertura scroll 0..255: 224 px coperti esattamente una volta: OK")

# 4. coerenza pixel: tile rt sullo schermo deve mostrare la riga hw
#    mameY = screenY + scroll (mod 480), con gal_x = 223 - screenY
for scroll in range(0, 256, 31):
    fine = scroll & 7
    rt0 = scroll >> 3
    for k in range(29):
        x_block = 216 - 8 * k + fine
        rt = (rt0 + k) % 60
        for c in range(8):
            gal_x = x_block + c
            if not (0 <= gal_x < 224):
                continue
            screenY = 223 - gal_x
            mameY = (screenY + scroll) % 480
            # il pixel c del tile (LSB-first = gal_x crescente) e' il pixel
            # mame_y_off = 7 - c della riga hw rt (converter: gal_x = 7 - mame_row_pixel)
            assert mameY // 8 == rt, (scroll, k, c, mameY // 8, rt)
            assert mameY % 8 == 7 - c, (scroll, k, c)
print("4. mapping pixel gal_x <-> mameY con scroll: OK")

print("\nTUTTI I TEST PASSATI")
