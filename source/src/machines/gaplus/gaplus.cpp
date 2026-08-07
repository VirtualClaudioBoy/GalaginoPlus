#include "gaplus.h"

// ============================================================================
// Reset
// ============================================================================

void gaplus::reset() {
  machineBase::reset();   // azzera memory[] (videoram/spriteram) e soundregs

  memset(namco15xx_ram, 0, sizeof(namco15xx_ram));

  // Lazy alloc DRAM interna (stile mappy/todruaga, vedi project_mappy.md):
  // main 24KB + sub 24KB + sub2 8KB + tilemap 8KB + colormap ~2.5KB (~66KB).
  if (!rom_cached) {
    const uint32_t CAPS = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    unsigned char *rm  = (unsigned char *)heap_caps_malloc(0x6000, CAPS);
    unsigned char *rs  = (unsigned char *)heap_caps_malloc(0x6000, CAPS);
    unsigned char *rs2 = (unsigned char *)heap_caps_malloc(0x2000, CAPS);
    // tilemap 512*8*2=8192B + cmap tiles/prio 2x512B + cmap sprite 1024B
    unsigned char *gx = (unsigned char *)heap_caps_malloc(8192 + 2*512 + 1024, CAPS);
    if (rm && rs && rs2 && gx) {
      memcpy(rm,  gaplus_rom_main, 0x6000);
      memcpy(rs,  gaplus_rom_sub,  0x6000);
      memcpy(rs2, gaplus_rom_sub2, 0x2000);
      memcpy(gx,        gaplus_tilemap,             8192);
      memcpy(gx + 8192, gaplus_colormap_tiles,      512);
      memcpy(gx + 8704, gaplus_colormap_tiles_prio, 512);
      memcpy(gx + 9216, gaplus_colormap_sprites,    1024);
      rom_main = rm;
      rom_sub  = rs;
      rom_sub2 = rs2;
      tiles        = (const unsigned short (*)[8])gx;
      cmap_tiles   = (const unsigned short (*)[4])(gx + 8192);
      cmap_prio    = (const unsigned short (*)[4])(gx + 8704);
      cmap_sprites = (const unsigned short (*)[8])(gx + 9216);
      rom_cached = true;
      Serial.println(F("[GAPLUS] ROM+gfx copiate in DRAM (~66 KB)"));
    } else {
      if (rm)  heap_caps_free(rm);
      if (rs)  heap_caps_free(rs);
      if (rs2) heap_caps_free(rs2);
      if (gx)  heap_caps_free(gx);
      Serial.println(F("[GAPLUS] alloc DRAM fallita: ROM/gfx dalla flash"));
    }
  }

  main_irq_mask = sub_irq_mask = sub2_irq_mask = 0;
  subs_reset = 1;     // sreset_w: al power-on sub+sub2 in reset, suono muto
  wsg_enable = 0;
  io_reset = 1;       // freset_w: namcoio in reset al power-on
  dipmux_sel = 0;

  snd_bang_cnt = 0;
  snd_bang_ptr = NULL;

  memset(starfield_control, 0, sizeof(starfield_control));
  starfield_framecount = 0;
  for (int i = 0; i < GAPLUS_NUM_STARS; i++) {
    star_x[i] = gaplus_starseed[i].x;
    star_y[i] = gaplus_starseed[i].y;
    star_col[i] = gaplus_starseed[i].col;
    star_set[i] = gaplus_starseed[i].set;
  }

  io_chips_reset();

  m6809_reset(&main_cpu);
  m6809_reset(&sub_cpu);
  m6809_reset(&sub2_cpu);
  install_rom_direct();
}

// fetch istruzioni diretto dalla ROM (in DRAM), senza dispatch virtuale
// (vedi project_mappy.md). m6809_reset() azzera la finestra: va
// reinstallata dopo OGNI reset.
void gaplus::install_rom_direct(void) {
  main_cpu.rom_direct = rom_main;
  main_cpu.rom_base = 0xA000;
  main_cpu.rom_size = 0x6000;
  sub_cpu.rom_direct = rom_sub;
  sub_cpu.rom_base = 0xA000;
  sub_cpu.rom_size = 0x6000;
  sub2_cpu.rom_direct = rom_sub2;
  sub2_cpu.rom_base = 0xE000;
  sub2_cpu.rom_size = 0x2000;
}

