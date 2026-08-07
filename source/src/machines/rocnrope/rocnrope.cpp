#include "rocnrope.h"

void rocnrope::reset() {
  machineBase::reset();
  memset(work_ram, 0, sizeof(work_ram));
  memset(snd_ram,  0, sizeof(snd_ram));
  memset(ay_addr,  0, sizeof(ay_addr));
  memset(ay_regs,  0, sizeof(ay_regs));
  irq_mask     = 0;
  snd_irq_last = 0;
  snd_irq_pend = 0;
  soundlatch   = 0;
  snd_icnt     = 0;
  m6809_reset(&main_cpu);
  // S=0 after reset causes early JSR to push to ROM space (write ignored),
  // then RTS reads reset vector → infinite restart. Set S to top of work RAM.
  main_cpu.S = 0x5FFF;
}

void rocnrope::start() {
  reset();
  game_started = 1;
}

// from MAME hiscore.dat: score table in work RAM. The start bytes (0x01)
// only show up once the game has written its default table — the RAM self
// test patterns that sweep 0x5000-0x5FFF at boot are filtered out by the
// stability check in the hiscore manager.
const hiscore_region_S *rocnrope::hiscoreRegions(unsigned char *count) {
  static const hiscore_region_S regions[] = {
    { 0x5160, 0x40, 0x01, 0x00 },   // high score table
    { 0x50a6, 3,    0x01, 0x00 },   // top score copy
  };
  *count = 2;
  return regions;
}

// ============================================================
// KONAMI-1 main CPU: opcode read (address-dependent XOR decryption)
//
// The KONAMI-1 custom CPU decrypts ONLY opcode fetches (not operands),
// with an XOR mask selected by address bits 1 and 3 (addr & 0x0A):
//   (a & 0x0A) == 0x0 -> ^0x22   (a & 0x0A) == 0x2 -> ^0x82
//   (a & 0x0A) == 0x8 -> ^0x28   (a & 0x0A) == 0xA -> ^0x88
// Equivalently: mask = (a&0x02 ? 0x80:0x20) | (a&0x08 ? 0x08:0x02).
// Only bytes at/above the encryption boundary (ROM at 0x6000) are decrypted.
// Operand bytes are fetched via m6809_read() and stay unencrypted.
// ============================================================
unsigned char rocnrope::m6809_read_opcode(m6809_state *s, uint16_t addr) {
  if (addr >= 0xFFF2 && addr <= 0xFFFD)
    return memory[RNR_VECTORS + (addr - 0xFFF2)];
  if (addr >= 0x6000) {
    uint8_t xormask = ((addr & 0x02) ? 0x80 : 0x20) | ((addr & 0x08) ? 0x08 : 0x02);
    return rocnrope_main_rom[addr - 0x6000] ^ xormask;
  }
  if (addr >= 0x4000)
    return memory[addr - 0x4000];  // sprite/video RAM (CPU 0x4000-0x5FFF)
  return work_ram[addr & 0x1FFF];  // work RAM (CPU 0x0000-0x3FFF, 8KB mirrored)
}

// ============================================================
// KONAMI-1 main CPU: data read (no decryption)
// ============================================================
unsigned char rocnrope::m6809_read(m6809_state *s, uint16_t addr) {
  if (addr >= 0xFFF2 && addr <= 0xFFFD)
    return memory[RNR_VECTORS + (addr - 0xFFF2)];
  if (addr >= 0x6000)
    return rocnrope_main_rom[addr - 0x6000];

  // Input ports
  if (addr == 0x3000) {
    uint8_t dsw2 = ROCNROPE_DSW2;
    if (input->demoSoundsOff()) dsw2 |= ROCNROPE_DSW2_DEMO_SOUND_OFF;
    else                        dsw2 &= ~ROCNROPE_DSW2_DEMO_SOUND_OFF;
    return dsw2;
  }
  if (addr == 0x3080) {
    uint8_t keys = input->buttons_get();
    uint8_t val  = 0xFF;
    // The coin key doubles as button 2 (enemy flash) during play: only insert
    // credits outside the game. Game state var (CPU 0x5002, IRQ dispatch):
    // 0=boot, 1=attract, 2=credited/press start, 3=gameplay, 4=game over.
    if ((keys & BUTTON_COIN) && memory[0x1002] <= 2) val &= ~0x01;
    if (keys & BUTTON_START) val &= ~0x08;
    return val;
  }
  if (addr == 0x3081) {
    uint8_t keys = input->buttons_get();
    uint8_t val  = 0xFF;
    if (keys & BUTTON_LEFT)  val &= ~0x01;
    if (keys & BUTTON_RIGHT) val &= ~0x02;
    if (keys & BUTTON_UP)    val &= ~0x04;
    if (keys & BUTTON_DOWN)  val &= ~0x08;
    if (keys & BUTTON_FIRE)  val &= ~0x10;
    if (keys & BUTTON_COIN)  val &= ~0x20;  // button 2 (flash) on the coin key
    return val;
  }
  if (addr == 0x3082) return 0xFF;  // P2 idle
  if (addr == 0x3083) return ROCNROPE_DSW1;
  if (addr == 0x3100) return ROCNROPE_DSW3;

  if (addr >= 0x4000)
    return memory[addr - 0x4000];  // sprite/video RAM (CPU 0x4000-0x5FFF)
  return work_ram[addr & 0x1FFF];  // work RAM (CPU 0x0000-0x3FFF, 8KB mirrored)
}

