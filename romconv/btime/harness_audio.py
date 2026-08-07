#!/usr/bin/env python3
# Harness di verifica offline (py65) per il boot della CPU audio (6502 NUDO,
# non cifrata) di Burger Time. Replica ESATTAMENTE audio_read/audio_write di
# btime.cpp per controllare se il programma audio scrive davvero sui
# registri AY (soundregs) e abilita mai la NMI, PRIMA di continuare a
# ipotizzare fix sul suono senza prove.
from py65.devices.mpu6502 import MPU

with open("../roms/ab14.12h", "rb") as f:
    audiorom = f.read()
assert len(audiorom) == 0x1000

class Mem:
    def __init__(self):
        self.ram = bytearray(0x400)
        self.soundlatch = 0
        self.ay_port = [0, 0]
        self.soundregs = bytearray(32)
        self.audio_nmi_enable = 0
        self.write_log = []
        self.nmi_enable_events = []

    def raw_read(self, addr):
        if addr < 0x2000: return self.ram[addr & 0x3ff]
        if 0xa000 <= addr < 0xc000: return self.soundlatch
        if addr >= 0xe000: return audiorom[addr & 0x0fff]
        return 0xff

    def __getitem__(self, addr):
        return self.raw_read(addr & 0xffff)

    def __setitem__(self, addr, val):
        addr &= 0xffff
        val &= 0xff
        self.write_log.append((addr, val))
        if addr < 0x2000:
            self.ram[addr & 0x3ff] = val
        elif 0x2000 <= addr < 0x4000:
            if self.ay_port[0] < 14: self.soundregs[0x00 + self.ay_port[0]] = val
        elif 0x4000 <= addr < 0x6000:
            self.ay_port[0] = val & 0x0f
        elif 0x6000 <= addr < 0x8000:
            if self.ay_port[1] < 14: self.soundregs[0x10 + self.ay_port[1]] = val
        elif 0x8000 <= addr < 0xa000:
            self.ay_port[1] = val & 0x0f
        elif 0xc000 <= addr < 0xe000:
            self.audio_nmi_enable = val & 1
            self.nmi_enable_events.append(val & 1)

mem = Mem()
mpu = MPU(memory=mem, pc=None)
mpu.reset()
print(f"reset vector = {mpu.pc:04x}")

STEPS = 50000
pc_hist = []
for i in range(STEPS):
    pc_hist.append(mpu.pc)
    try:
        mpu.step()
    except Exception as e:
        print(f"CRASH a step {i}, pc={mpu.pc:04x}: {e}")
        break

print(f"eseguiti {len(pc_hist)} step, PC finale={mpu.pc:04x}")
print(f"scritture totali: {len(mem.write_log)}")
print(f"soundregs finali: {list(mem.soundregs)}")
print(f"audio_nmi_enable eventi (scritture a 0xc000-0xdfff): {mem.nmi_enable_events[:20]}")
print(f"audio_nmi_enable finale: {mem.audio_nmi_enable}")

# quante scritture sono finite nelle zone AY (2000-9fff)?
ay_writes = [w for w in mem.write_log if 0x2000 <= w[0] < 0xa000]
print(f"scritture in zona AY (0x2000-0x9fff): {len(ay_writes)}")
print("prime 20:", ay_writes[:20])

# ultimi PC (loop finale)
print("ultimi 16 PC:", [f"{p:04x}" for p in pc_hist[-16:]])