void gaplus::io_chips_reset(void) {
  for (int c = 0; c < 2; c++) {
    memset(io[c].ram, 0, sizeof(io[c].ram));
    io[c].lastcoins = io[c].lastbuttons = 0;
    io[c].credits = 0;
    io[c].coins[0] = io[c].coins[1] = 0;
    io[c].coins_per_cred[0] = io[c].coins_per_cred[1] = 1;
    io[c].creds_per_coin[0] = io[c].creds_per_coin[1] = 1;
  }
  memset(customio3_ram, 0, sizeof(customio3_ram));
}

// ============================================================================
// Chip semplice @0x6820-0x682F (customio_3): cabinet/service + trigger
// sample esplosione (IGNORATO, nessun .wav nel romset: vedi project_gaplus.md)
// ============================================================================

unsigned char gaplus::customio3_r(unsigned char offset) {
  unsigned char mode = customio3_ram[8];
  switch (offset) {
    case 0: return 0x0F;   // IN2: cabinet upright (bit2=1) + service rilasciato (bit3=1)
    case 1: return (mode == 2) ? customio3_ram[1] : 0x0F;
    case 2: return (mode == 2) ? 0x0F : 0x0E;
    case 3: return (mode == 2) ? customio3_ram[3] : 0x01;
    default: return customio3_ram[offset];
  }
}

// ============================================================================
// Bus M6809 — dispatch per puntatore di stato (main/sub/sub2)
// Indirizzamento a BIT DI INDIRIZZO per gli enable IRQ e i reset (NON un
// LS259 dati come mappy/circusc): vedi project_gaplus.md per i range esatti.
// ============================================================================

unsigned char IRAM_ATTR gaplus::m6809_read(m6809_state *s, uint16_t addr) {
  if (s == &sub_cpu) {
    if (addr < 0x2000) return memory[addr];        // videoram+spriteram condivisa
    if (addr >= 0xA000) return rom_sub[addr - 0xA000];
    return 0xFF;
  }

  if (s == &sub2_cpu) {
    if ((addr & 0xFC00) == 0x0000) {                // namco_15xx amap
      uint16_t o = addr & 0x03FF;
      return (o < 0x40) ? soundregs[o] : namco15xx_ram[o - 0x40];
    }
    if (addr >= 0xE000) return rom_sub2[addr - 0xE000];
    return 0xFF;
  }

  // main
  if (addr < 0x2000) return memory[addr];           // videoram+spriteram

  if ((addr & 0xFC00) == 0x6000) {                  // namco_15xx (stessi registri del sub2)
    uint16_t o = addr & 0x03FF;
    return (o < 0x40) ? soundregs[o] : namco15xx_ram[o - 0x40];
  }
  if ((addr & 0xFFF0) == 0x6800) return 0xF0 | io[0].ram[addr & 0x0F];   // 56XX input
  if ((addr & 0xFFF0) == 0x6810) return 0xF0 | io[1].ram[addr & 0x0F];  // 58XX dip
  if ((addr & 0xFFF0) == 0x6820) return customio3_r(addr & 0x0F);

  if (addr >= 0xA000) return rom_main[addr - 0xA000];

  return 0xFF;
}

