#ifndef ALIBABA_H
#define ALIBABA_H

#include "alibaba_rom.h"
#include "alibaba_dipswitches.h"
#include "alibaba_logo.h"
#include "alibaba_tilemap.h"
#include "alibaba_spritemap.h"
#include "alibaba_clockmap.h"
#include "alibaba_cmap.h"
#include "../tileaddr.h"
#include "../pacman/pacman.h"

// ============================================================================
// Ali Baba and 40 Thieves (Sega, 1982) -- driver MAME pacman.cpp, ROM_START(alibaba)
// Hardware identica a Pac-Man (stesso schema tile/sprite/RAM, stesso chip
// audio Namco WSG), verificato via CRC32 esatto di tutte le 15 ROM.
//
// Differenze rispetto al pacman.h/cpp base:
//  - ROM programma NON contigua: 0x0000-0x3FFF + 0x8000-0x8FFF + 0xA000-0xA7FF
//    (alibaba_rom.h e' un array sparso da 0xA800 byte con i dati gia' posti
//    ai rispettivi offset CPU reali, il resto e' zero e mai fetchato).
//  - Latch di controllo su 2 LS259 separati (non uno solo come pacman base):
//    latch1 @0x5000-0x5007 (bit4/5=LED, bit6=coin lockout, bit7=coin counter,
//    tutti cosmetici, ignorati), latch2 @0x50C0-0x50C7 (bit0=abilita audio
//    Namco, bit1=flip screen, **bit2=irq mask** -- attenzione, NON e' il bit0
//    di 0x5000 come nel pacman base).
//  - Meccanismo "mystery" (lampada/orologio premio): stato a 3 variabili
//    (mystery_control/clock/prescaler), ticchetta ogni vblank, 2 registri di
//    lettura + 1 di scrittura. Grafica dedicata (gfx2/alibaba_clockmap, 32
//    fotogrammi di rotazione lancetta) sovrapposta al rendering standard.
//  - Sound register non contiguo: sound_w rimappa l'offset prima di scrivere
//    sui registri Namco (vedi wrZ80).
// ============================================================================

class alibaba : public pacman
{
public:
	alibaba() { }
	~alibaba() { }

	signed char machineType() override { return MCH_ALIBABA; }

	// high score persistente (NVS) -- chiave/regioni proprie
	const char *hiscoreKey() override { return "alibaba"; }
	const hiscore_region_S *hiscoreRegions(unsigned char *count) override;

	unsigned char rdZ80(unsigned short Addr) override;
	void wrZ80(unsigned short Addr, unsigned char Value) override;
	unsigned char opZ80(unsigned short Addr) override;

	void run_frame(void) override;
	void prepare_frame(void) override;
	void render_row(short row) override;
	const unsigned short *logo(void) override;

protected:
	const unsigned short *tileRom(unsigned short addr) override;
	const unsigned short *colorRom(unsigned short addr) override;
	const unsigned long *spriteRom(unsigned char flags, unsigned char code) override;

	void blit_clock(short row);

private:
	unsigned char irq_mask = 0;
	unsigned char flip_screen = 0;

	// "mystery" lamp/clock (vedi MAME alibaba_state::mystery_*)
	unsigned char mystery_control = 0;
	unsigned char mystery_clock = 0;
	unsigned char mystery_prescaler = 0;
};

#endif
