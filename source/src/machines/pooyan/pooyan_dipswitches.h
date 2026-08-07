#ifndef _pooyan_dipswitches_h_
#define _pooyan_dipswitches_h_

// Pooyan DIP switches (driver MAME pooyan.cpp)

// DSW0 (read at 0xA0E0) - Coinage Konami standard (KONAMI_COINAGE)
// bits 0-3: Coin A (0x0F = 1 coin 1 play, 0x00 = Free Play)
// bits 4-7: Coin B (0xF0 = 1 coin 1 play, 0x00 = Invalid/slot disabilitati)
#define POOYAN_DSW0  0xFF  // 1 coin 1 play both slots

// DSW1 (read at 0xA000)
// bits 0-1: Lives (0x03=3, 0x02=4, 0x01=5, 0x00=255 cheat)
// bit 2: Cabinet (0x00=Upright, 0x04=Cocktail)
// bit 3: Bonus (0x08=50K 80K+, 0x00=30K 70K+)
// bits 4-6: Difficulty (0x70=1 Easy ... 0x00=8 Hard)
// bit 7: Demo sounds (0x00=ON, 0x80=OFF)

#define POOYAN_DSW1_003_LIVES      0x03
#define POOYAN_DSW1_004_LIVES      0x02
#define POOYAN_DSW1_005_LIVES      0x01
#define POOYAN_DSW1_255_LIVES      0x00
#define POOYAN_DSW1_UPRIGHT        0x00
#define POOYAN_DSW1_COCKTAIL       0x04
#define POOYAN_DSW1_BONUS_50K_80K  0x08
#define POOYAN_DSW1_BONUS_30K_70K  0x00

#define POOYAN_DSW1_DIFFICULTY_1   0x70
#define POOYAN_DSW1_DIFFICULTY_2   0x60
#define POOYAN_DSW1_DIFFICULTY_3   0x50
#define POOYAN_DSW1_DIFFICULTY_4   0x40
#define POOYAN_DSW1_DIFFICULTY_5   0x30
#define POOYAN_DSW1_DIFFICULTY_6   0x20
#define POOYAN_DSW1_DIFFICULTY_7   0x10
#define POOYAN_DSW1_DIFFICULTY_8   0x00

#define POOYAN_DSW1_DEMO_SOUND_ON  0x00
#define POOYAN_DSW1_DEMO_SOUND_OFF 0x80

#define POOYAN_DSW1 (POOYAN_DSW1_003_LIVES | POOYAN_DSW1_UPRIGHT | POOYAN_DSW1_BONUS_50K_80K | POOYAN_DSW1_DIFFICULTY_1 | POOYAN_DSW1_DEMO_SOUND_ON)

// IN0 at 0xA080: coins, starts
// IN1 at 0xA0A0: P1 controls (2-way up/down + button)
// IN2 at 0xA0C0: P2 controls

#endif
