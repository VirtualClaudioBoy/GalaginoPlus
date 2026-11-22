#include "superpac.h"

// ============================================================================
// Reset
// ============================================================================

void superpac::reset() {
  machineBase::reset();   // clears memory[] (video/work/shared RAM) and soundregs

  // ROMs (the emulation core fetches from them) and tiles + colour tables
  // (the video core reads them twice a strip) into internal RAM, once.
  if (!rom_cached) {
    const uint32_t CAPS = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    unsigned char *rm = (unsigned char *)heap_caps_malloc(0x6000, CAPS);
    unsigned char *rs = (unsigned char *)heap_caps_malloc(0x2000, CAPS);
    unsigned char *gx = (unsigned char *)heap_caps_malloc(4096 + 4 * 512, CAPS);
    if (rm && rs && gx) {
      memcpy(rm, superpac_rom_main, 0x6000);
      memcpy(rs, superpac_rom_sub, 0x2000);
      memcpy(gx,        superpac_tilemap,                4096);
      memcpy(gx + 4096, superpac_colormap_tiles,          512);
      memcpy(gx + 4608, superpac_colormap_tiles_prio,     512);
      memcpy(gx + 5120, superpac_colormap_sprites,        512);
      memcpy(gx + 5632, superpac_colormap_sprites_super,  512);
      rom_main = rm;
      rom_sub = rs;
      tiles        = (const unsigned short (*)[8])gx;
      cmap_tiles   = (const unsigned short (*)[4])(gx + 4096);
      cmap_prio    = (const unsigned short (*)[4])(gx + 4608);
      cmap_sprites = (const unsigned short (*)[4])(gx + 5120);
      cmap_super   = (const unsigned short (*)[4])(gx + 5632);
      rom_cached = true;
    } else {
      if (rm) heap_caps_free(rm);
      if (rs) heap_caps_free(rs);
      if (gx) heap_caps_free(gx);
    }
  }

  // the LS259 is cleared at power-on: no interrupts, no sound, the 56XXs
  // and the sound CPU held in reset
  main_irq_mask = sub_irq_mask = 0;
  main_irq_line = sub_irq_line = 0;
  wsg_enable = 0;
  sub_reset = 1;
  io_reset = 1;
  dipmux_sel = 0;
  main_owed = sub_owed = 0;
  strip_sprites = strip_super = false;

  for (int c = 0; c < 2; c++) {
    memset(io[c].ram, 0, sizeof(io[c].ram));
    io[c].lastcoins = io[c].lastbuttons = 0;
  }
  io_chips_reset();

  m6809_reset(&main_cpu); // reset vector at 0xFFFE (main ROM)
  m6809_reset(&sub_cpu);
  install_rom_direct();
}

// Instructions are fetched straight from the ROM (see m6809.h); m6809_reset()
// clears the window, so this follows every reset.
void superpac::install_rom_direct(void) {
  main_cpu.rom_direct = rom_main;
  main_cpu.rom_base = 0xA000;
  main_cpu.rom_size = 0x6000;
  sub_cpu.rom_direct = rom_sub;
  sub_cpu.rom_base = 0xE000;
  sub_cpu.rom_size = 0x2000;
}

// The 56XX's reset line: its counters go back to their start, its RAM stays.
void superpac::io_chips_reset(void) {
  for (int c = 0; c < 2; c++) {
    io[c].credits = 0;
    io[c].coins[0] = io[c].coins[1] = 0;
    io[c].coins_per_cred[0] = io[c].coins_per_cred[1] = 1;
    io[c].creds_per_coin[0] = io[c].creds_per_coin[1] = 1;
  }
}

// ============================================================================
// Mainlatch LS259 (main 0x5000-0x500F, sub 0x2000-0x200F, A0 = data)
// ============================================================================

