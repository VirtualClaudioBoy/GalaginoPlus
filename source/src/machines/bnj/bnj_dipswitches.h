#ifndef _bnj_dipswitches_h_
#define _bnj_dipswitches_h_

// DSW1 @0x1000 (bit7 = VBLANK dinamico, aggiunto a runtime, NON un DIP --
// INPUT_PORTS_START(bnj), btime.cpp righe 1725-1748, letto per intero):
// bit0-1 Coin A (0x03=1C_1C default), bit2-3 Coin B (0x0c=1C_1C default),
// bit4-5 Test Mode (0x30=Off default), bit6 Cabinet (0=Upright default).
#define BNJ_DSW1 0x3F

// DSW2 @0x1001 (righe 1750-1767): bit0 Lives (1=3, 0=5 -- IMPOSTATO A 5,
// bit0=0, richiesta utente), bit1-2 Bonus_Life (mask 0x06: 0x06=Every
// 30000 default, 0x04=Every 70000, 0x02=20000 Only, 0x00=30000 Only),
// bit3 Allow_Continue (0x08=No, 0x00=Yes default -- si', il default
// sorgente e' 0x00=Yes), bit4 Difficulty (0x10=Easy default, 0x00=Hard),
// bit5-7 non usati (0, "should be OFF" da manuale).
#define BNJ_DSW2 0x16

#endif
