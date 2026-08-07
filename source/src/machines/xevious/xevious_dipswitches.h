#ifndef _xevious_dipswitches_h_
#define _xevious_dipswitches_h_

// DSWA/DSWB lette via bosco_dsw_r a 0x6800-0x6807 (galaga.cpp, formula
// ESATTA verificata sul sorgente reale: bit0=(DSWB>>offset)&1,
// bit1=(DSWA>>offset)&1, offset=indirizzo&7 -- NESSUN bit-reversal/
// inversione, a differenza dello shortcut di galaga.cpp che ne ha
// entrambi (bug trovato con harness Python: con quella formula DSWA=DSWB
// =0xFF produceva un valore che falliva un controllo di boot in CPU1,
// bloccando l'handshake con CPU2 per sempre -- vedi romconv/xevious/
// harness_cpu1.py/harness_cpu12.py). Default 0xFF = valori di fabbrica
// MAME (INPUT_PORTS_START(xevious): Coin_A 1C1C, Lives 3, Upright,
// Bonus_Life factory default, Difficulty Normal, Freeze Off).
#define XEVIOUS_DSWA  0xFF
#define XEVIOUS_DSWB  0xFF

#endif
