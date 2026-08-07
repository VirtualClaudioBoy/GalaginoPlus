#include "gyruss.h"

unsigned char gyruss::m6809_read(m6809_state *s, uint16_t addr) {
  if (addr == 0x0000) {
    return scanline_counter;
  }
  if (addr >= 0x4000 && addr <= 0x47FF) {
    return sub_ram[addr - 0x4000];
  }
  if (addr >= 0x6000 && addr <= 0x67FF) {
    return shared_ram[addr - 0x6000];
  }
  // ROM at 0xE000-0xFFFF only (no mirror, matching MAME)
  if (addr >= 0xE000) {
    return gyruss_rom_sub_raw[addr - 0xE000];
  }
  return 0xFF;
}

void gyruss::m6809_write(m6809_state *s, uint16_t addr, uint8_t val) {
  if (addr >= 0x4000 && addr <= 0x47FF) {
    sub_ram[addr - 0x4000] = val;
    return;
  }
  if (addr >= 0x6000 && addr <= 0x67FF) {
    shared_ram[addr - 0x6000] = val;
    return;
  }
  if (addr == 0x2000) {
    // MAME: gyruss_irq_clear_w — acknowledge IRQ (CLEAR_LINE)
    // IRQ mask is controlled by main CPU LS259 Q1 (0xC181), not here
    return;
  }
}

unsigned char gyruss::m6809_read_opcode(m6809_state *s, uint16_t addr) {
  // Konami-1 decrypted opcodes at 0xE000-0xFFFF only (no mirror)
  if (addr >= 0xE000) {
    return  gyruss_rom_sub_decrypt[addr - 0xE000];
  }
  return m6809_read(s, addr);
}

// ============================================================
// i8039 sample MCU (drums) — callback i8048
// port 0 = BUS (legge il comando = soundlatch2), 1 = P1 (->DAC), 2 = P2 (irq clear)
// Vedi MAME konami/gyruss.cpp: p1_out_cb->dac_w, io_map 0x00-0xff->soundlatch2.
// ============================================================
void gyruss::wrI8048_port(struct i8048_state_S *state, unsigned char port, unsigned char val) {
  (void)state;
  if (port == 1) {
    i8039_dac = val;            // P1 -> DAC: campione corrente dei drums
  }
  // port 2 (P2) = irq_clear: l'IRQ e' edge-trigger one-shot nel core -> no-op
}

unsigned char gyruss::rdI8048_port(struct i8048_state_S *state, unsigned char port) {
  (void)state;
  if (port == 0) return soundlatch2;   // INS A,BUS -> comando latchato
  return 0xFF;
}

unsigned char gyruss::rdI8048_xdm(struct i8048_state_S *state, unsigned char addr) {
  (void)state; (void)addr;
  return soundlatch2;                  // MOVX A,@R -> comando latchato (alternativa al BUS)
}

unsigned char gyruss::rdI8048_rom(struct i8048_state_S *state, unsigned short addr) {
  (void)state;
  return gyruss_rom_i8039[addr & 0x0FFF];
}

static void taskWrapper(void *param) {
  gyruss *gyr = (gyruss *)param;

  while (gyr->audio_running) {
    gyr->run_audio_batch(100);
  }
  
  gyr->audio_running = 1;
  vTaskDelete(NULL);
}

void gyruss::start_audio_task() {
  if (audio_task_handle) return;
  
  audio_running = 1;
  emu_core_id = ARDUINO_RUNNING_CORE == 0 ? 1 : 0;
  int audio_core = emu_core_id == 1 ? 0 : 1;
  
  // Disable watchdog here for audio task...
  if (emu_core_id == 1)
    disableCore0WDT();
  else
    disableCore1WDT();
  
  xTaskCreatePinnedToCore(taskWrapper, "gyruss::audio_task", 2048, this, 1, &audio_task_handle, audio_core);
}

void gyruss::stop_audio_task() {
  if (!audio_task_handle) return;
  
  audio_running = 0;
    
  while (!audio_running)
    vTaskDelay(1);

  if (emu_core_id == 1)
    enableCore0WDT();
  else
    enableCore1WDT();

  audio_running = 0;
  audio_task_handle = NULL;
}

