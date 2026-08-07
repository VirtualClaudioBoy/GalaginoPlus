#include "turtles.h"

void turtles::start() {
  ignoreFireButton = 0;
  game_started = 1;
}

void turtles::run_frame(void) {
  // Turtles runs ~5% faster than MAME with base INST_PER_FRAME=1250 (display targets
  // 16ms=62.5Hz vs hardware's 60.606Hz). Reduce iterations to compensate.
  static constexpr int TURTLES_IPF = 1150;
  for(int i = 0; i < TURTLES_IPF; i++) {
    current_cpu=0; StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]);
    current_cpu=1; StepZ80(&cpu[1]); snd_icnt++; StepZ80(&cpu[1]); snd_icnt++;

    if((snd_irq_state & 0x08) && (cpu[1].IFF & IFF_1)) {
      IntZ80(&cpu[1], INT_RST38);
      snd_irq_state = 0;
    }
  }

  if(irq_enable[0]) {
    current_cpu = 0;
    IntZ80(&cpu[0], INT_NMI);
  }
}

unsigned char turtles::opZ80(unsigned short Addr) {
  if (current_cpu == 0 && Addr < CPU1_ROM_SIZE)
    return turtles_main_rom[Addr];
  if (current_cpu == 1 && Addr < CPU2_ROM_SIZE)
    return turtles_audio_rom[Addr];
  return 0x00;
}

// from MAME hiscore.dat (turtles)
const hiscore_region_S *turtles::hiscoreRegions(unsigned char *count) {
  static const hiscore_region_S regions[] = {
    { 0x8200, 0x1e, 0x00, 0x00 },   // high score table
    { 0x80a8, 3,    0x00, 0x00 },   // top score
    { 0x9340, 1,    0x01, 0x01 },   // video RAM (init gate)
  };
  *count = 3;
  return regions;
}

// The game draws the "HIGH SCORE" top-bar value only once at boot, before
// the saved scores get injected — redraw it from the top score BCD at
// 0x80A8-0x80AA (little endian). Verified from a video RAM dump on HW:
// digit cells left→right at 0x9241..0x91A1 (stride 0x20), digit chars =
// 0x00-0x09, blank = 0x10; the game always shows the last two digits
// (score 0 is drawn as "00"). Inherited by amidar (same hardware/layout).
void turtles::hiscoreRestored() {
  static const unsigned short cells[6] = { 0x9241, 0x9221, 0x9201, 0x91e1, 0x91c1, 0x91a1 };
  unsigned char shown = 0;
  for (unsigned char i = 0; i < 6; i++) {
    unsigned char b = hiscoreRead(0x80aa - i / 2);
    unsigned char d = (i & 1) ? (b & 0x0f) : (b >> 4);
    if (d || i >= 4) shown = 1;
    hiscoreWrite(cells[i], shown ? d : 0x10);
  }
}

unsigned char turtles::rdZ80(unsigned short Addr) {
  if (current_cpu == 0) {
    if (Addr < CPU1_ROM_SIZE)
      return turtles_main_rom[Addr];

    if (Addr >= CPU1_RAM_ADDR && Addr < CPU1_RAM_ADDR + CPU1_RAM_SIZE)
      return memory[Addr - CPU1_RAM_ADDR];

    if (Addr >= CPU1_VRAM_ADDR && Addr < CPU1_VRAM_ADDR + CPU1_VRAM_SIZE)
      return memory[CPU1_VRAM_OFFSET + Addr - CPU1_VRAM_ADDR];

    if (Addr >= CPU1_ATTR_ADDR && Addr < CPU1_ATTR_ADDR + CPU1_ATTR_SIZE)
      return memory[CPU1_ATTR_OFFSET + Addr - CPU1_ATTR_ADDR];

    if (Addr >= CPU1_SPRITE_ADDR && Addr < CPU1_SPRITE_ADDR + CPU1_SPRITE_SIZE)
      return memory[CPU1_SPRITE_OFFSET + Addr - CPU1_SPRITE_ADDR];

    if (Addr >= CPU1_EXTRA1_ADDR && Addr < CPU1_EXTRA1_ADDR + CPU1_EXTRA1_SIZE)
      return memory[CPU1_EXTRA1_OFFSET + Addr - CPU1_EXTRA1_ADDR];

    if (Addr >= CPU1_EXTRA2_ADDR && Addr < CPU1_EXTRA2_ADDR + CPU1_EXTRA2_SIZE)
      return memory[CPU1_EXTRA2_OFFSET + Addr - CPU1_EXTRA2_ADDR];

    unsigned char keymask;
    unsigned char retval;

    // PPI #0 at 0xB000-0xB03F, port selected by bits[5:4] of address
    if (Addr >= 0xB000 && Addr < 0xB040) {
      unsigned char port = (Addr >> 4) & 3;
      keymask = input->buttons_get();
      switch (port) {
        case 0: // Port A = IN0: Coin1=b7, Coin2=b6, Left=b5, Right=b4, Fire=b3
          retval = TURTLES_IN0_VALUE;
          if (keymask & BUTTON_COIN)  retval &= ~0x80;
          if (keymask & BUTTON_LEFT)  retval &= ~0x20;
          if (keymask & BUTTON_RIGHT) retval &= ~0x10;
          if (keymask & BUTTON_FIRE)  retval &= ~0x08;
          return retval;
        case 1: // Port B = IN1: Start1=b7, Start2=b6, lives=b1:0
          retval = TURTLES_IN1_VALUE;
          if (keymask & BUTTON_START) retval &= ~0x80;
          return retval;
        case 2: // Port C = IN2: Down=b6, Up=b4
          retval = TURTLES_IN2_VALUE;
          if (keymask & BUTTON_DOWN) retval &= ~0x40;
          if (keymask & BUTTON_UP)   retval &= ~0x10;
          return retval;
        default:
          return 0xFF;
      }
    }

    // PPI #1 at 0xB800-0xB83F (sound side - read returns 0xFF)
    if (Addr >= 0xB800 && Addr < 0xB840)
      return 0xFF;

    if (Addr == 0xA800) // watchdog
      return 0xFF;
  }
  else {
    // Audio CPU
    if (Addr < CPU2_ROM_SIZE)
      return turtles_audio_rom[Addr];

    if (Addr >= CPU2_RAM_ADDR && Addr < CPU2_RAM_ADDR + CPU2_RAM_SIZE)
      return memory[CPU2_RAM_OFFSET + Addr - CPU2_RAM_ADDR];
  }
  return 0xFF;
}

