#ifndef LIZWIZ_H
#define LIZWIZ_H

#include "lizwiz_rom.h"
#include "lizwiz_dipswitches.h"
#include "lizwiz_logo.h"
#include "lizwiz_tilemap.h"
#include "lizwiz_spritemap.h"
#include "lizwiz_cmap.h"
#include "lizwiz_wavetable.h"
#include "../tileaddr.h"
#include "../pacman/pacman.h"

class lizwiz : public pacman
{
public:
	lizwiz() { }
	~lizwiz() { }

	signed char machineType() override { return MCH_LIZWIZ; } 
	unsigned char rdZ80(unsigned short Addr) override;
	void wrZ80(unsigned short Addr, unsigned char Value) override;
	void outZ80(unsigned short Port, unsigned char Value) override;
	unsigned char opZ80(unsigned short Addr) override;

	void run_frame(void) override;
	const signed char *waveRom(unsigned char value) override;
	const unsigned short *logo(void) override;

	// High score persistente (NVS). Chiave/regioni proprie (il gate in
	// pacman::hiscoreKey() esclude i cloni dalla chiave "pacman"), stessa
	// mappa RAM del pacman hardware -> default hiscoreRead/Write vanno bene.
	const char *hiscoreKey() override { return "lizwiz"; }
	const hiscore_region_S *hiscoreRegions(unsigned char *count) override;

protected:
	const unsigned short *tileRom(unsigned short addr) override;
	const unsigned short *colorRom(unsigned short addr) override;
	const unsigned long *spriteRom(unsigned char flags, unsigned char code) override;

private:
};

#endif