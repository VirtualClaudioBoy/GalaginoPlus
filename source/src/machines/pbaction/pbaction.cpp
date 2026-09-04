#include "pbaction.h"

// ===========================================================================
// Pinball Action (Tehkan, 1985) - port of MAME `pbaction` set 1.
// Reference: source/mame/mame-master/src/mame/tecmo/pbaction.cpp
// ===========================================================================

// Timing.  Main Z80 @ 4 MHz, audio Z80 @ 3 MHz, screen 60 Hz.
//
// Per loop pass we step the main CPU 9x and the audio CPU 3x.  Misura su HW
// (fps meter): ~1.25M step/s -> ~7ms per frame con il budget originale di
// bombjack (4+3 step, 8750 step/frame = solo ~45% del main Z80 reale a 4MHz).
// La fisica della palla gira nel loop principale tra due NMI: con quel budget
// non completava e il gioco andava a ~30Hz effettivi (lento).  Con 9+3 =
// 15000 step/frame (~11.250 istruzioni main = ~100% del reale) run_frame
// resta ~12ms, sotto il budget di 16.6ms.  NOTA: la scala NON e' liberamente
// aumentabile: a 4+3 frame scale (35000 step) run_frame superava i 16.6ms e
// gli fps crollavano (gioco MOLTO piu' lento); il collo di bottiglia e' il
// throughput dell'interprete Z80, non il budget teorico.
//
// Audio CTC channel 1 (music tick): control word 0xA7 (timer mode, /256
// prescaler), time constant 0x5D (93) -> 256 * 93 / 3 MHz = 7.94 ms ~ 126 Hz,
// i.e. ~2 IRQs per 60 Hz frame.  (CTC channel 0 fires once per sound command.)
#define PBACTION_CTC_TICKS_PER_FRAME  2

void pbaction::reset() {
  machineBase::reset();
  m_scroll = 0;
  m_flip = false;
  m_nmi_mask = false;
  sound_latch = 0;
  sound_cmd_pending = 0;
  ay_address[0] = ay_address[1] = ay_address[2] = 0;
  coinBackup = 0;
  coinFrameCounter = 0;
  memset(pbaction_palette, 0, sizeof(pbaction_palette));
}

// ---------------------------------------------------------------------------
// CPU memory access
// ---------------------------------------------------------------------------
unsigned char pbaction::opZ80(unsigned short Addr) {
  if (current_cpu == 0)
    return pbaction_main_rom[Addr];
  return (Addr < 0x2000) ? pbaction_audio_rom[Addr] : 0xff;
}

unsigned char pbaction::rdZ80(unsigned short Addr) {
  if (current_cpu == 0) {
    // 0x0000-0xbfff : ROM
    if (Addr < 0xc000)
      return pbaction_main_rom[Addr];

    // 0xc000-0xcfff : work RAM
    if (Addr < 0xd000)
      return memory[PBACTION_WORK_RAM + (Addr - 0xc000)];

    // 0xd000-0xdfff : fg/bg video + color RAM
    if (Addr < 0xe000)
      return memory[PBACTION_FG_VRAM + (Addr - 0xd000)];

    // 0xe000-0xe07f : sprite RAM
    if (Addr >= 0xe000 && Addr <= 0xe07f)
      return memory[PBACTION_SPRITE_RAM + (Addr - 0xe000)];

    // 0xe400-0xe5ff : palette RAM
    if (Addr >= 0xe400 && Addr <= 0xe5ff)
      return memory[PBACTION_PALETTE_RAM + (Addr - 0xe400)];

    // 0xe600-0xe606 : inputs / DSW / watchdog
    switch (Addr) {
    case PBACTION_P1: {
      // IN0 - IP_ACTIVE_HIGH.  pbaction is a pinball game:
      //   bit3 BUTTON1 = left flipper   bit4 BUTTON2 = right flipper
      //   bit0 BUTTON3 = ball shooter   bit2 BUTTON4 = nudge / tilt
      unsigned char k = input->buttons_get();
      unsigned char r = 0;
      if (k & BUTTON_COIN)                      r |= 0x08;  // left flipper
      if (k & BUTTON_FIRE)                      r |= 0x10;  // right flipper
      if (k & BUTTON_DOWN)                      r |= 0x01;  // ball shooter / plunger
      if (k & BUTTON_UP)                        r |= 0x04;  // nudge
      return r;
    }
    case PBACTION_P2:
      return 0x00;   // cocktail P2 controls, unused upright
    case PBACTION_SYSTEM: {
      // IN2 - IP_ACTIVE_HIGH : bit0 COIN1, bit2 START1, bit3 START2
      unsigned char k = input->buttons_get();
      unsigned char r = 0;
      if (k & BUTTON_START) r |= 0x04;
      if ((k & BUTTON_COIN) && !coinBackup && !isGameRunning()) {
        coinFrameCounter = 3;
        coinBackup = 1;
      }
      if (coinFrameCounter > 0)
        r |= 0x01;
      else if ((k & BUTTON_COIN) == 0)
        coinBackup = 0;
      return r;
    }
    case PBACTION_DSW1_PORT:
      return PBACTION_DSW1 |
             (input->demoSoundsOff() ? PBACTION_DSW1_DEMOSOUNDS_OFF : 0);
    case PBACTION_DSW2_PORT:
      return PBACTION_DSW2;
    case PBACTION_WATCHDOG:
      return 0xff;   // watchdog reset on read - value ignored
    }
    return 0xff;
  }

  // ---- audio CPU ----
  if (Addr < 0x2000)
    return pbaction_audio_rom[Addr];
  if (Addr >= 0x4000 && Addr <= 0x47ff)
    return memory[PBACTION_AUDIO_RAM + (Addr - 0x4000)];
  if (Addr == 0x8000) {
    // read sound latch (ISR 0x0108) - the real HW clears the audio IRQ here
    return sound_latch;
  }
  return 0xff;
}

