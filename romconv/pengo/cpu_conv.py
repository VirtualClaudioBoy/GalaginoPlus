# -*- coding: utf-8 -*-
"""
Converte le ROM CPU di Pengo (set MAME "pengoja", cifrato Sega 315-5010)
in un header C con DUE array:
  - pengo_rom    : byte decifrati con la tabella DATA  (letture rdZ80)
  - pengo_rom_op : byte decifrati con la tabella OPCODE (fetch M1, opZ80)

Algoritmo e tabella: MAME src/mame/machine/segacrpt_device.cpp
(sega_315_5010_device::decrypt + decode generica).
La cifratura agisce solo sui bit 7,5,3 (maschera 0xA8):
  row = A0 + A4<<1 + A8<<2 + A12<<3 (bit dell'indirizzo)
  col = D3 + D5<<1 dal byte sorgente; se D7=1: col = 3-col e xorval = 0xA8
  out = (src & ~0xA8) | (convtable[riga][col] ^ xorval)
"""

# Set pengoja ("Pengo (Japan, 315-5010 type, rev C)") — nomi file del vecchio
# romset MAME "pengo.zip", CRC verificati contro il driver MAME attuale.
INPUT_FILES = [
    "../roms/ep1689c.8",    # 0000-0fff
    "../roms/ep1690b.7",    # 1000-1fff
    "../roms/ep1691b.15",   # 2000-2fff
    "../roms/ep1692b.14",   # 3000-3fff
    "../roms/ep1693b.21",   # 4000-4fff
    "../roms/ep1694b.20",   # 5000-5fff
    "../roms/ep5118b.32",   # 6000-6fff
    "../roms/ep5119c.31",   # 7000-7fff
]
OUTPUT_FILE = "../../source/src/machines/pengo/pengo_rom.h"

# Sega 315-5010: 16 righe indirizzo x { tabella opcode, tabella data }
# (da MAME segacrpt_device.cpp, trascritta 1:1)
CONVTABLE = [
    #  opcode                    data                     A12.A8.A4.A0
    [0xa0, 0x80, 0xa8, 0x88], [0x28, 0xa8, 0x08, 0x88],  # ...0...0...0...0
    [0x28, 0xa8, 0x08, 0x88], [0xa0, 0x80, 0xa8, 0x88],  # ...0...0...0...1
    [0xa0, 0x80, 0x20, 0x00], [0xa0, 0x80, 0x20, 0x00],  # ...0...0...1...0
    [0x08, 0x28, 0x88, 0xa8], [0xa0, 0x80, 0xa8, 0x88],  # ...0...0...1...1
    [0x08, 0x00, 0x88, 0x80], [0x28, 0xa8, 0x08, 0x88],  # ...0...1...0...0
    [0xa0, 0x80, 0x20, 0x00], [0x08, 0x00, 0x88, 0x80],  # ...0...1...0...1
    [0xa0, 0x80, 0x20, 0x00], [0xa0, 0x80, 0x20, 0x00],  # ...0...1...1...0
    [0xa0, 0x80, 0x20, 0x00], [0x00, 0x08, 0x20, 0x28],  # ...0...1...1...1
    [0x88, 0x80, 0x08, 0x00], [0xa0, 0x80, 0x20, 0x00],  # ...1...0...0...0
    [0x88, 0x80, 0x08, 0x00], [0x00, 0x08, 0x20, 0x28],  # ...1...0...0...1
    [0x08, 0x28, 0x88, 0xa8], [0x08, 0x28, 0x88, 0xa8],  # ...1...0...1...0
    [0xa0, 0x80, 0xa8, 0x88], [0xa0, 0x80, 0x20, 0x00],  # ...1...0...1...1
    [0x08, 0x00, 0x88, 0x80], [0x88, 0x80, 0x08, 0x00],  # ...1...1...0...0
    [0x00, 0x08, 0x20, 0x28], [0x88, 0x80, 0x08, 0x00],  # ...1...1...0...1
    [0x08, 0x28, 0x88, 0xa8], [0x08, 0x28, 0x88, 0xa8],  # ...1...1...1...0
    [0x08, 0x00, 0x88, 0x80], [0xa0, 0x80, 0x20, 0x00],  # ...1...1...1...1
]


def sega_decode(rom):
    """Ritorna (data, opcodes) decifrati, come decode() di MAME."""
    data = bytearray(len(rom))
    opcodes = bytearray(len(rom))
    for a in range(len(rom)):
        src = rom[a]
        row = (a & 1) + (((a >> 4) & 1) << 1) + (((a >> 8) & 1) << 2) + (((a >> 12) & 1) << 3)
        col = ((src >> 3) & 1) + (((src >> 5) & 1) << 1)
        xorval = 0
        if src & 0x80:
            col = 3 - col
            xorval = 0xa8
        opcodes[a] = (src & ~0xa8 & 0xFF) | (CONVTABLE[2 * row][col] ^ xorval)
        data[a] = (src & ~0xa8 & 0xFF) | (CONVTABLE[2 * row + 1][col] ^ xorval)
    return data, opcodes


def write_array(f, name, rom):
    f.write(f"const unsigned char {name}[] = {{\n  ")
    for i, byte in enumerate(rom):
        f.write(f"0x{byte:02X}")
        if i < len(rom) - 1:
            f.write(",")
            if (i + 1) % 16 == 0:
                f.write("\n  ")
            else:
                f.write(" ")
    f.write("\n};\n\n")


if __name__ == "__main__":
    print("Pengo (set pengoja, Sega 315-5010): lettura e decifratura ROM CPU...")
    encrypted = bytearray()
    for filename in INPUT_FILES:
        with open(filename, "rb") as f:
            print(f"  - {filename}")
            encrypted.extend(f.read())

    if len(encrypted) != 0x8000:
        raise SystemExit(f"ERRORE: attesi 32768 byte, letti {len(encrypted)}")

    data, opcodes = sega_decode(encrypted)

    with open(OUTPUT_FILE, "w") as f:
        f.write("// File generato da cpu_conv.py\n")
        f.write("// Pengo (Japan, 315-5010 type, rev C) - set MAME 'pengoja'\n")
        f.write("// ROM CPU decifrate (Sega 315-5010):\n")
        f.write("//   pengo_rom    = tabella DATA   (letture memoria, rdZ80)\n")
        f.write("//   pengo_rom_op = tabella OPCODE (fetch M1, opZ80)\n\n")
        write_array(f, "pengo_rom", data)
        write_array(f, "pengo_rom_op", opcodes)

    print(f"OK: scritto {OUTPUT_FILE} (2 array da {len(data)} byte)")
