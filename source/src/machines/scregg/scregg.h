#ifndef SCREGG_H
#define SCREGG_H

#include "scregg_rom.h"
#include "scregg_chartiles.h"
#include "scregg_spritetiles.h"
#include "scregg_colorprom.h"
#include "scregg_logo.h"
#include "scregg_dipswitches.h"
#include "../../cpus/m6502/m6502.h"
#include "../machineBase.h"

class scregg : public machineBase {
public:
  scregg() { memset(&m_cpu, 0, sizeof(m_cpu)); }

  signed char machineType() override { return MCH_SCREGG; }
  void start() override;
  void reset() override;
  void run_frame() override;
  void prepare_frame() override;
  void render_row(short row) override;
  const unsigned short *logo() override { return scregg_logo; }
  const char *hiscoreKey() override { return "scregg"; }
  const hiscore_region_S *hiscoreRegions(unsigned char *count) override;
  unsigned char hiscoreRead(unsigned short addr) override { return memory[WORK_RAM_OFFSET+addr]; }
  void hiscoreWrite(unsigned short addr,unsigned char value) override { memory[WORK_RAM_OFFSET+addr]=value; }

private:
  static constexpr unsigned short WORK_RAM_OFFSET = 0x0000;
  static constexpr unsigned short VIDEO_RAM_OFFSET = 0x0800;
  static constexpr unsigned short COLOR_RAM_OFFSET = 0x0c00;
  static constexpr unsigned short MEM_FREE = 0x1000;
  static_assert(MEM_FREE <= RAMSIZE, "RAMSIZE too low for scregg");

  static uint8_t main_read(m6502_t *cpu, uint16_t addr);
  static void main_write(m6502_t *cpu, uint16_t addr, uint8_t value);
  static unsigned short xy_swap(unsigned short offset);
  void build_palette();
  void blit_tile(short row, char col) override;
  void blit_sprite(short row, unsigned char index) override;

  m6502_t m_cpu;
  unsigned char *work_ram = nullptr;
  unsigned char *video_ram = nullptr;
  unsigned char *color_ram = nullptr;
  unsigned short palette[8] = {};
  unsigned char ay_address[2] = {};
  unsigned char flip_screen = 0;
  unsigned char vblank = 0;
  bool coin_latched = false;
  unsigned long coin_pulse_until = 0;
  struct scregg_sprite_s {
    short x, y;
    unsigned char code;
    unsigned char flip_x, flip_y;
  } sprite_list[8];
  unsigned char sprite_count = 0;
};

#endif