void pbaction::wrZ80(unsigned short Addr, unsigned char Value) {
  if (current_cpu == 0) {
    if (Addr >= 0xc000 && Addr <= 0xcfff) {
      memory[PBACTION_WORK_RAM + (Addr - 0xc000)] = Value;
      return;
    }
    // d000-dfff : fg vram / fg cram / bg vram / bg cram (contiguous in memory[])
    if (Addr >= 0xd000 && Addr <= 0xdfff) {
      memory[PBACTION_FG_VRAM + (Addr - 0xd000)] = Value;
      return;
    }
    if (Addr >= 0xe000 && Addr <= 0xe07f) {
      memory[PBACTION_SPRITE_RAM + (Addr - 0xe000)] = Value;
      return;
    }
    if (Addr >= 0xe400 && Addr <= 0xe5ff) {
      palette_write(Addr - 0xe400, Value);
      return;
    }
    switch (Addr) {
    case PBACTION_P1:                 // 0xe600 : nmi_mask_w
      m_nmi_mask = (Value & 0x01);
      // The ROM enables the vblank NMI only once it has finished its power-on
      // RAM clear and entered the main loop -> use that as "game running", so
      // the emulation task starts pacing itself to the 60 Hz video sync
      // (otherwise run_frame free-runs and the game plays ~3-4x too fast).
      if (m_nmi_mask)
        game_started = 1;
      return;
    case PBACTION_DSW1_PORT:          // 0xe604 : flipscreen_w
      m_flip = (Value & 0x01);
      return;
    case PBACTION_WATCHDOG:           // 0xe606 : scroll_w
      m_scroll = (short)Value - 3;
      if (m_flip)
        m_scroll = -m_scroll;
      return;
    case PBACTION_SOUND_CMD:          // 0xe800 : sh_command_w
      sound_latch = Value;
      sound_cmd_pending = 1;          // ctc->trg0 pulse -> audio vector 0x00
      return;
    }
    return;
  }

  // ---- audio CPU ----
  if (Addr >= 0x4000 && Addr <= 0x47ff) {
    memory[PBACTION_AUDIO_RAM + (Addr - 0x4000)] = Value;
    return;
  }
  // 0xffff : sound_irq_ack_w - nothing to do in this simplified model
}