void IRAM_ATTR gaplus::m6809_write(m6809_state *s, uint16_t addr, uint8_t val) {
  if (s == &sub_cpu) {
    if (addr < 0x2000) { memory[addr] = val; return; }
    if ((addr & 0xF000) == 0x6000) {                // irq_2_ctrl_w: bit0 indirizzo
      sub_irq_mask = addr & 1;
      if (!sub_irq_mask) sub_cpu.irq_pending = 0;
      return;
    }
    return;   // ROM, ignorato
  }

  if (s == &sub2_cpu) {
    if ((addr & 0xFC00) == 0x0000) {                // namco_15xx amap
      uint16_t o = addr & 0x03FF;
      if (o < 0x40) soundregs[o] = val;
      else namco15xx_ram[o - 0x40] = val;
      return;
    }
    if (addr >= 0x4000 && addr < 0x8000) {          // irq_3_ctrl_w: bit13 offset
      sub2_irq_mask = (addr < 0x6000) ? 1 : 0;
      if (!sub2_irq_mask) sub2_cpu.irq_pending = 0;
      return;
    }
    return;   // watchdog (0x2000-0x3FFF) / ROM, ignorati
  }

  // main
  if (addr < 0x0800) {
    memory[addr] = val;
    if (!game_started && val != 0) game_started = 1;
    return;
  }
  if (addr < 0x2000) { memory[addr] = val; return; }   // spriteram

  if ((addr & 0xFC00) == 0x6000) {
    uint16_t o = addr & 0x03FF;
    if (o < 0x40) soundregs[o] = val;
    else namco15xx_ram[o - 0x40] = val;
    return;
  }
  if ((addr & 0xFFF0) == 0x6800) { io[0].ram[addr & 0x0F] = val & 0x0F; return; }
  if ((addr & 0xFFF0) == 0x6810) { io[1].ram[addr & 0x0F] = val & 0x0F; return; }
  if ((addr & 0xFFF0) == 0x6820) {
    unsigned char off = addr & 0x0F;
    if (off == 9 && val >= 0x0F) {
      snd_bang_cnt = sizeof(gaplus_sample_bang);
      snd_bang_ptr = (const signed char *)gaplus_sample_bang;
    }
    customio3_ram[off] = val;
    return;
  }

  if ((addr & 0xF000) == 0x7000) {                  // irq_1_ctrl_w: bit11 offset
    main_irq_mask = ((addr - 0x7000) & 0x800) ? 0 : 1;
    if (!main_irq_mask) main_cpu.irq_pending = 0;
    return;
  }
  if ((addr & 0xF000) == 0x8000) {                  // sreset_w: bit11 offset
    unsigned char running = ((addr - 0x8000) & 0x800) ? 0 : 1;
    if (running && subs_reset) {
      m6809_reset(&sub_cpu);
      m6809_reset(&sub2_cpu);
      install_rom_direct();     // il reset azzera la finestra rom_direct
    }
    subs_reset = !running;
    wsg_enable = running;
    return;
  }
  if ((addr & 0xF000) == 0x9000) {                  // freset_w: bit11 offset
    unsigned char new_io_reset = ((addr - 0x9000) & 0x800) ? 1 : 0;
    if (new_io_reset && !io_reset) io_chips_reset();
    io_reset = new_io_reset;
    return;
  }
  if ((addr & 0xF800) == 0xA000) {                  // starfield_control_w
    starfield_control[addr & 3] = val;
    return;
  }

  // addr >= 0xA000 (fuori dai range sopra): ROM, ignorato
}

unsigned char IRAM_ATTR gaplus::m6809_read_opcode(m6809_state *s, uint16_t addr) {
  if (s == &sub_cpu) {
    if (addr >= 0xA000) return rom_sub[addr - 0xA000];
  } else if (s == &sub2_cpu) {
    if (addr >= 0xE000) return rom_sub2[addr - 0xE000];
  } else {
    if (addr >= 0xA000) return rom_main[addr - 0xA000];
  }
  return m6809_read(s, addr);
}

// ============================================================================
// Frame loop: interleave fine 3 CPU + vblank (stesso schema di mappy/todruaga)
// ============================================================================

