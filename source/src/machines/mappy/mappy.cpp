#include "mappy.h"

// ============================================================================
// Reset
// ============================================================================

void mappy::reset() {
  machineBase::reset();   // azzera memory[] (VRAM/work/shared) e soundregs

  // Lazy alloc: ~37.5KB DRAM interna al primo avvio di Mappy: ROM (core
  // emulazione) + tilemap e colormap (core video, due passate tile per
  // strip). Vedi commento in mappy.h. Mai liberata, come Phoenix.
  if (!rom_cached) {
    const uint32_t CAPS = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    unsigned char *rm = (unsigned char *)heap_caps_malloc(0x6000, CAPS);
    unsigned char *rs = (unsigned char *)heap_caps_malloc(0x2000, CAPS);
    // tilemap 4KB + cmap tiles/prio/sprites 3x512B in un blocco unico
    unsigned char *gx = (unsigned char *)heap_caps_malloc(4096 + 3 * 512, CAPS);
    if (rm && rs && gx) {
      memcpy(rm, mappy_rom_main, 0x6000);
      memcpy(rs, mappy_rom_sub, 0x2000);
      memcpy(gx,        mappy_tilemap,             4096);
      memcpy(gx + 4096, mappy_colormap_tiles,       512);
      memcpy(gx + 4608, mappy_colormap_tiles_prio,  512);
      memcpy(gx + 5120, mappy_colormap_sprites,     512);
      rom_main = rm;
      rom_sub = rs;
      tiles        = (const unsigned short (*)[8])gx;
      cmap_tiles   = (const unsigned short (*)[4])(gx + 4096);
      cmap_prio    = (const unsigned short (*)[4])(gx + 4608);
      cmap_sprites = (const unsigned short (*)[16])(gx + 5120);
      rom_cached = true;
      Serial.println(F("[MAPPY] ROM+gfx copiate in DRAM (~37.5 KB)"));
    } else {
      if (rm) heap_caps_free(rm);
      if (rs) heap_caps_free(rs);
      if (gx) heap_caps_free(gx);
      Serial.println(F("[MAPPY] alloc DRAM fallita: ROM/gfx dalla flash"));
    }
  }

  main_irq_mask = sub_irq_mask = 0;
  wsg_enable = 0;
  sub_reset = 1;          // mainlatch Q5=0 al power-on: sub CPU in reset
  io_reset = 1;           // mainlatch Q4=0: namcoio in reset
  scroll = 0;
  dipmux_sel = 0;

  io_chips_reset();

  m6809_reset(&main_cpu); // vettore reset da 0xFFFE (ROM main)
  m6809_reset(&sub_cpu);
  install_rom_direct();
}

// fetch istruzioni diretto dalla ROM (in DRAM), senza passare da
// trampolino globale + dispatch virtuale: e' il grosso del costo per
// istruzione (visto con DEBUG_TIMING: Cpu 20.6ms/frame -> 48Hz).
// m6809_reset() azzera la finestra: va reinstallata dopo OGNI reset.
void mappy::install_rom_direct(void) {
  main_cpu.rom_direct = rom_main;
  main_cpu.rom_base = 0xA000;
  main_cpu.rom_size = 0x6000;
  sub_cpu.rom_direct = rom_sub;
  sub_cpu.rom_base = 0xE000;
  sub_cpu.rom_size = 0x2000;
}

void mappy::io_chips_reset(void) {
  for (int c = 0; c < 2; c++) {
    memset(io[c].ram, 0, sizeof(io[c].ram));
    io[c].lastcoins = io[c].lastbuttons = 0;
    io[c].credits = 0;
    io[c].coins[0] = io[c].coins[1] = 0;
    io[c].coins_per_cred[0] = io[c].coins_per_cred[1] = 1;
    io[c].creds_per_coin[0] = io[c].creds_per_coin[1] = 1;
  }
}

// ============================================================================
// Mainlatch LS259 (main 0x5000-0x500F, sub 0x2000-0x200F, A0 = dato)
// ============================================================================

