#ifndef PHOENIX_DIPSWITCHES_H
#define PHOENIX_DIPSWITCHES_H
// DSW0 (MAME phoenix, verificato su driver E:\Download\phoenix.cpp):
//   bit 0-1 = lives   (0=3, 1=4, 2=5, 3=6)          -> 0x03 (6 vite, richiesto utente)
//   bit 2-3 = bonus   (0=3K/30K, 1=4K/40K, ...)     -> 0x00 (piu' generoso: vita extra
//                                                       piu' presto = piu' facile;
//                                                       NB Phoenix non ha una vera dip
//                                                       "Difficulty", questo e' l'unico
//                                                       parametro che vi si avvicina)
//   bit 4   = coinage (0=1C/1C, 1=2C/1C)            -> 0x00 (1 coin 1 credito)
//   bit 5   = unknown (default Off = 1)             -> 0x20
//   bit 6   = unknown (default Off = 1)             -> 0x40
//   bit 7   = VBLANK live (gestito dinamicamente nel rdZ80)
// Totale bit 0-6 = 0x63 (default MAME 0x60 con vite=6 invece di 3).
#define PHOENIX_DSW0 0x63
#endif
