#ifndef VANVAN_H
#define VANVAN_H

#include "vanvan_dipswitches.h"
#include "vanvan_logo.h"
#include "vanvan_rom.h"
#include "vanvan_rom2.h"
#include "vanvan_tilemap.h"
#include "vanvan_spritemap.h"
#include "vanvan_cmap.h"
#include "../tileaddr.h"
#include "../machineBase.h"

// ============================================================
// Van Van Car (Sanritsu 1983) memory map (MAME pacman.cpp: vanvan/dremshpr_map):
//
// Main CPU: Z80 @ 3.072 MHz, standard Pac-Man hardware
//   0x0000-0x3fff: ROM (16KiB, vanvan_rom)
//   0x4000-0x43ff: Video RAM (mirrored every 0xa000)
//   0x4400-0x47ff: Color RAM (mirrored)
//   0x4800-0x4fef: RAM (mirrored)
//   0x4ff0-0x4fff: Sprite RAM (mirrored)
//   0x5000-0x5007: LS259 latch write (bit0 = NMI enable)
//   0x5060-0x506f: Sprite RAM bank 2 (write only)
//   0x5000: IN0 read, 0x5040: IN1 read, 0x5080: DSW1 read, 0x50c0: DSW2 read
//   0x8000-0x8fff: ROM extra bank (16KiB region, vanvan_rom2)
//
// I/O ports (out only):
//   0x01: SN76496 chip 1 write
//   0x02: SN76496 chip 2 write
//
// Interrupt: NMI on vblank, masked by latch bit 0 (no vectored IRQ)
//
// Video: same tile/sprite hardware as Pac-Man, but only 32 of the 36
// native tile rows are visible (2 rows cropped on each side) -> in this
// project's convention ("row" = native column axis, "col" = native row
// axis, col always 0-27) this means: no crop on col, crop row<2 || row>33.
// ============================================================

class vanvan : public machineBase
{
public:
  vanvan() {}
  ~vanvan() {}

  void reset() override;
  signed char machineType() override { return MCH_VANVAN; }
  signed char videoFlipY() override { return 1; }

  unsigned char opZ80(unsigned short Addr) override;
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;
  void outZ80(unsigned short Port, unsigned char Value) override;

  // persistent high scores
  const char *hiscoreKey() override { return "vanvan"; }
  const hiscore_region_S *hiscoreRegions(unsigned char *count) override;

  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  const unsigned short *logo(void) override;
  bool hasNamcoAudio() override { return false; }

#ifdef LED_PIN
  void menuLeds(CRGB *leds) override;
  void gameLeds(CRGB *leds) override;
#endif

protected:
  void blit_tile(short row, char col) override;
  void blit_sprite(short row, unsigned char s) override;

private:
  void SN76489_Write_2chip(int chip, unsigned char data);
  int sn_last_register[2] = {0, 0};
  unsigned char nmi_enable = 0;
  unsigned short startupFrameCount = 0;
  unsigned long lastFrameMs = 0;

#ifdef LED_PIN
  const CRGB menu_leds[7] = { LED_RED, LED_YELLOW, LED_RED, LED_WHITE, LED_RED, LED_YELLOW, LED_RED };
#endif
};

#endif
