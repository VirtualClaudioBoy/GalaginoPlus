#ifndef PHOENIX_H
#define PHOENIX_H

#include "../machineBase.h"

#include <pgmspace.h>
#include "phoenix_logo.h"
#include "phoenix_rom.h"
#include "phoenix_bgtiles.h"
#include "phoenix_fgtiles.h"
#include "phoenix_palette.h"
#include "phoenix_dipswitches.h"

// ============================================================================
// Phoenix (Amstar/Centuri 1980) — driver MAME phoenix/phoenix.cpp
// CPU Z80, monitor VERTICALE (cabinet ROT90). Arcade landscape MAME = 256x208
// (32 col x 26 row), mostrato in portrait 208x256. 2 layer tilemap (FG + BG),
// no IRQ (il game polla VBLANK su DSW0 bit 7), audio TMS3617 custom
// approssimato (vedi Audio::phoenix_render_buffer in emulation/audio.cpp).
//
// Porting a galagino (framebuffer 224x288 portrait, render in orientamento
// verticale nativo, NIENTE rotazione display HW — la rotazione 90 gradi e'
// cablata nella matematica dei tile, come galaxian/mooncresta).
// Finestra portrait 208x256 centrata: x offset 8 (col 0..25), righe 2..33.
//
// Memory map Z80:
//   0x0000-0x3FFF  ROM (16 KB)
//   0x4000-0x4FFF  VRAM 4 KB con 2 PAGINE (page index = videoreg bit 0)
//                    FG tilemap = vram[idx][0x000..0x3FF]
//                    BG tilemap = vram[idx][0x800..0xBFF]
//   0x5000-0x57FF  videoreg_w  (bit 0=page sel, bit 1=palette bank, cocktail)
//   0x5800-0x5FFF  scroll_w    (BG scroll orizzontale MAME = verticale portrait)
//   0x6000-0x67FF  sound A control (soundregs[0]: FIRE/WING/SWOOP + boom navetta)
//   0x6800-0x6FFF  sound B control (soundregs[1]: HIT nemici + melody select)
//   0x7000-0x77FF  IN0 read
//   0x7800-0x7FFF  DSW0 read (bit 7 = VBLANK live)
//
// Tile decode: 8x8, 2 bitplanes, 256 char per layer.
//   plane0 = bg/fgtiles[code * 8 + row]
//   plane1 = bg/fgtiles[code * 8 + row + 0x800]
//   pen(col,row) = bit(p0, col) | (bit(p1, col) << 1)
//
// Color attribute (MAME phoenix_v.cpp):
//   col = (code >> 5) & 0x07
//   final_col = col | (palette_bank ? 8 : 0) [+ 8 per FG]  → indice palette
//   pen finale in palette[256] = final_col * 4 + raw_pen
// ============================================================================

class Phoenix : public machineBase {
public:
  Phoenix();

  void init(Input *input, unsigned short *framebuffer,
            sprite_S *spritebuffer, unsigned char *memorybuffer) override;
  void reset() override;

  signed char machineType() override { return MCH_PHOENIX; }
  signed char videoFlipY()  override { return 0; }
  signed char videoFlipX()  override { return 0; }

  unsigned char rdZ80(unsigned short Addr) override;
  void          wrZ80(unsigned short Addr, unsigned char Value) override;
  unsigned char opZ80(unsigned short Addr) override;

  void run_frame()      override;
  void prepare_frame()  override;
  void render_row(short row) override;

  const unsigned short *logo(void) override;

  // High score persistente (NVS). Phoenix non ha work RAM separata dalla VRAM
  // (mappa: solo 0x4000-0x4FFF), quindi il punteggio vive come codici tile
  // direttamente nelle celle FG del tilemap — hiscoreRead/Write di default
  // (rdZ80/wrZ80) vanno gia' bene (nessun dispatch su current_cpu qui, CPU
  // singola). Regioni da MAME hiscore.dat (set "phoenix").
  const char *hiscoreKey() override { return "phoenix"; }
  const hiscore_region_S *hiscoreRegions(unsigned char *count) override;

private:
  // VRAM 4 KB x 2 pagine: page index = bit 0 di videoreg (write a 0x5000)
  // FG = vram[idx][0..0x3FF], BG = vram[idx][0x800..0xBFF]
  unsigned char vram[2][0x1000];

  unsigned char videoreg;        // bit 0 = page select, bit 1 = palette bank
  unsigned char scroll_x;        // BG scroll (verticale in portrait)
  unsigned char palette_bank;    // bit 1 di videoreg

  // VBLANK polling (no IRQ): pilotato deterministicamente in 2 fasi dentro
  // run_frame (vblank_active=true → bit 7 = 0; false → bit 7 = 1).
  bool vblank_active = false;

  // Pen dei tile pre-decodificati in orientamento NATIVO (non ruotato):
  // native[code*64 + row*8 + col] = pen 0..3. 16 KB per layer.
  unsigned char *bg_decoded;     // 16 KB
  unsigned char *fg_decoded;     // 16 KB
  unsigned short *palette_cache; // 256 colori x 2 byte
  bool            cache_done;
};

#endif // PHOENIX_H