void superpac::mainlatch_w(uint16_t addr) {
  unsigned char v = addr & 1;

  switch ((addr >> 1) & 7) {
    case 0:   // sub CPU interrupt enable; 0 also lowers the interrupt line
      sub_irq_mask = v;
      if (!v) sub_irq_line = sub_cpu.irq_pending = 0;
      break;
    case 1:   // main CPU interrupt enable
      main_irq_mask = v;
      if (!v) main_irq_line = main_cpu.irq_pending = 0;
      break;
    case 2:   // flip screen: ignored (upright only)
      break;
    case 3:   // WSG 15XX sound enable
      wsg_enable = v;
      break;
    case 4:   // !reset of the two 56XXs
      if (!v) io_chips_reset();
      io_reset = !v;
      break;
    case 5:   // !reset of the sub CPU
      if (v && sub_reset) {
        m6809_reset(&sub_cpu);   // let go of reset: it starts from its vector
        install_rom_direct();
        sub_owed = 0;
      }
      sub_reset = !v;
      break;
  }
}

// ============================================================================
// M6809 bus, told apart by which CPU's state is passed (main or sub)
// ============================================================================

unsigned char IRAM_ATTR superpac::m6809_read(m6809_state *s, uint16_t addr) {
  if (s == &sub_cpu) {
    // sub: 0x0000-0x03FF shared RAM (0x00-0x3F are the WSG registers)
    if (addr < 0x0400)
      return (addr < 0x40) ? soundregs[addr] : memory[SUPERPAC_SHARED_OFF + addr];
    if (addr >= 0xE000)
      return rom_sub[addr - 0xE000];
    return 0xFF;
  }

  // main
  if (addr < 0x2000)                      // video RAM + work/sprite RAM
    return memory[addr];

  if ((addr & 0xFC00) == 0x4000) {        // RAM shared with the sub CPU
    uint16_t o = addr & 0x03FF;
    return (o < 0x40) ? soundregs[o] : memory[SUPERPAC_SHARED_OFF + o];
  }

  if ((addr & 0xFFE0) == 0x4800)          // 56XX 0/1: 4 bits, the high ones read 1
    return 0xF0 | io[(addr >> 4) & 1].ram[addr & 0x0F];

  if (addr >= 0xA000)
    return rom_main[addr - 0xA000];

  return 0xFF;                            // 0x2000 reads as 0xFF (and flips the screen: ignored)
}

void IRAM_ATTR superpac::m6809_write(m6809_state *s, uint16_t addr, uint8_t val) {
  if (s == &sub_cpu) {
    if (addr < 0x0400) {
      if (addr < 0x40) soundregs[addr] = val;
      else             memory[SUPERPAC_SHARED_OFF + addr] = val;
      return;
    }
    if ((addr & 0xFFF0) == 0x2000)
      mainlatch_w(addr);
    return;
  }

  // main
  if (addr < 0x2000) {
    memory[addr] = val;
    // the first character written to video RAM: the program is up
    if (!game_started && addr < 0x0400 && val != 0)
      game_started = 1;
    return;
  }

  if ((addr & 0xFC00) == 0x4000) {
    uint16_t o = addr & 0x03FF;
    if (o < 0x40) soundregs[o] = val;     // WSG registers (audio.cpp reads them)
    else          memory[SUPERPAC_SHARED_OFF + o] = val;
    return;
  }

  if ((addr & 0xFFE0) == 0x4800) {        // 56XX: 4-bit RAM
    io[(addr >> 4) & 1].ram[addr & 0x0F] = val & 0x0F;
    return;
  }

  if ((addr & 0xFFF0) == 0x5000) {
    mainlatch_w(addr);
    return;
  }

  // 0x2000 = flip screen, 0x8000 = watchdog: ignored
}

unsigned char IRAM_ATTR superpac::m6809_read_opcode(m6809_state *s, uint16_t addr) {
  if (s == &sub_cpu) {
    if (addr >= 0xE000)
      return rom_sub[addr - 0xE000];
  } else {
    if (addr >= 0xA000)
      return rom_main[addr - 0xA000];
  }
  return m6809_read(s, addr);
}

