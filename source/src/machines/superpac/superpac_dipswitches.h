#ifndef _superpac_dipswitches_h_
#define _superpac_dipswitches_h_

// DSW1 (56XX #1 pins 22-29), active low, MAME's default 0xFF:
// bits 0-3 difficulty (0x0F = rank 0, normal), bits 4-5 coin B (0x30 = 1C1C),
// bit 6 demo sounds (0x40 = on), bit 7 freeze / rack test (0x80 = off)
#define SUPERPAC_DSW1  0xFF

// DSW2 (through the LS157 multiplexer), default 0xFF:
// bits 0-2 coin A (0x07 = 1C1C), bits 3-5 bonus life (0x38 = 30k & 100k),
// bits 6-7 lives (0xC0 = 3)
#define SUPERPAC_DSW2  0xFF

// DSW0 (4 bits, 56XX #1 pins 30-33), active low:
// bit 2 cabinet (0x04 = upright), bit 3 service mode (0x08 = off)
#define SUPERPAC_DSW0  0x0F

#endif
