#ifndef TURTLES_DIPSWITCHES_H
#define TURTLES_DIPSWITCHES_H

// PPI #0 Port A (0xB000) = IN0
// bit | 7     | 6     | 5    | 4     | 3       | 2     | 1  | 0        |
//     | Coin1 | Coin2 | Left | Right | Button1 | Coin3 | ?  | Up(cock) |
#define TURTLES_IN0_IDLE  0b11111111

// PPI #0 Port B (0xB010) = IN1
// bit | 7      | 6      | 5       | 4       | 3       | 2  | 1:0  |
//     | Start1 | Start2 | L(cock) | R(cock) | B(cock) | ?  | Lives|
#define TURTLES_IN1_IDLE     0b11111100
#define TURTLES_IN1_3_LIVES  0b00000000
#define TURTLES_IN1_4_LIVES  0b00000001
#define TURTLES_IN1_5_LIVES  0b00000010
#define TURTLES_IN1_FREE     0b00000011

// PPI #0 Port C (0xB020) = IN2
// bit | 7   | 6    | 5   | 4  | 3       | 2:1     | 0        |
//     | dip | Down | dip | Up | Cabinet | Coinage | D(cock)  |
// Coinage encoding (verified on hardware): 00=1C_1C, 01=1C_2C, 10=1C_3C, 11=1C_4C
#define TURTLES_IN2_IDLE     0b11110000
#define TURTLES_IN2_1C_1C    0b00000000
#define TURTLES_IN2_1C_2C    0b00000010
#define TURTLES_IN2_1C_3C    0b00000100
#define TURTLES_IN2_1C_4C    0b00000110
#define TURTLES_IN2_UPRIGHT  0b00000000
#define TURTLES_IN2_COCKTAIL 0b00001000

#define TURTLES_IN0_VALUE  (TURTLES_IN0_IDLE)
#define TURTLES_IN1_VALUE  (TURTLES_IN1_IDLE | TURTLES_IN1_5_LIVES)
#define TURTLES_IN2_VALUE  (TURTLES_IN2_IDLE | TURTLES_IN2_1C_1C | TURTLES_IN2_UPRIGHT)

#endif
