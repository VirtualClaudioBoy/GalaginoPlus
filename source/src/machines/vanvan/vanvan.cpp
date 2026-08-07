#include "vanvan.h"

void vanvan::reset() {
  machineBase::reset();
  nmi_enable = 0;
  startupFrameCount = 0;
  sn_last_register[0] = sn_last_register[1] = 0;
}

unsigned char vanvan::opZ80(unsigned short Addr) {
  if (Addr < 0x4000) return vanvan_rom[Addr];
  if (Addr >= 0x8000 && Addr < 0x9000) return vanvan_rom2[Addr - 0x8000];
  return rdZ80(Addr);
}

// from MAME hiscore.dat (vanvan). NB: both patterns are all-zero, which our
// zeroed RAM matches right at boot — the 30-frame stability check is the
// only gate here. If on hardware the game clears/initializes this area
// AFTER the injection (restored scores wiped), raise HISCORE_STABLE_FRAMES
// or gate on a byte the game only writes at init end.
const hiscore_region_S *vanvan::hiscoreRegions(unsigned char *count) {
  static const hiscore_region_S regions[] = {
    { 0x4809, 6,    0x00, 0x00 },   // top score
    { 0x4c60, 0xf0, 0x00, 0x00 },   // high score table
  };
  *count = 2;
  return regions;
}

unsigned char vanvan::rdZ80(unsigned short Addr) {
  if (Addr < 0x4000) return vanvan_rom[Addr];
  if (Addr >= 0x8000 && Addr < 0x9000) return vanvan_rom2[Addr - 0x8000];

  unsigned short a = Addr & 0x7fff;   // a15 don't-care for the RAM/IO region

  if ((a & 0xf000) == 0x4000) {
    // this includes spriteram
    return memory[a - 0x4000];
  }

  if ((a & 0xf000) == 0x5000) {
    unsigned char keymask = input->buttons_get();

    if (a == 0x5080) return VANVAN_DSW1;
    if (a == 0x50c0) return VANVAN_DSW2;

    if (a == 0x5000) {
      unsigned char retval = 0xff;
      if (keymask & BUTTON_UP)    retval &= ~0x01;
      if (keymask & BUTTON_LEFT)  retval &= ~0x02;
      if (keymask & BUTTON_RIGHT) retval &= ~0x04;
      if (keymask & BUTTON_DOWN)  retval &= ~0x08;
      if (keymask & BUTTON_FIRE)  retval &= ~0x10;
      if (keymask & BUTTON_COIN)  retval &= ~0x20;
      return retval;
    }

    if (a == 0x5040) {
      unsigned char retval = 0xff;
      if (keymask & BUTTON_START) retval &= ~0x20;
      return retval;
    }
  }
  return 0xff;
}

void vanvan::wrZ80(unsigned short Addr, unsigned char Value) {
  // 0xb800-0xb87f: known-harmless leftover write (Sanritsu dev code writes a
  // color LUT copy here; MAME itself .nopw()'s it - real hardware has ROM
  // there). Silently ignore.
  if (Addr >= 0x8000) return;

  unsigned short a = Addr & 0x7fff;

  if ((a & 0xf000) == 0x4000) {
    memory[a - 0x4000] = Value;

    if (!game_started && Value != 0 && startupFrameCount > 120)
      game_started = 1;
    return;
  }

  if ((a & 0xff00) == 0x5000) {
    // 0x5060 to 0x506f writes through to ram (spriteram2)
    if ((a & 0xfff0) == 0x5060)
      memory[a - 0x4000] = Value;

    if (a == 0x5000)
      nmi_enable = Value & 1;

    return;
  }
}

void vanvan::outZ80(unsigned short Port, unsigned char Value) {
  unsigned char port = Port & 0xff;
  if (port == 0x01)      SN76489_Write_2chip(0, Value);
  else if (port == 0x02) SN76489_Write_2chip(1, Value);
}

void vanvan::SN76489_Write_2chip(int chip, unsigned char data) {
  if (data & 0x80) {                 // Latch command
    int reg = (data >> 5) & 0x03;    // Channel 0-3
    int type = (data >> 4) & 0x01;   // 0=Frequency, 1=Volume
    sn_last_register[chip] = (reg * 2) + type;

    if (reg < 4) {
      if (type == 0) { // Frequency (4 lower bits)
        sn_period[chip][reg] = (sn_period[chip][reg] & 0x3F0) | (data & 0x0F);
      }
      else { // Volume
        unsigned char vol = data & 0x0F;
        sn_volume[chip][reg] = vol;
        if (vol < sn_min_volume[chip][reg])
          sn_min_volume[chip][reg] = vol;
        if (vol < 15)
          sn_hold[chip][reg] = 6;
      }
    }
  }
  else { // Data write
    int reg = sn_last_register[chip] / 2;
    int type = sn_last_register[chip] % 2;

    if (reg < 4 && type == 0) { // If latch was for frequency
      if (reg == 3) {   // Noise channel
        sn_period[chip][3] = data & 0x07;
      }
      else { // Tone channels
        sn_period[chip][reg] = (sn_period[chip][reg] & 0x00F) | ((data & 0x3F) << 4);
      }
    }
  }
}

void vanvan::run_frame(void) {
  for (int i = 0; i < INST_PER_FRAME; i++) {
    StepZ80(cpu); StepZ80(cpu); StepZ80(cpu); StepZ80(cpu);
  }

  if (!game_started && startupFrameCount <= 121)
    startupFrameCount++;

  if (nmi_enable)
    IntZ80(cpu, INT_NMI);
}

