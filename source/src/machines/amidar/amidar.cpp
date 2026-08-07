#include "amidar.h"

// Amidar ROM is 16KB (0x0000-0x3FFF); reads above that return 0xFF.
// wrZ80(), run_frame(), prepare_frame(), render_row() inherited from turtles.
// inZ80(), outZ80() inherited from scramble (AY-3-8910 identical wiring).

// from MAME hiscore.dat (amidar, entry includes our amidar1 set)
const hiscore_region_S *amidar::hiscoreRegions(unsigned char *count) {
  static const hiscore_region_S regions[] = {
    { 0x8200, 0x1e, 0x00, 0x01 },   // high score table
    { 0x80a8, 3,    0x00, 0x01 },   // top score
  };
  *count = 2;
  return regions;
}

unsigned char amidar::opZ80(unsigned short Addr) {
  if (current_cpu == 0 && Addr < CPU1_ROM_SIZE)
    return amidar_main_rom[Addr];
  if (current_cpu == 1 && Addr < CPU2_ROM_SIZE)
    return amidar_audio_rom[Addr];
  return 0x00;
}

unsigned char amidar::rdZ80(unsigned short Addr) {
  if (current_cpu == 0) {
    if (Addr < CPU1_ROM_SIZE)
      return amidar_main_rom[Addr];

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
          retval = AMIDAR_IN0_VALUE;
          if (keymask & BUTTON_COIN)  retval &= ~0x80;
          if (keymask & BUTTON_LEFT)  retval &= ~0x20;
          if (keymask & BUTTON_RIGHT) retval &= ~0x10;
          if (keymask & BUTTON_FIRE)  retval &= ~0x08;
          return retval;
        case 1: // Port B = IN1: Start1=b7, Start2=b6, Lives=b1:0
          retval = AMIDAR_IN1_VALUE;
          if (keymask & BUTTON_START) retval &= ~0x80;
          return retval;
        case 2: // Port C = IN2: Down=b6, Up=b4, DemoSounds=b1, BonusLife=b2
          retval = AMIDAR_IN2_VALUE;
          if (keymask & BUTTON_DOWN) retval &= ~0x40;
          if (keymask & BUTTON_UP)   retval &= ~0x10;
          return retval;
        default: // Port D = IN3: Coinage (0xFF = 1C/1C)
          return AMIDAR_IN3_VALUE;
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
      return amidar_audio_rom[Addr];

    if (Addr >= CPU2_RAM_ADDR && Addr < CPU2_RAM_ADDR + CPU2_RAM_SIZE)
      return memory[CPU2_RAM_OFFSET + Addr - CPU2_RAM_ADDR];
  }
  return 0xFF;
}

void amidar::blit_tile(short row, char col) {
  if ((row < 2) || (row >= 34)) return;

  unsigned short addr = tileaddr[row][col];
  const unsigned short *tile = amidar_tilemap[memory[CPU1_VRAM_OFFSET + addr]];
  int c = memory[CPU1_ATTR_OFFSET + 2 * (addr & 31) + 1] & 7;
  const unsigned short *colors = amidar_colormap[c];

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

void amidar::blit_sprite(short row, unsigned char s) {
  const unsigned long *spr = amidar_spritemap[sprite[s].flags & 3][sprite[s].code];
  const unsigned short *colors = amidar_colormap[sprite[s].color];

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

const unsigned short *amidar::logo(void) {
  return amidar_logo;
}

#ifdef LED_PIN
void amidar::gameLeds(CRGB *leds) {
  static char sub_cnt = 0;
  if (sub_cnt++ == 12) {
    sub_cnt = 0;
    static char pos = 0;
    for (char c = 0; c < NUM_LEDS; c++) {
      leds[c] = (c == pos) ? LED_YELLOW : ((c % 2 == 0) ? LED_RED : LED_BLACK);
    }
    pos = (pos + 1) % NUM_LEDS;
  }
}

void amidar::menuLeds(CRGB *leds) {
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}
#endif