void mappy::mainlatch_w(uint16_t addr) {
  unsigned char v = addr & 1;

  switch ((addr >> 1) & 7) {
    case 0:   // sub CPU irq mask; mask->0 = acknowledge (CLEAR_LINE in MAME)
      sub_irq_mask = v;
      if (!v) sub_cpu.irq_pending = 0;
      break;
    case 1:   // main CPU irq mask
      main_irq_mask = v;
      if (!v) main_cpu.irq_pending = 0;
      break;
    case 2:   // flip screen: ignorato (solo upright)
      break;
    case 3:   // WSG 15XX sound enable
      wsg_enable = v;
      break;
    case 4:   // !reset dei due namcoio 58XX
      if (!v && !io_reset)
        io_chips_reset();   // fronte di reset: azzera ram e contatori
      io_reset = !v;
      break;
    case 5:   // !reset della sub CPU
      if (v && sub_reset) {
        m6809_reset(&sub_cpu);   // rilascio del reset: riparte dal vettore
        install_rom_direct();    // il reset azzera la finestra rom_direct
      }
      sub_reset = !v;
      break;
  }
}

// ============================================================================
// Bus M6809 — dispatch per puntatore di stato (main vs sub)
// ============================================================================

unsigned char IRAM_ATTR mappy::m6809_read(m6809_state *s, uint16_t addr) {
  if (s == &sub_cpu) {
    // sub: 0x0000-0x03FF RAM condivisa (0x00-0x3F = registri WSG)
    if (addr < 0x0400)
      return (addr < 0x40) ? soundregs[addr] : memory[MAPPY_SHARED_OFF + addr];
    if (addr >= 0xE000)
      return rom_sub[addr - 0xE000];
    return 0xFF;
  }

  // main
  if (addr < 0x2800)                      // VRAM + work/sprite RAM
    return memory[addr];

  if ((addr & 0xFC00) == 0x4000) {        // RAM condivisa col sub
    uint16_t o = addr & 0x03FF;
    return (o < 0x40) ? soundregs[o] : memory[MAPPY_SHARED_OFF + o];
  }

  if ((addr & 0xFFE0) == 0x4800)          // namcoio 0/1: 4 bit, alti a 1
    return 0xF0 | io[(addr >> 4) & 1].ram[addr & 0x0F];

  if (addr >= 0xA000)
    return rom_main[addr - 0xA000];

  return 0xFF;
}

void IRAM_ATTR mappy::m6809_write(m6809_state *s, uint16_t addr, uint8_t val) {
  if (s == &sub_cpu) {
    if (addr < 0x0400) {
      if (addr < 0x40) soundregs[addr] = val;
      else             memory[MAPPY_SHARED_OFF + addr] = val;
      return;
    }
    if ((addr & 0xFFF0) == 0x2000)
      mainlatch_w(addr);
    return;
  }

  // main
  if (addr < 0x2800) {
    memory[addr] = val;
    // primo carattere scritto in VRAM = boot avviato (sync 60Hz, attract)
    if (!game_started && addr < 0x0800 && val != 0)
      game_started = 1;
    return;
  }

  if ((addr & 0xF800) == 0x3800) {        // scroll_w: valore = offset >> 3
    scroll = (addr >> 3) & 0xFF;
    return;
  }

  if ((addr & 0xFC00) == 0x4000) {
    uint16_t o = addr & 0x03FF;
    if (o < 0x40) soundregs[o] = val;     // registri WSG (letti da audio.cpp)
    else          memory[MAPPY_SHARED_OFF + o] = val;
    return;
  }

  if ((addr & 0xFFE0) == 0x4800) {        // namcoio: RAM 4 bit
    io[(addr >> 4) & 1].ram[addr & 0x0F] = val & 0x0F;
    return;
  }

  if ((addr & 0xFFF0) == 0x5000) {
    mainlatch_w(addr);
    return;
  }

  // 0x8000 = watchdog: ignorato
}