// ============================================================================
// Frame loop. A frame starts at vblank: the interrupt lines of the CPUs that
// have theirs enabled go up and stay up until the program lowers them
// (mainlatch), the 56XXs run the command left for them a line later (MAME:
// 50 microseconds after vblank, time for the interrupt routine to write it),
// and both CPUs get 96 cycles a line, turn about.
// ============================================================================

void superpac::run_line(m6809_state *s, short *owed, unsigned char irq_line) {
  *owed += SUPERPAC_CYCLES_PER_LINE;
  if (irq_line) {
    uint16_t pc = s->PC;
    m6809_irq(s);          // taken if the CPU allows it; wakes SYNC and CWAI
    if (s->PC != pc) *owed -= 19;   // taken: the registers go on the stack
  }
  while (*owed > 0 && !s->halted)
    *owed -= m6809_step(s, 1);
  if (s->halted && *owed > 0)
    *owed = 0;             // waiting for an interrupt: the time just passes
}

void superpac::run_frame(void) {
  if (main_irq_mask) main_irq_line = 1;
  if (sub_irq_mask)  sub_irq_line = 1;
  unsigned char io_runs = !io_reset;

  for (int line = 0; line < SUPERPAC_LINES; line++) {
    run_line(&main_cpu, &main_owed, main_irq_line);
    if (!sub_reset)
      run_line(&sub_cpu, &sub_owed, sub_irq_line);

    if (line == 0 && io_runs) {
      customio_run(0);
      customio_run(1);
    }
  }
}

// ============================================================================
// Namco 56XX, after MAME's namcoio.cpp
// ============================================================================

// 4-bit input ports, ACTIVE LOW (1 = not pressed).
// chip 0: 0=COINS 1=P1 2=P2 3=BUTTONS; chip 1: 0=dipmux(DSW2) 1/2=DSW1 3=DSW0
unsigned char superpac::io_in(unsigned char chip, unsigned char port) {
  unsigned char v = 0x0F;

  if (chip == 0) {
    unsigned char keymask = input->buttons_get();
    switch (port) {
      case 0:   // COINS: bit0 coin 1, bit1 coin 2, bit3 service
        if (keymask & BUTTON_COIN)  v &= ~0x01;
        break;
      case 1:   // P1: bit0 up, bit1 right, bit2 down, bit3 left
        if (keymask & BUTTON_UP)    v &= ~0x01;
        if (keymask & BUTTON_RIGHT) v &= ~0x02;
        if (keymask & BUTTON_DOWN)  v &= ~0x04;
        if (keymask & BUTTON_LEFT)  v &= ~0x08;
        break;
      case 2:   // P2 (cocktail): not connected
        break;
      case 3:   // BUTTONS: bit0 button 1, bit2 start 1, bit3 start 2
        if (keymask & BUTTON_FIRE)  v &= ~0x01;
        if (keymask & BUTTON_START) v &= ~0x04;
        if (keymask & BUTTON_EXTRA) v &= ~0x08;
        break;
    }
    return v;
  }

  // chip 1: DIP switches
  unsigned char dsw1 = SUPERPAC_DSW1 & ~(input->demoSoundsOff() ? 0x40 : 0x00);
  switch (port) {
    case 0: return (dipmux_sel ? (SUPERPAC_DSW2 >> 4) : SUPERPAC_DSW2) & 0x0F;
    case 1: return dsw1 & 0x0F;
    case 2: return (dsw1 >> 4) & 0x0F;
    case 3: return SUPERPAC_DSW0 & 0x0F;
  }
  return v;
}

