#include "pooyan.h"

unsigned char pooyan::opZ80(unsigned short Addr) {
  if(current_cpu == 0) {
    if(Addr < 0x8000)
      return pooyan_rom[Addr];
  }
  else {
    // Sound CPU ROM 0x0000-0x1FFF (8KB data)
    if(Addr < 0x2000)
      return pooyan_snd_rom[Addr];
    // 0x2000-0x2FFF mapped as ROM but no data (returns 0xFF)
  }
  return 0xff;
}

// from MAME hiscore.dat (pooyan)
const hiscore_region_S *pooyan::hiscoreRegions(unsigned char *count) {
  static const hiscore_region_S regions[] = {
    { 0x89c0, 0x1e, 0x00, 0x00 },
    { 0x8a00, 0x1e, 0x00, 0x01 },
    { 0x8e00, 0x1e, 0x00, 0x00 },
    { 0x88a8, 3,    0x00, 0x01 },   // top score
  };
  *count = 4;
  return regions;
}

// The game draws the "HIGH SCORE" top-bar value only once at boot, before
// the saved scores get injected — redraw it from the top score BCD at
// 0x88A8-0x88AA (little endian). Verified from a video RAM dump on HW:
// digit cells left→right at 0x8641..0x85A1 (stride 0x20, one per screen
// column), digit chars = 0x00-0x09, blank = 0x10, leading zeros blanked.
void pooyan::hiscoreRestored() {
  static const unsigned short cells[6] = { 0x8641, 0x8621, 0x8601, 0x85e1, 0x85c1, 0x85a1 };
  unsigned char shown = 0;
  for (unsigned char i = 0; i < 6; i++) {
    unsigned char b = hiscoreRead(0x88aa - i / 2);
    unsigned char d = (i & 1) ? (b & 0x0f) : (b >> 4);
    if (d || i == 5) shown = 1;
    hiscoreWrite(cells[i], shown ? d : 0x10);
  }
}

unsigned char pooyan::rdZ80(unsigned short Addr) {
  if(current_cpu == 1) {
    // ---- Sound CPU memory map (MAME: timeplt_sound_map) ----
    // ROM 0x0000-0x2FFF (only 0x0000-0x1FFF has data)
    if(Addr < 0x2000)
      return pooyan_snd_rom[Addr];

    if(Addr < 0x3000)
      return 0xFF;  // unmapped ROM space

    // RAM 0x3000-0x33FF mirrored across 0x3000-0x3FFF
    if(Addr >= 0x3000 && Addr <= 0x3FFF)
      return snd_ram[Addr & 0x3FF];

    // AY#1 data read (0x4000, mirrored 0x4000-0x4FFF)
    if((Addr & 0xF000) == 0x4000) {
      unsigned char reg = ay_addr[0] & 0x0F;
      if(reg == 14) return soundlatch;         // Port A = soundlatch
      if(reg == 15) {
        // Port B = timer: LS90 bi-quinary counter, divide-by-5120
        // MAME: timeplt_timer[(total_cycles / 512) % 10]
        // 2560 snd_icnt/frame, /44 = 58.2 tick/frame (target MAME 58.3)
        static const unsigned char timeplt_timer[10] = {
          0x00, 0x10, 0x20, 0x30, 0x40, 0x90, 0xa0, 0xb0, 0xa0, 0xd0
        };
        return timeplt_timer[(snd_icnt / 44) % 10];
      }
      return ay_regs[0][reg];
    }

    // AY#2 data read (0x6000, mirrored 0x6000-0x6FFF)
    if((Addr & 0xF000) == 0x6000) {
      unsigned char reg = ay_addr[1] & 0x0F;
      return ay_regs[1][reg];
    }

    return 0xFF;
  }

  // ---- Main CPU memory map ----
  // ROM 0x0000-0x7FFF
  if(Addr < 0x8000)
    return pooyan_rom[Addr];

  // Color RAM 0x8000-0x83FF
  if(Addr <= 0x83FF)
    return memory[POOYAN_COLORRAM + (Addr & 0x03FF)];

  // Video RAM 0x8400-0x87FF
  if(Addr <= 0x87FF)
    return memory[POOYAN_VIDEORAM + (Addr & 0x03FF)];

  // Work RAM 0x8800-0x8FFF
  if(Addr <= 0x8FFF)
    return memory[POOYAN_WORKRAM + (Addr & 0x07FF)];

  // Sprite RAM bank 0: 0x9000-0x90FF, mirror 0x0B00
  if((Addr & 0xF400) == 0x9000)
    return memory[POOYAN_SPRITES0 + (Addr & 0xFF)];

  // Sprite RAM bank 1: 0x9400-0x94FF, mirror 0x0B00
  if((Addr & 0xF400) == 0x9400)
    return memory[POOYAN_SPRITES1 + (Addr & 0xFF)];

  // I/O reads (0xA000+, con mirror MAME)
  // IN0/IN1/IN2/DSW0: base 0xA080/0xA0A0/0xA0C0/0xA0E0, mirror 0x5E1F
  if((Addr & 0xA1E0) == 0xA080) {
    // IN0: coins, start (active-LOW: 0xFF = idle, clear bit = pressed)
    unsigned char keymask = input->buttons_get();
    unsigned char retval = 0xFF;
    if(keymask & BUTTON_COIN)   retval &= ~0x01;  // coin 1
    if(keymask & BUTTON_START)  retval &= ~0x08;  // start 1
    return retval;
  }

  if((Addr & 0xA1E0) == 0xA0A0) {
    // IN1: P1 controls (2-way verticale + fire, active-LOW)
    unsigned char keymask = input->buttons_get();
    unsigned char retval = 0xFF;
    if(keymask & BUTTON_UP)     retval &= ~0x04;
    if(keymask & BUTTON_DOWN)   retval &= ~0x08;
    if(keymask & BUTTON_FIRE)   retval &= ~0x10;
    return retval;
  }

  if((Addr & 0xA1E0) == 0xA0C0) {
    // IN2: P2 (active-LOW, idle)
    return 0xFF;
  }

  if((Addr & 0xA1E0) == 0xA0E0)
    return POOYAN_DSW0;

  // DSW1: base 0xA000, mirror 0x5E7F
  if((Addr & 0xA180) == 0xA000)
    return POOYAN_DSW1 | (input->demoSoundsOff() ? POOYAN_DSW1_DEMO_SOUND_OFF : POOYAN_DSW1_DEMO_SOUND_ON);

  return 0x00;
}