// ============================================================
// KONAMI-1 main CPU: data write
// ============================================================
void rocnrope::m6809_write(m6809_state *s, uint16_t addr, uint8_t val) {
  if (addr < 0x4000) {
    work_ram[addr & 0x1FFF] = val;
    return;
  }
  if (addr < 0x6000) {
    memory[addr - 0x4000] = val;    // sprite/video RAM (CPU 0x4000-0x5FFF)
    return;
  }

  // Watchdog at 0x8000 (ignore)
  if (addr == 0x8000) return;

  // LS259 latch: 0x8080-0x8087 (addr bits 2:0 = Q output, data bit0 = value)
  if (addr >= 0x8080 && addr <= 0x8087) {
    uint8_t bit  = addr & 0x07;
    uint8_t bval = val & 1;
    switch (bit) {
      case 0: break;  // Q0 = flip screen (ignored)
      case 1:         // Q1 = sound CPU IRQ trigger (edge-triggered)
        if (bval && !snd_irq_last) snd_irq_pend = 1;
        snd_irq_last = bval;
        break;
      case 2: break;  // Q2 = mute (ignored)
      case 3: break;  // Q3 = coin counter 0
      case 4: break;  // Q4 = coin counter 1
      case 7:
        irq_mask = bval;
        if (!bval) main_cpu.irq_pending = 0;  // CLEAR_LINE when disabling
        break;  // Q7 = IRQ enable
    }
    return;
  }

  // Sound data latch: 0x8100
  if (addr == 0x8100) {
    soundlatch = val;
    return;
  }

  // Interrupt vector write: 0x8182-0x818D → stores to RAM[RNR_VECTORS+offset]
  // CPU reads vectors from 0xFFF2-0xFFFD (same offset: addr-0x8182 = vect idx)
  if (addr >= 0x8182 && addr <= 0x818D) {
    memory[RNR_VECTORS + (addr - 0x8182)] = val;
    return;
  }
}

// ============================================================
// Z80 sound CPU (timeplt_audio, same as Tutankham/TimePlt)
// ============================================================
unsigned char rocnrope::opZ80(unsigned short Addr) {
  if (Addr < 0x2000) return rocnrope_audio_rom[Addr];
  return 0xFF;
}

unsigned char rocnrope::rdZ80(unsigned short Addr) {
  if (Addr < 0x2000) return rocnrope_audio_rom[Addr];
  if (Addr < 0x3000) return 0xFF;

  if (Addr >= 0x3000 && Addr <= 0x33FF)
    return snd_ram[Addr & 0x3FF];

  // AY#1 data read (0x4000-0x4FFF)
  if ((Addr & 0xF000) == 0x4000) {
    uint8_t reg = ay_addr[0] & 0x0F;
    if (reg == 14) return soundlatch;
    if (reg == 15) {
      // LS90 bi-quinary music tempo timer. MAME: timer[(Z80_cycles/512)%10]
      // with Z80 @ 1.79 MHz = 58.3 steps/frame. snd_icnt advances 1875/frame
      // (625 x 3), so /32 gives 58.6 steps/frame. (/24 was ~11% slow.)
      static const uint8_t timer[10] = {
        0x00, 0x10, 0x20, 0x30, 0x40, 0x90, 0xa0, 0xb0, 0xa0, 0xd0
      };
      return timer[(snd_icnt / 32) % 10];
    }
    return ay_regs[0][reg];
  }

  // AY#2 data read (0x6000-0x6FFF)
  if ((Addr & 0xF000) == 0x6000) {
    uint8_t reg = ay_addr[1] & 0x0F;
    return ay_regs[1][reg];
  }

  return 0x00;
}