unsigned char IRAM_ATTR mappy::m6809_read_opcode(m6809_state *s, uint16_t addr) {
  // nessuna cifratura: fetch diretto dalla ROM per il caso comune
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
// Frame loop: interleave fine main/sub (MAME usa 100 slice/frame) + vblank
// ============================================================================

void mappy::run_frame(void) {
  for (int i = 0; i < MAPPY_SLICES; i++) {
    m6809_step(&main_cpu, 4);
    if (!sub_reset)
      m6809_step(&sub_cpu, 4);
  }

  // vblank: IRQ alle CPU se abilitate dal mainlatch, poi i 58XX eseguono
  // il comando lasciato in ram[8] (in MAME 50us dopo il vblank)
  if (main_irq_mask)
    m6809_irq(&main_cpu);

  if (sub_irq_mask && !sub_reset)
    m6809_irq(&sub_cpu);

  if (!io_reset) {
    customio_run(0);
    customio_run(1);
  }
}

// ============================================================================
// Namco 58XX — porting fedele di MAME namcoio.cpp (vedi
// romconv/mappy/namcoio_58xx_protocol.md)
// ============================================================================

// porte di input a 4 bit, ATTIVE BASSE (1 = non premuto).
// chip 0: 0=COINS 1=P1 2=P2 3=BUTTONS; chip 1: 0=dipmux(DSW2) 1/2=DSW1 3=DSW0
unsigned char mappy::io_in(unsigned char chip, unsigned char port) {
  unsigned char keymask = input->buttons_get();
  unsigned char v = 0x0F;

  if (chip == 0) {
    switch (port) {
      case 0:   // COINS: bit0 coin1, bit1 coin2, bit3 service
        if (keymask & BUTTON_COIN)  v &= ~0x01;
        break;
      case 1:   // P1: 2-way, bit1 right, bit3 left
        if (keymask & BUTTON_RIGHT) v &= ~0x02;
        if (keymask & BUTTON_LEFT)  v &= ~0x08;
        break;
      case 2:   // P2 (cocktail): non collegato
        break;
      case 3:   // BUTTONS: bit0 btn1 (salto), bit2 start1, bit3 start2
        if (keymask & BUTTON_FIRE)  v &= ~0x01;
        if (keymask & BUTTON_START) v &= ~0x04;
        break;
    }
    return v;
  }

  // chip 1: DIP switch
  unsigned char dsw1 = MAPPY_DSW1 & ~(input->demoSoundsOff() ? 0x20 : 0x00);
  switch (port) {
    case 0: return (dipmux_sel ? (MAPPY_DSW2 >> 4) : MAPPY_DSW2) & 0x0F;
    case 1: return dsw1 & 0x0F;
    case 2: return (dsw1 >> 4) & 0x0F;
    case 3: return MAPPY_DSW0 & 0x0F;
  }
  return 0x0F;
}

// credit mode (comando 3, swap=2): il chip conta monete/start da solo e
// riporta i crediti in BCD. ram[2]=decine, ram[3]=unita', ram[0]/ram[1] =
// eventi add/sub (handshake col gioco), ram[4]=P1, ram[6]=P2,
// ram[5]/ram[7] = bottoni/start "level|edge"
void mappy::handle_coins(unsigned char chip, unsigned char swap) {
  n58xx_S *c = &io[chip];
  int credit_add = 0, credit_sub = 0;

  int val = ~io_in(chip, 0);              // COINS (pin 38-41)
  int toggled = val ^ c->lastcoins;
  c->lastcoins = val;

  if (val & toggled & 0x01) {             // coin 1
    c->coins[0]++;
    if (c->coins[0] >= (c->coins_per_cred[0] & 7)) {
      credit_add = c->creds_per_coin[0] - (c->coins_per_cred[0] >> 3);
      c->coins[0] -= c->coins_per_cred[0] & 7;
    } else if (c->coins_per_cred[0] & 8)
      credit_add = 1;
  }
  if (val & toggled & 0x02) {             // coin 2
    c->coins[1]++;
    if (c->coins[1] >= (c->coins_per_cred[1] & 7)) {
      credit_add = c->creds_per_coin[1] - (c->coins_per_cred[1] >> 3);
      c->coins[1] -= c->coins_per_cred[1] & 7;
    } else if (c->coins_per_cred[1] & 8)
      credit_add = 1;
  }
  if (val & toggled & 0x08)               // service
    credit_add = 1;

  val = ~io_in(chip, 3);                  // BUTTONS (pin 30-33)
  toggled = val ^ c->lastbuttons;
  c->lastbuttons = val;

  if ((c->ram[8 + 1] & 0x0F) == 0) {      // ram[9]==0: start abilitati
    if (val & toggled & 0x04) {           // start 1
      if (c->credits >= 1) credit_sub = 1;
    } else if (val & toggled & 0x08) {    // start 2
      if (c->credits >= 2) credit_sub = 2;
    }
  }

  c->credits += credit_add - credit_sub;

  c->ram[0 ^ swap] = (c->credits / 10) & 0x0F;
  c->ram[1 ^ swap] = (c->credits % 10) & 0x0F;
  if (credit_add) c->ram[2 ^ swap] = credit_add & 0x0F;
  if (credit_sub) c->ram[3 ^ swap] = credit_sub & 0x0F;

  c->ram[4] = (~io_in(chip, 1)) & 0x0F;                          // P1
  c->ram[5] = (((val & 0x05) << 1) | (val & toggled & 0x05)) & 0x0F;
  c->ram[6] = (~io_in(chip, 2)) & 0x0F;                          // P2
  c->ram[7] = ((val & 0x0A) | ((val & toggled & 0x0A) >> 1)) & 0x0F;
}

void mappy::customio_run(unsigned char chip) {
  n58xx_S *c = &io[chip];

  switch (c->ram[8] & 0x0F) {
    case 0:   // nop
      break;

    case 1:   // lettura diretta degli switch
      c->ram[4] = (~io_in(chip, 0)) & 0x0F;
      c->ram[5] = (~io_in(chip, 1)) & 0x0F;
      c->ram[6] = (~io_in(chip, 2)) & 0x0F;
      c->ram[7] = (~io_in(chip, 3)) & 0x0F;
      break;

    case 2:   // coinage
      c->coins_per_cred[0] = c->ram[9]  & 0x0F;
      c->creds_per_coin[0] = c->ram[10] & 0x0F;
      c->coins_per_cred[1] = c->ram[11] & 0x0F;
      c->creds_per_coin[1] = c->ram[12] & 0x0F;
      break;

    case 3:   // credit mode
      handle_coins(chip, 2);
      break;

    case 4:   // lettura DIP multiplexata (pin 13 -> LS157 select)
      dipmux_sel = 0;
      c->ram[0] = (~io_in(chip, 0)) & 0x0F;
      c->ram[2] = (~io_in(chip, 1)) & 0x0F;
      c->ram[4] = (~io_in(chip, 2)) & 0x0F;
      c->ram[6] = (~io_in(chip, 3)) & 0x0F;
      dipmux_sel = 1;
      c->ram[1] = (~io_in(chip, 0)) & 0x0F;
      c->ram[3] = (~io_in(chip, 1)) & 0x0F;
      c->ram[5] = (~io_in(chip, 2)) & 0x0F;
      c->ram[7] = (~io_in(chip, 3)) & 0x0F;
      break;

    case 5: { // bootup check: XOR ripetuti pilotati da un LFSR a 7 bit.
              // Mappy lo verifica all'accensione (9-15 = 3 6 5 f a c e)
      #define NEXT(n) ((((n) & 1) ? (n) ^ 0x90 : (n)) >> 1)
      int i, n, rng, seed;

      n = ((c->ram[9] & 0x0F) * 16 + (c->ram[10] & 0x0F)) & 0x7F;
      seed = 0x22;
      for (i = 0; i < n; i++)
        seed = NEXT(seed);

      for (i = 1; i < 8; i++) {
        n = 0;
        rng = seed;
        if (rng & 1) n ^= ~(c->ram[11] & 0x0F);
        rng = NEXT(rng);
        seed = rng;
        if (rng & 1) n ^= ~(c->ram[10] & 0x0F);
        rng = NEXT(rng);
        if (rng & 1) n ^= ~(c->ram[9] & 0x0F);
        rng = NEXT(rng);
        if (rng & 1) n ^= ~(c->ram[15] & 0x0F);
        rng = NEXT(rng);
        if (rng & 1) n ^= ~(c->ram[14] & 0x0F);
        rng = NEXT(rng);
        if (rng & 1) n ^= ~(c->ram[13] & 0x0F);
        rng = NEXT(rng);
        if (rng & 1) n ^= ~(c->ram[12] & 0x0F);

        c->ram[i] = (~n) & 0x0F;
      }
      c->ram[0] = (c->ram[9] & 0x0F) == 0x0F ? 0x0F : 0x00;
      #undef NEXT
      break;
    }
  }
}

// ============================================================================
// Video — sprite
// Coordinate MAME (X orizzontale 0..287, Y verticale 0..223) -> galagino:
//   gal_y = mame_x            (asse 288 verticale)
//   gal_x = 223 - mame_y      (asse 224 orizzontale) -> top del 16x16: 208-sy
// Varianti flip del converter: indice = (mame_flipy<<1)|mame_flipx = tab3&3
// ============================================================================

void mappy::prepare_frame(void) {
  static const unsigned char gfx_offs[2][2] = { { 0, 1 }, { 2, 3 } };

  const unsigned char *tab1 = memory + 0x1780;   // tile, colore
  const unsigned char *tab2 = memory + 0x1F80;   // Y, X
  const unsigned char *tab3 = memory + 0x2780;   // flags, enable/Xmsb

  active_sprites = 0;
  for (int offs = 0; offs < 0x80 && active_sprites < 124; offs += 2) {
    if (tab3[offs + 1] & 2)   // bit1 = disable
      continue;

    unsigned char code  = tab1[offs];
    unsigned char color = tab1[offs + 1] & 0x0F;
    unsigned char flags = tab3[offs];
    unsigned char flipx = flags & 1;
    unsigned char flipy = (flags >> 1) & 1;
    unsigned char sizex = (flags >> 2) & 1;   // raddoppio in mame X = gal Y
    unsigned char sizey = (flags >> 3) & 1;   // raddoppio in mame Y = gal X

    int sx = tab2[offs + 1] + 0x100 * (tab3[offs + 1] & 1) - 40;
    int sy = 256 - tab2[offs] + 1;

    code &= ~sizex;
    code &= ~(sizey << 1);
    sy -= 16 * sizey;
    sy = (sy & 0xFF) - 32;

    // ogni cella 16x16 del blocco (fino a 2x2) diventa uno sprite galagino
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
        sp->flags = flags & 3;   // variante flip della spritemap
      }
    }
  }
}