void gyruss::run_audio_batch(int steps) {
  for (int i = 0; i < steps; i++) {
    StepZ80(&cpu[1]);
    if (sound_irq_pending && (cpu[1].IFF & IFF_1)) {
      IntZ80(&cpu[1], INT_RST38);
      sound_irq_pending = 0;
    }
  }
  audio_cycle_approx += steps * 8;
}

void gyruss::start() {
  start_audio_task();
}

void gyruss::reset() {
  stop_audio_task();
  m6809_reset(&sub_cpu);
  machineBase::reset();
  memset(sub_ram, 0, sizeof(sub_ram));
  memset(shared_ram, 0, sizeof(shared_ram));

  // i8039 sample MCU (drums)
  i8048_reset(&i8039_cpu);
  i8039_cpu.irq_oneshot = 1;   // HOLD_LINE: auto-clear all'acknowledge (solo gyruss, dk/dkjr non toccati)
  // ROM 8039 copiata in RAM interna + fetch diretto (niente dispatch virtuale
  // ne' letture flash nel loop di produzione: vedi GYR_I8039ROM_OFF)
  memcpy(&memory[GYR_I8039ROM_OFF], gyruss_rom_i8039, 4096);
  i8039_cpu.rom_direct = &memory[GYR_I8039ROM_OFF];
  i8039_cpu.rom_mask = 0x0FFF;
  soundlatch2 = 0;
  i8039_dac = 128;             // DAC a riposo = centro (silenzio)
  i8039_irq_pending = 0;
  drum_wr = drum_rd = 0;       // ring campioni vuoto
  drum_last = 128;             // fallback a riposo = silenzio
}

inline bool gyruss::is_audio_cpu() {
  // Determine if current context is audio Z80 (works for both single-core and dual-core)
  return (xPortGetCoreID() != emu_core_id);  // dual-core: check which core
}

unsigned char gyruss::opZ80(unsigned short Addr) {
  if (!is_audio_cpu()) {
    if (Addr < 0x6000)
      return gyruss_rom_main[Addr];
    return rdZ80(Addr);
  } 
  else {
    if (Addr < 0x4000)
      return gyruss_rom_audio[Addr];
    return rdZ80(Addr);
  }
}

unsigned char gyruss::rdZ80(unsigned short Addr) {
  if (!is_audio_cpu()) {
    if (Addr < 0x6000)
      return gyruss_rom_main[Addr];

    if (Addr >= 0x8000 && Addr <= 0x83FF)
      return memory[GYR_CRAM_OFF + (Addr & 0x3FF)];

    if (Addr >= 0x8400 && Addr <= 0x87FF)
      return memory[GYR_VRAM_OFF + (Addr & 0x3FF)];

    if (Addr >= 0x9000 && Addr <= 0x9FFF)
      return memory[GYR_WRAM_OFF + (Addr - 0x9000)];

    if (Addr >= 0xA000 && Addr <= 0xA7FF)
      return shared_ram[Addr - 0xA000];

    if (Addr == 0xC000) return GYRUSS_DSW2 | (input->demoSoundsOff() ? 0x80 : 0x00);

    if (Addr == 0xC080) {
      unsigned char keymask = input->buttons_get();
      unsigned char retval = 0xFF;
      if (keymask & BUTTON_COIN)  retval &= ~0x01;
      if (keymask & BUTTON_START) retval &= ~0x08;
        return retval;
    }

    if (Addr == 0xC0A0) {
      unsigned char keymask = input->buttons_get();
      unsigned char retval = 0xFF;
      if (keymask & BUTTON_LEFT)  retval &= ~0x01;
      if (keymask & BUTTON_RIGHT) retval &= ~0x02;
      if (keymask & BUTTON_UP)    retval &= ~0x04;
      if (keymask & BUTTON_DOWN)  retval &= ~0x08;
      if (keymask & BUTTON_FIRE)  retval &= ~0x10;
      return retval;
    }

    if (Addr == 0xC0C0) return 0xFF;
    if (Addr == 0xC0E0) return GYRUSS_DSW1;
    if (Addr == 0xC100) return GYRUSS_DSW3 | (input->demoSoundsOff() ? 0x01 : 0x00);

    return 0xFF;
  }
  else {
    if (Addr < 0x4000)
      return gyruss_rom_audio[Addr];

    if (Addr >= 0x6000 && Addr <= 0x63FF)
      return memory[GYR_ARAM_OFF + (Addr - 0x6000)];

    if (Addr == 0x8000) {
      return sound_latch;
    }

    return 0xFF;
  }
}