void pooyan::wrZ80(unsigned short Addr, unsigned char Value) {
  if(current_cpu == 1) {
    // ---- Sound CPU writes (MAME: timeplt_sound_map) ----
    // RAM 0x3000-0x3FFF (1KB mirrored)
    if(Addr >= 0x3000 && Addr <= 0x3FFF) {
      snd_ram[Addr & 0x3FF] = Value;
      return;
    }

    // AY#1 data write (0x4000-0x4FFF mirrored)
    if((Addr & 0xF000) == 0x4000) {
      unsigned char reg = ay_addr[0] & 0x0F;
      ay_regs[0][reg] = Value;
      if(reg < 14) soundregs[reg] = Value;  // copy to audio engine
      return;
    }

    // AY#1 address write (0x5000-0x5FFF mirrored)
    if((Addr & 0xF000) == 0x5000) {
      ay_addr[0] = Value & 0x0F;
      return;
    }

    // AY#2 data write (0x6000-0x6FFF mirrored)
    if((Addr & 0xF000) == 0x6000) {
      unsigned char reg = ay_addr[1] & 0x0F;
      ay_regs[1][reg] = Value;
      if(reg < 14) soundregs[16 + reg] = Value;  // copy to audio engine
      return;
    }

    // AY#2 address write (0x7000-0x7FFF mirrored)
    if((Addr & 0xF000) == 0x7000) {
      ay_addr[1] = Value & 0x0F;
      return;
    }

    // Filter control writes 0x8000-0xFFFF (ignore, no RC filters in galagino)
    return;
  }

  // ---- Main CPU writes ----
  if(Addr < 0x8000)
    return;  // ROM

  // Color RAM 0x8000-0x83FF
  if(Addr <= 0x83FF) {
    memory[POOYAN_COLORRAM + (Addr & 0x03FF)] = Value;
    return;
  }

  // Video RAM 0x8400-0x87FF
  if(Addr <= 0x87FF) {
    if(!game_started)
      game_started = 1;
    memory[POOYAN_VIDEORAM + (Addr & 0x03FF)] = Value;
    return;
  }

  // Work RAM 0x8800-0x8FFF
  if(Addr <= 0x8FFF) {
    memory[POOYAN_WORKRAM + (Addr & 0x07FF)] = Value;
    return;
  }

  // Sprite RAM bank 0: 0x9000-0x90FF, mirror 0x0B00
  if((Addr & 0xF400) == 0x9000) {
    memory[POOYAN_SPRITES0 + (Addr & 0xFF)] = Value;
    return;
  }

  // Sprite RAM bank 1: 0x9400-0x94FF, mirror 0x0B00
  if((Addr & 0xF400) == 0x9400) {
    memory[POOYAN_SPRITES1 + (Addr & 0xFF)] = Value;
    return;
  }

  // LS259 latch 0xA180-0xA187 (mirror 0x5E78): un output per indirizzo, data bit 0
  if((Addr & 0xA180) == 0xA180) {
    unsigned char bit_sel = Addr & 0x07;
    unsigned char bit_val = Value & 1;
    switch(bit_sel) {
      case 0: nmi_enable = bit_val; break;           // Q0 = NMI (irq) enable
      case 1:                                        // Q1 = sound CPU IRQ trigger
        // MAME: edge-triggered (0→1 only), not level-triggered
        if(bit_val && !snd_irq_last) snd_irq_pending = 1;
        snd_irq_last = bit_val;
        break;
      case 2: break;                                 // Q2 = mute audio (ignore)
      case 3: break;                                 // Q3 = coin counter 1
      case 4: break;                                 // Q4 = coin counter 2
      case 5: break;                                 // Q5 = PAY OUT (not used)
      case 6: break;                                 // Q6 = unused
      case 7: flip_screen = !bit_val; break;         // Q7 = flip screen (inverted, ignored: upright)
    }
    return;
  }

  // Sound latch 0xA100 (mirror 0x5E7F)
  if((Addr & 0xA180) == 0xA100) {
    soundlatch = Value;
    return;
  }

  // Watchdog 0xA000 (mirror 0x5E7F)
  if((Addr & 0xA180) == 0xA000)
    return;
}

