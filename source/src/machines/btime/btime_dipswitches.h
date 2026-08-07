#ifndef _btime_dipswitches_h_
#define _btime_dipswitches_h_

// DSW1 @0x4003 (bit7 = VBLANK dinamico, aggiunto a runtime, NON un DIP):
// CoinA 1C_1C(0x03) | CoinB 1C_1C(0x0c) | "Leave Off" bit4 settato di
// default (0x10). Default MAME (INPUT_PORTS_START(btime)).
#define BTIME_DSW1 0x1F

// DSW2 @0x4004 (INPUT_PORTS_START(btime), righe 1316-1333 di btime.cpp):
// bit0 Lives (1=3, 0=5) -> IMPOSTATO A 5 (bit0=0, richiesta utente).
// bit1-2 Bonus_Life (mask 0x06: 0x06=10000, 0x04=15000, 0x02=20000,
// 0x00=30000) -> IMPOSTATO A 10000 (0x06, richiesta utente).
// bit3 Enemies (1=4, 0=6) -> invariato, default MAME = 4.
// bit4 End of Level Pepper (1=No, 0=Yes) -> IMPOSTATO A Yes (bit4=0,
// richiesta utente -- coincide col default MAME, gia' Yes di fabbrica).
#define BTIME_DSW2 0x0E

#endif