// ---------------------------------------------------------------------------
// Audio CPU I/O : CTC at 0x00-0x03, AY-3-8910 x3 at 0x10/0x20/0x30
// ---------------------------------------------------------------------------
void pbaction::outZ80(unsigned short Port, unsigned char Value) {
  if (current_cpu != 1)
    return;

  unsigned char p = Port & 0xff;
  int chip;
  switch (p & 0xf0) {
  case 0x10: chip = 0; break;
  case 0x20: chip = 1; break;
  case 0x30: chip = 2; break;
  default:   return;   // 0x00-0x03 CTC, 0x12/0x13 unknown - ignored
  }

  if ((p & 1) == 0)
    ay_address[chip] = Value & 0x0f;
  else
    soundregs[(chip * 16) + ay_address[chip]] = Value;
}

unsigned char pbaction::inZ80(unsigned short Port) {
  if (current_cpu != 1)
    return 0xff;

  unsigned char p = Port & 0xff;
  if (p == 0x11) return soundregs[0 * 16 + ay_address[0]];
  if (p == 0x21) return soundregs[1 * 16 + ay_address[1]];
  if (p == 0x31) return soundregs[2 * 16 + ay_address[2]];
  return 0xff;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
void pbaction::run_frame(void) {
  const int ctc_period = INST_PER_FRAME / PBACTION_CTC_TICKS_PER_FRAME;

  for (int i = 0; i < INST_PER_FRAME; i++) {
    current_cpu = 0;
    StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]);
    StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]);
    StepZ80(&cpu[0]);

    current_cpu = 1;
    StepZ80(&cpu[1]); StepZ80(&cpu[1]); StepZ80(&cpu[1]);

    // audio CTC channel 0 : one IRQ (IM2 vector 0x00, ISR 0x0108) per sound
    // command, delivered as soon as the main CPU latches it
    if (sound_cmd_pending) {
      sound_cmd_pending = 0;
      IntZ80(&cpu[1], 0x00);
    }

    // audio CTC channel 1 : periodic music tick (IM2 vector 0x02, ISR 0x0113)
    if ((i % ctc_period) == ctc_period - 1)
      IntZ80(&cpu[1], 0x02);
  }

  // main CPU : vblank NMI, gated by nmi_mask
  if (m_nmi_mask) {
    current_cpu = 0;
    IntZ80(&cpu[0], INT_NMI);
  }

  if (coinFrameCounter > 0)
    coinFrameCounter--;

  current_cpu = 0;   // leave dispatch coherent for hiscore access
}

// ---------------------------------------------------------------------------
// Palette : xBGR_444 RAM (xxxxBBBBGGGGRRRR, LE) -> RGB565, byte swapped
// ---------------------------------------------------------------------------
void pbaction::palette_write(unsigned short offset, unsigned char value) {
  memory[PBACTION_PALETTE_RAM + offset] = value;

  unsigned short entry = offset >> 1;
  unsigned char lo = memory[PBACTION_PALETTE_RAM + (entry << 1)];
  unsigned char hi = memory[PBACTION_PALETTE_RAM + (entry << 1) + 1];

  unsigned char r4 =  lo       & 0x0f;
  unsigned char g4 = (lo >> 4) & 0x0f;
  unsigned char b4 =  hi       & 0x0f;

  unsigned char r8 = (r4 << 4) | r4;
  unsigned char g8 = (g4 << 4) | g4;
  unsigned char b8 = (b4 << 4) | b4;

  unsigned short c = ((r8 & 0xf8) << 8) | ((g8 & 0xfc) << 3) | (b8 >> 3);
  pbaction_palette[entry] = (c >> 8) | (c << 8);   // byte swap for the SPI panel
}