// sprite 16x16 4bpp: 2 unsigned long per riga galagino (nibble LSB-first),
// trasparenza = colormap 0 (lookup 0xF nel converter)
void mappy::blit_sprite(short row, unsigned char s) {
  const unsigned long *spr = mappy_sprites[sprite[s].flags & 3][sprite[s].code];
  const unsigned short *colors = cmap_sprites[sprite[s].color & 0x0F];

  short y_offset = sprite[s].y - 8 * row;

  unsigned char lines2draw = 8;
  if (y_offset < -8) lines2draw = 16 + y_offset;

  unsigned short startline = 0;
  if (y_offset > 0) {
    startline = y_offset;
    lines2draw = 8 - y_offset;
  }

  if (y_offset < 0)
    spr += 2 * (-y_offset);   // salta le righe gia' disegnate nella strip sopra

  short x = sprite[s].x;
  unsigned char c0 = (x < 0) ? -x : 0;
  unsigned char c1 = (x > 224 - 16) ? 224 - x : 16;

  unsigned short *ptr = frame_buffer + x + 224 * startline;
  for (unsigned char r = 0; r < lines2draw; r++, ptr += 224, spr += 2) {
    for (unsigned char c = c0; c < c1; c++) {
      unsigned short col = colors[(spr[c >> 3] >> (4 * (c & 7))) & 0x0F];
      if (col) ptr[c] = col;
    }
  }
}