// Credit mode: the chip counts coins and starts by itself and reports the
// credits in BCD. ram[0]=tens, ram[1]=units, ram[2]/ram[3] = credits added
// and taken (the program clears them), ram[4]=P1, ram[6]=P2,
// ram[5]/ram[7] = buttons and starts, held and just pressed.
void superpac::handle_coins(unsigned char chip) {
  n56xx_S *c = &io[chip];
  int credit_add = 0, credit_sub = 0;

  int val = ~io_in(chip, 0);              // COINS (pins 38-41)
  int toggled = val ^ c->lastcoins;
  c->lastcoins = val;

  for (int i = 0; i < 2; i++) {
    if (val & toggled & (1 << i)) {       // coin 1, coin 2
      c->coins[i]++;
      if (c->coins[i] >= (c->coins_per_cred[i] & 7)) {
        credit_add = c->creds_per_coin[i] - (c->coins_per_cred[i] >> 3);
        c->coins[i] -= c->coins_per_cred[i] & 7;
      } else if (c->coins_per_cred[i] & 8)
        credit_add = 1;
    }
  }
  if (val & toggled & 0x08)               // service
    credit_add = 1;

  val = ~io_in(chip, 3);                  // BUTTONS (pins 30-33)
  toggled = val ^ c->lastbuttons;
  c->lastbuttons = val;

  if ((c->ram[9] & 0x0F) == 0) {          // the program allows starting
    if (val & toggled & 0x04) {           // start 1
      if (c->credits >= 1) credit_sub = 1;
    } else if (val & toggled & 0x08) {    // start 2
      if (c->credits >= 2) credit_sub = 2;
    }
  }

  c->credits += credit_add - credit_sub;

  c->ram[0] = (c->credits / 10) & 0x0F;
  c->ram[1] = (c->credits % 10) & 0x0F;
  if (credit_add) c->ram[2] = credit_add & 0x0F;
  if (credit_sub) c->ram[3] = credit_sub & 0x0F;

  c->ram[4] = (~io_in(chip, 1)) & 0x0F;                          // P1
  c->ram[5] = (((val & 0x05) << 1) | (val & toggled & 0x05)) & 0x0F;
  c->ram[6] = (~io_in(chip, 2)) & 0x0F;                          // P2
  c->ram[7] = ((val & 0x0A) | ((val & toggled & 0x0A) >> 1)) & 0x0F;
}

void superpac::customio_run(unsigned char chip) {
  n56xx_S *c = &io[chip];

  switch (c->ram[8] & 0x0F) {
    case 0:   // nop
      break;

    case 1:   // read the switches; out port A (ram[9]) is the LS157's select on chip 1
      c->ram[0] = (~io_in(chip, 0)) & 0x0F;
      c->ram[1] = (~io_in(chip, 1)) & 0x0F;
      c->ram[2] = (~io_in(chip, 2)) & 0x0F;
      c->ram[3] = (~io_in(chip, 3)) & 0x0F;
      if (chip == 1) dipmux_sel = c->ram[9] & 1;
      break;

    case 2:   // coinage
      c->coins_per_cred[0] = c->ram[9]  & 0x0F;
      c->creds_per_coin[0] = c->ram[10] & 0x0F;
      c->coins_per_cred[1] = c->ram[11] & 0x0F;
      c->creds_per_coin[1] = c->ram[12] & 0x0F;
      break;

    case 4:   // credit mode
      handle_coins(chip);
      break;

    case 7:   // bootup check (Libble Rabble's)
      c->ram[2] = 0x0E;
      c->ram[7] = 0x06;
      break;

    case 8: { // bootup check: the sum of ram[9..15]. Super Pac-Man writes
              // seven 0xF and expects 6 9 back.
      int sum = 0;
      for (int i = 9; i < 16; i++)
        sum += c->ram[i] & 0x0F;
      c->ram[0] = (sum >> 4) & 0x0F;
      c->ram[1] = sum & 0x0F;
      break;
    }

    case 9:   // read the DIP switches, both halves of the multiplexed ones
      if (chip == 1) dipmux_sel = 0;
      c->ram[0] = (~io_in(chip, 0)) & 0x0F;
      c->ram[2] = (~io_in(chip, 1)) & 0x0F;
      c->ram[4] = (~io_in(chip, 2)) & 0x0F;
      c->ram[6] = (~io_in(chip, 3)) & 0x0F;
      if (chip == 1) dipmux_sel = 1;
      c->ram[1] = (~io_in(chip, 0)) & 0x0F;
      c->ram[3] = (~io_in(chip, 1)) & 0x0F;
      c->ram[5] = (~io_in(chip, 2)) & 0x0F;
      c->ram[7] = (~io_in(chip, 3)) & 0x0F;
      break;
  }
}

