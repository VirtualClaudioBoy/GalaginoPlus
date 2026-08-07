#ifndef TURTLES_H
#define TURTLES_H

#include "turtles_logo.h"
#include "turtles_main_rom.h"
#include "turtles_audio_rom.h"
#include "turtles_spritemap.h"
#include "turtles_tilemap.h"
#include "turtles_cmap.h"
#include "turtles_dipswitches.h"
#include "../tileaddr.h"
#include "../machineBase.h"
#include "../scramble/scramble.h"

// ============================================================
// Turtles (Konami 1981) memory map:
// Main CPU (Z80 @ 3.072 MHz):
//   0x0000-0x4fff: ROM (5x4KB = 20KB)
//   0x8000-0x87ff: Work RAM (2KB)
//   0x9000-0x93ff: Video RAM (1KB)
//   0x9800-0x983f: Attribute RAM (64 bytes)
//   0x9840-0x985f: Sprite RAM (8 sprites x 4 bytes)
//   0x9860-0x987f: Extra RAM
//   0x9880-0x98ff: Extra RAM
//   0xa008:        NMI enable
//   0xa800:        Watchdog reset
//   0xb000-0xb03f: PPI8255 #0 (inputs, port = (addr>>4)&3)
//   0xb800-0xb83f: PPI8255 #1 (sound, same decode)
//
// Audio CPU (Z80 @ 1.78975 MHz):
//   0x0000-0x1fff: ROM (2x4KB = 8KB)
//   0x8000-0x8fff: Sound RAM (4KB)
//   0x9000-0x9fff: Audio filters (ignored)
//
// IO (audio CPU, mask 0x00ff):
//   0x0010: AY8910 #1 address_w
//   0x0020: AY8910 #1 data_r/data_w
//   0x0040: AY8910 #2 address_w
//   0x0080: AY8910 #2 data_r/data_w
// ============================================================

class turtles : public scramble
{
public:
  turtles() {}
  ~turtles() {}

  signed char machineType() override { return MCH_TURTLES; }
  void start() override;

  // persistent high scores. rdZ80/wrZ80 dispatch on current_cpu, which the
  // emulation task flips mid-frame on the other core — access memory[]
  // directly instead. Valid for work RAM (0x8000) and video RAM (0x9000)
  // addresses only, which is all the hiscore regions use.
  const char *hiscoreKey() override { return "turtles"; }
  const hiscore_region_S *hiscoreRegions(unsigned char *count) override;
  void hiscoreRestored() override;   // shared with amidar (same top bar layout)
  unsigned char hiscoreRead(unsigned short addr) override {
    if (addr < CPU1_VRAM_ADDR) return memory[addr - CPU1_RAM_ADDR];
    return memory[CPU1_VRAM_OFFSET + addr - CPU1_VRAM_ADDR];
  }
  void hiscoreWrite(unsigned short addr, unsigned char value) override {
    if (addr < CPU1_VRAM_ADDR) memory[addr - CPU1_RAM_ADDR] = value;
    else memory[CPU1_VRAM_OFFSET + addr - CPU1_VRAM_ADDR] = value;
  }

  unsigned char opZ80(unsigned short Addr) override;
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;

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

  // Shared memory map (Turtles hardware = Amidar hardware)
  static constexpr unsigned short CPU1_RAM_ADDR    = 0x8000;
  static constexpr unsigned short CPU1_VRAM_ADDR   = 0x9000;
  static constexpr unsigned short CPU1_ATTR_ADDR   = 0x9800;
  static constexpr unsigned short CPU1_SPRITE_ADDR = 0x9840;
  static constexpr unsigned short CPU1_EXTRA1_ADDR = 0x9860;
  static constexpr unsigned short CPU1_EXTRA2_ADDR = 0x9880;

  static constexpr unsigned short CPU1_RAM_SIZE    = 0x0800;
  static constexpr unsigned short CPU1_VRAM_SIZE   = 0x0400;
  static constexpr unsigned short CPU1_ATTR_SIZE   = 0x0040;
  static constexpr unsigned short CPU1_SPRITE_SIZE = 0x0020;
  static constexpr unsigned short CPU1_EXTRA1_SIZE = 0x0020;
  static constexpr unsigned short CPU1_EXTRA2_SIZE = 0x0080;

  static constexpr unsigned short CPU1_RAM_OFFSET    = 0x0000;
  static constexpr unsigned short CPU1_VRAM_OFFSET   = CPU1_RAM_OFFSET    + CPU1_RAM_SIZE;
  static constexpr unsigned short CPU1_ATTR_OFFSET   = CPU1_VRAM_OFFSET   + CPU1_VRAM_SIZE;
  static constexpr unsigned short CPU1_SPRITE_OFFSET = CPU1_ATTR_OFFSET   + CPU1_ATTR_SIZE;
  static constexpr unsigned short CPU1_EXTRA1_OFFSET = CPU1_SPRITE_OFFSET + CPU1_SPRITE_SIZE;
  static constexpr unsigned short CPU1_EXTRA2_OFFSET = CPU1_EXTRA1_OFFSET + CPU1_EXTRA1_SIZE;
  static constexpr unsigned short CPU1_MEM_FREE      = CPU1_EXTRA2_OFFSET + CPU1_EXTRA2_SIZE;

  static constexpr unsigned short CPU2_RAM_ADDR   = 0x8000;
  static constexpr unsigned short CPU2_RAM_SIZE   = 0x1000;
  static constexpr unsigned short CPU2_RAM_OFFSET = CPU1_MEM_FREE;
  static constexpr unsigned short CPU2_MEM_FREE   = CPU2_RAM_OFFSET + CPU2_RAM_SIZE;
  static_assert(CPU2_MEM_FREE <= RAMSIZE, "RAMSIZE too low for turtles/amidar");

private:
  static constexpr unsigned short CPU1_ROM_SIZE = 0x5000;
  static constexpr unsigned short CPU2_ROM_SIZE = 0x2000;

  unsigned char bg_r = 0, bg_g = 0, bg_b = 0;

#ifdef LED_PIN
  const CRGB menu_leds[7] = { LED_GREEN, LED_YELLOW, LED_GREEN, LED_WHITE, LED_GREEN, LED_YELLOW, LED_GREEN };
#endif
};

#endif
