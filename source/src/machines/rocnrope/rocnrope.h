#ifndef ROCNROPE_H
#define ROCNROPE_H

#include "rocnrope_logo.h"
#include "rocnrope_main_rom.h"
#include "rocnrope_audio_rom.h"
#include "rocnrope_tilemap.h"
#include "rocnrope_spritemap.h"
#include "rocnrope_dipswitches.h"
#include "../tileaddr.h"
#include "../machineBase.h"
#include "../../cpus/m6809/m6809.h"

// ============================================================
// Roc'n Rope (Konami 1983) memory map:
//
// Main CPU: KONAMI-1 (MC6809E + address-dependent opcode encryption) @ 1.536 MHz
//   0x3000       read: DSW2
//   0x3080       read: SYSTEM (coin/start)
//   0x3081       read: P1 joystick
//   0x3082       read: P2 joystick
//   0x3083       read: DSW1 (coinage)
//   0x3100       read: DSW3 (bonus)
//   0x4000-0x402F: Sprite RAM bank 1 (color/flip) — spriteram[1]
//   0x4030-0x43FF: Work RAM
//   0x4400-0x442F: Sprite RAM bank 0 (pos/code) — spriteram[0]
//   0x4430-0x47FF: Work RAM
//   0x4800-0x4BFF: Color RAM (attribute, 1KB)
//   0x4C00-0x4FFF: Video RAM (tile codes, 1KB)
//   0x5000-0x5FFF: Work RAM (4KB)
//   0x6000-0xFFFF: ROM (40KB, 5x8KB, KONAMI-1 encrypted)
//   0x8000        write: Watchdog
//   0x8080-0x8087 write: LS259 latch (flip, sound IRQ, mute, coin, IRQ enable)
//   0x8100        write: Sound data latch
//   0x8182-0x818D write: Interrupt vectors (→ RAM at 0xFFF2-0xFFFD)
//   0xFFF2-0xFFFD: Interrupt vector RAM (writable, overlays ROM 0xFFF at read)
//
// Audio CPU: Z80 @ 1.789 MHz (timeplt_audio)
//   0x0000-0x1FFF: ROM (2x4KB)
//   0x3000-0x3FFF: RAM (1KB mirrored)
//   0x4000-0x4FFF: AY#1 data r/w
//   0x5000-0x5FFF: AY#1 address w
//   0x6000-0x6FFF: AY#2 data r/w
//   0x7000-0x7FFF: AY#2 address w
//   Port A read = sound latch
// ============================================================

// Memory layout:
//   work_ram[0x0000-0x1FFF]: work RAM (CPU 0x0000-0x3FFF, 8KB mirrored: addr & 0x1FFF)
//   memory[0x0000-0x1FFF]:   sprite/video RAM (CPU 0x4000-0x5FFF, map: addr - 0x4000)
//   memory[0x2000-0x200B]:   interrupt vectors (CPU 0xFFF2-0xFFFD, 12 bytes)
// work_ram and memory[] are SEPARATE to prevent the RAM self-test from detecting
// false collisions between the two address regions.

#define RNR_COLORRAM  0x0800   // CPU 0x4800 → MEM[0x0800]
#define RNR_VIDEORAM  0x0C00   // CPU 0x4C00 → MEM[0x0C00]
#define RNR_SPRITE0   0x0400   // CPU 0x4400 → MEM[0x0400] (pos/code)
#define RNR_SPRITE1   0x0000   // CPU 0x4000 → MEM[0x0000] (color/flip)
#define RNR_VECTORS   0x2000   // CPU 0xFFF2 → MEM[0x2000] (12 bytes)

class rocnrope : public machineBase
{
public:
  rocnrope() {}
  ~rocnrope() {}

  signed char machineType() override { return MCH_ROCNROPE; }
  void start() override;
  void reset() override;

  // M6809 main CPU callbacks
  unsigned char m6809_read(m6809_state *s, uint16_t addr) override;
  void m6809_write(m6809_state *s, uint16_t addr, uint8_t val) override;
  unsigned char m6809_read_opcode(m6809_state *s, uint16_t addr) override;

  // Z80 sound CPU callbacks
  unsigned char opZ80(unsigned short Addr) override;
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;

  // persistent high scores. The regions live in main CPU (M6809) address
  // space, so the Z80 defaults of hiscoreRead/Write don't fit here.
  const char *hiscoreKey() override { return "rocnrope"; }
  const hiscore_region_S *hiscoreRegions(unsigned char *count) override;
  unsigned char hiscoreRead(unsigned short addr) override { return m6809_read(&main_cpu, addr); }
  void hiscoreWrite(unsigned short addr, unsigned char value) override { m6809_write(&main_cpu, addr, value); }

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
  // Work RAM: CPU 0x0000-0x3FFF (8KB, mirrored: addr & 0x1FFF)
  // Kept SEPARATE from memory[] which holds sprite/video RAM (CPU 0x4000-0x5FFF).
  // The RAM test writes different patterns to work RAM and sprite/video RAM;
  // sharing memory[] for both caused the self-test to fail.
  uint8_t work_ram[0x2000];

  // M6809 main CPU state
  m6809_state main_cpu;

  // Control registers
  uint8_t irq_mask      = 0;
  uint8_t snd_irq_last  = 0;
  uint8_t snd_irq_pend  = 0;
  uint8_t soundlatch    = 0;

  // Sound CPU state (timeplt_audio)
  uint8_t snd_ram[1024];
  uint8_t ay_addr[2];
  uint8_t ay_regs[2][16];
  unsigned long snd_icnt = 0;

#ifdef LED_PIN
  const CRGB menu_leds[7] = { LED_YELLOW, LED_GREEN, LED_RED, LED_WHITE, LED_RED, LED_GREEN, LED_YELLOW };
#endif
};

#endif