void gaplus::run_frame(void) {
  for (int i = 0; i < GAPLUS_SLICES; i++) {
    m6809_step(&main_cpu, 4);
    if (!subs_reset) {
      m6809_step(&sub_cpu, 4);
      m6809_step(&sub2_cpu, 4);
    }
  }

  if (main_irq_mask)
    m6809_irq(&main_cpu);

  if (!subs_reset) {
    if (sub_irq_mask)  m6809_irq(&sub_cpu);
    if (sub2_irq_mask) m6809_irq(&sub2_cpu);
  }

  if (!io_reset) {
    customio_run_56(0);   // namcoio_1 = 56XX (input)
    customio_run_58(1);   // namcoio_2 = 58XX (dip)
  }

  starfield_advance();
}

// ============================================================================
// Namco 56XX (chip0, input) + 58XX (chip1, dip) — porting fedele di MAME
// namcoio.cpp, riuso diretto da todruaga.cpp (qui i RUOLI sono INVERTITI:
// 56xx=input/crediti, 58xx=SOLO dip, vedi project_gaplus.md)
// ============================================================================

// porte a 4 bit, ATTIVE BASSE (1 = non premuto).
// chip 0 (56XX): 0=COINS 1=P1 2=P2 3=BUTTONS
// chip 1 (58XX): 0=DSWA_HIGH 1=DSWB_LOW 2=DSWB_HIGH 3=DSWA_LOW
unsigned char gaplus::io_in(unsigned char chip, unsigned char port) {
  unsigned char keymask = input->buttons_get();
  unsigned char v = 0x0F;

  if (chip == 0) {
    switch (port) {
      case 0:   // COINS: bit0 coin1, bit1 coin2, bit3 service
        if (keymask & BUTTON_COIN)  v &= ~0x01;
        break;
      case 1:   // P1 joystick: bit0 su, bit1 destra, bit2 giu', bit3 sinistra
        if (keymask & BUTTON_UP)    v &= ~0x01;
        if (keymask & BUTTON_RIGHT) v &= ~0x02;
        if (keymask & BUTTON_DOWN)  v &= ~0x04;
        if (keymask & BUTTON_LEFT)  v &= ~0x08;
        break;
      case 2:   // P2 (cocktail): non collegato
        break;
      case 3:   // BUTTONS: bit0 btn1 (fuoco), bit2 start1, bit3 start2
        if (keymask & BUTTON_FIRE)  v &= ~0x01;
        if (keymask & BUTTON_START) v &= ~0x04;
        break;
    }
    return v;
  }

  // chip 1: DIP switch (58XX, 4 porte indipendenti, nessun multiplex)
  unsigned char dswa_low = GAPLUS_DSWA_LOW & ~(input->demoSoundsOff() ? 0x08 : 0x00);
  switch (port) {
    case 0: return GAPLUS_DSWA_HIGH & 0x0F;
    case 1: return GAPLUS_DSWB_LOW  & 0x0F;
    case 2: return GAPLUS_DSWB_HIGH & 0x0F;
    case 3: return dswa_low & 0x0F;
  }
  return 0x0F;
}