// ============================================================================
// Video — tilemap 36 colonne x 60 righe (mappy_tilemap_scan MAME)
// Strip galagino row (0..35) = colonna hw. Le colonne hw 0,1,34,35 sono le
// barre punteggio fisse; le altre scorrono lungo l'asse X galagino di
// "scroll" pixel (m_scroll, uniforme). prio=1 = secondo passaggio sopra gli
// sprite per i tile con attributo bit6 (colormap tiles_prio, 0 = salta).
// ============================================================================

void mappy::blit_tile(unsigned short idx, short x, char prio) {
  unsigned char attr = memory[0x800 + idx];
  if (prio && !(attr & 0x40))
    return;

  const unsigned short *tile = tiles[memory[idx]];
  const unsigned short *colors = prio ? cmap_prio[attr & 0x3F]
                                      : cmap_tiles[attr & 0x3F];

  unsigned char c0 = (x < 0) ? -x : 0;
  unsigned char c1 = (x > 224 - 8) ? 224 - x : 8;

  unsigned short *ptr = frame_buffer + x;
  for (unsigned char r = 0; r < 8; r++, ptr += 224) {
    unsigned short pix = tile[r] >> (2 * c0);
    for (unsigned char c = c0; c < c1; c++, pix >>= 2) {
      if (!prio)
        ptr[c] = colors[pix & 3];        // primo passaggio: tilemap OPACO
      else {
        unsigned short col = colors[pix & 3];
        if (col) ptr[c] = col;           // ridisegno prioritario
      }
    }
  }
}