void rocnrope::wrZ80(unsigned short Addr, unsigned char Value) {
  if (Addr >= 0x3000 && Addr <= 0x33FF) {
    snd_ram[Addr & 0x3FF] = Value;
    return;
  }
  if ((Addr & 0xF000) == 0x4000) {
    uint8_t reg = ay_addr[0] & 0x0F;
    ay_regs[0][reg] = Value;
    if (reg < 14) soundregs[reg] = Value;
    return;
  }
  if ((Addr & 0xF000) == 0x5000) {
    ay_addr[0] = Value & 0x0F;
    return;
  }
  if ((Addr & 0xF000) == 0x6000) {
    uint8_t reg = ay_addr[1] & 0x0F;
    ay_regs[1][reg] = Value;
    if (reg < 14) soundregs[16 + reg] = Value;
    return;
  }
  if ((Addr & 0xF000) == 0x7000) {
    ay_addr[1] = Value & 0x0F;
    return;
  }
}

// ============================================================
// Frame execution: M6809 @ 1.536 MHz + Z80 @ 1.789 MHz
// ============================================================
void rocnrope::run_frame(void) {
  for (int i = 0; i < INST_PER_FRAME / 2; i++) {
    // 9 instructions x 625 = 5625/frame ~ 25.3k cycles at ~4.5 CPI, matching
    // the real 1.536 MHz 6809 (25600 cycles/frame). 8 was ~12% too slow.
    m6809_step(&main_cpu, 9);

    StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]);
    snd_icnt += 3;  // music timer resolution: 3 x 625 = 1875/frame (see rdZ80 reg 15)

    if (snd_irq_pend && (cpu[0].IFF & IFF_1)) {
      IntZ80(&cpu[0], INT_RST38);
      snd_irq_pend = 0;
    }
  }

  // VBlank IRQ to main CPU (every frame when irq_mask=1)
  if (irq_mask)
    m6809_irq(&main_cpu);
}

// ============================================================
// Sprite extraction (before rendering)
// Sprite RAM layout (MAME rocnrope draw_sprites):
//   spriteram[0] at CPU 0x4400 = MEM[RNR_SPRITE0]: y_pos + code
//   spriteram[1] at CPU 0x4000 = MEM[RNR_SPRITE1]: attr + x_pos
//
//   offs step -2, from 0x2E down to 0x00 (24 sprites):
//     y_raw   = spriteram[0][offs]     → X on MAME portrait screen: 240 - y_raw
//     code    = spriteram[0][offs+1]
//     attr    = spriteram[1][offs]     → color[3:0], flipX[6], flipY[7] inverted
//     x_raw   = spriteram[1][offs+1]  → Y on MAME portrait screen
//
// Coordinate mapping: Roc'n Rope is ROT270 in MAME, i.e. the OPPOSITE
// rotation of Time Pilot (ROT90). Relative to the TimePlt recipe the
// final portrait image is rotated 180°: both screen axes reversed.
//   MAME native dest: destx = 240 - y_raw, desty = x_raw
//   TimePlt (ROT90):  x_port = 224 - desty, y_port = destx + 16
//   ROT270 = +180°:   x_port = desty - 16  → spr.x = x_raw - 16
//                     y_port = 256 - destx → spr.y = y_raw + 16
// Both flip flags are inverted vs the ROT90 recipe (180° pixel flip).
// ============================================================
void rocnrope::prepare_frame(void) {
  active_sprites = 0;
  if (!game_started) return;

  for (int offs = 0x2E; offs >= 0 && active_sprites < 128; offs -= 2) {
    uint8_t y_raw = memory[RNR_SPRITE0 + offs];
    uint8_t code  = memory[RNR_SPRITE0 + offs + 1];
    uint8_t attr  = memory[RNR_SPRITE1 + offs];
    uint8_t x_raw = memory[RNR_SPRITE1 + offs + 1];

    uint8_t color  = attr & 0x0F;
    uint8_t flip_x = (attr >> 6) & 1;
    uint8_t flip_y = (~attr >> 7) & 1;  // bit7=0 means flipY (inverted)

    // Skip offscreen sprites
    int mame_x = 240 - y_raw;
    if (mame_x <= -16 || mame_x >= 256) continue;

    struct sprite_S spr;
    spr.x     = (int)x_raw - 16;
    spr.y     = (int)y_raw + 16;
    spr.code  = code;
    spr.color = color;
    // 180° vs ROT90 recipe: both flip senses inverted
    spr.flags = (flip_y ? 0 : 1) | (flip_x ? 0 : 2);

    if (spr.y > -16 && spr.y < 304 && spr.x > -16 && spr.x < 224)
      sprite[active_sprites++] = spr;
  }
}

