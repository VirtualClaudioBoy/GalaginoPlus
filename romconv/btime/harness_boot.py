#!/usr/bin/env python3
# Harness di verifica offline (pip install py65) per il boot del main CPU
# DECO CPU-7 di Burger Time. Replica ESATTAMENTE la logica di
# main_read/main_write/main_fetch di btime.cpp (mappa memoria + decrittazione
# CPU-7 dinamica) per controllare che il boot proceda in modo plausibile
# PRIMA di consegnare il codice per il flash reale.
import sys
from py65.devices.mpu6502 import MPU

ROMS = "../roms/"

def load(name):
    with open(ROMS + name, "rb") as f:
        return f.read()

maincpu = bytearray(0x1000) + load("aa04.9b") + load("aa06.13b") + load("aa05.10b") + load("aa07.15b")
assert len(maincpu) == 0x5000

def bitswap_cpu7(v):
    return (((v>>6)&1)<<7) | (((v>>5)&1)<<6) | (((v>>3)&1)<<5) | (((v>>4)&1)<<4) | \
           (((v>>2)&1)<<3) | (((v>>7)&1)<<2) | (((v>>1)&1)<<1) | (((v>>0)&1)<<0)

class Mem:
    def __init__(self):
        self.ram = bytearray(0x800)
        self.video = bytearray(0x400)
        self.color = bytearray(0x400)
        self.palette = bytearray(16)
        self.had_written = False
        self.pending_fetch_addr = None
        self.write_log = []
        self.io_reads = {}
        self.dsw1_read_count = 0

    def xy_swap(self, off):
        x, y = off // 32, off % 32
        return 32*y + x

    def raw_read(self, addr):
        if addr < 0x0800: return self.ram[addr]
        if 0x1000 <= addr < 0x1400: return self.video[addr-0x1000]
        if 0x1400 <= addr < 0x1800: return self.color[addr-0x1400]
        if 0x1800 <= addr < 0x1c00: return self.video[self.xy_swap(addr-0x1800)]
        if 0x1c00 <= addr < 0x2000: return self.color[self.xy_swap(addr-0x1c00)]
        if addr == 0x4000: return 0xff  # P1 nessun tasto premuto
        if addr == 0x4001: return 0xff  # P2
        if addr == 0x4002: return 0x3f  # SYSTEM nessuna moneta/start
        if addr == 0x4003:
            self.dsw1_read_count += 1
            vbl = (self.dsw1_read_count >> 3) & 1
            return 0x1f | (vbl << 7)
        if addr == 0x4004: return 0x0b  # DSW2
        if addr >= 0xb000: return maincpu[addr-0xb000]
        return 0xff

    def __getitem__(self, addr):
        addr &= 0xffff
        val = self.raw_read(addr)
        if self.pending_fetch_addr == addr:
            self.pending_fetch_addr = None
            if self.had_written:
                self.had_written = False
                if (addr & 0x104) == 0x104:
                    val = bitswap_cpu7(val)
        return val

    def __setitem__(self, addr, val):
        addr &= 0xffff
        val &= 0xff
        self.had_written = True
        self.write_log.append((addr, val))
        if addr < 0x0800: self.ram[addr] = val
        elif 0x1000 <= addr < 0x1400: self.video[addr-0x1000] = val
        elif 0x1400 <= addr < 0x1800: self.color[addr-0x1400] = val
        elif 0x1800 <= addr < 0x1c00: self.video[self.xy_swap(addr-0x1800)] = val
        elif 0x1c00 <= addr < 0x2000: self.color[self.xy_swap(addr-0x1c00)] = val
        elif 0x0c00 <= addr <= 0x0c0f: self.palette[addr-0x0c00] = val

mem = Mem()
mpu = MPU(memory=mem, pc=None)  # pc=None forza la lettura del vero reset vector (0xFFFC)
mpu.reset()
print(f"reset vector = {mpu.pc:04x}")

STEPS = 200000
pc_hist = []
illegal = 0
max_pc_seen = 0
regressions = 0

for i in range(STEPS):
    pc = mpu.pc
    if pc not in mpu.instruct or mpu.instruct[pc].__name__ if False else True:
        pass
    opcode = mem.raw_read(pc)  # solo per diagnosi, non usato per l'esecuzione
    mem.pending_fetch_addr = pc
    try:
        mpu.step()
    except Exception as e:
        print(f"CRASH a step {i}, pc={pc:04x}: {e}")
        break
    pc_hist.append(pc)
    if pc > max_pc_seen:
        max_pc_seen = pc
    elif pc < max_pc_seen - 0x200 and i > 50:
        regressions += 1

print(f"eseguiti {len(pc_hist)} step, PC finale={mpu.pc:04x}, max_pc={max_pc_seen:04x}")
print(f"cicli CPU totali: {mpu.processorCycles}")
print(f"scritture totali in memoria: {len(mem.write_log)}")
print(f"video_ram non-zero: {sum(1 for b in mem.video if b)}/{len(mem.video)}")
print(f"color_ram non-zero: {sum(1 for b in mem.color if b)}/{len(mem.color)}")
print(f"palette scritta: {list(mem.palette)}")

# ultimi 32 PC (per capire se e' finito in un loop stabile e dove)
print("ultimi 32 PC:", [f"{p:04x}" for p in pc_hist[-32:]])

# istogramma dei PC piu' visitati (loop principale)
from collections import Counter
c = Counter(pc_hist)
print("PC piu' frequenti:", [(f"{p:04x}", n) for p, n in c.most_common(10)])