// credit mode: il chip conta monete/start da solo, crediti in BCD.
// swap=0 (56XX): decine/unita' in ram[0]/ram[1]; ram[4]=P1,ram[6]=P2,
// ram[5]/[7]=bottoni/start "level|edge" (identico a mappy/todruaga)
void gaplus::handle_coins(unsigned char chip, unsigned char swap) {
  nio_S *c = &io[chip];
  int credit_add = 0, credit_sub = 0;

  int val = ~io_in(chip, 0);
  int toggled = val ^ c->lastcoins;
  c->lastcoins = val;

  if (val & toggled & 0x01) {
    c->coins[0]++;
    if (c->coins[0] >= (c->coins_per_cred[0] & 7)) {
      credit_add = c->creds_per_coin[0] - (c->coins_per_cred[0] >> 3);
      c->coins[0] -= c->coins_per_cred[0] & 7;
    } else if (c->coins_per_cred[0] & 8)
      credit_add = 1;
  }
  if (val & toggled & 0x02) {
    c->coins[1]++;
    if (c->coins[1] >= (c->coins_per_cred[1] & 7)) {
      credit_add = c->creds_per_coin[1] - (c->coins_per_cred[1] >> 3);
      c->coins[1] -= c->coins_per_cred[1] & 7;
    } else if (c->coins_per_cred[1] & 8)
      credit_add = 1;
  }
  if (val & toggled & 0x08)
    credit_add = 1;

  val = ~io_in(chip, 3);
  toggled = val ^ c->lastbuttons;
  c->lastbuttons = val;

  if ((c->ram[8 + 1] & 0x0F) == 0) {
    if (val & toggled & 0x04) {
      if (c->credits >= 1) credit_sub = 1;
    } else if (val & toggled & 0x08) {
      if (c->credits >= 2) credit_sub = 2;
    }
  }

  c->credits += credit_add - credit_sub;

  c->ram[0 ^ swap] = (c->credits / 10) & 0x0F;
  c->ram[1 ^ swap] = (c->credits % 10) & 0x0F;
  if (credit_add) c->ram[2 ^ swap] = credit_add & 0x0F;
  if (credit_sub) c->ram[3 ^ swap] = credit_sub & 0x0F;

  c->ram[4] = (~io_in(chip, 1)) & 0x0F;
  c->ram[5] = (((val & 0x05) << 1) | (val & toggled & 0x05)) & 0x0F;
  c->ram[6] = (~io_in(chip, 2)) & 0x0F;
  c->ram[7] = ((val & 0x0A) | ((val & toggled & 0x0A) >> 1)) & 0x0F;
}

void gaplus::customio_run_56(unsigned char chip) {
  nio_S *c = &io[chip];

  switch (c->ram[8] & 0x0F) {
    case 0:   // nop
      break;

    case 1:   // lettura diretta degli switch (56XX: scrive ram[0..3]!)
      c->ram[0] = (~io_in(chip, 0)) & 0x0F;
      c->ram[1] = (~io_in(chip, 1)) & 0x0F;
      c->ram[2] = (~io_in(chip, 2)) & 0x0F;
      c->ram[3] = (~io_in(chip, 3)) & 0x0F;
      dipmux_sel = c->ram[9] & 1;
      break;

    case 2:   // coinage
      c->coins_per_cred[0] = c->ram[9]  & 0x0F;
      c->creds_per_coin[0] = c->ram[10] & 0x0F;
      c->coins_per_cred[1] = c->ram[11] & 0x0F;
      c->creds_per_coin[1] = c->ram[12] & 0x0F;
      break;

    case 4:   // credit mode (56XX: BCD in ram[0]/ram[1], swap=0)
      handle_coins(chip, 0);
      break;

    case 8: { // bootup check a checksum: ram[0..1] = somma di ram[9..15]
      int sum = 0;
      for (int i = 9; i < 16; i++)
        sum += c->ram[i] & 0x0F;
      c->ram[0] = (sum >> 4) & 0x0F;
      c->ram[1] = sum & 0x0F;
      break;
    }

    case 9:   // lettura DIP multiplexata (pin 13 -> LS157 select)
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
  }
}

