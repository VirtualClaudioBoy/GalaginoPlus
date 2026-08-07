#!/usr/bin/env python3
# ============================================================
# Harness Python a 2 CPU (CPU1 main + CPU2 motion) per capire se CPU1 si
# blocca perche' aspetta un handshake da CPU2 (RAM condivisa 0x8571-0x8577,
# vedi harness_cpu1.py) che nel nostro C++ potrebbe non arrivare mai.
# Stessa mappa IO/memoria di xevious.cpp, replicata in Python.
# ============================================================
import os, collections
import z80

ROMS = "../roms/"

def load(name):
    with open(os.path.join(ROMS, name), "rb") as f:
        return f.read()

cpu1_rom = load("xvi_1.3p") + load("xvi_2.3m") + load("xvi_3.2m") + load("xvi_4.2l")
cpu2_rom = load("xvi_5.3f") + load("xvi_6.3j")
assert len(cpu1_rom) == 0x4000
assert len(cpu2_rom) == 0x2000

XEVIOUS_DSWA = 0xFF
XEVIOUS_DSWB = 0xFF

mem = bytearray(0x10000)   # RAM condivisa 0x7800-0xcfff (usiamo indirizzi reali)

class IOState:
    def __init__(self):
        self.namco_cnt = 0
        self.namco_busy = 0
        self.cs_ctrl = 0
        self.credit_mode = 0
        self.credit = 0
        self.coincredMode = 0
        self.rng = 0xACE1
        self.irq_enable = [0, 0, 0]
        self.sub_cpu_reset = True

st = IOState()

def make_rd(cpu_rom, cpu_rom_size):
    def rd(addr):
        if addr < cpu_rom_size:
            return cpu_rom[addr]
        if (addr & 0xfff8) == 0x6800:
            bit = addr & 7
            b0 = (XEVIOUS_DSWB >> bit) & 1
            b1 = (XEVIOUS_DSWA >> bit) & 1
            return b0 | (b1 << 1)
        if addr == 0x7100:
            return 0x00 if st.namco_busy else 0x10
        if 0x7000 <= addr <= 0x70ff:
            if st.cs_ctrl & 1:
                if not st.credit_mode:
                    map71 = [0b11111111, 0xff, 0xff]
                    if st.namco_cnt > 2: return 0xff
                    v = map71[st.namco_cnt]; st.namco_cnt += 1
                    return v
                else:
                    mapb1 = [16*(st.credit//10)+st.credit%10, 0b11111111, 0b11111111]
                    if st.namco_cnt > 2: return 0xff
                    v = mapb1[st.namco_cnt]; st.namco_cnt += 1
                    return v
            elif st.cs_ctrl & 4:
                st.rng ^= (st.rng << 7) & 0xffffffff
                st.rng ^= (st.rng >> 9)
                st.rng ^= (st.rng << 8) & 0xffffffff
                return st.rng & 0xff
            return 0xff
        if 0x7800 <= addr <= 0xcfff:
            return mem[addr]
        if addr >= 0xf000:
            return 0x00
        return 0xff
    return rd

def make_wr(is_cpu1):
    def wr(addr, value):
        if addr < 0x4000:
            return
        if (addr & 0xffe0) == 0x6800:
            return
        if (addr & 0xfff8) == 0x6820:
            bit = addr & 7
            if bit in (0, 1, 2):
                st.irq_enable[bit] = value
            elif bit == 3:
                st.sub_cpu_reset = (value == 0)
                st.credit_mode = 0
                st.namco_cnt = 0
                if st.sub_cpu_reset:
                    m2.pc = 0   # ResetZ80 equivalente semplificato
            return
        if addr == 0x6830:
            return
        if addr == 0x7100:
            st.namco_cnt = 0
            st.cs_ctrl = value
            st.namco_busy = 5000
            return
        if 0x7000 <= addr <= 0x70ff:
            if is_cpu1 and (st.cs_ctrl & 1):
                if st.coincredMode:
                    st.coincredMode -= 1
                    return
                if value == 1: st.coincredMode = 4
                elif value == 2: st.credit_mode = 1
                elif value == 5: st.credit_mode = 0
                st.namco_cnt += 1
            return
        if (addr & 0xff80) == 0xd000:
            return
        if 0x7800 <= addr <= 0xcfff:
            if addr == 0x8002 and mem[addr] != value:
                writes_8002.append((total_ticks_ref[0], is_cpu1, value))
            mem[addr] = value
            return
        if addr >= 0xf000:
            return
    return wr

writes_8002 = []
total_ticks_ref = [0]

m1 = z80.Z80Machine()
m1.set_read_callback(make_rd(cpu1_rom, 0x4000))
m1.set_write_callback(make_wr(True))
m1.pc = 0

m2 = z80.Z80Machine()
m2.set_read_callback(make_rd(cpu2_rom, 0x2000))
m2.set_write_callback(make_wr(False))
m2.pc = 0

TICK_CHUNK = 200
MAX_TICKS = 3_000_000
busy_decay_period = 41
next_busy_decay = busy_decay_period
vblank_period = 51200
next_vblank = vblank_period
snapshot_period = 200_000
next_snapshot = snapshot_period

pc1_hist = collections.Counter()
pc2_hist = collections.Counter()
total_ticks = 0
handshake_seen_at = None

while total_ticks < MAX_TICKS:
    total_ticks_ref[0] = total_ticks
    m1.ticks_to_stop = total_ticks + TICK_CHUNK
    m1.run()
    if not st.sub_cpu_reset:
        m2.ticks_to_stop = total_ticks + TICK_CHUNK
        m2.run()
    total_ticks += TICK_CHUNK

    pc1_hist[m1.pc] += 1
    if not st.sub_cpu_reset:
        pc2_hist[m2.pc] += 1

    while next_busy_decay <= total_ticks:
        if st.namco_busy: st.namco_busy -= 1
        next_busy_decay += busy_decay_period

    if total_ticks >= next_vblank:
        m1.on_handle_active_int()
        if not st.sub_cpu_reset:
            m2.on_handle_active_int()
        next_vblank += vblank_period

    if handshake_seen_at is None and any(mem[a] for a in range(0x8571, 0x8578)):
        handshake_seen_at = total_ticks

    if total_ticks >= next_snapshot:
        print(f"[t={total_ticks:>8}] cpu1_pc={m1.pc:#06x} cpu2_pc={m2.pc:#06x} "
              f"mem[0x8002]={mem[0x8002]:#04x} sub_cpu_reset={st.sub_cpu_reset} "
              f"irq_enable={st.irq_enable}")
        next_snapshot += snapshot_period

print(f"Eseguiti {total_ticks} t-state (~{total_ticks/3072000*1000:.1f} ms)")
print(f"CPU1 pc finale: {m1.pc:#06x}   CPU2 pc finale: {m2.pc:#06x}   sub_cpu_reset={st.sub_cpu_reset}")
print(f"Handshake 0x8571-0x8577 diventato non-zero a: {handshake_seen_at}")
print()
print("Top 10 PC CPU1:")
for a, c in pc1_hist.most_common(10):
    print(f"  {a:#06x}: {c}")
print("Top 10 PC CPU2:")
for a, c in pc2_hist.most_common(10):
    print(f"  {a:#06x}: {c}")
print()
print("mem[0x8571:0x8578] =", [hex(b) for b in mem[0x8571:0x8578]])
print("mem[0x8002] =", hex(mem[0x8002]))
print("scritture a 0x8002:", writes_8002[:20])