// Pooyan: Main Z80 @ 3.072 MHz + Sound Z80 @ 1.789 MHz + 2x AY-3-8910
// Stessi clock di Time Pilot: 1280 x (4 step main + 2 step sound) per frame
void pooyan::run_frame(void) {
  for(int i = 0; i < 1280; i++) {
    current_cpu = 0;
    StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]);

    current_cpu = 1;
    StepZ80(&cpu[1]); snd_icnt++;
    StepZ80(&cpu[1]); snd_icnt++;

    // "latch" IRQ: only deliver when sound CPU has interrupts enabled (EI)
    // Same pattern as Frogger — prevents lost IRQs during DI periods
    if(snd_irq_pending && (cpu[1].IFF & IFF_1)) {
      IntZ80(&cpu[1], INT_RST38);
      snd_irq_pending = 0;
    }
  }

  // Main CPU: NMI at VBlank
  if(nmi_enable) {
    current_cpu = 0;
    IntZ80(&cpu[0], INT_NMI);
  }
}

// Sprites use LANDSCAPE orientation (no pre-rotation in romconv).
// Transposed rendering in blit_sprite handles the ROT90 display rotation.
// MAME disegna offs ASCENDING (0x10..0x3E): l'ultimo estratto finisce sopra,
// stesso ordine della lista sprite (blit in ordine crescente di s).
void pooyan::prepare_frame(void) {
  active_sprites = 0;

  const unsigned char *bank0 = &memory[POOYAN_SPRITES0];
  const unsigned char *bank1 = &memory[POOYAN_SPRITES1];

  for(int offs = 0x10; offs <= 0x3E && active_sprites < 128; offs += 2) {
    struct sprite_S spr;

    unsigned char sx_raw = bank0[offs];           // landscape X position
    unsigned char sy_raw = bank1[offs + 1];       // landscape Y position
    unsigned char code   = bank0[offs + 1] & 0x3F;  // 64 sprite
    unsigned char attr   = bank1[offs];             // color + flip

    unsigned char color = attr & 0x0F;          // 4-bit color group
    unsigned char flipx = (~attr >> 6) & 1;     // bit6 inverted = flipX
    unsigned char flipy = (attr >> 7) & 1;      // bit7 = flipY

    // MAME: sy = 240 - spriteram2[offs+1]  (timeplt: 241 - raw)
    int sy = 240 - sy_raw;

    // Skip offscreen sprites
    if(sy <= -16 || sy >= 256) continue;

    // Transposed coordinate mapping (come timeplt, con offset -16 per sy=240-raw):
    // ROM row (landscape dy) → screen X (reversed via 15-r in blit)
    // ROM col (landscape dx) → screen Y (forward)
    spr.x = (int)sy_raw - 16;
    spr.y = (int)sx_raw + 16;

    spr.code = code;
    spr.color = color;

    // Landscape sprite orientations: [0]=normal, [1]=flipY, [2]=flipX, [3]=flipXY
    spr.flags = (flipy ? 1 : 0) | (flipx ? 2 : 0);

    if((spr.y > -16) && (spr.y < 304) && (spr.x > -16) && (spr.x < 224)) {
      sprite[active_sprites++] = spr;
    }
  }
}