void gaplus::customio_run_58(unsigned char chip) {
  nio_S *c = &io[chip];

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

    case 3:   // credit mode (58XX: BCD in ram[2]/ram[3], swap=2)
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

    case 5: { // bootup check: XOR ripetuti pilotati da un LFSR a 7 bit
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
// Starfield (CUS26): 3 set indipendenti, velocita' da starfield_control[1..3]
// (registro RAW MAME, il VALORE conta: 0x87 ferma, 0x85/0x86/0x06 giu',
// 0x80/0x82/0x81 su', 0x9f sinistra, 0xaf destra). Coordinate MAME: campo
// "x" (asse 288, largo) -> galagino y DIRETTO; campo "y" (asse 224, alto)
// -> galagino x INVERTITO (223-y), stesso schema sprite gia' validato per
// mappy/todruaga/galaga su questa famiglia hw. Vedi project_gaplus.md.
// ============================================================================

void gaplus::starfield_advance(void) {
  starfield_framecount++;
  if (!(starfield_control[0] & 1))
    return;

  for (int i = 0; i < GAPLUS_NUM_STARS; i++) {
    switch (starfield_control[star_set[i] + 1]) {
      case 0x87: break;                          // ferma
      case 0x85: case 0x86: star_x[i] += 1.0f; break;
      case 0x06: star_x[i] += 2.0f; break;
      case 0x80: star_x[i] -= 1.0f; break;
      case 0x82: star_x[i] -= 2.0f; break;
      case 0x81: star_x[i] -= 3.0f; break;
      case 0x9f: star_y[i] += 3.0f; break;
      case 0xaf: star_y[i] -= 3.0f; break;
    }

    if (star_x[i] < 16.0f)          star_x[i] += (288.0f - 32.0f);
    if (star_x[i] >= 288.0f - 16.0f) star_x[i] -= (288.0f - 32.0f);
    if (star_y[i] < 0.0f)            star_y[i] += 224.0f;
    if (star_y[i] >= 224.0f)         star_y[i] -= 224.0f;
  }
}

// ============================================================================
// Video — sprite (16x16 3bpp, raddoppio INDIPENDENTE X/Y + flag "duplicate")
// Coordinate MAME (X orizzontale 0..287, Y verticale 0..223) -> galagino:
//   gal_x = 208 - (sy + 16*y), gal_y = sx + 16*x (stesso schema mappy/
//   todruaga: sy/sx qui hanno costanti diverse, -8/-71 invece di +1/-40)
// ============================================================================

void gaplus::prepare_frame(void) {
  static const unsigned char gfx_offs[2][2] = { { 0, 1 }, { 2, 3 } };

  const unsigned char *tab1 = memory + 0x0F80;   // codice, colore
  const unsigned char *tab2 = memory + 0x1780;   // Y, X
  const unsigned char *tab3 = memory + 0x1F80;   // flag, enable/Xmsb

  active_sprites = 0;
  for (int offs = 0; offs < 0x80 && active_sprites < 124; offs += 2) {
    if (tab3[offs + 1] & 2)   // bit1 = disable
      continue;

    unsigned short code  = tab1[offs] | ((tab3[offs] & 0x40) << 2);
    unsigned char  color = tab1[offs + 1] & 0x3F;
    unsigned char  flagsb = tab3[offs];
    unsigned char  flipx = flagsb & 1;
    unsigned char  flipy = (flagsb >> 1) & 1;
    unsigned char  sizex = (flagsb >> 3) & 1;
    unsigned char  sizey = (flagsb >> 5) & 1;
    unsigned char  duplicate = flagsb & 0x80;

    int sx = tab2[offs + 1] + 0x100 * (tab3[offs + 1] & 1) - 71;
    int sy = 256 - tab2[offs] - 8;
    sy -= 16 * sizey;
    sy = (sy & 0xFF) - 32;

    for (unsigned char y = 0; y <= sizey; y++) {
      for (unsigned char x = 0; x <= sizex; x++) {
        short gal_x = 208 - (sy + 16 * y);
        short gal_y = sx + 16 * x;
        if (gal_x <= -16 || gal_x >= 224 || gal_y <= -16 || gal_y >= 288)
          continue;

        unsigned char off = duplicate ? 0 : gfx_offs[y ^ (sizey & flipy)][x ^ (sizex & flipx)];
        unsigned short code_final = (code + off) & 0x1FF;
        if (code_final >= 384)   // fuori dai 384 sprite decodificati: scarta
          continue;

        sprite_S *sp = &sprite[active_sprites++];
        sp->code = code_final & 0xFF;
        sp->color_block = (code_final >> 8) & 1;
        sp->color = color;
        sp->x = gal_x;
        sp->y = gal_y;
        sp->flags = flagsb & 3;   // variante flip della spritemap
      }
    }
  }
}

// sprite 16x16 3bpp (valori 0-7 impacchettati a nibble): trasparenza =
// colormap 0 (lookup 0xFF nel converter)
void gaplus::blit_sprite(short row, unsigned char s) {
  unsigned short code = sprite[s].code | ((unsigned short)sprite[s].color_block << 8);
  const unsigned long *spr = gaplus_sprites[sprite[s].flags & 3][code];
  const unsigned short *colors = cmap_sprites[sprite[s].color & 0x3F];

  short y_offset = sprite[s].y - 8 * row;

  unsigned char lines2draw = 8;
  if (y_offset < -8) lines2draw = 16 + y_offset;

  unsigned short startline = 0;
  if (y_offset > 0) {
    startline = y_offset;
    lines2draw = 8 - y_offset;
  }

  if (y_offset < 0)
    spr += 2 * (-y_offset);

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
// Video — tilemap 36x28 STATICA (tileaddr.h, stesso hw di galaga: NESSUNO
// scroll, 2 passate per le categorie di priorita' attr bit6, come mappy/
// todruaga ma senza lo scroll per colonna)
// ============================================================================

void gaplus::blit_tile(short row, char col, char prio) {
  unsigned short addr = tileaddr[row][col];
  unsigned char attr = memory[0x400 + addr];
  if (((attr >> 6) & 1) != prio)
    return;

  unsigned short tile_idx = memory[addr] + ((attr & 0x80) << 1);
  const unsigned short *tile = tiles[tile_idx];
  const unsigned short *colors = prio ? cmap_prio[attr & 0x3F] : cmap_tiles[attr & 0x3F];

  unsigned short *ptr = frame_buffer + 8 * col;
  for (char r = 0; r < 8; r++, ptr += (224 - 8)) {
    unsigned short pix = *tile++;
    for (char c = 0; c < 8; c++, pix >>= 2) {
      unsigned short cc = colors[pix & 3];
      if (cc) *ptr = cc;   // pen lut==0x0F (nudge->0) e' TRASPARENTE in
                           // ENTRAMBE le passate (configure_groups MAME,
                           // 0xff): lascia intravedere lo starfield sotto
      ptr++;
    }
  }
}

// ============================================================================
// High score (MAME hiscore.dat: namco/gaplus.cpp, voce condivisa da tutto
// il gruppo galaga3/gaplus). Tutte le regioni in videoram/spriteram
// condivisa (addr<0x2000), RAM pura.
// ============================================================================

const hiscore_region_S *gaplus::hiscoreRegions(unsigned char *count) {
  static const hiscore_region_S regions[] = {
    { 0x03ED, 0x08, 0x30, 0x20 },
    { 0x0900, 0xA0, 0x20, 0x00 },
    { 0x09B6, 0x03, 0x00, 0x00 },
  };
  *count = sizeof(regions) / sizeof(regions[0]);
  return regions;
}

void gaplus::render_row(short row) {
  // stelle (sfondo, dietro tilemap+sprite, come starfield_render() MAME)
  if (starfield_control[0] & 1) {
    for (int i = 0; i < GAPLUS_NUM_STARS; i++) {
      if (star_col[i] == 0) continue;
      int gy = (int)star_x[i];
      if (gy < 8 * row || gy >= 8 * row + 8) continue;
      int gx = 223 - (int)star_y[i];
      if (gx < 0 || gx >= 224) continue;

      if (star_set[i] == 1 && starfield_control[2] != 0x85 && (i & 1) == 0) {
        unsigned v = starfield_framecount + i;
        int bit = ((v >> 3) & 1) ? 1 : 2;
        if ((v >> bit) & 1) continue;
      }

      frame_buffer[224 * (gy - 8 * row) + gx] = star_col[i];
    }
  }

  for (char col = 0; col < 28; col++)
    blit_tile(row, col, 0);

  for (unsigned char s = 0; s < active_sprites; s++)
    if ((sprite[s].y < 8 * (row + 1)) && ((sprite[s].y + 16) > 8 * row))
      blit_sprite(row, s);

  for (char col = 0; col < 28; col++)
    blit_tile(row, col, 1);
}
