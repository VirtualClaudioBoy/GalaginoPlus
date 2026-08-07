#ifndef AMIDAR_DIPSWITCHES_H
#define AMIDAR_DIPSWITCHES_H

// PPI #0 Port A (0xB000) = IN0  — identical to Turtles
// bit | 7     | 6     | 5    | 4     | 3       | 2       | 1  | 0        |
//     | Coin1 | Coin2 | Left | Right | Button1 | Service | ?  | Up(cock) |
#define AMIDAR_IN0_IDLE  0b11111111

// PPI #0 Port B (0xB010) = IN1
// bit | 7      | 6      | 5       | 4       | 3       | 2  | 1:0  |
//     | Start1 | Start2 | L(cock) | R(cock) | B(cock) | ?  | Lives|
// Lives encoding: 11=3, 10=4, 01=5, 00=255(cheat)  [reversed vs Turtles]
#define AMIDAR_IN1_IDLE    0b11111100
#define AMIDAR_IN1_3_LIVES 0b00000011
#define AMIDAR_IN1_4_LIVES 0b00000010
#define AMIDAR_IN1_5_LIVES 0b00000001

// PPI #0 Port C (0xB020) = IN2
// bit | 7   | 6    | 5   | 4  | 3       | 2          | 1           | 0        |
//     | dip | Down | dip | Up | Cabinet | Bonus_Life | Demo_Sounds | D(cock)  |
// Demo Sounds: 0=On, 1=Off
// Bonus Life:  0=30000/50000, 1=50000/50000
#define AMIDAR_IN2_IDLE       0b11110000
#define AMIDAR_IN2_DEMO_ON    0b00000000
#define AMIDAR_IN2_DEMO_OFF   0b00000010
#define AMIDAR_IN2_BONUS_LOW  0b00000000
#define AMIDAR_IN2_BONUS_HIGH 0b00000100
#define AMIDAR_IN2_UPRIGHT    0b00000000
#define AMIDAR_IN2_COCKTAIL   0b00001000

// PPI #0 Port D (0xB030) = IN3 — Coinage
// bits 3:0 = Coin A, bits 7:4 = Coin B
// 1C/1C: CoinA=0x0F, CoinB=0xF0 → 0xFF (matches Turtles default of 0xFF for port 3)
#define AMIDAR_IN3_1C_1C 0xFF

#define AMIDAR_IN0_VALUE  (AMIDAR_IN0_IDLE)
#define AMIDAR_IN1_VALUE  (AMIDAR_IN1_IDLE | AMIDAR_IN1_3_LIVES)
#define AMIDAR_IN2_VALUE  (AMIDAR_IN2_IDLE | AMIDAR_IN2_DEMO_ON | AMIDAR_IN2_BONUS_LOW | AMIDAR_IN2_UPRIGHT)
#define AMIDAR_IN3_VALUE  AMIDAR_IN3_1C_1C

#endif
