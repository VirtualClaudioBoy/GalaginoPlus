#ifndef _todruaga_dipswitches_h_
#define _todruaga_dipswitches_h_

// DSW1 (56XX #1 porte 22-29), attivo basso � default MAME 0xFF:
// bit0-1 vite (0x03=3, 0x02=2, 0x01=1, 0x00=5), bit2-3 Coin A (0x0C=1C1C),
// bit4 freeze (0x10=off), bit5 service mode (0x20=off),
// bit6-7 Coin B (0xC0=1C1C)
#define TODRUAGA_DSW1  0xFC

// DSW2 (multiplexato via LS157): tutti gli 8 bit inutilizzati in todruaga
#define TODRUAGA_DSW2  0xFF

// DSW0 (4 bit, 56XX #1 porte 30-33), attivo basso:
// bit0-1 inutilizzati, bit2 cabinet (1=upright), bit3 service (1=off)
#define TODRUAGA_DSW0  0x0F

#endif
