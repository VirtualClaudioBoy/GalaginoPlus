#ifndef anteater_H
#define anteater_H

#include "anteater_rom1.h"
#include "anteater_rom2.h"
#include "anteater_dipswitches.h"
#include "anteater_logo.h"
#include "anteater_tilemap.h"
#include "anteater_spritemap.h"
#include "anteater_cmap.h"
#include "../tileaddr.h"
#include "../frogger/frogger.h"

class anteater : public frogger
{
public:
	anteater() { }
	~anteater() { }

 	void reset() override;

	signed char machineType() override { return MCH_ANTEATER; } 
	unsigned char rdZ80(unsigned short Addr) override;
	void wrZ80(unsigned short Addr, unsigned char Value) override;
	void outZ80(unsigned short Port, unsigned char Value) override;
	unsigned char opZ80(unsigned short Addr) override;
	unsigned char inZ80(unsigned short Port) override;

	void run_frame(void) override;
	void prepare_frame(void) override;
	void render_row(short row) override;

	const unsigned short *logo(void) override;

	// High score persistente (NVS). rdZ80/wrZ80 dispatchano su current_cpu
	// (come frogger, da cui eredita l'hardware), che a fine frame resta a 1
	// a meno che irq_enable[0] sia attivo -> accesso diretto alla RAM
	// principale (0x8000-0x87FF), bypassando il dispatch.
	const char *hiscoreKey() override { return "anteater"; }
	const hiscore_region_S *hiscoreRegions(unsigned char *count) override;
	unsigned char hiscoreRead(unsigned short addr) override { return memory[addr - 0x8000]; }
	void hiscoreWrite(unsigned short addr, unsigned char value) override { memory[addr - 0x8000] = value; }

#ifdef LED_PIN	
	void menuLeds(CRGB *leds) override;
	void gameLeds(CRGB *leds) override;
#endif

protected:
	void blit_tile(short row, char col) override;
	void blit_sprite(short row, unsigned char s) override;
	void blit_tile_scroll(short row, signed char col, short scroll);

	virtual const unsigned short *tileRom(unsigned short addr) override;
	virtual const unsigned short *colorRom(unsigned short addr) override;
	virtual const unsigned long *spriteRom(unsigned char flags, unsigned char code) override;

private:
	unsigned char showCustomBackground;
	unsigned char ignoreFireButton;
	
#ifdef LED_PIN
	const CRGB menu_leds[7] = { LED_RED, LED_GREEN, LED_YELLOW, LED_YELLOW, LED_YELLOW, LED_GREEN, LED_RED };
#endif

};

#endif