#!/usr/bin/env python3
# ============================================================
# Harness Python (pip install z80) per far girare la ROM reale della CPU1
# di Xevious contro l'ESATTA logica di rdZ80/wrZ80 implementata in
# xevious.cpp, per capire dove si blocca il boot SENZA hardware reale.
# Metodo gia' usato con successo in questo progetto per gaplus/alibaba
# (vedi memoria project_gaplus.md/project_alibaba.md).
# ============================================================
import os, sys, collections
import z80

ROMS = "../roms/"

def load(name):
    with open(os.path.join(ROMS, name), "rb") as f:
        return f.read()

cpu1_rom = load("xvi_1.3p") + load("xvi_2.3m") + load("xvi_3.2m") + load("xvi_4.2l")
assert len(cpu1_rom) == 0x4000

XEVIOUS_DSW0 = 0xFF
XEVIOUS_DSW1 = 0xFF

class State:
    def __init__(self):
        self.namco_cnt = 0
        self.namco_busy = 0
        self.cs_ctrl = 0
        self.credit_mode = 0
        self.credit = 0
        self.coincredMode = 0
        self.prev_mask = 0
        self.fire_timer = 0
        self.rng = 0xACE1
        self.io_log = []       # (kind, addr, value) per i port 0x7000-0x71ff
        self.write_log = collections.Counter()   # istogramma indirizzi scritti fuori ROM/RAM note

st = State()

def rd(addr):
    if addr < 0x4000:
        return cpu1_rom[addr]

    if (addr & 0xfff8) == 0x6800:
        bit = addr & 7
        b0 = 0 if (XEVIOUS_DSW0 & (0x80 >> bit)) else 1
        b1 = 0 if (XEVIOUS_DSW1 & (0x80 >> bit)) else 2
        return b0 | b1

    if addr == 0x7100:
        v = 0x00 if st.namco_busy else 0x10
        st.io_log.append(("ctrl_r", addr, v))
        return v

    if 0x7000 <= addr <= 0x70ff:
        if st.cs_ctrl & 1:
            if not st.credit_mode:
                map71 = [0b11111111, 0xff, 0xff]
                if st.namco_cnt > 2:
                    v = 0xff
                else:
                    v = map71[st.namco_cnt]
                    st.namco_cnt += 1
                st.io_log.append(("51xx_r_noncred", addr, v))
                return v
            else:
                mapb1 = [16*(st.credit//10)+st.credit%10, 0b11111111, 0b11111111]
                if st.namco_cnt > 2:
                    v = 0xff
                else:
                    v = mapb1[st.namco_cnt]
                    st.namco_cnt += 1
                st.io_log.append(("51xx_r_cred", addr, v))
                return v
        elif st.cs_ctrl & 4:
            st.rng ^= (st.rng << 7) & 0xffffffff
            st.rng ^= (st.rng >> 9)
            st.rng ^= (st.rng << 8) & 0xffffffff
            v = st.rng & 0xff
            st.io_log.append(("50xx_r", addr, v))
            return v
        st.io_log.append(("06xx_r_noselect", addr, 0xff))
        return 0xff

    if 0x7800 <= addr <= 0xcfff:
        return mem[addr]

    if addr >= 0xf000:
        # planet-map: non modellato nell'harness (serve solo per capire il
        # boot, non il gameplay) -- ritorna 0 fisso
        return 0x00

    return 0xff

mem = bytearray(0x10000)

def wr(addr, value):
    if addr < 0x4000:
        return

    if (addr & 0xffe0) == 0x6800:
        return  # WSG, ignorato nell'harness

    if (addr & 0xfff8) == 0x6820:
        bit = addr & 7
        if bit == 3:
            st.credit_mode = 0
            st.namco_cnt = 0
        return

    if addr == 0x6830:
        return

    if addr == 0x7100:
        st.namco_cnt = 0
        st.cs_ctrl = value
        st.namco_busy = 5000
        st.io_log.append(("ctrl_w", addr, value))
        return

    if 0x7000 <= addr <= 0x70ff:
        if st.cs_ctrl & 1:
            if st.coincredMode:
                st.coincredMode -= 1
                return
            if value == 1: st.coincredMode = 4
            elif value == 2: st.credit_mode = 1
            elif value == 5: st.credit_mode = 0
            st.namco_cnt += 1
        st.io_log.append(("51xx_w", addr, value))
        return

    if (addr & 0xff80) == 0xd000:
        return

    if 0x7800 <= addr <= 0xcfff:
        mem[addr] = value
        return

    if addr >= 0xf000:
        return

    st.write_log[addr] += 1

m = z80.Z80Machine()
m.set_read_callback(rd)
m.set_write_callback(wr)
m.pc = 0

TICK_CHUNK = 200
MAX_TICKS = 4_000_000   # ~1.3M istruzioni a 4 t-state medi, abbondante per il boot

pc_hist = collections.Counter()
pc_trace = collections.deque(maxlen=400)
total_ticks = 0
vblank_period = 51200   # ~3.072MHz / 60Hz
next_vblank = vblank_period
busy_decay_period = 41  # come run_frame() reale: namco_busy-- ogni ~41 t-state
next_busy_decay = busy_decay_period

while total_ticks < MAX_TICKS:
    m.ticks_to_stop = total_ticks + TICK_CHUNK
    m.run()
    total_ticks += TICK_CHUNK
    pc_hist[m.pc] += 1
    pc_trace.append(m.pc)

    while next_busy_decay <= total_ticks:
        if st.namco_busy:
            st.namco_busy -= 1
        next_busy_decay += busy_decay_period

    if total_ticks >= next_vblank:
        m.on_handle_active_int()
        next_vblank += vblank_period

print(f"Eseguiti {total_ticks} t-state (~{total_ticks/3072000*1000:.1f} ms a 3.072MHz)")
print(f"PC finale: {m.pc:#06x}")
print()
print("Top 15 indirizzi PC piu' visitati (indizio di loop):")
for addr, count in pc_hist.most_common(15):
    print(f"  {addr:#06x}: {count} volte")

print()
print("Ultimi 400 campioni PC (ogni 200 t-state), per vedere il loop reale:")
print(" ".join(f"{a:04x}" for a in pc_trace))

print()
print(f"Ultime 40 interazioni IO (0x7000-0x71ff):")
for kind, addr, val in st.io_log[-40:]:
    print(f"  {kind:20s} addr={addr:#06x} val={val:#04x}")

print()
if st.write_log:
    print("Scritture verso indirizzi NON gestiti (fuori mappa nota):")
    for addr, count in st.write_log.most_common(10):
        print(f"  {addr:#06x}: {count} volte")
else:
    print("Nessuna scrittura verso indirizzi non gestiti.")
