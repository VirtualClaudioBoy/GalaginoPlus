#ifndef _pbaction_dipswitches_h_
#define _pbaction_dipswitches_h_

// Pinball Action (set 1) - DIP switches.
// Verified against `mame pbaction -listxml` (MAME 0.288) and the driver
// INPUT_PORTS_START(pbaction) in tecmo/pbaction.cpp.
//
// Both DSW ports are IP_ACTIVE_HIGH: the value below is what the CPU reads
// at 0xe604 (DSW1) / 0xe605 (DSW2).  Bit 0x80 of DSW1 ("Demo Sounds",
// 0 = On) is OR-ed in at runtime from input->demoSoundsOff(), so it is
// left as 0 here.

// ---- DSW1 (port 0xe604) -------------------------------------------------
//  bit 0-1  Coin B      0=1C/1C  1=1C/2C  2=1C/3C  3=1C/6C
//  bit 2-3  Coin A      4=2C/1C  0=1C/1C  8=1C/2C  0x0c=1C/3C
//  bit 4-5  Lives       0x30=2   0=3      0x10=4   0x20=5
//  bit 6    Cabinet     0x40=Upright   0=Cocktail
//  bit 7    Demo Sounds 0x80=Off  0=On          (set from demoSoundsOff())
#define PBACTION_DSW1  0x40   // Coin A/B 1C/1C, 3 lives, Upright

// ---- DSW2 (port 0xe605) -----------------------------------------------
//  bit 0-2  Bonus Life  0="70k 200k" 1="70k 200k 1000k" 2="100k" 3="100k 300k"
//                       4="100k 300k 1000k" 5="200k" 6="200k 1000k" 7=None
//  bit 3    Extra        0=Easy   0x08=Hard
//  bit 4-5  Difficulty (Flippers)  0=Easy 0x10=Medium 0x20=Hard 0x30=Hardest
//  bit 6-7  Difficulty (Outlanes)  0=Easy 0x40=Medium 0x80=Hard 0xc0=Hardest
#define PBACTION_DSW2  0x00   // "70k 200k", Extra Easy, both difficulties Easy

// Demo Sounds bit (DSW1 bit 7) - OR-ed in when demo sounds are switched off.
#define PBACTION_DSW1_DEMOSOUNDS_OFF  0x00

#endif