void gyruss::wrZ80(unsigned short Addr, unsigned char Value) {
  if (!is_audio_cpu()) {
    if (Addr >= 0x8000 && Addr <= 0x83FF) {
      memory[GYR_CRAM_OFF + (Addr & 0x3FF)] = Value;
      return;
    }

    if (Addr >= 0x8400 && Addr <= 0x87FF) {
      memory[GYR_VRAM_OFF + (Addr & 0x3FF)] = Value;
      if (!game_started && Value != 0) game_started = 1;
      return;
    }

    if (Addr >= 0x9000 && Addr <= 0x9FFF) {
      memory[GYR_WRAM_OFF + (Addr - 0x9000)] = Value;
      return;
    }

    if (Addr >= 0xA000 && Addr <= 0xA7FF) {
      shared_ram[Addr - 0xA000] = Value;
      return;
    }

    if (Addr == 0xC000) return;

    if (Addr == 0xC080) {
      // MAME: sh_irqtrigger_w — latch IRQ for audio Z80 (HOLD_LINE)
      sound_irq_pending = 1;
      return;
    }

    if (Addr == 0xC100) {
      sound_latch = Value;
      return;
    }

    // MAME: LS259 mainlatch at 0xC180-0xC187, each address sets one Q bit
    // Q0=NMI mask, Q2/Q3=coin counters, Q5=flip screen
    // Q1=sub CPU IRQ mask (kept always enabled, game init clears it otherwise)
    if (Addr >= 0xC180 && Addr <= 0xC187) {
      int bit = Addr & 7;
      int bval = Value & 1;
      switch (bit) {
        case 0: irq_enable[0] = bval; break;
        case 5: flip_screen = bval; break;
      }
      return;
    }
    return;
  }
  else {
    if (Addr >= 0x6000 && Addr <= 0x63FF) {
      memory[GYR_ARAM_OFF + (Addr - 0x6000)] = Value;
      return;
    }
  }
}

void gyruss::outZ80(unsigned short Port, unsigned char Value) {
  // MAME Gyruss audio IO: 5 AY chips, stride-4 ports
  // AY1: addr=0x00, read=0x01, write=0x02
  // AY2: addr=0x04, read=0x05, write=0x06
  // AY3: addr=0x08, read=0x09, write=0x0a
  // AY4: addr=0x0c, read=0x0d, write=0x0e
  // AY5: addr=0x10, read=0x11, write=0x12
  // 0x14: i8039 IRQ trigger -> asserisce l'IRQ dell'8039 (drums)
  // 0x18: soundlatch2 -> comando campione per l'8039

  uint8_t port_lo = Port & 0xFF;
  if (port_lo == 0x18) { soundlatch2 = Value; return; }       // comando drums
  if (port_lo == 0x14) { i8039_irq_pending = 1; return; }     // pulsa IRQ 8039 (flag cross-core)
  if (port_lo > 0x12) return;

  int ay_chip = port_lo / 4;    // 0-4 = AY1-AY5
  int func = port_lo % 4;       // 0=addr, 1=read(ignored), 2=write

  if (func == 0) {
    // Address latch
    ay_address[ay_chip] = Value & 0x0F;          
  } 
  else if (func == 2) {
    // Data write — map AY1-5 to soundregs
    if (ay_chip < 5) {
      soundregs[(ay_chip * 16) + ay_address[ay_chip]] = Value;
    }
  }
}

unsigned char gyruss::inZ80(unsigned short Port) {
  // AY data read ports: 0x01(AY1), 0x05(AY2), 0x09(AY3), 0x0d(AY4), 0x11(AY5)
  uint8_t port_lo = Port & 0xFF;
  if (port_lo > 0x12) return 0xFF;
    
  int ay_chip = port_lo / 4;
  int func = port_lo % 4;

  if (func == 1 && ay_chip < 5) {
    int reg = ay_address[ay_chip];
    // AY3 register 14 (port A) returns a timer value (MAME: porta_r)
    if (ay_chip == 2 && reg == 14) {
      static const uint8_t timer_table[10] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x09, 0x0a, 0x0b, 0x0a, 0x0d
      };
      return timer_table[(audio_cycle_approx / 1024) % 10];
    }
    if (ay_chip < 5) {
      return soundregs[(ay_chip * 16) + reg];
    }
  }
  return 0xFF;
}