// ---------------------------------------------------------------------------
// Sprites - MAME pbaction_state::draw_sprites()
//   spriteram : 32 entries x 4 bytes  (e000-e07f)
//     byte 0  code ; bit7 = double (32x32) size
//     byte 1  bit0-3 color ; bit6 flipx ; bit7 flipy
//     byte 2  y position
//     byte 3  x position
//   an entry is skipped if the PREVIOUS entry (offs-4) is double sized.
//   The port screen is ROT90: MAME x -> port Y, MAME y -> port X.
// ---------------------------------------------------------------------------
// Re-scan the hardware sprite list into sprite[]/active_sprites from LIVE
// sprite RAM.  Called at the start of each render half (see render_row) so the
// ball sprite is captured at the same age as the tilemap that half draws.
void pbaction::scan_sprites(void) {
  active_sprites = 0;
  unsigned char *sr = &memory[PBACTION_SPRITE_RAM];

  for (int offs = 0x80 - 4; offs >= 0 && active_sprites < 32; offs -= 4) {
    // if the previous sprite is double sized, this slot is its 2nd half
    if (offs > 0 && (sr[offs - 4] & 0x80))
      continue;

    unsigned char b0 = sr[offs + 0];
    unsigned char b1 = sr[offs + 1];
    unsigned char by = sr[offs + 2];
    unsigned char bx = sr[offs + 3];

    bool big  = (b0 & 0x80);
    int  size = big ? 32 : 16;

    // MAME screen coords of the sprite top-left (draw_sprites):
    //   sy = (b0&0x80) ? 225-by : 241-by ;  sx = bx ;  drawX = sx - scroll
    // A uniform (-16, +16) correction, verified pixel-for-pixel against MAME:
    // the title screen matches with 0 differing pixels and the in-game ball
    // sits exactly at the plunger.  -16 on the MAME-y axis is the 16px
    // visarea-y top margin the tile path bakes into tileaddr[] but sprites
    // otherwise miss; +16 on the MAME-x axis lines the decoded sprite gfx
    // (spritelayout1/2) up with how MAME's gfxdecode places it in the cell.
    int mame_sy = (big ? 225 : 241) - 16 - by;
    int mame_sx = bx - m_scroll + 16;

    struct sprite_S spr;
    spr.code     = big ? (b0 & 0x1f) : b0;   // 32 large / 256 normal sprites
    spr.color    = b1 & 0x0f;
    spr.is_32x32 = big ? 1 : 0;
    spr.flip_x   = (b1 & 0x40) ? 1 : 0;
    spr.flip_y   = (b1 & 0x80) ? 1 : 0;

    // Same rotation convention as starforce::blit_sprite:
    //   spr.y  (framebuffer line base)   = MAME sx
    //   spr.x  (pre-mirror column base)  = MAME sy   -> fb col = 223 - (spr.x + x)
    spr.y = mame_sx;
    spr.x = mame_sy;

    if (spr.y + size <= 0 || spr.y >= 288 ||
        spr.x + size <= 0 || spr.x >= 224 + size)
      continue;

    sprite[active_sprites++] = spr;
  }
}

void pbaction::prepare_frame(void) {
  scan_sprites();   // also refreshed per render-half in render_row()
}

// ---------------------------------------------------------------------------
// Tilemap helpers.  Both layers: 8x8, 32x32 SCAN_ROWS, same handedness as
// Bomb Jack (also Tehkan / ROT90) -> tileaddr[row][col] gives the flat
// tilemap index, and the pre-rotated tile[r][c] blits straight to
// frame_buffer[r*224 + col*8 + c] (exactly like bombjack::blit_tile_fg).
//
// pbaction adds a shared horizontal scroll (MAME x axis).  In the rotated
// port frame that is the *vertical* axis, i.e. it shifts which framebuffer
// LINE a given tile pixel-row lands on.  We handle it by reading, for each
// of the 8 framebuffer lines, the tilemap cell + tile pixel-row that the
// (scrolled) MAME x coordinate points at.
//
// MAME: set_scrollx(0, m_scroll) makes screen-x X show tilemap-x X + m_scroll,
// while a sprite is drawn at sx - m_scroll.  So the tilemap read must ADD
// m_scroll (matching the sprites' - m_scroll on the write side keeps the ball
// aligned with the playfield it sits on - the sign was backwards before and
// the ball rested ~2*m_scroll off the flippers in-game; invisible on the
// title screen where m_scroll == 0).
//
// tileaddr[row][col] = (29-col)*32 + (row-2).  The (row-2) part is the MAME
// tile column (x); we reconstruct the fixed (29-col)*32 half from it and add
// the scrolled MAME-x tile index.
// ---------------------------------------------------------------------------
void pbaction::blit_tile_bg(short row, char col) {
  int base_idx = tileaddr[row][col];        // (29-col)*32 + (row-2)
  int map_y32  = base_idx & ~31;            // (29-col)*32   (stays constant)

  unsigned short *ptr = frame_buffer + col * 8;

  for (int r = 0; r < 8; r++, ptr += 224) {
    // MAME x coordinate of this framebuffer line
    int mame_x = ((row - 2) * 8 + r + m_scroll) & 0xff;    // tilemap wraps at 256
    int map_x  = mame_x >> 3;                              // MAME tile column 0..31
    int line   = mame_x & 7;                               // pixel row within tile

    unsigned short idx = map_y32 | map_x;
    unsigned char chr  = memory[PBACTION_BG_VRAM + idx];
    unsigned char attr = memory[PBACTION_BG_CRAM + idx];

    unsigned int  tile_id = chr + 0x10 * (attr & 0x70);
    // bg is 4bpp (16 pens/group), palette base 128 -> group stride 16
    const unsigned short *colors = &pbaction_palette[128 + ((attr & 0x07) << 4)];
    int src_line = (attr & 0x80) ? (7 - line) : line;      // MAME flipy

    if (tile_id >= 2048) tile_id &= 2047;
    const unsigned char *g = pbaction_bg_tiles[tile_id][src_line];

    ptr[0] = colors[g[0]];  ptr[1] = colors[g[1]];
    ptr[2] = colors[g[2]];  ptr[3] = colors[g[3]];
    ptr[4] = colors[g[4]];  ptr[5] = colors[g[5]];
    ptr[6] = colors[g[6]];  ptr[7] = colors[g[7]];
  }
}

