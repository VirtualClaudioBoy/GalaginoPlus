#ifndef POOYAN_H
#define POOYAN_H

#include "pooyan_logo.h"
#include "pooyan_rom.h"
#include "pooyan_snd_rom.h"
#include "pooyan_dipswitches.h"
#include "pooyan_tilemap.h"
#include "pooyan_spritemap.h"
#include "../tileaddr.h"
#include "../machineBase.h"

// Pooyan (Konami 1982) memory map — hardware gemello di Time Pilot, gfx 4bpp:
//   Main CPU (Z80 @ 3.072 MHz):
//     0x0000-0x7FFF: ROM (32KB: 1.4a+2.5a+3.6a+4.7a)
//     0x8000-0x83FF: Color RAM (1KB)
//     0x8400-0x87FF: Video RAM (1KB)
//     0x8800-0x8FFF: Work RAM (2KB)
//     0x9000-0x90FF: Sprite RAM bank 0 (mirror 0x0B00)
//     0x9400-0x94FF: Sprite RAM bank 1 (mirror 0x0B00)
//     0xA000 read:  DSW1 (lives/bonus/difficulty/demo)  [mirror 0x5E7F]
//     0xA080 read:  IN0 (coins, start)                  [mirror 0x5E1F]
//     0xA0A0 read:  IN1 (P1: up/down + fire)
//     0xA0C0 read:  IN2 (P2)
//     0xA0E0 read:  DSW0 (coinage)
//     0xA000 write: Watchdog
//     0xA100 write: Sound latch
//     0xA180-0xA187 write: LS259 (Q0 NMI enable, Q1 snd irq, Q2 mute,
//                          Q3/Q4 coin counter, Q7 flip inverted)
//   Niente scanline counter (no multiplex sprite), niente video enable.

// Memory layout in our buffer:
//   0x0000-0x03FF: Color RAM (1KB) [from 0x8000]
//   0x0400-0x07FF: Video RAM (1KB) [from 0x8400]
//   0x0800-0x0FFF: Work RAM (2KB) [from 0x8800]
//   0x1000-0x10FF: Sprite RAM bank 0 [from 0x9000]
//   0x1100-0x11FF: Sprite RAM bank 1 [from 0x9400]
//   Total: 0x1200 = 4608 bytes (fits in RAMSIZE 16384)

#define POOYAN_COLORRAM  0x0000
#define POOYAN_VIDEORAM  0x0400
#define POOYAN_WORKRAM   0x0800
#define POOYAN_SPRITES0  0x1000
#define POOYAN_SPRITES1  0x1100

class pooyan : public machineBase
{
public:
	pooyan() { }
	~pooyan() { }

	signed char machineType() override { return MCH_POOYAN; }

	// persistent high scores. rdZ80/wrZ80 dispatch on current_cpu, which the
	// emulation task flips mid-frame on the other core — access memory[]
	// directly instead. Covers color (0x8000), video (0x8400) and work
	// (0x8800-0x8FFF) RAM, which is all the hiscore regions use.
	const char *hiscoreKey() override { return "pooyan"; }
	const hiscore_region_S *hiscoreRegions(unsigned char *count) override;
	void hiscoreRestored() override;
	unsigned char hiscoreRead(unsigned short addr) override {
		if (addr < 0x8400) return memory[POOYAN_COLORRAM + (addr & 0x03FF)];
		if (addr < 0x8800) return memory[POOYAN_VIDEORAM + (addr & 0x03FF)];
		return memory[POOYAN_WORKRAM + (addr & 0x07FF)];
	}
	void hiscoreWrite(unsigned short addr, unsigned char value) override {
		if (addr < 0x8400)      memory[POOYAN_COLORRAM + (addr & 0x03FF)] = value;
		else if (addr < 0x8800) memory[POOYAN_VIDEORAM + (addr & 0x03FF)] = value;
		else                    memory[POOYAN_WORKRAM  + (addr & 0x07FF)] = value;
	}

	unsigned char rdZ80(unsigned short Addr) override;
	void wrZ80(unsigned short Addr, unsigned char Value) override;
	unsigned char opZ80(unsigned short Addr) override;

	void run_frame(void) override;
	void prepare_frame(void) override;
	void render_row(short row) override;
	const unsigned short *logo(void) override;

#ifdef LED_PIN
	void menuLeds(CRGB *leds) override;
	void gameLeds(CRGB *leds) override;
#endif

protected:
	void blit_tile(short row, char col) override;
	void blit_sprite(short row, unsigned char s) override;

private:
	unsigned char nmi_enable;
	unsigned char flip_screen;
	unsigned char soundlatch;

	// Sound CPU state (timeplt_audio, identico a Time Pilot)
	Z80 snd_cpu;
	unsigned char snd_ram[1024];
	unsigned char snd_irq_pending = 0;
	unsigned char snd_irq_last = 0;    // previous Q1 state for edge detection
	unsigned long snd_icnt = 0;

	// AY-3-8910 registers
	unsigned char ay_addr[2];
	unsigned char ay_regs[2][16];

#ifdef LED_PIN
	const CRGB menu_leds[7] = { LED_RED, LED_YELLOW, LED_RED, LED_YELLOW, LED_RED, LED_YELLOW, LED_RED };
#endif
};

#endif
