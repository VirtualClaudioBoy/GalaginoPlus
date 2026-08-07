#ifndef _mappy_dipswitches_h_
#define _mappy_dipswitches_h_

// DSW1 (58XX #1 porte 22-29), attivo basso — default MAME 0xFF:
// bit0-2 difficolta' (7=Rank A ... 0=Rank H), bit3-4 Coin B (0x18=1C1C),
// bit5 demo sounds (0x20=on), bit6 rack test (0x40=off), bit7 freeze (0x80=off)
#define MAPPY_DSW1  0xFF

// DSW2 (multiplexato via LS157), default 0xFF:
// bit0-2 Coin A (0x07=1C1C), bit3-5 bonus (0x38=20k&70k con 3 vite),
// bit6-7 vite (0xC0=3)
#define MAPPY_DSW2  0xFF

// DSW0 (4 bit, 58XX #1 porte 30-33), attivo basso: bit0 = service mode off
#define MAPPY_DSW0  0x0F

#endif