void pbaction::blit_tile_fg(short row, char col) {
  int base_idx = tileaddr[row][col];
  int map_y32  = base_idx & ~31;

  unsigned short *ptr = frame_buffer + col * 8;

  for (int r = 0; r < 8; r++, ptr += 224) {
    int mame_x = ((row - 2) * 8 + r + m_scroll) & 0xff;
    int map_x  = mame_x >> 3;
    int line   = mame_x & 7;

    unsigned short idx = map_y32 | map_x;
    unsigned char chr  = memory[PBACTION_FG_VRAM + idx];
    unsigned char attr = memory[PBACTION_FG_CRAM + idx];

    unsigned int  tile_id = chr + 0x10 * (attr & 0x30);
    const unsigned short *colors = &pbaction_palette[(attr & 0x0f) << 3];
    int src_line = (attr & 0x40) ? (7 - line) : line;      // MAME flipx -> port line
    bool flipc   = (attr & 0x80);                          // MAME flipy -> port column

    if (tile_id >= 1024) tile_id &= 1023;
    const unsigned char *g = pbaction_fg_tiles[tile_id][src_line];

    if (!flipc) {
      if (g[0]) ptr[0] = colors[g[0]];
      if (g[1]) ptr[1] = colors[g[1]];
      if (g[2]) ptr[2] = colors[g[2]];
      if (g[3]) ptr[3] = colors[g[3]];
      if (g[4]) ptr[4] = colors[g[4]];
      if (g[5]) ptr[5] = colors[g[5]];
      if (g[6]) ptr[6] = colors[g[6]];
      if (g[7]) ptr[7] = colors[g[7]];
    }
    else {
      if (g[0]) ptr[7] = colors[g[0]];
      if (g[1]) ptr[6] = colors[g[1]];
      if (g[2]) ptr[5] = colors[g[2]];
      if (g[3]) ptr[4] = colors[g[3]];
      if (g[4]) ptr[3] = colors[g[4]];
      if (g[5]) ptr[2] = colors[g[5]];
      if (g[6]) ptr[1] = colors[g[6]];
      if (g[7]) ptr[0] = colors[g[7]];
    }
  }
}

