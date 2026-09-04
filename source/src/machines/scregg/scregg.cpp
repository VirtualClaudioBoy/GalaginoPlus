#include "scregg.h"

// MAME hiscore.dat, entry shared by Scrambled Egg and Scregg.
const hiscore_region_S *scregg::hiscoreRegions(unsigned char *count) {
  static const hiscore_region_S regions[] = {
    {0x0400,0x001e,0x17,0x00},
    {0x0015,0x0003,0x00,0x03}
  };
  *count=sizeof(regions)/sizeof(regions[0]);
  return regions;
}

unsigned short scregg::xy_swap(unsigned short offset) {
  return 32 * (offset & 31) + (offset >> 5);
}

void scregg::build_palette() {
  for (int i = 0; i < 8; i++) {
    unsigned char p = scregg_colorprom[i];
    unsigned char r = 0x21 * ((p >> 0) & 1) + 0x47 * ((p >> 1) & 1) + 0x97 * ((p >> 2) & 1);
    unsigned char g = 0x21 * ((p >> 3) & 1) + 0x47 * ((p >> 4) & 1) + 0x97 * ((p >> 5) & 1);
    unsigned char b = 0x52 * ((p >> 6) & 1) + 0xad * ((p >> 7) & 1);
    unsigned short rgb = ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
    palette[i] = (rgb >> 8) | (rgb << 8);
  }
}

void scregg::start() {
  work_ram = memory + WORK_RAM_OFFSET;
  video_ram = memory + VIDEO_RAM_OFFSET;
  color_ram = memory + COLOR_RAM_OFFSET;
  memset(memory, 0, MEM_FREE);
  build_palette();
  m_cpu.read = main_read;
  m_cpu.write = main_write;
  m_cpu.fetch = nullptr;
  m_cpu.user = this;
  m6502_reset(&m_cpu);
}

void scregg::reset() {
  machineBase::reset();
  work_ram = memory + WORK_RAM_OFFSET;
  video_ram = memory + VIDEO_RAM_OFFSET;
  color_ram = memory + COLOR_RAM_OFFSET;
  ay_address[0] = ay_address[1] = 0;
  flip_screen = 0;
  vblank = 0;
  coin_latched = false;
  coin_pulse_until = 0;
  sprite_count = 0;
  build_palette();
  if (m_cpu.read) m6502_reset(&m_cpu);
}

uint8_t scregg::main_read(m6502_t *cpu, uint16_t addr) {
  scregg *s = static_cast<scregg *>(cpu->user);
  if (addr >= 0x3000 && addr < 0x8000) return scregg_rom[addr - 0x3000];
  if (addr >= 0xf000) return scregg_rom[0x4000 + (addr & 0x0fff)];
  if (addr < 0x0800) return s->work_ram[addr];
  if (addr >= 0x1000 && addr < 0x1400) return s->video_ram[addr - 0x1000];
  if (addr >= 0x1400 && addr < 0x1800) return s->color_ram[addr - 0x1400];
  if (addr >= 0x1800 && addr < 0x1c00) return s->video_ram[xy_swap(addr - 0x1800)];
  if (addr >= 0x1c00 && addr < 0x2000) return s->color_ram[xy_swap(addr - 0x1c00)];

  unsigned char keys = s->input->buttons_get();
  if (addr == 0x2000) return SCREGG_DSW1 | (s->vblank ? 0x80 : 0);
  if (addr == 0x2001) return SCREGG_DSW2;
  if (addr == 0x2002) {
    unsigned char value = 0xff;
    if (keys & BUTTON_RIGHT) value &= ~0x01;
    if (keys & BUTTON_LEFT)  value &= ~0x02;
    if (keys & BUTTON_UP)    value &= ~0x04;
    if (keys & BUTTON_DOWN)  value &= ~0x08;
    if (keys & BUTTON_FIRE)  value &= ~0x10;
    // The ROM needs a real (but short) active-low pulse: a single port read
    // is too brief, while forwarding the held physical level makes its coin
    // debounce loop wait indefinitely. Keep the edge active for 50 ms, then
    // force it high until the physical button is released.
    bool coin_now = (keys & BUTTON_COIN) != 0;
    if (coin_now && !s->coin_latched) {
      s->coin_latched = true;
      s->coin_pulse_until = millis() + 50;
    } else if (!coin_now) {
      s->coin_latched = false;
      s->coin_pulse_until = 0;
    }
    if (s->coin_latched && (long)(millis() - s->coin_pulse_until) < 0)
      value &= ~0x40;
    return value;
  }
  if (addr == 0x2003) {
    unsigned char value = 0xff;
    if (keys & BUTTON_START) value &= ~0x40;
    return value;
  }
  if (addr == 0x2004 || addr == 0x2005) {
    cpu->irq = 0;
    return 0;
  }
  return 0xff;
}