// Produce campioni drums nel ring, chiamata da run_frame (core emulazione).
// Steppare l'8039 nel render audio (loopTask) congela gyruss su HW (isolato
// con stub 2026-07-11): qui produciamo in anticipo fino a GYR_DRUM_FILL
// campioni (~32ms), il render li consuma uno per campione a 24kHz.
// GYRUSS_DRUMS_SPEED step per slot = timeline 8039 costante -> pitch
// corretto anche se la produzione avviene a raffiche (stesso principio dei
// transfer buffer di dkong). A regime: ~400 slot/frame sul core emu.
void gyruss::produce_drum_samples(void) {
  while (((unsigned short)(drum_wr - drum_rd) & (GYR_DRUM_RING - 1)) < GYR_DRUM_FILL) {
    service_i8039_irq();
    for (int k = 0; k < GYRUSS_DRUMS_SPEED; k++) step_i8039();
    drum_ring[drum_wr] = i8039_dac;
    drum_wr = (drum_wr + 1) & (GYR_DRUM_RING - 1);
  }
}

void gyruss::run_frame(void) {
  for (int i = 0; i < 1536; i++) {
    if ((i % 6) == 0) scanline_counter++;

    StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]);
    m6809_step(&sub_cpu, 4);
  }

  m6809_irq(&sub_cpu);

  if (irq_enable[0]) IntZ80(&cpu[0], INT_NMI);

#if GYRUSS_ENABLE_DRUMS
  produce_drum_samples();
#endif
}

void gyruss::prepare_frame(void) {
  active_sprites = 0;

  // Sprite RAM at sub CPU address 0x4040-0x40FF (confirmed from MAME)
  // Un solo pass (come SPINNERINO): il secondo pass sullo snapshot di meta'
  // frame (multiplexPart1) disegnava sprite ormai stantii -> sprite fantasma.
  unsigned char *sr = &sub_ram[0x40];
  preapre_sprites(sr);
}

void gyruss::preapre_sprites(unsigned char *sr) {
  for (int offs = 0xBC; offs >= 0 && active_sprites < 64; offs -= 4) {
    // MAME bitmap: drawgfx at (sr[offs], 241-sr[offs+3]), 8w×16h
    // ROT90 tile-derived mapping: frame_x = bitmap_y - 16, frame_y = bitmap_x + 16
    // Transposed: ROM row r → frame_x (16 wide), ROM col c → frame_y (8 tall)
    short bx = sr[offs];             // MAME bitmap_x
    short by = 241 - sr[offs + 3];   // MAME bitmap_y
    unsigned char gfx_bank = sr[offs + 1] & 1;
    unsigned char code = ((sr[offs + 2] & 0x20) << 2) | (sr[offs + 1] >> 1);
    char color = sr[offs + 2] & 0x0f;
    char hw_flip_x = (~sr[offs + 2] >> 6) & 1;  // MAME: ~bit6
    char hw_flip_y = (sr[offs + 2] >> 7) & 1;    // MAME: bit7

    sprite[active_sprites].x = by - 16;      // frame_x start (16 wide)
    sprite[active_sprites].y = bx + 16;      // frame_y start (8 tall)
    sprite[active_sprites].code = code;
    sprite[active_sprites].color = color;
    // ROT90 swaps flip axes: bitmap flip_y → frame flip_x (variant bit 0)
    //                        bitmap flip_x → frame flip_y (variant bit 1)
    sprite[active_sprites].flip_y = hw_flip_y;  // → variant bit 0 (row reversal = frame_x flip)
    sprite[active_sprites].flip_x = hw_flip_x;  // → variant bit 1 (pixel reversal = frame_y flip)
    sprite[active_sprites].color_block = gfx_bank;

    active_sprites++;
  }
}