void turtles::wrZ80(unsigned short Addr, unsigned char Value) {
  if (current_cpu == 0) {
    if (Addr >= CPU1_RAM_ADDR && Addr < CPU1_RAM_ADDR + CPU1_RAM_SIZE) {
      memory[Addr - CPU1_RAM_ADDR] = Value;
      return;
    }
    if (Addr >= CPU1_VRAM_ADDR && Addr < CPU1_VRAM_ADDR + CPU1_VRAM_SIZE) {
      memory[CPU1_VRAM_OFFSET + Addr - CPU1_VRAM_ADDR] = Value;
      return;
    }
    if (Addr >= CPU1_ATTR_ADDR && Addr < CPU1_ATTR_ADDR + CPU1_ATTR_SIZE) {
      memory[CPU1_ATTR_OFFSET + Addr - CPU1_ATTR_ADDR] = Value;
      return;
    }
    if (Addr >= CPU1_SPRITE_ADDR && Addr < CPU1_SPRITE_ADDR + CPU1_SPRITE_SIZE) {
      memory[CPU1_SPRITE_OFFSET + Addr - CPU1_SPRITE_ADDR] = Value;
      return;
    }
    if (Addr >= CPU1_EXTRA1_ADDR && Addr < CPU1_EXTRA1_ADDR + CPU1_EXTRA1_SIZE) {
      memory[CPU1_EXTRA1_OFFSET + Addr - CPU1_EXTRA1_ADDR] = Value;
      return;
    }
    if (Addr >= CPU1_EXTRA2_ADDR && Addr < CPU1_EXTRA2_ADDR + CPU1_EXTRA2_SIZE) {
      memory[CPU1_EXTRA2_OFFSET + Addr - CPU1_EXTRA2_ADDR] = Value;
      return;
    }

    // Control registers
    if (Addr == 0xA000) { bg_r = Value & 1; return; }
    if (Addr == 0xA008) { irq_enable[0] = Value & 1; return; }
    if (Addr == 0xA020) { bg_g = Value & 1; return; }
    if (Addr == 0xA028) { bg_b = Value & 1; return; }
    if (Addr >= 0xA000 && Addr < 0xA040) return; // flip, coin (ignored)

    // PPI #0 at 0xB000-0xB03F (inputs - writes to control register only)
    if (Addr >= 0xB000 && Addr < 0xB040) return;

    // PPI #1 at 0xB800-0xB83F (sound)
    if (Addr >= 0xB800 && Addr < 0xB840) {
      unsigned char port = (Addr >> 4) & 3;
      switch (port) {
        case 0: // Port A = sound latch
          sound_latch = Value;
          return;
        case 1: // Port B = IRQ trigger to audio CPU
          if ((Value & 0x08) && !snd_irq_last)
            snd_irq_state = Value;
          snd_irq_last = Value & 0x08;
          return;
        default:
          return;
      }
    }
  }
  else {
    // Audio CPU
    if (Addr >= CPU2_RAM_ADDR && Addr < CPU2_RAM_ADDR + CPU2_RAM_SIZE) {
      memory[CPU2_RAM_OFFSET + Addr - CPU2_RAM_ADDR] = Value;
      return;
    }
    if (Addr >= 0x9000 && Addr < 0xA000) return; // audio filters, ignored
  }
}

