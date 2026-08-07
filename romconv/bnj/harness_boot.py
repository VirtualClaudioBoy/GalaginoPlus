#!/usr/bin/env python3
# Harness di verifica offline (pip install py65) per il boot del main CPU
# DECO C10707 di Bump'n'Jump. Replica main_read/main_write di bnj.cpp
# (mappa memoria) per controllare che il boot proceda in modo plausibile
# PRIMA di consegnare per il flash reale -- STESSO metodo di Burger Time
# (harness_boot.py): decrittazione applicata SOLO al primo byte letto per
# ogni istruzione (l'opcode), MAI agli operandi -- esattamente come
# SPLIT_DECRYPTION in deco222.cpp (decrypt8(value,pc,opcode): solo se
# opcode==true). Bug reale trovato durante lo sviluppo di QUESTO harness:
# una prima versione decifrava l'INTERA ROM uniformemente (operandi
# inclusi), facendo leggere target di JSR/JMP sbagliati (es. "JSR $BCD7"
# che punta in zona ROM vuota 0xFF) -- STESSO tipo di errore concettuale
# gia' visto per CPU-7, ma qui C10707 e' incondizionata quindi e' facile
# dimenticarsi che la separazione fetch/dato serve comunque.
from py65.devices.mpu6502 import MPU

ROMS = "../roms/"

def load(name):
    with open(ROMS + name, "rb") as f:
        return f.read()

maincpu = load("bnj12b.bin") + load("bnj12c.bin") + load("bnj12d.bin")
assert len(maincpu) == 0x6000  # 0xa000-0xffff

def bitswap_c10707(v):
    return (v & 0x9F) | ((v & 0x20) << 1) | ((v & 0x40) >> 1)

class Mem:
    def __init__(self):
        self.ram = bytearray(0x800)
        self.video = bytearray(0x400)
        self.color = bytearray(0x400)
        self.bg = bytearray(0x400)
        self.palette = bytearray(16)
        self.pending_fetch_addr = None
        self.write_log = []
        self.dsw_read_count = 0

    def xy_swap(self, off):
        x, y = off // 32, off % 32
        return 32*y + x

    def raw_read(self, addr):
        if addr >= 0xa000: return maincpu[addr - 0xa000]
        if addr < 0x0800: return self.ram[addr]
        if addr == 0x1000:
            self.dsw_read_count += 1
            vbl = (self.dsw_read_count >> 3) & 1
            return 0x3f | (vbl << 7)
        if addr == 0x1001: return 0x17
        if addr == 0x1002: return 0xff  # P1 nessun tasto
        if addr == 0x1003: return 0xff  # P2
        if addr == 0x1004: return 0xff  # SYSTEM nessuna moneta/start
        if 0x4000 <= addr < 0x4400: return self.video[addr-0x4000]
        if 0x4400 <= addr < 0x4800: return self.color[addr-0x4400]
        if 0x4800 <= addr < 0x4c00: return self.video[self.xy_swap(addr-0x4800)]
        if 0x4c00 <= addr < 0x5000: return self.color[self.xy_swap(addr-0x4c00)]
        if 0x5000 <= addr < 0x5400: return self.bg[addr-0x5000]
        return 0xff

    def __getitem__(self, addr):
        addr &= 0xffff
        val = self.raw_read(addr)
        if self.pending_fetch_addr == addr:
            self.pending_fetch_addr = None
            if addr >= 0xa000:
                val = bitswap_c10707(val)
        return val

    def __setitem__(self, addr, val):
        addr &= 0xffff
        val &= 0xff
        self.write_log.append((addr, val))
        if addr < 0x0800: self.ram[addr] = val
        elif 0x4000 <= addr < 0x4400: self.video[addr-0x4000] = val
        elif 0x4400 <= addr < 0x4800: self.color[addr-0x4400] = val
        elif 0x4800 <= addr < 0x4c00: self.video[self.xy_swap(addr-0x4800)] = val
        elif 0x4c00 <= addr < 0x5000: self.color[self.xy_swap(addr-0x4c00)] = val
        elif 0x5000 <= addr < 0x5400: self.bg[addr-0x5000] = val
        elif 0x5c00 <= addr <= 0x5c0f: self.palette[addr-0x5c00] = val

mem = Mem()
mpu = MPU(memory=mem, pc=None)  # pc=None forza la lettura del vero reset vector (0xFFFC)
mpu.reset()
print(f"reset vector = {mpu.pc:04x}")

STEPS = 300000
pc_hist = []
max_pc_seen = 0
for i in range(STEPS):
    pc = mpu.pc
    mem.pending_fetch_addr = pc
    try:
        mpu.step()
    except Exception as e:
        print(f"CRASH a step {i}, pc={pc:04x}: {e}")
        break
    pc_hist.append(pc)
    if pc > max_pc_seen:
        max_pc_seen = pc

print(f"eseguiti {len(pc_hist)} step, PC finale={mpu.pc:04x}, max_pc={max_pc_seen:04x}")
print(f"cicli CPU totali: {mpu.processorCycles}")
print(f"scritture totali in memoria: {len(mem.write_log)}")
print(f"video non-zero: {sum(1 for b in mem.video if b)}/{len(mem.video)}")
print(f"color non-zero: {sum(1 for b in mem.color if b)}/{len(mem.color)}")
print(f"bg non-zero: {sum(1 for b in mem.bg if b)}/{len(mem.bg)}")
print("ultimi 32 PC:", [f"{p:04x}" for p in pc_hist[-32:]])

from collections import Counter
c = Counter(pc_hist)
print("PC piu' frequenti:", [(f"{p:04x}", n) for p, n in c.most_common(10)])