void gyruss::blit_tile(short row, char col) {
  // Gyruss uses column-major VRAM with 90-degree CRT rotation
  // tile_reverse_col=1, tile_col_offset=0, tile_row_offset=0
  int vram_row = row - 2;
  int vram_col = 29 - col;  // reversed column order

  if (vram_row < 0 || vram_row >= 32 || vram_col < 0 || vram_col >= 32) return;

  unsigned short vram_addr = (31 - vram_col) * 32 + vram_row;
  unsigned char tile_code_raw = memory[GYR_VRAM_OFF + vram_addr];
  unsigned char color_attr = memory[GYR_CRAM_OFF + vram_addr];
  unsigned short tile_code = ((color_attr & 0x20) ? 256 : 0) + tile_code_raw;
  if (tile_code >= 512) tile_code = 0;

  unsigned char color_group = color_attr & 0x0F;
  unsigned char flip_x = (color_attr >> 6) & 1;
  unsigned char flip_y = (color_attr >> 7) & 1;

  const unsigned short *tile_data = gyruss_tilemap[tile_code];
  const unsigned short *colors = gyruss_char_colormap[color_group];
  unsigned short *ptr = frame_buffer + 8 * col;

  for (int r = 0; r < 8; r++) {
    int src_r = flip_y ? (7 - r) : r;
    unsigned short pix = tile_data[src_r];

    if (flip_x) {
      for (int c = 0; c < 8; c++)
        ptr[c] = colors[(pix >> ((7 - c) * 2)) & 3];
    } 
    else {
      for (int c = 0; c < 8; c++)
        ptr[c] = colors[(pix >> (c * 2)) & 3];
    }
    ptr += 224;
  }
}

void gyruss::blit_sprite(short row, unsigned char s_idx) {
  struct sprite_S *s = &sprite[s_idx];

  int spr_start_y = s->y;       // frame_y start (8 tall after ROT90)
  int row_pixel_start = row * 8;

  // Transposed: 16 wide (frame_x) × 8 tall (frame_y) after ROT90
  if (spr_start_y >= row_pixel_start + 8 || spr_start_y + 8 <= row_pixel_start)
    return;

  int code = s->code + (s->color_block ? 256 : 0);
  if (code >= 512) code = 0;

  // ROT90 swaps flip axes: flip_y → variant bit 0, flip_x → variant bit 1
  int variant = 0;
  if (s->flip_y) variant |= 1;   // row reversal = frame_x flip
  if (s->flip_x) variant |= 2;   // pixel reversal = frame_y flip

  unsigned char color_group = s->color;
  const unsigned short *colors = gyruss_sprite_colormap[color_group];

  // Pre-compute visible frame_y range within this row strip
  int c_start = (row_pixel_start > spr_start_y) ? (row_pixel_start - spr_start_y) : 0;
  int c_end = ((row_pixel_start + 8) < (spr_start_y + 8)) ? (row_pixel_start + 8 - spr_start_y) : 8;
  int y_base = spr_start_y - row_pixel_start;

  // ROM row r → frame_x offset (0..15), ROM col c → frame_y offset (0..7)
  for (int r = 0; r < 16; r++) {
    int screen_x = s->x + r;
    if (screen_x < 0 || screen_x >= 224) continue;

    unsigned long row_data = gyruss_sprites[variant][code][r];

    for (int c = c_start; c < c_end; c++) {
      unsigned char px = (row_data >> (c * 4)) & 0x0F;
      if (px) {
        frame_buffer[(y_base + c) * 224 + screen_x] = colors[px];
      }
    }
  }
}

void gyruss::render_row(short row) {
  if (row < 2 || row >= 34) return;

  for (char col = 0; col < 28; col++) {
    blit_tile(row, col);
  }

  for (int s = 0; s < active_sprites; s++) {
    blit_sprite(row, s);
  }
}

const unsigned short *gyruss::logo(void) {
  return gyruss_logo;
}

// High score: da MAME hiscore.dat, set "gyruss" (voce condivisa dal gruppo
// gyruss/gyrussb/gyrussce).
const hiscore_region_S *gyruss::hiscoreRegions(unsigned char *count) {
  static const hiscore_region_S regions[] = {
    { 0x9488, 0x28, 0x00, 0x83 },
    { 0x940b, 0x03, 0x00, 0x01 },
  };
  *count = sizeof(regions) / sizeof(regions[0]);
  return regions;
}

#ifdef LED_PIN
void gyruss::menuLeds(CRGB *leds) {
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

void gyruss::gameLeds(CRGB *leds) {
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}
#endif