void turtles::prepare_frame(void) {
  active_sprites = 0;

  // Sprite data at 0x9840, 4 bytes per sprite (8 sprites)
  // base[0]=Y  base[1]=code|flipx|flipy  base[2]=color  base[3]=X
  for (int idx = 7; idx >= 0 && active_sprites < 128; idx--) {
    unsigned char *base = memory + CPU1_SPRITE_OFFSET + idx * 4;

    struct sprite_S spr;
    spr.code  = base[1] & 0x3f;
    spr.flags = (base[1] >> 6) & 3;
    spr.color = base[2] & 7;

    // 90° rotation: portrait X = landscape Y, portrait Y = landscape X
    spr.x = base[0] - 16;
    spr.y = base[3] + 16;

    if (base[3] && (spr.y > -16) && (spr.y < 288) && (spr.x > -16) && (spr.x < 224))
      sprite[active_sprites++] = spr;
  }
}

void turtles::blit_tile(short row, char col) {
  if ((row < 2) || (row >= 34)) return;

  unsigned short addr = tileaddr[row][col];
  const unsigned short *tile = turtles_tilemap[memory[CPU1_VRAM_OFFSET + addr]];
  int c = memory[CPU1_ATTR_OFFSET + 2 * (addr & 31) + 1] & 7;
  const unsigned short *colors = turtles_colormap[c];

  unsigned short *ptr = frame_buffer + 8 * col;
  for (char r = 0; r < 8; r++, ptr += (224 - 8)) {
    unsigned short pix = *tile++;
    for (char c = 0; c < 8; c++, pix >>= 2) {
      long index = ((pix & 2) >> 1) | ((pix & 1) << 1);
      if (pix & 3) *ptr = colors[index];
      ptr++;
    }
  }
}

void turtles::blit_sprite(short row, unsigned char s) {
  const unsigned long *spr = turtles_spritemap[sprite[s].flags & 3][sprite[s].code];
  const unsigned short *colors = turtles_colormap[sprite[s].color];

  unsigned long mask = 0xffffffff;
  if (sprite[s].x < 0)        mask <<= -2 * sprite[s].x;
  if (sprite[s].x > 224 - 16) mask >>= 2 * (sprite[s].x - (224 - 16));

  short y_offset = sprite[s].y - 8 * row;
  unsigned char lines2draw = 8;
  if (y_offset < -8) lines2draw = 16 + y_offset;

  unsigned short startline = 0;
  if (y_offset > 0) {
    startline = y_offset;
    lines2draw = 8 - y_offset;
  }
  if (y_offset < 0) spr -= y_offset;

  unsigned short *ptr = frame_buffer + sprite[s].x + 224 * startline;
  for (char r = 0; r < lines2draw; r++, ptr += (224 - 16)) {
    unsigned long pix = *spr++ & mask;
    for (char c = 0; c < 16; c++, pix >>= 2) {
      long index = ((pix & 2) >> 1) | ((pix & 1) << 1);
      if (pix & 3) *ptr = colors[index];
      ptr++;
    }
  }
}

void turtles::render_row(short row) {
  if (row <= 1 || row >= 34) return;

  if (bg_r || bg_g || bg_b) {
    unsigned char r = bg_r ? 0x55 : 0;
    unsigned char g = bg_g ? 0x47 : 0;
    unsigned char b = bg_b ? 0x55 : 0;
    unsigned short c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    unsigned short color = (c >> 8) | (c << 8); // byte-swap per SPI
    for (int i = 0; i < 224 * 8; i++) frame_buffer[i] = color;
  }

  for (char col = 0; col < 28; col++)
    blit_tile(row, col);

  for (unsigned char s = 0; s < active_sprites; s++) {
    if ((sprite[s].y < 8 * (row + 1)) && ((sprite[s].y + 16) > 8 * row))
      blit_sprite(row, s);
  }
}

const unsigned short *turtles::logo(void) {
  return turtles_logo;
}

#ifdef LED_PIN
void turtles::gameLeds(CRGB *leds) {
  static char sub_cnt = 0;
  if (sub_cnt++ == 12) {
    sub_cnt = 0;
    static char pos = 0;
    for (char c = 0; c < NUM_LEDS; c++) {
      leds[c] = (c == pos) ? LED_YELLOW : ((c % 2 == 0) ? LED_GREEN : LED_BLACK);
    }
    pos = (pos + 1) % NUM_LEDS;
  }
}

void turtles::menuLeds(CRGB *leds) {
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}
#endif
