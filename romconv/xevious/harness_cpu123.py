#!/usr/bin/env python3
# ============================================================
# Harness a 3 CPU: replica FEDELE della logica di xevious.cpp
# (rdZ80/wrZ80/run_frame) per due scopi:
#  1) estrarre il PROTOCOLLO reale che Xevious usa col chip 51xx
#     (quali comandi scrive a 0x7100/0x7000, quali byte legge) — per
#     capire se lo shortcut copiato da galaga (case 2 = credit mode) e'
#     giusto o va adattato;
#  2) osservare il "reset a 0x0000" (attract-loop) in modo deterministico.
# Non richiede HW. Usa pip install z80.
# ============================================================
import os, sys
import z80

ROMS = "../roms/"
def load(n):
    with open(os.path.join(ROMS, n), "rb") as f:
        return f.read()

cpu1_rom = load("xvi_1.3p")+load("xvi_2.3m")+load("xvi_3.2m")+load("xvi_4.2l")  # 0x4000
cpu2_rom = load("xvi_5.3f")+load("xvi_6.3j")                                     # 0x2000
cpu3_rom = load("xvi_7.2c")                                                      # 0x1000
assert len(cpu1_rom)==0x4000 and len(cpu2_rom)==0x2000 and len(cpu3_rom)==0x1000

# gfx4 planet-map (xvi_9/10/11) ricostruita come region GFX4 (0x4000):
#   rom2a@0x0000 (xvi_9, 0x1000), rom2b@0x1000 (xvi_10, 0x2000),
#   rom2c@0x3000 (xvi_11, 0x1000)
planet = bytearray(0x4000)
planet[0x0000:0x1000] = load("xvi_9.2a")
planet[0x1000:0x3000] = load("xvi_10.2b")
planet[0x3000:0x4000] = load("xvi_11.2c")
rom2a, rom2b, rom2c = planet, planet[0x1000:], planet[0x3000:]

DSWA = 0xFF
DSWB = 0xFF
mem = bytearray(0x10000)   # usa indirizzi reali per la RAM 0x7800-0xcfff

class S:
    cs_ctrl = 0
    namco_busy = 0
    namco_cnt = 0
    credit_mode = 0
    coincred = 0
    credit = 0
    irq_enable = [0,0,0]
    sub_reset = True
    nmi_cnt = 0
    rng = 0xACE1
    xbs = [0,0]
    # opzioni di test
    inject_coin_frame = None
    inject_start_frame = None
st = S()

log_06xx = []      # traccia protocollo
MAXLOG = 400

def bb_r(offset):
    adr_2b = ((st.xbs[1]&0x7e)<<6)|((st.xbs[0]&0xfe)>>1)
    if adr_2b & 1:
        dat1 = ((rom2a[adr_2b>>1]&0xf0)<<4)|rom2b[adr_2b]
    else:
        dat1 = ((rom2a[adr_2b>>1]&0x0f)<<8)|rom2b[adr_2b]
    adr_2c = ((dat1&0x1ff)<<2)|((st.xbs[1]&1)<<1)|(st.xbs[0]&1)
    if dat1 & 0x400: adr_2c ^= 1
    if dat1 & 0x200: adr_2c ^= 2
    if offset & 1:
        return rom2c[adr_2c|0x800]
    dat2 = rom2c[adr_2c]
    dat2 = (dat2&0x3f)|((dat2&0x80)>>1)|((dat2&0x40)<<1)
    if dat1 & 0x400: dat2 ^= 0x40
    if dat1 & 0x200: dat2 ^= 0x80
    return dat2

# stato input simulato (active-low come 51xx): parte tutto rilasciato
cur_frame = [0]
coin_pulse = [0]
start_pulse = [0]