void vanvan::prepare_frame(void) {
  active_sprites = 0;
  for (int idx = 0; idx < 8 && active_sprites < 92; idx++) {
    unsigned char *sprite_base_ptr = memory + 2 * (7 - idx);
    struct sprite_S spr;

    spr.code = sprite_base_ptr[0x0ff0] >> 2;
    spr.color = sprite_base_ptr[0x0ff1] & 63;
    spr.flags = sprite_base_ptr[0x0ff0] & 3;

    // adjust sprite position on screen for upright screen
    spr.x = 255 - 16 - sprite_base_ptr[0x1060];
    spr.y = 16 + 256 - sprite_base_ptr[0x1061];

    if ((spr.code < 64) &&
        (spr.y > -16) && (spr.y < 288) &&
        (spr.x > -16) && (spr.x < 224)) {

      sprite[active_sprites++] = spr;
    }
  }
}

// draw a single 8x8 tile
void vanvan::blit_tile(short row, char col) {
  // Van Van Car's visible area crops 2 tile rows on top and bottom
  // (MAME visarea is 32 of the 36 native tile rows) -> leave a black
  // margin here, matching the pattern used by ladybug.cpp for its
  // own cropped tilemap.
  if (row < 2 || row > 33) return;

  unsigned short addr = tileaddr[row][col];
  const unsigned short *tile = vanvan_tilemap[memory[addr]];
  const unsigned short *colors = vanvan_colormap[memory[0x400 + addr] & 63];
  unsigned short *ptr = frame_buffer + 8 * col;

  // Pen 0 is hardwired to black in every color block of the real color
  // PROM (checked the raw LUT bytes: entry 0 is always 0x0), yet the real
  // cabinet's playfield background is a light gray. So that gray isn't
  // coming from the tile color table at all - force it explicitly here
  // instead of the ROM-derived black for pen 0.
  const unsigned short VANVAN_BG_GRAY = 0x748c;
  for (char r = 0; r < 8; r++, ptr += (224 - 8)) {
    unsigned short pix = *tile++;
    for (char c = 0; c < 8; c++, pix >>= 2) {
      *ptr = (pix & 3) ? colors[pix & 3] : VANVAN_BG_GRAY;
      ptr++;
    }
  }
}

void vanvan::blit_sprite(short row, unsigned char s) {
  const unsigned long *spr = vanvan_sprites[sprite[s].flags & 3][sprite[s].code];
  const unsigned short *colors = vanvan_colormap[sprite[s].color & 63];

  unsigned long mask = 0xffffffff;
  if (sprite[s].x < 0)      mask <<= -2 * sprite[s].x;
  if (sprite[s].x > 224-16) mask >>= (2 * (sprite[s].x - (224-16)));

  short y_offset = sprite[s].y - 8 * row;

  unsigned char lines2draw = 8;
  if (y_offset < -8) lines2draw = 16 + y_offset;

  unsigned short startline = 0;
  if (y_offset > 0) {
    startline = y_offset;
    lines2draw = 8 - y_offset;
  }

  if (y_offset < 0)
    spr -= y_offset;

  unsigned short *ptr = frame_buffer + sprite[s].x + 224 * startline;

  for (char r = 0; r < lines2draw; r++, ptr += (224-16)) {
    unsigned long pix = *spr++ & mask;
    for (char c = 0; c < 16; c++, pix >>= 2) {
      // Sprite pen 0 is ALWAYS transparent on Pac-Man hardware, regardless of
      // what the color PROM stores for entry 0. Test the pen index, not the
      // resolved RGB value: color blocks 10-15 have colors[0]=0x20 (non-zero
      // near-black), so the old `if (col)` check drew pen-0 pixels as black
      // stripes AND wrote them out of the framebuffer when a sprite clipped
      // off-screen (x<0 / x>208) -> heap corruption -> StoreProhibited crash.
      if (pix & 3) *ptr = colors[pix & 3];
      ptr++;
    }
  }
}

void vanvan::render_row(short row) {
  for (char col = 0; col < 28; col++)
    blit_tile(row, col);

  // Same crop as blit_tile: rows 0-1 and 34-35 are outside the real
  // cabinet's visible area. Sprites weren't excluded there before, so a
  // sprite exiting the screen (e.g. the attract-mode car) kept being drawn
  // into that margin instead of disappearing off-screen.
  if (row < 2 || row > 33) return;

  for (unsigned char s = 0; s < active_sprites; s++) {
    if ((sprite[s].y < 8 * (row+1)) && ((sprite[s].y+16) > 8 * row))
      blit_sprite(row, s);
  }
}

const unsigned short *vanvan::logo(void) {
  return vanvan_logo;
}

#ifdef LED_PIN
void vanvan::gameLeds(CRGB *leds) {
  static char sub_cnt = 0;
  if (sub_cnt++ == 4) {
    sub_cnt = 0;
    static char led = 0;
    char il = (led < NUM_LEDS) ? led : ((2*NUM_LEDS-2)-led);
    for (char c = 0; c < NUM_LEDS; c++) {
      if (c == il) leds[c] = LED_WHITE;
      else         leds[c] = LED_RED;
    }
    led = (led + 1) % (2*NUM_LEDS-2);
  }
}

void vanvan::menuLeds(CRGB *leds) {
  memcpy(leds, menu_leds, NUM_LEDS*sizeof(CRGB));
}
#endif
