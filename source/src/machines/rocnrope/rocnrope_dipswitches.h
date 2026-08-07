#ifndef ROCNROPE_DIPSWITCHES_H
#define ROCNROPE_DIPSWITCHES_H

// ============================================================
// Roc'n Rope DIP switches (from MAME rocnrope.cpp INPUT_PORTS)
//
// DSW1 (port 0x3083): Coinage
//   KONAMI_COINAGE: bits 7:0 = coin B (bits 7:4) + coin A (bits 3:0)
//   Default 0xFF = 1 coin / 1 credit
//
// DSW2 (port 0x3000):
//   bits 1:0  = Lives:      11=3, 10=4, 01=5, 00=255(cheat)
//   bit  2    = Cabinet:    0=Upright, 1=Cocktail
//   bits 6:3  = Difficulty: 0x58 default (level 5 of 16)
//   bit  7    = Demo Sounds: 0=On, 1=Off
//
// DSW3 (port 0x3100):
//   bits 2:0  = First Bonus:     110=20000 (default)
//   bits 5:3  = Repeated Bonus:  010=60000 (default)
//   bit  6    = Grant Repeated:  0=Yes
//   bit  7    = unused (active-low = 1)
// ============================================================

// DSW1: 1 coin / 1 credit (KONAMI_COINAGE default)
#define ROCNROPE_DSW1  0xFF

// DSW2: 3 lives, upright, difficulty 5, demo sounds on
#define ROCNROPE_DSW2  0b01011011  // lives=11(3), cabinet=0, diff=0x58, demoSounds=0(on), bit7=0
#define ROCNROPE_DSW2_DEMO_SOUND_ON  0x00
#define ROCNROPE_DSW2_DEMO_SOUND_OFF 0x80

// DSW3: first bonus 20000, repeated 60000, grant yes, bit7=1(unused active-low)
#define ROCNROPE_DSW3  0b10010110  // firstBonus=110(20k), repBonus=010(60k), grant=0(yes), bit7=1

// System port (0x3080): coin, start, service
//   bit0 = coin1 (active-low)
//   bit3 = start1 (active-low)
//   bit4 = start2 (active-low)
//   bit7 = service (active-low)
#define ROCNROPE_SYSTEM_IDLE 0xFF

// Joystick port (0x3081/0x3082): 4-way + 2 fire buttons (active-low)
//   bit0=left, bit1=right, bit2=up, bit3=down, bit4=btn1, bit5=btn2
#define ROCNROPE_JOY_IDLE 0xFF

#endif