// ---------------------------------------------------------------------------
// Sprite blit for one 8-line strip.
//
// Coordinate model (validated pixel-for-pixel against a MAME title-screen
// frame).  The gfx arrays are pre-rotated 90 CW by the converter
// (spr_prerot[a][b] == spr_orig[size-1-b][a]).  For a sprite whose port-space
// top-left is (s->y, s->x) (== MAME sx, sy with the big-sprite correction
// applied in prepare_frame), pixel (ox, oy) inside it lands at:
//   fb_line   = s->y + ox        (- strip origin)
//   fb_column = 223 - (s->x + oy)
//   pen       = spr_prerot[ox][size-1-oy]
// MAME flipx mirrors ox, MAME flipy mirrors oy.
// ---------------------------------------------------------------------------
void pbaction::blit_sprite(short row, unsigned char s_idx) {
  const struct sprite_S *s = &sprite[s_idx];
  const int size = s->is_32x32 ? 32 : 16;
  const int m1 = size - 1;

  const int y_strip = row * 8;
  // fb_line spans [s->y, s->y + size)  (ox axis)
  if (s->y + size <= y_strip || s->y >= y_strip + 8)
    return;

  if (s->is_32x32 ? (s->code >= 32) : (s->code >= 256))
    return;

  const unsigned short *colors = &pbaction_palette[s->color << 3];

  int ox0 = (s->y < y_strip) ? (y_strip - s->y) : 0;
  int ox1 = (s->y + size > y_strip + 8) ? (y_strip + 8 - s->y) : size;

  for (int ox = ox0; ox < ox1; ox++) {
    int fb_line = (s->y + ox) - y_strip;             // 0..7
    int a = s->flip_x ? (m1 - ox) : ox;              // pre-rot first index
    unsigned short *fb = &frame_buffer[fb_line * 224];

    for (int oy = 0; oy < size; oy++) {
      int fb_col = 223 - (s->x + oy);
      if (fb_col < 0 || fb_col >= 224)
        continue;
      int oyf = s->flip_y ? (m1 - oy) : oy;
      int b = m1 - oyf;                               // pre-rot second index

      unsigned char pen = s->is_32x32 ? pbaction_sprites32[s->code][a][b]
                                      : pbaction_sprites16[s->code][a][b];
      if (pen)
        fb[fb_col] = colors[pen];
    }
  }
}

// ---------------------------------------------------------------------------
// Row renderer.  36 strips of 224x8.  pbaction shows MAME columns 0..31
// (port rows 2..33) and MAME rows 2..29 (port cols 0..27).
// Draw order matches MAME screen_update: bg, sprites, fg.
// ---------------------------------------------------------------------------
void pbaction::render_row(short row) {
  if (row < 2 || row >= 34)
    return;

  // half-rate video renders rows 2..17 then (after the emu advances a frame)
  // rows 18..33; re-sample the sprite list at the top of each half so the ball
  // matches the tilemap age of the half it is drawn in.
  if (row == 2 || row == 18)
    scan_sprites();

  for (char col = 0; col < 28; col++)
    blit_tile_bg(row, col);

  for (unsigned char s = 0; s < active_sprites; s++)
    blit_sprite(row, s);

  for (char col = 0; col < 28; col++)
    blit_tile_fg(row, col);
}

const unsigned short *pbaction::logo(void) {

  return pbaction_logo;
}

// bit0 of work RAM 0xC000 == game in progress (runtime-verified: C000=0x41
// during play; the game's own coin routine at 0x0E6E does BIT 0,(0xC000)/
// RET NZ to ignore coins while a game is running)
bool pbaction::isGameRunning() const {
  return (memory[PBACTION_WORK_RAM] & 0x01) != 0;
}

// High score : MAME hiscore.dat, set "pbaction" (shared by pbaction2..t):
//   @:maincpu,program,c093,51,07,00   score table (0x51 bytes, first 0x07, last 0x00)
//   @:maincpu,program,c12f,27,48,4b   initials    (0x27 bytes, first 0x48, last 0x4b)
// Both live in the main work RAM (0xc000 base -> memory[] offset 0).
const hiscore_region_S *pbaction::hiscoreRegions(unsigned char *count) {
  static const hiscore_region_S regions[] = {
    { 0xc093, 0x51, 0x07, 0x00 },
    { 0xc12f, 0x27, 0x48, 0x4b },
  };
  *count = sizeof(regions) / sizeof(regions[0]);
  return regions;
}


#ifdef LED_PIN
void pbaction::gameLeds(CRGB *leds) {
  // pbaction: yellow on red "knight rider"
  static char sub_cnt = 0;
  static char led = 0;
  if (sub_cnt++ == 4) {
    sub_cnt = 0;
    char il = (led < NUM_LEDS) ? led : ((2 * NUM_LEDS - 2) - led);
    for (char c = 0; c < NUM_LEDS; c++)
      leds[c] = (c == il) ? LED_YELLOW : LED_RED;
    led = (led + 1) % (2 * NUM_LEDS - 2);
  }
}

void pbaction::menuLeds(CRGB *leds) {
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}
#endif
