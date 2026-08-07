#ifndef VANVAN_DIPSWITCHES_H
#define VANVAN_DIPSWITCHES_H

// ============================================================
// Van Van Car (Sanritsu 1983) DIP switches (from MAME pacman.cpp INPUT_PORTS(vanvan))
//
// DSW1 (port 0x5080):
//   bit 0    = Cabinet:     0=Upright, 1=Cocktail
//   bit 1    = Flip Screen: 0=Off, 1=On           (note: inverted, 1=Off is default)
//   bits 3:2 = Bonus Life:  00=70k/200k, 01=40k/140k, 10=20k/100k, 11=None (default)
//   bits 5:4 = Lives:       00=6, 01=5, 10=4, 11=3 (default)
//   bits 7:6 = Coinage:     00=2C/1C, 01=1C/3C, 10=1C/2C, 11=1C/1C (default)
//
// DSW2 (port 0x50c0): mostly unknown/cheats, all default Off (0)
//   bit 1 = Invulnerability (cheat)
//   bit 3 = Missile effect (0=killer car destroyed, 1=not destroyed)
// ============================================================

// DSW1 default: upright, flip off, bonus none, 3 lives, 1 coin / 1 credit
#define VANVAN_DSW1  0xFE   // 0b11111110: cabinet=0(upright), flip=1(off), bonus=11(none), lives=11(3), coinage=11(1C_1C)

// DSW2 default: no cheats active
#define VANVAN_DSW2  0x00

// System / joystick idle states (active-low, all inactive = 0xFF)
#define VANVAN_IN0_IDLE 0xFF
#define VANVAN_IN1_IDLE 0xFF

#endif