void scregg::main_write(m6502_t *cpu, uint16_t addr, uint8_t value) {
  scregg *s = static_cast<scregg *>(cpu->user);
  if (addr < 0x0800) { s->work_ram[addr] = value; return; }
  if (addr >= 0x1000 && addr < 0x1400) { s->video_ram[addr - 0x1000] = value; return; }
  if (addr >= 0x1400 && addr < 0x1800) { s->color_ram[addr - 0x1400] = value; return; }
  if (addr >= 0x1800 && addr < 0x1c00) { s->video_ram[xy_swap(addr - 0x1800)] = value; return; }
  if (addr >= 0x1c00 && addr < 0x2000) { s->color_ram[xy_swap(addr - 0x1c00)] = value; return; }
  if (addr == 0x2000) { s->flip_screen = value & 1; return; }
  if (addr == 0x2001) { cpu->irq = 0; return; }
  if (addr >= 0x2004 && addr <= 0x2007) {
    int chip = (addr >= 0x2006) ? 1 : 0;
    if ((addr & 1) == 0) s->ay_address[chip] = value & 0x0f;
    else if (s->ay_address[chip] < 14) s->soundregs[chip * 16 + s->ay_address[chip]] = value;
  }
}

void scregg::run_frame() {
  // 1.5 MHz / 60 Hz, split into the hardware's 8-scanline IRQ phases.
  const int phases = 34;
  for (int phase = 0; phase < phases; phase++) {
    vblank = (phase >= 30);
    m_cpu.irq = (phase & 1) ? 1 : 0;
    m6502_exec(&m_cpu, 25000 / phases);
  }
  if (!game_started) game_started = 1;
}

void scregg::prepare_frame() {
  sprite_count = 0;
  for (int i = 0; i < 8; i++) {
    unsigned short offs = 128 * i;
    unsigned char attr = video_ram[offs];
    if (!(attr & 1)) continue;
    short native_x = 240 - video_ram[offs + 96];
    short native_y = 240 - video_ram[offs + 64];
    scregg_sprite_s &sp = sprite_list[sprite_count++];
    unsigned char sprite_flip_x = (attr & 0x04) ? 1 : 0;
    unsigned char sprite_flip_y = (attr & 0x02) ? 1 : 0;
    if (flip_screen) {
      native_x = 240 - native_x;
      native_y = 240 - native_y;
      sprite_flip_x = !sprite_flip_x;
      sprite_flip_y = !sprite_flip_y;
    }
    // Native 240-pixel MAME portrait coordinates. Conversion to the common
    // 224-pixel output happens only after the complete strip is rendered.
    sp.x = native_y - 8;
    // Match the MAME reference composition: all sprites sit one 8-pixel
    // character row higher than in the first port mapping.
    sp.y = 264 - native_x;
    sp.code = video_ram[offs + 32];
    sp.flip_x = sprite_flip_x;
    sp.flip_y = sprite_flip_y;
  }
}

void scregg::blit_tile(short row, char col) {
  if (row < 4 || row > 33) return;
  short xTile = 34 - row;
  // Native tile columns 1..30 cover all 240 visible pixels.
  short yTile = col + 1;
  if (flip_screen) {
    xTile = 31 - xTile;
    yTile = 31 - yTile;
  }
  unsigned short offs = 32 * (31 - xTile) + yTile;
  unsigned short code = video_ram[offs] + 256 * (color_ram[offs] & 3);
  for (int y = 0; y < 8; y++) {
    unsigned short *dst = frame_buffer + y * 240;
    for (int x = 0; x < 8; x++) {
      int source_y = flip_screen ? y : 7 - y;
      int source_x = flip_screen ? x : 7 - x;
      int output_x = col * 8 + x;
      dst[output_x] = palette[scregg_chartiles[code][source_y][source_x]];
    }
  }
}

void scregg::blit_sprite(short row, unsigned char index) {
  const scregg_sprite_s &sp = sprite_list[index];
  short band = row * 8;
  for (int y = 0; y < 16; y++) {
    short py = sp.y + y;
    if (py < band || py >= band + 8) continue;
    unsigned short *dst = frame_buffer + (py - band) * 240;
    int sy = sp.flip_x ? y : 15 - y;
    for (int wrap = -256; wrap <= 256; wrap += 256) {
      for (int x = 0; x < 16; x++) {
        short px = sp.x + wrap + x;
        if (px < 0 || px >= 240) continue;
        int sx = sp.flip_y ? x : 15 - x;
        unsigned char pen = scregg_spritetiles[sp.code][sy][sx];
        if (pen) dst[px] = palette[pen];
      }
    }
  }
}

void scregg::render_row(short row) {
  // Native 240-pixel output: no scaling and no horizontal compression.
  for (char col = 0; col < 30; col++) blit_tile(row, col);
  for (unsigned char i = 0; i < sprite_count; i++) blit_sprite(row, i);
}