void pooyan::blit_tile(short row, char col) {
  unsigned short addr = tileaddr[row][col];

  if((row < 2) || (row >= 34))
    return;

  unsigned char vram_val = memory[POOYAN_VIDEORAM + addr];
  unsigned char cram_val = memory[POOYAN_COLORRAM + addr];

  // MAME get_bg_tile_info: code = videoram (256 tile, no bank),
  // color = attr & 0x0F, flip = TILE_FLIPYX(attr >> 6)
  unsigned char tile_code = vram_val;
  unsigned char color = cram_val & 0x0F;

  // Flip flags: sotto ROT90 gli assi si scambiano (stesso fix di rocnrope #18a):
  // MAME FLIPX nativo (bit6) = flip verticale screen (code flip_y),
  // MAME FLIPY nativo (bit7) = flip orizzontale screen (code flip_x).
  // Derivazione: screen[r][c] = header[r][c] = native[7-c][r].
  unsigned char flip_x = (cram_val >> 7) & 1;
  unsigned char flip_y = (cram_val >> 6) & 1;

  const unsigned long *tile = pooyan_tilemap[tile_code];
  const unsigned short *colors = pooyan_char_colormap[color];

  unsigned short *ptr = frame_buffer + 8 * col;

  for(char r = 0; r < 8; r++, ptr += (224 - 8)) {
    int src_r = flip_y ? (7 - r) : r;
    unsigned long pix = tile[src_r];
    if(flip_x) {
      for(char c = 0; c < 8; c++) {
        unsigned char p = (pix >> ((7 - c) * 4)) & 0x0F;
        ptr[c] = colors[p];
      }
      ptr += 8;
    } else {
      for(char c = 0; c < 8; c++, pix >>= 4) {
        *ptr = colors[pix & 0x0F];
        ptr++;
      }
    }
  }
}

// Transposed sprite rendering for landscape-oriented sprite data (come timeplt).
// ROM row (landscape dy) → screen X offset, ROM col (landscape dx) → screen Y offset.
// Trasparenza: MAME transpen_mask — pen trasparente se LUT[group*16+pen]==0
// (bitmask precalcolata nel converter, NON è il semplice pen==0).
void pooyan::blit_sprite(short row, unsigned char s) {
  const unsigned char (*spr_data)[8] = pooyan_spritemap[sprite[s].flags & 3][sprite[s].code];
  const unsigned short *colors = pooyan_sprite_colormap[sprite[s].color];
  unsigned short transmask = pooyan_sprite_transmask[sprite[s].color];

  int spr_x = sprite[s].x;   // screen X start (ROM rows map here)
  int spr_y = sprite[s].y;   // screen Y start (ROM cols map here)
  int row_start = 8 * row;

  // Sprite occupies screen Y: [spr_y, spr_y + 15]
  // Current tile row: screen Y [row_start, row_start + 7]
  int y_begin = (spr_y > row_start) ? spr_y : row_start;
  int y_end   = ((spr_y + 15) < (row_start + 7)) ? (spr_y + 15) : (row_start + 7);
  if(y_begin > y_end) return;

  // ROM row r → screen_x = spr_x + (15 - r)  [reversed row for correct orientation]
  // ROM col c = screen_y - spr_y              [forward column for correct tiling]
  for(int r = 0; r < 16; r++) {
    int screen_x = spr_x + 15 - r;

    if(screen_x < 0 || screen_x >= 224) continue;
    const unsigned char *row_data = spr_data[r];

    for(int screen_y = y_begin; screen_y <= y_end; screen_y++) {
      int c = screen_y - spr_y;   // forward column mapping
      unsigned char px = (row_data[c >> 1] >> ((c & 1) * 4)) & 0x0F;
      if(!((transmask >> px) & 1)) {
        frame_buffer[(screen_y - row_start) * 224 + screen_x] = colors[px];
      }
    }
  }
}

void pooyan::render_row(short row) {
  if(row <= 1 || row >= 34) return;

  // MAME render order: tilemap → sprites (nessuna category, passaggio singolo)
  for(char col = 0; col < 28; col++)
    blit_tile(row, col);

  for(unsigned char s = 0; s < active_sprites; s++) {
    if((sprite[s].y < 8 * (row + 1)) && ((sprite[s].y + 16) > 8 * row))
      blit_sprite(row, s);
  }
}

const unsigned short *pooyan::logo(void) {
  return pooyan_logo;
}

#ifdef LED_PIN
void pooyan::gameLeds(CRGB *leds) {
  static char sub_cnt = 0;
  if(sub_cnt++ == 32) {
    sub_cnt = 0;
    static char led = 0;
    char il = (led < NUM_LEDS) ? led : ((2 * NUM_LEDS - 2) - led);
    for(char c = 0; c < NUM_LEDS; c++) {
      if(c == il) leds[c] = LED_RED;
      else        leds[c] = LED_YELLOW;
    }
    led = (led + 1) % (2 * NUM_LEDS - 2);
  }
}

void pooyan::menuLeds(CRGB *leds) {
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}
#endif
