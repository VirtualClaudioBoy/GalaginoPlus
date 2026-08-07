#ifndef _circusc_dipswitches_h_
#define _circusc_dipswitches_h_

// DSW1 @0x1400 (attivo basso): bit0-3 Coin A, bit4-7 Coin B; 0xFF = 1C/1C
#define CIRCUSC_DSW1  0xFF

// DSW2 @0x1800: bit0-1 vite (0x03=3, 0x02=4, 0x01=5, 0x00=7),
// bit2 cabinet (0=UPRIGHT!), bit3 bonus (0x08=20k/90k/70k+),
// bit4 inutilizzato (default 0), bit5-6 difficolta' (0x40=Normal),
// bit7 demo sounds (0=ON!). Default MAME = 0x4B
#define CIRCUSC_DSW2  0x4B
// maschera per demo sounds off (bit7=1 = off)
#define CIRCUSC_DSW2_DEMO_SOUND_OFF  0x80

#endif
