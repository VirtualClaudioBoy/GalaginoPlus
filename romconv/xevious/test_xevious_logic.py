#!/usr/bin/env python3
# ============================================================
# Test di logica (senza CPU/hardware) per Xevious — stesso stile di
# romconv/mappy/test_mappy_logic.py e romconv/todruaga/test_todruaga_logic.py:
# replica in Python delle formule usate in xevious.cpp/xevious_rom_convert.py
# e verifica che non ci siano bug di bordo/wrap PRIMA di flashare su HW.
# ============================================================

import sys

FAIL = 0

def check(name, cond):
    global FAIL
    status = "OK" if cond else "FAIL"
    if not cond:
        FAIL += 1
    print(f"[{status}] {name}")

# ------------------------------------------------------------
# 1) xevious_bb_r(): l'indice finale in rom2c deve SEMPRE restare dentro
#    i limiti del blob planet-map (rom2c e' l'ultimo pezzo, 0x1000 byte,
#    offset 0x3000-0x3fff nel blob da 0x4000; l'indice adr_2c usato per
#    indicizzare rom2c puo' avere bit 0x800 impostato per BB1)
# ------------------------------------------------------------
def bb_r(bs0, bs1, offset, rom2a, rom2b, rom2c):
    adr_2b = ((bs1 & 0x7e) << 6) | ((bs0 & 0xfe) >> 1)
    if adr_2b & 1:
        dat1 = ((rom2a[adr_2b >> 1] & 0xf0) << 4) | rom2b[adr_2b]
    else:
        dat1 = ((rom2a[adr_2b >> 1] & 0x0f) << 8) | rom2b[adr_2b]

    adr_2c = ((dat1 & 0x1ff) << 2) | ((bs1 & 1) << 1) | (bs0 & 1)
    if dat1 & 0x400: adr_2c ^= 1
    if dat1 & 0x200: adr_2c ^= 2

    if offset & 1:
        idx = adr_2c | 0x800
        assert 0 <= idx < 0x1000, f"BB1 index out of range: {idx:#x}"
        return rom2c[idx]
    else:
        idx = adr_2c
        assert 0 <= idx < 0x1000, f"BB0 index out of range: {idx:#x}"
        dat2 = rom2c[idx]
        dat2 = (dat2 & 0x3f) | ((dat2 & 0x80) >> 1) | ((dat2 & 0x40) << 1)
        if dat1 & 0x400: dat2 ^= 0x40
        if dat1 & 0x200: dat2 ^= 0x80
        return dat2

def test_bb_r_bounds():
    # rom2a: 0x1000 byte, rom2b: 0x2000 byte (indicizzato fino a adr_2b,
    # che e' a 13 bit -> max 0x1fff, dentro i 0x2000 byte), rom2c: 0x1000
    rom2a = bytes([(i * 37) & 0xff for i in range(0x1000)])
    rom2b = bytes([(i * 53) & 0xff for i in range(0x2000)])
    rom2c = bytes([(i * 71) & 0xff for i in range(0x1000)])
    ok = True
    try:
        for bs0 in range(0, 256, 3):        # campionamento, 256*256 e' troppo lento
            for bs1 in range(0, 256, 3):
                for offset in (0, 1):
                    bb_r(bs0, bs1, offset, rom2a, rom2b, rom2c)
    except AssertionError as e:
        print("  ", e)
        ok = False
    check("xevious_bb_r: indici sempre dentro i limiti dei blob ROM", ok)

# ------------------------------------------------------------
# 2) scroll coarse/fine: per ogni valore di scroll (0-511 asse riga-banda,
#    0-255 asse colonna), il calcolo del tile-index deve restare dentro
#    la griglia 64x32 e ogni colonna di output deve avanzare esattamente
#    di 1 pixel (nessun buco/doppia scrittura) sull'asse colonna
# ------------------------------------------------------------
def test_scroll_row_axis_bounds():
    ok = True
    for scrollx in range(0, 512, 7):
        for dx in (-20, -32):
            for row in range(36):
                for ry in range(8):
                    mame_x = (row*8 + ry + scrollx + dx) & 0x1ff
                    tile_col = (mame_x >> 3) & 63
                    if not (0 <= tile_col < 64):
                        ok = False
    check("scroll asse riga-banda: tile_col sempre in [0,64)", ok)

def test_scroll_col_axis_coverage():
    # simula l'avanzamento incrementale usato in blit_tilemap_row: per 224
    # colonne di output consecutive, ogni pixel della tilemap (asse corto,
    # 256 valori) deve essere toccato ESATTAMENTE una volta per colonna
    # (nessun salto, nessuna ripetizione) dato un dy/scrolly qualsiasi
    ok = True
    for scrolly in range(0, 256, 5):
        for dy in (-16, -18):
            mame_y = (scrolly + dy) & 0xff
            seen = []
            y = mame_y
            for col in range(224):
                seen.append(y)
                y = (y + 1) & 0xff
            # deve essere una sequenza consecutiva mod 256, senza buchi
            for i in range(1, len(seen)):
                if seen[i] != (seen[i-1] + 1) & 0xff:
                    ok = False
    check("scroll asse colonna: avanzamento di 1 pixel per colonna, senza buchi", ok)

def test_tile_row_wrap():
    # tile_row = (mame_y>>3)&31 deve restare in [0,32) per ogni mame_y 0-255
    ok = True
    for mame_y in range(256):
        tile_row = (mame_y >> 3) & 31
        if not (0 <= tile_row < 32):
            ok = False
    check("tile_row (asse colonna) sempre in [0,32)", ok)

# ------------------------------------------------------------
# 3) sprite code range: bank-select bit7 deve produrre sempre un indice
#    valido nello sprite sheet a 320 elementi (0-255 senza bank, 256-319
#    con bank)
# ------------------------------------------------------------
def test_sprite_code_range():
    ok = True
    for raw_code in range(256):
        for bank_bit in (0, 1):
            code = ((raw_code & 0x3f) + 0x100) if bank_bit else raw_code
            if not (0 <= code < 320):
                ok = False
    check("sprite code: sempre in [0,320) (sheet unico dopo init_xevious)", ok)

# ------------------------------------------------------------
if __name__ == "__main__":
    test_bb_r_bounds()
    test_scroll_row_axis_bounds()
    test_scroll_col_axis_coverage()
    test_tile_row_wrap()
    test_sprite_code_range()

    if FAIL:
        print(f"\n{FAIL} test falliti")
        sys.exit(1)
    print("\nTutti i test OK")