// ============================================================================
// Video: sprites
// MAME coordinates (X across 0..287, Y down 0..223) -> galagino:
//   gal_y = mame_x            (the 288 axis)
//   gal_x = 223 - mame_y      (the 224 axis) -> top of a 16x16: 208-sy
// The converter's flip variants: index = (mame_flipy<<1)|mame_flipx = flags&3
// ============================================================================

void superpac::prepare_frame(void) {
  static const unsigned char gfx_offs[2][2] = { { 0, 1 }, { 2, 3 } };

  const unsigned char *tab1 = memory + 0x0F80;   // tile, colour
  const unsigned char *tab2 = memory + 0x1780;   // Y, X
  const unsigned char *tab3 = memory + 0x1F80;   // flags, disable/X msb

  active_sprites = 0;
  for (int offs = 0; offs < 0x80 && active_sprites < 124; offs += 2) {
    if (tab3[offs + 1] & 2)   // bit1 = off
      continue;

    unsigned char code  = tab1[offs];
    unsigned char color = tab1[offs + 1] & 0x3F;
    unsigned char flags = tab3[offs];
    unsigned char flipx = flags & 1;
    unsigned char flipy = (flags >> 1) & 1;
    unsigned char sizex = (flags >> 2) & 1;   // doubled along mame X = gal Y
    unsigned char sizey = (flags >> 3) & 1;   // doubled along mame Y = gal X

    int sx = tab2[offs + 1] + 0x100 * (tab3[offs + 1] & 1) - 40;
    int sy = 256 - tab2[offs] + 1;

    code &= ~sizex;
    code &= ~(sizey << 1);
    sy -= 16 * sizey;
    sy = (sy & 0xFF) - 32;

    // each 16x16 cell of the block (up to 2x2) is one galagino sprite
    for (unsigned char y = 0; y <= sizey; y++) {
      for (unsigned char x = 0; x <= sizex; x++) {
        short gal_x = 208 - (sy + 16 * y);
        short gal_y = sx + 16 * x;
        if (gal_x <= -16 || gal_x >= 224 || gal_y <= -16 || gal_y >= 288)
          continue;

        sprite_S *sp = &sprite[active_sprites++];
        sp->code  = (code + gfx_offs[y ^ (sizey & flipy)][x ^ (sizex & flipx)]) & 0x7F;
        sp->color = color;
        sp->x     = gal_x;
        sp->y     = gal_y;
        sp->flags = flags & 3;   // which flip variant of the sprite map
      }
    }
  }
}