def make_rd(rom, size, cpuidx):
    def rd(addr):
        if addr < size:
            return rom[addr]
        if (addr & 0xfff8) == 0x6800:
            bit = addr & 7
            return ((DSWB>>bit)&1)|(((DSWA>>bit)&1)<<1)
        if addr == 0x7100:
            return 0x00 if st.namco_busy else 0x10
        if 0x7000 <= addr <= 0x70ff:
            if st.cs_ctrl & 1:
                if not st.credit_mode:
                    if st.namco_cnt > 2: return 0xff
                    v = [0xff,0xff,0xff][st.namco_cnt]; st.namco_cnt += 1
                    if len(log_06xx)<MAXLOG: log_06xx.append(("rd51_nocred", st.namco_cnt-1, v))
                    return v
                else:
                    mapb1 = [ (16*(st.credit//10)+st.credit%10), 0xff, 0xff ]
                    # coin/start simulati come pulse
                    if coin_pulse[0] and st.credit < 99:
                        st.credit += 1; coin_pulse[0] = 0
                    if start_pulse[0] and st.credit:
                        st.credit -= 1; start_pulse[0] = 0
                    if st.namco_cnt > 2: return 0xff
                    v = mapb1[st.namco_cnt]; st.namco_cnt += 1
                    if len(log_06xx)<MAXLOG: log_06xx.append(("rd51_cred", st.namco_cnt-1, v, st.credit))
                    return v
            elif st.cs_ctrl & 4:
                st.rng ^= (st.rng<<7)&0xffffffff
                st.rng ^= (st.rng>>9)
                st.rng ^= (st.rng<<8)&0xffffffff
                return st.rng & 0xff
            return 0xff
        if 0x7800 <= addr <= 0xcfff:
            return mem[addr]
        if addr >= 0xf000:
            return bb_r(addr & 1)
        return 0xff
    return rd

def make_wr(cpuidx):
    def wr(addr, value):
        if addr < 0x4000: return
        if (addr & 0xffe0) == 0x6800:  # WSG sound
            return
        if (addr & 0xfff8) == 0x6820:  # ls259 latch
            bit = addr & 7
            if bit in (0,1,2):
                st.irq_enable[bit] = value
            elif bit == 3:
                st.sub_reset = (value == 0)
                st.credit_mode = 0
                st.namco_cnt = 0
                if st.sub_reset:
                    machines[1].pc = 0
                    machines[2].pc = 0
            return
        if addr == 0x6830: return   # watchdog no-op
        if addr == 0x7100:
            st.namco_cnt = 0
            st.cs_ctrl = value
            st.namco_busy = 5000
            if len(log_06xx)<MAXLOG: log_06xx.append(("ctrl", value))
            return
        if 0x7000 <= addr <= 0x70ff:
            if st.cs_ctrl & 1:
                if len(log_06xx)<MAXLOG: log_06xx.append(("wr51", value, "coincred=%d credmode=%d"%(st.coincred,st.credit_mode)))
                if st.coincred:
                    st.coincred -= 1; return
                if value == 1: st.coincred = 4
                elif value == 2: st.credit_mode = 1
                elif value == 5: st.credit_mode = 0
                st.namco_cnt += 1
            return
        if (addr & 0xff80) == 0xd000: return   # scroll latch
        if 0x7800 <= addr <= 0xcfff:
            mem[addr] = value; return
        if addr >= 0xf000:
            st.xbs[addr & 1] = value; return
    return wr

machines = []
for idx,(rom,size) in enumerate([(cpu1_rom,0x4000),(cpu2_rom,0x2000),(cpu3_rom,0x1000)]):
    m = z80.Z80Machine()
    m.set_read_callback(make_rd(rom,size,idx))
    m.set_write_callback(make_wr(idx))
    m.pc = 0
    machines.append(m)

INST=1250
frames = int(sys.argv[1]) if len(sys.argv)>1 else 1200
reset_events = []
prev_pc0 = 0

for frame in range(1, frames+1):
    cur_frame[0] = frame
    if st.inject_coin_frame and frame == st.inject_coin_frame: coin_pulse[0]=1
    if st.inject_start_frame and frame == st.inject_start_frame: start_pulse[0]=1
    for i in range(INST):
        m0 = machines[0]
        # step CPU1 4 volte (tick-based single step)
        base = None
        for s in range(4):
            before = m0.pc
            m0.ticks_to_stop = 1
            m0.run()
            if before >= 0x0400 and m0.pc < 0x0100:
                if len(reset_events) < 12:
                    reset_events.append((frame,i,before,m0.pc,m0.sp))
        if not st.sub_reset:
            for s in range(4):
                machines[1].ticks_to_stop=1; machines[1].run()
            for s in range(4):
                machines[2].ticks_to_stop=1; machines[2].run()
        if st.namco_busy: st.namco_busy -= 1
        if (st.cs_ctrl & 0xe0) != 0:
            if st.nmi_cnt < (st.cs_ctrl>>5)*64:
                st.nmi_cnt += 1
            else:
                machines[0].on_handle_active_int()  # NMI approssimata
                st.nmi_cnt = 0
        if (not st.sub_reset) and (not st.irq_enable[2]) and (i==INST//4 or i==3*INST//4):
            machines[2].on_handle_active_int()
    # fine frame: RST38
    if st.irq_enable[0]:
        machines[0].on_handle_active_int()
    if (not st.sub_reset) and st.irq_enable[1]:
        machines[1].on_handle_active_int()

    if frame % 120 == 1:
        print(f"frame={frame} sub_reset={int(st.sub_reset)} cpu1.pc={machines[0].pc:#06x} "
              f"cpu2.pc={machines[1].pc:#06x} cs_ctrl={st.cs_ctrl:#04x} "
              f"credit_mode={st.credit_mode} credit={st.credit} irq_en={st.irq_enable}")

print("\n=== primi eventi protocollo 06xx/51xx ===")
for e in log_06xx[:120]:
    print(" ", e)
print("\n=== eventi 'jump to low' (reset) CPU1 ===")
for e in reset_events:
    print("  frame=%d i=%d from=%04x -> pc=%04x sp=%04x" % e)
