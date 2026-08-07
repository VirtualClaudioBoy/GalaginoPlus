#!/usr/bin/env python3
# Verifiche offline della logica todruaga.cpp (senza HW):
# 1. namcoio 56XX mode 8 (bootup check a CHECKSUM) contro i valori
#    documentati in MAME namcoio.cpp (superpac/motos: 7x0xF -> 6 9;
#    phozon: 1..7 -> 1 c)
# 2. mappy_tilemap_scan: la render_tiles (identica a mappy, gia' validata
#    su HW) deve toccare gli stessi idx del mapper MAME
# 3. copertura orizzontale scroll 0..255 senza buchi
# 4. vettori RESET/IRQ delle due ROM 6809 dentro le rispettive finestre
#    (controlla il layering td2_3.1d@0x8000 + td2_1.1b@0xC000)
# 5. sanity degli header generati (64 gruppi colore sprite, dimensioni)

import os, re

HERE = os.path.dirname(os.path.abspath(__file__))
ROMS = os.path.join(HERE, "../roms")
OUTD = os.path.join(HERE, "../../source/src/machines/todruaga")

# ---------------------------------------------------------------
# 1. 56XX mode 8: ram[0]=sum>>4, ram[1]=sum&0xF con sum = somma ram[9..15]
def customio56_mode8(ram):
    s = sum(ram[i] & 0x0F for i in range(9, 16))
    return [(s >> 4) & 0x0F, s & 0x0F]

ram = [0] * 16
ram[9:16] = [0xF] * 7            # superpac/motos: attesi 6 9 (0x69)
assert customio56_mode8(ram) == [0x6, 0x9], customio56_mode8(ram)
ram[9:16] = [1, 2, 3, 4, 5, 6, 7]  # phozon: attesi 1 c (0x1c)
assert customio56_mode8(ram) == [0x1, 0xC], customio56_mode8(ram)
print("1. namcoio 56XX mode 8 (checksum superpac/motos + phozon): OK")

# ---------------------------------------------------------------
# 2. tilemap scan (identico a test_mappy_logic.py: il codice C++ e' lo stesso)
def mame_scan(col, row):
    col -= 2
    if col & 0x20:
        if row & 0x20:
            return 0x7FF
        return ((row + 2) & 0x0F) + (row & 0x10) + ((col & 3) << 5) + 0x780
    return col + (row << 5)

def my_indices(row, scroll):
    colp = row - 2
    out = {}
    if row < 2 or row > 33:
        for rt in range(28):
            out[rt] = ((rt + 2) & 0x0F) + (rt & 0x10) + ((colp & 3) << 5) + 0x780
    else:
        rt0 = scroll >> 3
        for k in range(29):
            rt = (rt0 + k) % 60
            out[rt] = (rt << 5) + colp
    return out

for row in (0, 1, 34, 35):
    mine = my_indices(row, 0)
    for rt in range(28):
        assert mine[rt] == mame_scan(row, rt), (row, rt)
for row in range(2, 34):
    for scroll in range(0, 256, 17):
        for rt, idx in my_indices(row, scroll).items():
            assert idx == mame_scan(row, rt), (row, scroll, rt)
print("2. tilemap scan fisse+scorrevoli = mappy_tilemap_scan MAME: OK")

# ---------------------------------------------------------------
# 3. copertura pixel scroll
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

# ---------------------------------------------------------------
# 4. vettori 6809 dal layering ROM main (0x8000-0xFFFF) e sub (0xE000-0xFFFF)
with open(os.path.join(ROMS, "td2_3.1d"), "rb") as f: r3 = f.read()
with open(os.path.join(ROMS, "td2_1.1b"), "rb") as f: r1 = f.read()
with open(os.path.join(ROMS, "td1_4.1k"), "rb") as f: r4 = f.read()
main = r3 + r1   # 0x8000..0xFFFF
def vec(rom, base, off):
    return (rom[off - base] << 8) | rom[off - base + 1]
m_reset = vec(main, 0x8000, 0xFFFE)
m_irq   = vec(main, 0x8000, 0xFFF8)
s_reset = vec(r4, 0xE000, 0xFFFE)
s_irq   = vec(r4, 0xE000, 0xFFF8)
assert 0x8000 <= m_reset <= 0xFFFF, hex(m_reset)
assert 0x8000 <= m_irq <= 0xFFFF, hex(m_irq)
assert 0xE000 <= s_reset <= 0xFFFF, hex(s_reset)
assert 0xE000 <= s_irq <= 0xFFFF, hex(s_irq)
print(f"4. vettori: main RESET={hex(m_reset)} IRQ={hex(m_irq)}, "
      f"sub RESET={hex(s_reset)} IRQ={hex(s_irq)}: OK")

# ---------------------------------------------------------------
# 5. sanity header generati
def count_rows(path, symbol):
    txt = open(path).read()
    m = re.search(re.escape(symbol) + r"\[\]\[\d+\] = \{(.*?)\n\};", txt, re.S)
    body = m.group(1)
    depth = 0; rows = 0
    for ch in body:
        if ch == '{':
            if depth == 0: rows += 1
            depth += 1
        elif ch == '}':
            depth -= 1
    return rows

n = count_rows(os.path.join(OUTD, "todruaga_cmap.h"), "const unsigned short todruaga_colormap_sprites")
assert n == 64, f"colormap sprite: {n} gruppi != 64"
n = count_rows(os.path.join(OUTD, "todruaga_cmap.h"), "const unsigned short todruaga_colormap_tiles")
assert n == 64, f"colormap tiles: {n} gruppi != 64"
nums = re.findall(r"0x[0-9A-Fa-f]+|\d+", open(os.path.join(OUTD, "todruaga_rom_main.h")).read().split("{",1)[1])
assert len(nums) == 0x8000, f"rom_main: {len(nums)} byte != 32768"
nums = re.findall(r"0x[0-9A-Fa-f]+|\d+", open(os.path.join(OUTD, "todruaga_rom_sub.h")).read().split("{",1)[1])
assert len(nums) == 0x2000, f"rom_sub: {len(nums)} byte != 8192"
print("5. header: 64 gruppi sprite/tiles, rom_main 32KB, rom_sub 8KB: OK")

# hiscore: regioni dentro la work RAM (< 0x2800)
for base, ln in ((0x102A, 0x32), (0x100B, 0x03)):
    assert base + ln <= 0x2800
print("6. regioni hiscore (102a/32, 100b/3) dentro la work RAM: OK")

print("\nTUTTI I TEST PASSATI")
