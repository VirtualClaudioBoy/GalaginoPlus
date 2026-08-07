#!/usr/bin/env python3
# Diagnostica bug "punteggio mostra lettere" dopo l'aggiunta dell'hiscore.
# Replica boot + attract mode (NESSUNA moneta inserita) e traccia il
# contenuto della regione hiscore 0x0033-0x0059 (39 byte, dalla voce
# hiscore.dat "@:maincpu,program,0033,27,00,ff") ad ogni step in cui
# CAMBIA, per vedere:
# 1) se/quando il gate (byte@0x0033==0x00 E byte@0x0059==0xff) diventa vero
#    DURANTE L'ATTRACT MODE (prima di qualunque game reale) -- se si, e'
#    sicuro che il manager catturi/iniettti li';
# 2) se la regione continua a CAMBIARE anche DOPO che il gate e' stato
#    soddisfatto per 30 frame consecutivi (heuristic: se cambia parecchio
#    dopo, e' probabile che sia la stessa area usata per il punteggio
#    LIVE di una partita in corso, non solo la tabella idle).
import sys
from py65.devices.mpu6502 import MPU

ROMS = "../roms/"

def load(name):
    with open(ROMS + name, "rb") as f:
        return f.read()

maincpu = bytearray(0x1000) + load("aa04.9b") + load("aa06.13b") + load("aa05.10b") + load("aa07.15b")

def bitswap_cpu7(v):
    return (((v>>6)&1)<<7) | (((v>>5)&1)<<6) | (((v>>3)&1)<<5) | (((v>>4)&1)<<4) | \
           (((v>>2)&1)<<3) | (((v>>7)&1)<<2) | (((v>>1)&1)<<1) | (((v>>0)&1)<<0)

REGION_ADDR = 0x0033
REGION_LEN = 0x27

class Mem:
    def __init__(self):
        self.ram = bytearray(0x800)
        self.video = bytearray(0x400)
        self.color = bytearray(0x400)
        self.palette = bytearray(16)
        self.had_written = False
        self.pending_fetch_addr = None
        self.dsw1_read_count = 0
        self.last_region = bytes(self.ram[REGION_ADDR:REGION_ADDR+REGION_LEN])
        self.region_changes = []  # (step, bytes)
        self.gate_frames = 0
        self.gate_first_true_step = None
        self.frame_counter = 0
        self.coin_inserted = False

    def xy_swap(self, off):
        x, y = off // 32, off % 32
        return 32*y + x

    def raw_read(self, addr):
        if addr < 0x0800: return self.ram[addr]
        if 0x1000 <= addr < 0x1400: return self.video[addr-0x1000]
        if 0x1400 <= addr < 0x1800: return self.color[addr-0x1400]
        if 0x1800 <= addr < 0x1c00: return self.video[self.xy_swap(addr-0x1800)]
        if 0x1c00 <= addr < 0x2000: return self.color[self.xy_swap(addr-0x1c00)]
        if addr == 0x4000: return 0xff
        if addr == 0x4001: return 0xff
        if addr == 0x4002:
            r = 0x3f
            if self.coin_inserted: r |= 0x40
            return r
        if addr == 0x4003:
            self.dsw1_read_count += 1
            vbl = (self.dsw1_read_count >> 3) & 1
            return 0x1f | (vbl << 7)
        if addr == 0x4004: return 0x0b
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
        if addr < 0x0800: self.ram[addr] = val
        elif 0x1000 <= addr < 0x1400: self.video[addr-0x1000] = val
        elif 0x1400 <= addr < 0x1800: self.color[addr-0x1400] = val
        elif 0x1800 <= addr < 0x1c00: self.video[self.xy_swap(addr-0x1800)] = val
        elif 0x1c00 <= addr < 0x2000: self.color[self.xy_swap(addr-0x1c00)] = val
        elif 0x0c00 <= addr <= 0x0c0f: self.palette[addr-0x0c00] = val

mem = Mem()
mpu = MPU(memory=mem, pc=None)
mpu.reset()
print(f"reset vector = {mpu.pc:04x}")

STEPS = 3_000_000
gate_true_run = 0
gate_satisfied_at = None

for i in range(STEPS):
    pc = mpu.pc
    mem.pending_fetch_addr = pc
    try:
        mpu.step()
    except Exception as e:
        print(f"CRASH a step {i}, pc={pc:04x}: {e}")
        break

    region = bytes(mem.ram[REGION_ADDR:REGION_ADDR+REGION_LEN])
    if region != mem.last_region:
        mem.region_changes.append((i, region.hex()))
        mem.last_region = region

    # gate: byte iniziale ==0x00, byte finale ==0xff (come nel manager reale)
    gate_now = (region[0] == 0x00 and region[-1] == 0xff)
    if gate_now:
        gate_true_run += 1
    else:
        gate_true_run = 0
    if gate_true_run == 30 and gate_satisfied_at is None:
        gate_satisfied_at = i
        print(f"GATE soddisfatto (30 frame-equivalenti consecutivi) a step {i}, pc={mpu.pc:04x}")
        print(f"  contenuto regione: {region.hex()}")

print(f"\ntotale step eseguiti: {i+1}")
print(f"numero di CAMBIAMENTI della regione 0x0033-0x0059 durante tutta la corsa: {len(mem.region_changes)}")
print(f"gate soddisfatto per la prima volta a step: {gate_satisfied_at}")
print("\nprimi 15 cambiamenti:")
for step, hexval in mem.region_changes[:15]:
    print(f"  step {step:>9}: {hexval}")
print("\nultimi 15 cambiamenti:")
for step, hexval in mem.region_changes[-15:]:
    print(f"  step {step:>9}: {hexval}")

# quanti cambiamenti DOPO che il gate e' stato soddisfatto la prima volta?
if gate_satisfied_at is not None:
    after = [c for c in mem.region_changes if c[0] > gate_satisfied_at]
    print(f"\ncambiamenti DOPO il primo gate-match: {len(after)}")
    if after:
        print("primi 10 dopo il gate:")
        for step, hexval in after[:10]:
            print(f"  step {step:>9}: {hexval}")