// ============================================================
// Tile rendering: 4bpp with tileaddr mapping + flip support
// Tile code: 8-bit from videoRAM + bit7 of colorRAM extends to 512
// Color: bits 3:0 of colorRAM (16 palettes × 16 colors)
// Flip: bit6=flipX, bit5=flipY of colorRAM
// ============================================================
void rocnrope::blit_tile(short row, char col) {
  if (row < 2 || row >= 34) return;

  // ROT270 scan_rows mapping (NOT the Pacman/TimePlt tileaddr[][] table,
  // which encodes the opposite ROT90 rotation): native tile row my = col+2
  // (portrait col → native row, forward), native tile col mx = 33-row
  // (portrait row → native col, reversed). addr = my*32 + mx.
  unsigned short ta   = 32 * (col + 2) + (33 - row);
  uint8_t tile_code   = memory[RNR_VIDEORAM + ta];
  uint8_t attr        = memory[RNR_COLORRAM + ta];

  unsigned short code = tile_code + ((attr & 0x80) ? 256 : 0);
  uint8_t color  = attr & 0x0F;
  // 90° rotation swaps the flip axes: MAME native FLIPX (bit6) becomes a
  // vertical flip on the portrait screen (our flip_y) and native FLIPY (bit5)
  // a horizontal one (our flip_x).
  uint8_t flip_x = (attr >> 5) & 1;
  uint8_t flip_y = (attr >> 6) & 1;

  const uint8_t (*tile)[4] = rocnrope_tilemap[code];
  const uint16_t *pal = rocnrope_tile_cmap[color];

  unsigned short *ptr = frame_buffer + 8 * col;
  // 180° vs the ROT90 tile pre-rotation in the header: both axes reversed,
  // which is the same as inverting both flip senses.
  for (int r = 0; r < 8; r++, ptr += (224 - 8)) {
    int sr = flip_y ? r : (7 - r);
    for (int c = 0; c < 8; c++) {
      int sc = flip_x ? c : (7 - c);
      uint8_t px = (tile[sr][sc >> 1] >> ((sc & 1) << 2)) & 0x0F;
      ptr[c] = pal[px];
    }
    ptr += 8;
  }
}

// ============================================================
// Sprite rendering (transposed, landscape orientation)
// ROM row (landscape dy) → screen X offset (15-r reversed)
// ROM col (landscape dx) → screen Y offset (forward)
// pixel==0 is transparent
// ============================================================
void rocnrope::blit_sprite(short row, unsigned char s) {
  uint8_t  code   = sprite[s].code;
  uint8_t  color  = sprite[s].color;
  uint8_t  flags  = sprite[s].flags;
  int      spr_x  = sprite[s].x;
  int      spr_y  = sprite[s].y;

  uint8_t flip_y = flags & 1;
  uint8_t flip_x = (flags >> 1) & 1;

  const uint16_t *pal = rocnrope_sprite_cmap[color];
  int row_start = 8 * row;

  int y_begin = (spr_y > row_start)      ? spr_y      : row_start;
  int y_end   = (spr_y + 15 < row_start + 7) ? (spr_y + 15) : (row_start + 7);
  if (y_begin > y_end) return;

  for (int r = 0; r < 16; r++) {
    int sr = flip_y ? (15 - r) : r;
    int screen_x = spr_x + 15 - r;  // reversed rows → screen X
    if (screen_x < 0 || screen_x >= 224) continue;

    const uint8_t *row_data = rocnrope_spritemap[code][sr];

    for (int screen_y = y_begin; screen_y <= y_end; screen_y++) {
      int dc = screen_y - spr_y;
      int sc = flip_x ? (15 - dc) : dc;
      uint8_t px = (row_data[sc >> 1] >> ((sc & 1) << 2)) & 0x0F;
      if (px)
        frame_buffer[(screen_y - row_start) * 224 + screen_x] = pal[px];
    }
  }
}

// ============================================================
// Render row: tiles then sprites (no layering needed for this game)
// ============================================================
void rocnrope::render_row(short row) {
  if (row < 2 || row >= 34) return;
  if (!game_started) return;

  for (char col = 0; col < 28; col++)
    blit_tile(row, col);

  for (unsigned char s = 0; s < active_sprites; s++) {
    if (sprite[s].y < 8 * (row + 1) && (sprite[s].y + 16) > 8 * row)
      blit_sprite(row, s);
  }
}

const unsigned short *rocnrope::logo(void) {
  return rocnrope_logo;
}

#ifdef LED_PIN
void rocnrope::gameLeds(CRGB *leds) {
  static char sub_cnt = 0;
  if (sub_cnt++ == 32) {
    sub_cnt = 0;
    static char led = 0;
    char il = (led < NUM_LEDS) ? led : ((2 * NUM_LEDS - 2) - led);
    for (char c = 0; c < NUM_LEDS; c++) {
      if (c == il) leds[c] = LED_YELLOW;
      else         leds[c] = CRGB(0, 80, 0);
    }
    led = (led + 1) % (2 * NUM_LEDS - 2);
  }
}

void rocnrope::menuLeds(CRGB *leds) {
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}
#endif
