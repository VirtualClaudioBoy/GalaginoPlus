#ifndef CIRCUSC_H
#define CIRCUSC_H

// ============================================================================
// Circus Charlie (Konami 1984, GX380) — riferimento MAME konami/circusc.cpp
//
// Main CPU: KONAMI-1 (M6809E con XOR degli opcode dipendente dall'indirizzo,
//   stessa cifratura di Roc'n Rope) @ 18.432/12 = 1.536 MHz
//   0x0000-0x0007 (mirror 0x3F8) W: LS259 (Q0 flip, Q1 irq mask/INTST,
//     Q2 mute, Q3/Q4 coin counter, Q5 sprite bank OBJ CHENG)
//   0x0400 W watchdog; 0x0800 W soundlatch; 0x0C00 W trigger IRQ Z80 audio
//   0x1000 R SYSTEM, 0x1001 R P1, 0x1002 R P2, 0x1400 R DSW1, 0x1800 R DSW2
//   0x1C00 W scroll (il DATO scritto, non l'offset)
//   0x2000-0x3FFF RAM: 0x3000 colorram 1KB, 0x3400 videoram 1KB,
//     0x3800-0x39FF sprite RAM (2 banchi da 0x100), resto work RAM
//   0x6000-0xFFFF ROM 40KB
//
// Audio: Z80 @ 14.318/4 = 3.58 MHz, ROM 16KB, RAM 1KB @0x4000 (mirror),
//   0x6000 R soundlatch, 0x8000 R timer ((cicli>>9)&0x1E),
//   0xA000+off W: off&7 = 0 latch SN, 1 SN76489A#1, 2 SN76489A#2, 3 DAC
//   8 bit, 4 filtri RC discreti (ignorati). 2x SN76489A @ 1.79 MHz.
//
// Video ROT90, ricetta timeplt (strip galagino r=2..33 = colonna MAME
// tx=r-2, colonna galagino c = riga MAME ty=29-c via tileaddr[][]):
//   tile 8x8 4bpp (512, code = vram + (attr&0x20)<<3), 16 gruppi colore,
//   flip: attr bit6/bit7. PRIORITA' (inversa di timeplt!): MAME disegna
//   prima i tile con attr bit4=1 (SOTTO gli sprite), poi gli sprite, poi
//   i tile con bit4=0 OPACHI SOPRA. Scroll per colonna: colonne MAME 0-9
//   (strip 2-11) fisse = HUD, colonne 10-31 (strip 12-33) scrollate
//   verticalmente (in portrait: orizzontalmente) del valore 0x1C00.
//   Sprite 16x16 4bpp (384), double-buffered: a vblank si fotografa il
//   banco scelto da Q5; 64 slot da 4 byte; trasparenza = pen che mappa
//   come il pen 0 del gruppo (transpen_mask, gia' nel cmap del converter).
// ============================================================================

#include "circusc_logo.h"
#include "circusc_main_rom.h"
#include "circusc_audio_rom.h"
#include "circusc_tilemap.h"
#include "circusc_spritemap.h"
#include "circusc_cmap.h"
#include "circusc_dipswitches.h"
#include "../tileaddr.h"
#include "../machineBase.h"
#include "../../cpus/m6809/m6809.h"

// Offset in memory[] (= CPU addr - 0x2000, regione RAM 0x2000-0x3FFF)
#define CC_COLORRAM   0x1000
#define CC_VIDEORAM   0x1400
#define CC_SPRITERAM  0x1800   // 2 banchi da 0x100

// Il render SN76489 di audio.cpp usa sn_inc=11 fisso, tarato per i chip
// a ~4.2MHz (Mr.Do/Ladybug). I due SN76489A di Circus Charlie girano a
// 14.318/8 = 1.79MHz: i periodi vanno scalati di 4.224/1.790 = x2.36.
// Se su HW la musica risulta stonata, ritoccare qui (151/64 = 2.359).
#define CIRCUSC_SN_PERIOD_SCALE(n)  (((n) * 151) >> 6)

// Volume del DAC 8 bit (effetti/percussioni) nel mix con gli SN;
// il campione esce gia' scalato da renderDrumSample (max 127*vol).
#define CIRCUSC_DAC_VOLUME  2

// Ring buffer DAC (stile gyruss drums: si produce nel run_frame sul core
// emulazione, si consuma nel render audio — MAI steppare CPU nel render!)
#define CC_DAC_RING  1024

class circusc : public machineBase
{
public:
  circusc() {}
  ~circusc() {}

  signed char machineType() override { return MCH_CIRCUSC; }
  void start() override;
  void reset() override;

  // M6809 main CPU (KONAMI-1)
  unsigned char m6809_read(m6809_state *s, uint16_t addr) override;
  void m6809_write(m6809_state *s, uint16_t addr, uint8_t val) override;
  unsigned char m6809_read_opcode(m6809_state *s, uint16_t addr) override;

  // Z80 sound CPU
  unsigned char opZ80(unsigned short Addr) override;
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;

  // campione DAC per il mix in sn76489_render_buffer (pop dal ring)
  int renderDrumSample() override {
    if (dac_rd != dac_wr) {
      dac_last = dac_ring[dac_rd];
      dac_rd = (dac_rd + 1) & (CC_DAC_RING - 1);
    }
    return ((int)dac_last - 128) * CIRCUSC_DAC_VOLUME;
  }

  // High score persistente (hiscore.dat konami/circusc.cpp): regioni in
  // RAM/videoram del main (CPU 0x2000-0x3FFF) -> passa da m6809_read/write
  const char *hiscoreKey() override { return "circusc"; }
  const hiscore_region_S *hiscoreRegions(unsigned char *count) override;
  unsigned char hiscoreRead(unsigned short addr) override { return m6809_read(&main_cpu, addr); }
  void hiscoreWrite(unsigned short addr, unsigned char value) override { m6809_write(&main_cpu, addr, value); }

  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  const unsigned short *logo(void) override { return circusc_logo; }

protected:
  void blit_sprite(short row, unsigned char s) override;

private:
  void blit_tile_x(unsigned short addr, short x, unsigned char pass);
  void render_tiles(short row, unsigned char pass);
  void sn_write(int chip, unsigned char data);

  m6809_state main_cpu;

  // snapshot vblank del banco sprite attivo (double buffering hardware)
  uint8_t spr_buf[0x100];

  // registri di controllo
  uint8_t irq_mask   = 0;
  uint8_t spritebank = 0;
  uint8_t scroll     = 0;
  uint8_t soundlatch = 0;

  // audio Z80
  uint8_t snd_ram[1024];
  uint8_t snd_irq_pend = 0;
  uint8_t sn_latch = 0;
  uint8_t sn_last_reg[2] = {0, 0};
  unsigned long snd_icnt = 0;   // per il timer 0x8000 (vedi rdZ80)

  // DAC ring (produttore: run_frame, ~400 campioni/frame a 24kHz)
  volatile uint8_t  dac_ring[CC_DAC_RING];
  volatile uint16_t dac_wr = 0, dac_rd = 0;
  uint8_t dac_val = 128, dac_last = 128;
  int dac_acc = 0;
};

#endif