// 16x16 sprite at 2 bits a pixel: one unsigned long a row, LSB-first.
// Colour 0 = not drawn (lookup 0xF in the converter).
void superpac::blit_sprite(short row, unsigned char s) {
  const unsigned long *spr = superpac_sprites[sprite[s].flags & 3][sprite[s].code];
  const unsigned short *colors = cmap_sprites[sprite[s].color];
  const unsigned short *super = cmap_super[sprite[s].color];

  short y_offset = sprite[s].y - 8 * row;

  unsigned char lines2draw = 8;
  if (y_offset < -8) lines2draw = 16 + y_offset;

  unsigned short startline = 0;
  if (y_offset > 0) {
    startline = y_offset;
    lines2draw = 8 - y_offset;
  }

  if (y_offset < 0)
    spr += -y_offset;   // the rows drawn in the strip above

  short x = sprite[s].x;
  unsigned char c0 = (x < 0) ? -x : 0;
  unsigned char c1 = (x > 224 - 16) ? 224 - x : 16;

  unsigned short *ptr = frame_buffer + x + 224 * startline;
  unsigned short *sup = super_px + x + 224 * startline;
  for (unsigned char r = 0; r < lines2draw; r++, ptr += 224, sup += 224, spr++) {
    unsigned long pix = *spr >> (2 * c0);
    for (unsigned char c = c0; c < c1; c++, pix >>= 2) {
      unsigned short col = colors[pix & 3];
      if (col) {
        ptr[c] = col;
        sup[c] = super[pix & 3];
        if (sup[c]) strip_super = true;
      }
    }
  }
}

// ============================================================================
// Video: the tilemap, 36 columns x 28 rows out of a 32x32 map
// (superpac_tilemap_scan in MAME). Galagino's strip `row` (0..35) is a
// hardware column; columns 0,1 and 34,35 are the map's last and first two
// rows on their side. prio=1 is the second pass, over the sprites, of the
// tiles with attribute bit 6 (colour table tiles_prio, 0 = leave alone).
// ============================================================================

void superpac::render_tiles(short row, char prio) {
  int col = row - 2;

  for (unsigned char rt = 0; rt < 28; rt++) {
    unsigned short idx = (col & 0x20) ? (rt + 2) + ((col & 0x1F) << 5)
                                      : col + ((rt + 2) << 5);
    unsigned char attr = memory[0x400 + idx];
    if (prio && !(attr & 0x40))
      continue;

    const unsigned short *tile = tiles[memory[idx]];
    unsigned short *ptr = frame_buffer + 216 - 8 * rt;

    if (!prio) {
      const unsigned short *colors = cmap_tiles[attr & 0x3F];
      for (unsigned char r = 0; r < 8; r++, ptr += 224) {
        unsigned short pix = tile[r];
        for (unsigned char c = 0; c < 8; c++, pix >>= 2)
          ptr[c] = colors[pix & 3];
      }
    } else {
      const unsigned short *colors = cmap_prio[attr & 0x3F];
      for (unsigned char r = 0; r < 8; r++, ptr += 224) {
        unsigned short pix = tile[r];
        for (unsigned char c = 0; c < 8; c++, pix >>= 2) {
          unsigned short colr = colors[pix & 3];
          if (colr) ptr[c] = colr;
        }
      }
    }
  }
}

void superpac::render_row(short row) {
  render_tiles(row, 0);

  strip_sprites = strip_super = false;
  for (unsigned char s = 0; s < active_sprites; s++) {
    if ((sprite[s].y < 8 * (row + 1)) && ((sprite[s].y + 16) > 8 * row)) {
      if (!strip_sprites) {
        memset(super_px, 0, sizeof(super_px));
        strip_sprites = true;
      }
      blit_sprite(row, s);
    }
  }

  render_tiles(row, 1);

  // sprite pens 0 and 1 are in front of even those (Pac & Pal's ghost eyes)
  if (strip_super)
    for (unsigned short i = 0; i < 224 * 8; i++)
      if (super_px[i]) frame_buffer[i] = super_px[i];
}

// ============================================================================
// High scores (MAME hiscore.dat: superpac/superpacm)
// ============================================================================

const hiscore_region_S *superpac::hiscoreRegions(unsigned char *count) {
  static const hiscore_region_S regions[] = {
    { 0x1138, 0x28, 0x00, 0x20 },   // the table
    { 0x1087, 0x03, 0x00, 0x00 },   // the high score
    { 0x03ee, 0x07, 0x30, 0x20 },   // video RAM: its digits on screen
  };
  *count = sizeof(regions) / sizeof(regions[0]);
  return regions;
}