void mappy::render_tiles(short row, char prio) {
  int colp = row - 2;   // "col -= 2" di mappy_tilemap_scan

  if (row < 2 || row > 33) {
    // colonne fisse: indicizzazione speciale in 0x780-0x7FF, nessuno scroll
    for (unsigned char rt = 0; rt < 28; rt++) {
      unsigned short idx = ((rt + 2) & 0x0F) + (rt & 0x10) + ((colp & 3) << 5) + 0x780;
      blit_tile(idx, 216 - 8 * rt, prio);
    }
  } else {
    // porzione scorrevole 32x60: riga hw rt sullo schermo a
    // gal_x = 216 - 8*rt + scroll (mod 480); 29 tile coprono i 224 px
    unsigned char rt0 = scroll >> 3;
    short fine = scroll & 7;
    for (unsigned char k = 0; k < 29; k++) {
      unsigned char rt = rt0 + k;
      if (rt >= 60) rt -= 60;
      short x = 216 - 8 * k + fine;
      if (x <= -8 || x >= 224) continue;
      blit_tile((rt << 5) + colp, x, prio);
    }
  }
}

void mappy::render_row(short row) {
  render_tiles(row, 0);

  for (unsigned char s = 0; s < active_sprites; s++)
    if ((sprite[s].y < 8 * (row + 1)) && ((sprite[s].y + 16) > 8 * row))
      blit_sprite(row, s);

  render_tiles(row, 1);
}

// ============================================================================
// High score (MAME hiscore.dat: mappy/mappyj)
// ============================================================================

const hiscore_region_S *mappy::hiscoreRegions(unsigned char *count) {
  static const hiscore_region_S regions[] = {
    { 0x1460, 0x28, 0x00, 0x41 },   // tabella best-5 con iniziali
    { 0x1385, 0x03, 0x00, 0x00 },   // high score corrente
    { 0x07ed, 0x06, 0x00, 0x20 },   // VRAM: cifre "HIGH SCORE" a video
    { 0x07e0, 0x02, 0x00, 0x02 },
  };
  *count = sizeof(regions) / sizeof(regions[0]);
  return regions;
}
