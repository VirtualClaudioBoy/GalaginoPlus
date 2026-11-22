#ifndef _motos_dipswitches_h_
#define _motos_dipswitches_h_

// DSW1 (56XX #1 pins 22-29), active low, MAME's default 0xFF:
// bit 0 service mode (0x01 = off), bits 1-2 coinage (0x06 = 1C1C), bit 3 lives (0x08 = 3),
// bit 4 difficulty (0x10 = rank A), bits 5-6 bonus life (0x60 = 10k, 30k and every 50k),
// bit 7 demo sounds (0x80 = on)
#define MOTOS_DSW1  0xFF

// DSW2 (through the LS157 multiplexer): not used by the game
#define MOTOS_DSW2  0xFF

// DSW0 (4 bits, 56XX #1 pins 30-33), active low: bits 0 and 1 are the second buttons of the two players,
// bit 2 cabinet (0x04 = upright), bit 3 service mode again (0x08 = off)
#define MOTOS_DSW0  0x0F

#endif
